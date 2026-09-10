// Fixed-function Vulkan render path (native-Vulkan-backend migration P2).
// Internal to the vulkan module: vulkan_backend.cpp drives this renderer and
// is the only consumer of this header, so Vulkan types are allowed here.
//
// The renderer owns a single BGRA8 color target that the HLE's
// pretransformed draws go into. It accepts only what the P2 gate needs:
// full-surface clears and CPU-vertex draws with position at offset 0 as
// float4 (x, y, z, rhw) plus an optional D3DCOLOR diffuse; anything else is
// dropped with a one-time warning. Frames batch into one command buffer and
// are submitted on EndFrame (present or readback).

#ifndef CXBX_HLE_D3D8_VULKAN_RENDERER_H
#define CXBX_HLE_D3D8_VULKAN_RENDERER_H

#include <cstdint>

namespace cxbx
{
namespace d3d8
{
namespace vulkan
{

// Opaque renderer state; defined in vulkan_renderer.cpp.
struct RendererState;

// Creates the color target, sync objects, and the shared pipeline cache.
// width/height follow the emulated device's backbuffer dimensions. The
// device/queue/image handles are opaque Vulkan handles passed through
// void* so this internal header needs no Vulkan declarations.
bool RendererInitialize(void* device, void* physicalDevice, void* queue,
                        unsigned int queueFamily, unsigned int width,
                        unsigned int height);

// Destroys renderer resources (device-level only; the caller owns the
// device and destroys it after RendererShutdown).
void RendererShutdown();

bool RendererValid();

// Sets the emulated viewport in surface pixels (draws map pretransformed
// coordinates through it). Defaults to the full target.
void RendererSetViewport(float x, float y, float width, float height);

// PC D3DCLEAR flag subset (bit 0 = target, bit 1 = z, bit 2 = stencil) with
// an X_D3DCOLOR clear value and the Clear call's z/stencil values.
bool RendererClear(unsigned int flags, unsigned int color, float z,
                   unsigned int stencil);

// Stores d3d8 depth-test state for subsequent draws (type: 0 = ZEnable,
// 1 = ZWriteEnable, 2 = ZFUNC with the host D3DCMPFUNC value).
void RendererSetDepthState(unsigned int type, unsigned int value);

// Draws CPU vertices: position float4 (x, y, z, rhw) at offset 0,
// optional D3DCOLOR diffuse at diffuseOffset (0xFFFFFFFF = none), optional
// float2 texcoord at texCoordOffset (0xFFFFFFFF = none). primitiveType uses
// the host D3DPRIMITIVETYPE enumeration and primitiveCount follows
// DrawPrimitiveUP semantics (converted to a vertex count internally).
bool RendererDrawUP(unsigned int primitiveType, unsigned int primitiveCount,
                    const void* data, unsigned int stride,
                    unsigned int diffuseOffset, unsigned int texCoordOffset);

// Draws indexed vertices from a caller-pulled staging block: vertexData
// holds vertexCount vertices (same layout rules as RendererDrawUP),
// indexData holds indexCount uint16 indices relative to vertex block start,
// and vertexOffset (the SetIndices base vertex) is added to every index.
// primitiveCount follows DrawIndexedPrimitive semantics.
bool RendererDrawIndexed(unsigned int primitiveType, unsigned int primitiveCount,
                         const void* vertexData, unsigned int vertexCount,
                         unsigned int stride, unsigned int diffuseOffset,
                         unsigned int texCoordOffset, const void* indexData,
                         unsigned int indexCount, int vertexOffset);

// Target dimensions for callers that stage readback shadows.
unsigned int RendererTargetWidth();
unsigned int RendererTargetHeight();

// Switches the draw target: key == nullptr binds the main (present-source)
// target; otherwise the key identifies (or lazily creates) a
// render-to-texture target of the given size.
void RendererSetRenderTarget(void* key, unsigned int width,
                             unsigned int height);

// Binds a registered render target as a stage texture (render-to-texture
// sampling). Returns false when the key has no target.
bool RendererSetStageRenderTargetTexture(unsigned int stage, void* key);

// Binds (or unbinds, key == nullptr) the stage texture. pixels are the
// level-0 texels in the host format's byte layout (row pitch in bytes);
// the renderer uploads and caches by key. hostFormat is the host D3DFORMAT
// value; unsupported formats bind the white dummy with a one-time warning.
bool RendererSetTexture(unsigned int stage, void* key, const void* pixels,
                        unsigned int pitch, unsigned int width,
                        unsigned int height, unsigned int hostFormat);

// d3d8 texture-stage state, one component at a time (mirrors the HLE's
// deferred texture-state apply loop). TextureOp types: 0 = COLOROP,
// 1 = COLORARG1, 2 = COLORARG2 (D3DTOP/D3DTA values consumed by the
// fragment shader). SamplerState types: 0 = ADDRESSU, 1 = ADDRESSV,
// 2 = MAGFILTER, 3 = MINFILTER (D3DTADDRESS_*/D3DTEXF_* values).
void RendererSetTextureOp(unsigned int stage, unsigned int type,
                          unsigned int value);
void RendererSetSamplerState(unsigned int stage, unsigned int type,
                             unsigned int value);

// Activates (def60 = the raw 60-dword X_D3DPIXELSHADERDEF) or deactivates
// (nullptr) the register-combiner interpreter for subsequent draws. The
// definition's constants come from RendererSetPixelShaderConstant.
void RendererSetPixelShader(const std::uint32_t* def60);

// Updates combiner constant register (0..7) with four floats.
void RendererSetPixelShaderConstant(unsigned int registerIndex,
                                    const float* value);

// Submits the pending batch, then copies the target draws currently land in
// (the bound render target, else the main target) into dst (row pitch in
// bytes). Safe to call with an empty batch.
bool RendererReadTarget(void* dst, unsigned int pitch);

// Dimensions of the target RendererReadTarget reads.
void RendererCurrentTargetSize(unsigned int* width, unsigned int* height);

// Submits the pending batch, then copies the main (present-source) target
// into dst regardless of the bound render target.
bool RendererReadMainTarget(void* dst, unsigned int pitch);

// Submits the pending batch, then copies the target into the given
// swapchain image (which transitions to present source). The image is a
// non-dispatchable Vulkan handle, which is a 64-bit integer on Windows.
bool RendererCopyToSwapchain(std::uint64_t swapchainImage,
                             unsigned int imageWidth,
                             unsigned int imageHeight);

bool RendererHasPendingFrame();

// Submits the pending batch (final step before the backend copies the
// target into the swapchain for present).
bool RendererEndFrameForPresent();

void RendererShutdownAfterDeviceLoss();

} // namespace vulkan
} // namespace d3d8
} // namespace cxbx

#endif // CXBX_HLE_D3D8_VULKAN_RENDERER_H
