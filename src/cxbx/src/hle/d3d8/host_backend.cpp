#include "host_backend.h"

#include "vulkan/vulkan_backend.h"
#include "vulkan/vulkan_renderer.h"

// The vendored DirectX SDK basetsd.h shadows the Windows SDK one and does
// not define POINTER_64, which winnt.h requires (same workaround as the
// other windows.h consumers in this tree).
#define POINTER_64 __ptr64

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace cxbx
{
namespace d3d8
{

namespace
{
HostBackendFlavor g_ActiveFlavor = HostBackendFlavor::D3D8;
bool g_PresenterReady = false;

bool EnvironmentFlagEnabled(const char* name)
{
    char value[8] = {};
    const DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));
    return length > 0 && length < sizeof(value) && value[0] == '1';
}

// P6 state blocks: between Begin and End, every backend-relevant state
// forward is recorded as an op (d3d8 recording semantics) so a later apply
// can replay it. Texture ops record the cache key only; replay rebinds the
// cached upload (or white when it was evicted) instead of copying pixels.
struct RecordedStateOp
{
    enum class Kind
    {
        RasterState,
        DepthState,
        TextureOp,
        SamplerState,
        PixelShader,
        PixelShaderConstant,
        Viewport,
        Texture,
    };
    Kind kind = Kind::RasterState;
    unsigned int a = 0;
    unsigned int b = 0;
    unsigned int c = 0;
    float viewport[4] = {};
    void* key = nullptr;
    bool hasDef = false;
    std::uint32_t def60[60] = {};
    float constant[4] = {};
};

constexpr std::size_t kMaxRecordedOps = 2048;

struct RecordedStateBlock
{
    unsigned int token = 0;
    std::vector<RecordedStateOp> ops;
};

bool g_StateRecording = false;
std::vector<RecordedStateOp> g_RecordedOps;
std::vector<RecordedStateBlock> g_RecordedBlocks;
bool g_StateBlockOverflowLogged = false;
bool g_StateBlockUnknownLogged = false;

// P8 part 2, phase A: the backend-side state mirror. Pure device state the
// HLE consults internally (the guest reads its own state arrays, so no
// guest-visible Get wrapper depends on this). Recorded unconditionally from
// the Set* forwards, the dedicated record entry points, and the state-block
// replay; read with found-or-fallback semantics.
struct StateMirror
{
    // Host render-state values; valid[i] set on first record.
    unsigned int renderState[256] = {};
    bool renderStateValid[256] = {};
    // Host transform-state indices (the SetTransform wrapper records
    // State < 256 after Xbox-to-host translation).
    float transform[300][16] = {};
    bool transformValid[300] = {};
    // Full D3DVIEWPORT8 payload as six floats: X Y Width Height MinZ MaxZ.
    float viewport[6] = {};
    bool viewportValid = false;
    // Texture-stage state keyed [stage][host D3DTSS enum].
    unsigned int textureState[4][32] = {};
    bool textureStateValid[4][32] = {};
};

StateMirror g_StateMirror;

void RecordStateOp(RecordedStateOp::Kind kind, unsigned int a, unsigned int b,
                   unsigned int c = 0, void* key = nullptr)
{
    if(!g_StateRecording)
    {
        return;
    }
    if(g_RecordedOps.size() >= kMaxRecordedOps)
    {
        if(!g_StateBlockOverflowLogged)
        {
            g_StateBlockOverflowLogged = true;
            printf("VULKAN| state block recording exceeded %zu ops; further "
                   "state changes are not recorded\n",
                   kMaxRecordedOps);
        }
        return;
    }
    RecordedStateOp op = {};
    op.kind = kind;
    op.a = a;
    op.b = b;
    op.c = c;
    op.key = key;
    g_RecordedOps.push_back(op);
}

void ReplayRecordedOps(const std::vector<RecordedStateOp>& ops)
{
    for(const RecordedStateOp& op : ops)
    {
        switch(op.kind)
        {
            case RecordedStateOp::Kind::RasterState:
                HostBackendSetRasterState(op.a, op.b);
                break;
            case RecordedStateOp::Kind::DepthState:
                HostBackendSetDepthState(op.a, op.b);
                break;
            case RecordedStateOp::Kind::TextureOp:
                vulkan::RendererSetTextureOp(op.a, op.b, op.c);
                break;
            case RecordedStateOp::Kind::SamplerState:
                vulkan::RendererSetSamplerState(op.a, op.b, op.c);
                break;
            case RecordedStateOp::Kind::PixelShader:
                vulkan::RendererSetPixelShader(op.hasDef ? op.def60 : nullptr);
                break;
            case RecordedStateOp::Kind::PixelShaderConstant:
                vulkan::RendererSetPixelShaderConstant(op.a, op.constant);
                break;
            case RecordedStateOp::Kind::Viewport:
                vulkan::RendererSetViewport(op.viewport[0], op.viewport[1],
                                            op.viewport[2], op.viewport[3]);
                break;
            case RecordedStateOp::Kind::Texture:
                if(op.key != nullptr)
                {
                    vulkan::RendererRebindStageTexture(op.a, op.key);
                }
                else
                {
                    vulkan::RendererSetTexture(op.a, nullptr, nullptr, 0, 0, 0,
                                               0);
                }
                break;
        }
    }
}
} // namespace

HostBackendFlavor HostBackendFlavorFromEnvironment()
{
    char value[16] = {};
    const DWORD length =
        GetEnvironmentVariableA("CXBX_HOST_BACKEND", value, sizeof(value));
    if(length >= sizeof(value))
    {
        // Overlong values are unrecognized by definition: default (Vulkan).
        return HostBackendFlavor::Vulkan;
    }
    return HostBackendParseFlavor(length == 0 ? nullptr : value);
}

void HostBackendInitialize(const void* nativeWindow)
{
    const HostBackendFlavor flavor = HostBackendFlavorFromEnvironment();
    g_ActiveFlavor = flavor;

    if(flavor != HostBackendFlavor::Vulkan)
    {
        return;
    }

    const bool validate = EnvironmentFlagEnabled("CXBX_VULKAN_VALIDATE");
    const bool debugUtils = EnvironmentFlagEnabled("CXBX_D3D_PERF_MARKERS");
    printf("VULKAN| backend (default or CXBX_HOST_BACKEND=vulkan): presenter "
           "bring-up (validation=%d debug-utils=%d)\n",
           validate ? 1 : 0, debugUtils ? 1 : 0);
    if(!vulkan::Initialize(nativeWindow, validate, debugUtils))
    {
        g_PresenterReady = false;
        printf("VULKAN| presenter bring-up failed; d3d8 remains the presenter\n");
        return;
    }
    g_PresenterReady = true;
}

bool HostBackendPresents()
{
    return g_ActiveFlavor == HostBackendFlavor::Vulkan && g_PresenterReady;
}

bool HostBackendRenders()
{
    return HostBackendPresents() && vulkan::RendererValid();
}

bool HostBackendComposeOverlay(const void* pixels, unsigned int width,
                               unsigned int height, unsigned int pitch)
{
    return HostBackendRenders() &&
           vulkan::RendererComposeOverlay(pixels, width, height, pitch);
}

void HostBackendSetTargetSize(unsigned int width, unsigned int height)
{
    vulkan::SetTargetSize(width, height);
    g_PresenterReady = vulkan::PresenterValid();
}

void HostBackendClear(unsigned int flags, unsigned int color, float z,
                      unsigned int stencil)
{
    if(HostBackendRenders())
    {
        vulkan::RendererClear(flags, color, z, stencil);
    }
}

void HostBackendSetDepthState(unsigned int type, unsigned int value)
{
    // The depth-test trio rides a dedicated forward (per-state wrappers
    // bypass HostBackendSetRasterState); mirror it under the host render
    // states the internal readers consult.
    unsigned int renderState = 0;
    if(type == 0)
    {
        renderState = 7; // D3DRS_ZENABLE
    }
    else if(type == 1)
    {
        renderState = 14; // D3DRS_ZWRITEENABLE
    }
    else if(type == 2)
    {
        renderState = 23; // D3DRS_ZFUNC
    }
    if(renderState != 0)
    {
        g_StateMirror.renderState[renderState] = value;
        g_StateMirror.renderStateValid[renderState] = true;
    }
    if(HostBackendRenders())
    {
        RecordStateOp(RecordedStateOp::Kind::DepthState, type, value);
        vulkan::RendererSetDepthState(type, value);
    }
}

void HostBackendSetRasterState(unsigned int state, unsigned int value)
{
    if(state < 256)
    {
        g_StateMirror.renderState[state] = value;
        g_StateMirror.renderStateValid[state] = true;
    }
    if(HostBackendRenders())
    {
        RecordStateOp(RecordedStateOp::Kind::RasterState, state, value);
        vulkan::RendererSetRasterState(state, value);
    }
}

void HostBackendRecordRenderState(unsigned int state, unsigned int value)
{
    if(state < 256)
    {
        g_StateMirror.renderState[state] = value;
        g_StateMirror.renderStateValid[state] = true;
    }
}

bool HostBackendGetRenderState(unsigned int state, unsigned long* value)
{
    if(state >= 256 || !g_StateMirror.renderStateValid[state])
    {
        return false;
    }
    if(value != nullptr)
    {
        *value = g_StateMirror.renderState[state];
    }
    return true;
}

void HostBackendRecordTransform(unsigned int state, const float* matrix)
{
    if(state >= 300 || matrix == nullptr)
    {
        return;
    }
    memcpy(g_StateMirror.transform[state], matrix, sizeof(float) * 16);
    g_StateMirror.transformValid[state] = true;
}

bool HostBackendGetTransform(unsigned int state, float* matrix)
{
    if(state >= 300 || !g_StateMirror.transformValid[state])
    {
        return false;
    }
    if(matrix != nullptr)
    {
        memcpy(matrix, g_StateMirror.transform[state], sizeof(float) * 16);
    }
    return true;
}

void HostBackendRecordViewport(const float* viewport)
{
    if(viewport == nullptr)
    {
        return;
    }
    memcpy(g_StateMirror.viewport, viewport, sizeof(float) * 6);
    g_StateMirror.viewportValid = true;
}

bool HostBackendGetViewport(float* viewport)
{
    if(!g_StateMirror.viewportValid)
    {
        return false;
    }
    if(viewport != nullptr)
    {
        memcpy(viewport, g_StateMirror.viewport, sizeof(float) * 6);
    }
    return true;
}

void HostBackendRecordTextureStageState(unsigned int stage, unsigned int type,
                                        unsigned int value)
{
    if(stage >= 4 || type >= 32)
    {
        return;
    }
    g_StateMirror.textureState[stage][type] = value;
    g_StateMirror.textureStateValid[stage][type] = true;
}

bool HostBackendGetTextureStageState(unsigned int stage, unsigned int type,
                                     unsigned long* value)
{
    if(stage >= 4 || type >= 32 || !g_StateMirror.textureStateValid[stage][type])
    {
        return false;
    }
    if(value != nullptr)
    {
        *value = g_StateMirror.textureState[stage][type];
    }
    return true;
}

void HostBackendStateBlockBegin()
{
    if(!HostBackendRenders())
    {
        return;
    }
    g_StateRecording = true;
    g_RecordedOps.clear();
}

void HostBackendStateBlockEnd(unsigned int token)
{
    if(!g_StateRecording)
    {
        return;
    }
    g_StateRecording = false;
    if(token == 0 || !HostBackendRenders())
    {
        g_RecordedOps.clear();
        return;
    }
    constexpr std::size_t kMaxRecordedBlocks = 16;
    for(RecordedStateBlock& block : g_RecordedBlocks)
    {
        if(block.token == token)
        {
            block.ops = std::move(g_RecordedOps);
            g_RecordedOps.clear();
            return;
        }
    }
    if(g_RecordedBlocks.size() >= kMaxRecordedBlocks)
    {
        g_RecordedBlocks.erase(g_RecordedBlocks.begin());
    }
    RecordedStateBlock block;
    block.token = token;
    block.ops = std::move(g_RecordedOps);
    g_RecordedBlocks.push_back(std::move(block));
    g_RecordedOps.clear();
}

void HostBackendStateBlockCapture(unsigned int token)
{
    if(token == 0 || !HostBackendRenders())
    {
        return;
    }
    // Capture redefines the block content: drop any recorded op list.
    for(std::size_t i = 0; i < g_RecordedBlocks.size(); ++i)
    {
        if(g_RecordedBlocks[i].token == token)
        {
            g_RecordedBlocks.erase(g_RecordedBlocks.begin() + i);
            break;
        }
    }
    vulkan::RendererStateBlockCapture(token);
}

void HostBackendStateBlockApply(unsigned int token)
{
    if(token == 0 || !HostBackendRenders())
    {
        return;
    }
    for(const RecordedStateBlock& block : g_RecordedBlocks)
    {
        if(block.token == token)
        {
            ReplayRecordedOps(block.ops);
            return;
        }
    }
    if(!vulkan::RendererStateBlockApply(token) && !g_StateBlockUnknownLogged)
    {
        g_StateBlockUnknownLogged = true;
        printf("VULKAN| state block token %u has no backend mirror; host "
               "state only\n",
               token);
    }
}

void HostBackendStateBlockDelete(unsigned int token)
{
    if(token == 0)
    {
        return;
    }
    for(std::size_t i = 0; i < g_RecordedBlocks.size(); ++i)
    {
        if(g_RecordedBlocks[i].token == token)
        {
            g_RecordedBlocks.erase(g_RecordedBlocks.begin() + i);
            break;
        }
    }
    if(HostBackendRenders())
    {
        vulkan::RendererStateBlockDelete(token);
    }
}

void HostBackendDrawUP(unsigned int primitiveType, unsigned int primitiveCount,
                       const void* data, unsigned int stride,
                       unsigned int diffuseOffset,
                       unsigned int texCoordOffset,
                       unsigned int texCoordSets)
{
    if(HostBackendRenders())
    {
        vulkan::RendererDrawUP(primitiveType, primitiveCount, data, stride,
                               diffuseOffset, texCoordOffset, texCoordSets);
    }
}

void HostBackendDrawIndexed(unsigned int primitiveType,
                            unsigned int primitiveCount,
                            const void* vertexData, unsigned int vertexCount,
                            unsigned int stride, unsigned int diffuseOffset,
                            unsigned int texCoordOffset,
                            const void* indexData, unsigned int indexCount,
                            int vertexOffset, unsigned int texCoordSets)
{
    if(HostBackendRenders())
    {
        vulkan::RendererDrawIndexed(primitiveType, primitiveCount, vertexData,
                                    vertexCount, stride, diffuseOffset,
                                    texCoordOffset, indexData, indexCount,
                                    vertexOffset, texCoordSets);
    }
}

void HostBackendSetTexture(unsigned int stage, void* hostTexture,
                           const void* pixels, unsigned int pitch,
                           unsigned int width, unsigned int height,
                           unsigned int hostFormat)
{
    if(HostBackendRenders())
    {
        RecordStateOp(RecordedStateOp::Kind::Texture, stage, 0, 0, hostTexture);
        vulkan::RendererSetTexture(stage, hostTexture, pixels, pitch, width,
                                   height, hostFormat);
    }
}

void HostBackendSetTextureOp(unsigned int stage, unsigned int type,
                             unsigned int value)
{
    if(HostBackendRenders())
    {
        RecordStateOp(RecordedStateOp::Kind::TextureOp, stage, type, value);
        vulkan::RendererSetTextureOp(stage, type, value);
    }
}

void HostBackendSetSamplerState(unsigned int stage, unsigned int type,
                                unsigned int value)
{
    if(HostBackendRenders())
    {
        RecordStateOp(RecordedStateOp::Kind::SamplerState, stage, type, value);
        vulkan::RendererSetSamplerState(stage, type, value);
    }
}

void HostBackendSetViewport(float x, float y, float width, float height)
{
    if(HostBackendRenders())
    {
        if(g_StateRecording && g_RecordedOps.size() < kMaxRecordedOps)
        {
            RecordedStateOp op = {};
            op.kind = RecordedStateOp::Kind::Viewport;
            op.viewport[0] = x;
            op.viewport[1] = y;
            op.viewport[2] = width;
            op.viewport[3] = height;
            g_RecordedOps.push_back(op);
        }
        vulkan::RendererSetViewport(x, y, width, height);
    }
}

void HostBackendSetRenderTarget(void* key, unsigned int width,
                                unsigned int height)
{
    if(HostBackendRenders())
    {
        vulkan::RendererSetRenderTarget(key, width, height);
    }
}

bool HostBackendSetStageRenderTargetTexture(unsigned int stage, void* key)
{
    if(!HostBackendRenders())
    {
        return false;
    }
    return vulkan::RendererSetStageRenderTargetTexture(stage, key);
}

void HostBackendReleaseTexture(void* hostTexture)
{
    if(HostBackendRenders())
    {
        vulkan::RendererReleaseTexture(hostTexture);
    }
}

void HostBackendReleaseRenderTarget(void* key)
{
    if(HostBackendRenders())
    {
        vulkan::RendererReleaseRenderTarget(key);
    }
}

void HostBackendSetPixelShader(const std::uint32_t* def60)
{
    if(HostBackendRenders())
    {
        if(g_StateRecording && g_RecordedOps.size() < kMaxRecordedOps)
        {
            RecordedStateOp op = {};
            op.kind = RecordedStateOp::Kind::PixelShader;
            op.hasDef = def60 != nullptr;
            if(def60 != nullptr)
            {
                memcpy(op.def60, def60, sizeof(op.def60));
            }
            g_RecordedOps.push_back(op);
        }
        vulkan::RendererSetPixelShader(def60);
    }
}

void HostBackendSetPixelShaderConstant(unsigned int registerIndex,
                                       const float* value)
{
    if(HostBackendRenders())
    {
        if(g_StateRecording && g_RecordedOps.size() < kMaxRecordedOps)
        {
            RecordedStateOp op = {};
            op.kind = RecordedStateOp::Kind::PixelShaderConstant;
            op.a = registerIndex;
            if(value != nullptr)
            {
                memcpy(op.constant, value, sizeof(op.constant));
            }
            g_RecordedOps.push_back(op);
        }
        vulkan::RendererSetPixelShaderConstant(registerIndex, value);
    }
}

bool HostBackendTargetSize(unsigned int* width, unsigned int* height)
{
    if(!HostBackendRenders())
    {
        return false;
    }
    if(width != nullptr)
    {
        *width = vulkan::RendererTargetWidth();
    }
    if(height != nullptr)
    {
        *height = vulkan::RendererTargetHeight();
    }
    return true;
}

bool HostBackendReadFrame(void* dst, unsigned int pitch)
{
    if(!HostBackendRenders())
    {
        return false;
    }
    return vulkan::RendererReadTarget(dst, pitch);
}

bool HostBackendCurrentTargetSize(unsigned int* width, unsigned int* height)
{
    if(!HostBackendRenders())
    {
        return false;
    }
    vulkan::RendererCurrentTargetSize(width, height);
    return true;
}

bool HostBackendReadMainTarget(void* dst, unsigned int pitch)
{
    if(!HostBackendRenders())
    {
        return false;
    }
    return vulkan::RendererReadMainTarget(dst, pitch);
}

bool HostBackendPresentFrame(const void* pixels, unsigned int width,
                             unsigned int height, unsigned int pitch)
{
    if(!HostBackendPresents())
    {
        return false;
    }
    const bool presented = vulkan::PresentFrame(pixels, width, height, pitch);
    // Follow the backend's latched validity: a hard failure inside the
    // presenter hands ownership back to the d3d8 host Present.
    g_PresenterReady = vulkan::PresenterValid();
    return presented;
}

void HostBackendShutdown()
{
    if(g_PresenterReady)
    {
        vulkan::Shutdown();
        g_PresenterReady = false;
    }
}

HostBackendFlavor HostBackendActive()
{
    return g_ActiveFlavor;
}

} // namespace d3d8
} // namespace cxbx
