#include "host_backend.h"

#include "vulkan/vulkan_backend.h"
#include "vulkan/vulkan_renderer.h"

// The vendored DirectX SDK basetsd.h shadows the Windows SDK one and does
// not define POINTER_64, which winnt.h requires (same workaround as the
// other windows.h consumers in this tree).
#define POINTER_64 __ptr64

#include <windows.h>

#include <cstdio>

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
} // namespace

HostBackendFlavor HostBackendFlavorFromEnvironment()
{
    char value[16] = {};
    const DWORD length =
        GetEnvironmentVariableA("CXBX_HOST_BACKEND", value, sizeof(value));
    if(length == 0 || length >= sizeof(value))
    {
        return HostBackendFlavor::D3D8;
    }
    return HostBackendParseFlavor(value);
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
    printf("VULKAN| CXBX_HOST_BACKEND=vulkan: presenter bring-up "
           "(validation=%d)\n",
           validate ? 1 : 0);
    if(!vulkan::Initialize(nativeWindow, validate))
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
    if(HostBackendRenders())
    {
        vulkan::RendererSetDepthState(type, value);
    }
}

void HostBackendDrawUP(unsigned int primitiveType, unsigned int primitiveCount,
                       const void* data, unsigned int stride,
                       unsigned int diffuseOffset,
                       unsigned int texCoordOffset)
{
    if(HostBackendRenders())
    {
        vulkan::RendererDrawUP(primitiveType, primitiveCount, data, stride,
                               diffuseOffset, texCoordOffset);
    }
}

void HostBackendDrawIndexed(unsigned int primitiveType,
                            unsigned int primitiveCount,
                            const void* vertexData, unsigned int vertexCount,
                            unsigned int stride, unsigned int diffuseOffset,
                            unsigned int texCoordOffset,
                            const void* indexData, unsigned int indexCount,
                            int vertexOffset)
{
    if(HostBackendRenders())
    {
        vulkan::RendererDrawIndexed(primitiveType, primitiveCount, vertexData,
                                    vertexCount, stride, diffuseOffset,
                                    texCoordOffset, indexData, indexCount,
                                    vertexOffset);
    }
}

void HostBackendSetTexture(unsigned int stage, void* hostTexture,
                           const void* pixels, unsigned int pitch,
                           unsigned int width, unsigned int height,
                           unsigned int hostFormat)
{
    if(HostBackendRenders())
    {
        vulkan::RendererSetTexture(stage, hostTexture, pixels, pitch, width,
                                   height, hostFormat);
    }
}

void HostBackendSetTextureOp(unsigned int stage, unsigned int type,
                             unsigned int value)
{
    if(HostBackendRenders())
    {
        vulkan::RendererSetTextureOp(stage, type, value);
    }
}

void HostBackendSetSamplerState(unsigned int stage, unsigned int type,
                                unsigned int value)
{
    if(HostBackendRenders())
    {
        vulkan::RendererSetSamplerState(stage, type, value);
    }
}

void HostBackendSetViewport(float x, float y, float width, float height)
{
    if(HostBackendRenders())
    {
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

void HostBackendSetPixelShader(const std::uint32_t* def60)
{
    if(HostBackendRenders())
    {
        vulkan::RendererSetPixelShader(def60);
    }
}

void HostBackendSetPixelShaderConstant(unsigned int registerIndex,
                                       const float* value)
{
    if(HostBackendRenders())
    {
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
