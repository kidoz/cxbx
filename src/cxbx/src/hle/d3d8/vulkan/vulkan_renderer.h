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

// Creates the color target, sync objects, and the shared pipeline cache.
// width/height follow the emulated device's backbuffer dimensions. The
// device/queue/image handles are opaque Vulkan handles passed through
// void* so this internal header needs no Vulkan declarations. debugUtils
// mirrors the d3d8 flavor's CXBX_D3D_PERF_MARKERS: VK_EXT_debug_utils
// frame/draw labels and object names make external captures
// (RenderDoc/apitrace) self-describing; it requires the instance to have
// the debug_utils extension enabled.
bool RendererInitialize(void* device, void* physicalDevice, void* queue,
                        unsigned int queueFamily, unsigned int width,
                        unsigned int height, bool debugUtils);

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

// Host D3D8 render-state numbers and already translated values for blend,
// alpha test, color writes and culling. Unknown states are ignored.
void RendererSetRasterState(unsigned int state, unsigned int value);

// Draws CPU vertices: position float4 (x, y, z, rhw) at offset 0,
// optional D3DCOLOR diffuse at diffuseOffset (0xFFFFFFFF = none), optional
// float2 texcoord at texCoordOffset (0xFFFFFFFF = none). primitiveType uses
// the host D3DPRIMITIVETYPE enumeration and primitiveCount follows
// DrawPrimitiveUP semantics (converted to a vertex count internally).
// texCoordSets selects the texcoord layout: 0 = the legacy single float2 at
// texCoordOffset; 3 = one float3 set at texCoordOffset (cube-map
// directions, neutral projective q); 4 = four vec4 sets at texCoordOffset +
// 16*i with the projective q in each set's w (the per-stage coordinates
// program shaders write; the fragment samples stage i from set i).
bool RendererDrawUP(unsigned int primitiveType, unsigned int primitiveCount,
                    const void* data, unsigned int stride,
                    unsigned int diffuseOffset, unsigned int texCoordOffset,
                    unsigned int texCoordSets, unsigned int fogOffset);

// Draws indexed vertices from a caller-pulled staging block: vertexData
// holds vertexCount vertices (same layout rules as RendererDrawUP),
// indexData holds indexCount uint16 indices relative to vertex block start,
// and vertexOffset (the SetIndices base vertex) is added to every index.
// primitiveCount follows DrawIndexedPrimitive semantics. fogOffset points at
// the per-vertex o[FOG] float (0xFFFFFFFF = none: the draw cannot fog).
bool RendererDrawIndexed(unsigned int primitiveType, unsigned int primitiveCount,
                         const void* vertexData, unsigned int vertexCount,
                         unsigned int stride, unsigned int diffuseOffset,
                         unsigned int texCoordOffset, const void* indexData,
                         unsigned int indexCount, int vertexOffset,
                         unsigned int texCoordSets, unsigned int fogOffset);

// Target dimensions for callers that stage readback shadows.
unsigned int RendererTargetWidth();
unsigned int RendererTargetHeight();

// Switches the draw target: key == nullptr binds the main (present-source)
// target; otherwise the key identifies (or lazily creates) a
// render-to-texture target of the given size. depthKey is the stable
// identity of the attached depth-stencil resource and textureKey the stable
// identity of the owning texture when the colour target is a texture level
// (both GUEST texture pointers, resolved by the HLE's surface-link
// registry): shadow-map and stage-override binds resolve targets by them
// (nullptr clears).
void RendererSetRenderTarget(void* key, unsigned int width,
                             unsigned int height, void* depthKey,
                             void* textureKey);

// Binds a registered render target as a stage texture (render-to-texture
// sampling): key is the OWNING TEXTURE's guest pointer (the entry's
// textureKey). Returns false when no target is registered under it.
bool RendererSetStageRenderTargetTexture(unsigned int stage, void* key);

// Binds (or unbinds, key == nullptr) the stage texture. pixels are the
// level-0 texels in the host format's byte layout (row pitch in bytes);
// the renderer uploads and caches by key. hostFormat is the host D3DFORMAT
// value; unsupported formats bind the white dummy with a one-time warning.
bool RendererSetTexture(unsigned int stage, void* key, const void* pixels,
                        unsigned int pitch, unsigned int width,
                        unsigned int height, unsigned int hostFormat);

// Binds a cube map on the stage: pixels hold six square faces face-major
// (face order +X, -X, +Y, -Y, +Z, -Z; each face's rows at the given pitch);
// size is the per-face edge. hostFormat 21/22 or the DXT1/2/3/4/5 fourccs
// (block-compressed faces decode at bind time). The stage samples the cube
// through the direction texCoord[stage].xyz (2D sampling is bypassed while
// the cube bind is active; the next RendererSetTexture rebinds 2D).
bool RendererSetCubeTexture(unsigned int stage, void* key, const void* pixels,
                            unsigned int pitch, unsigned int size,
                            unsigned int hostFormat);

// Binds a volume texture on the stage: pixels hold slices slice-major
// (each slice's rows at rowPitch). hostFormat 21/22 only. The stage samples
// the volume through the texcoord's xyz.
bool RendererSetVolumeTexture(unsigned int stage, void* key,
                              const void* pixels, unsigned int rowPitch,
                              unsigned int width, unsigned int height,
                              unsigned int depth, unsigned int hostFormat);

// Shadow-map bind: stage i samples the depth image registered under
// depthKey by RendererSetRenderTarget and compares the depth texel with the
// texcoord's r per the shadow function. False when no target registered
// the key (the caller should fall back to the white dummy).
bool RendererSetDepthStageTexture(unsigned int stage, void* depthKey);

// D3DRS_SHADOWFUNC: the d3d8 D3DCMPFUNC enumeration (1..8) shadow-map
// stages compare with.
void RendererSetShadowFunc(unsigned int func);

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

// Updates a BUMPENVMAP(_LUM) stage's (1..3) bump-environment state: type
// 0..3 = BUMPENVMAT00/01/10/11, 4 = BUMPENVLSCALE, 5 = BUMPENVLOFFSET;
// value is the float bit pattern. Consumed by the interpreter's
// bump-perturbed sampling when the active def's texture modes say so.
void RendererSetBumpEnvState(unsigned int stage, unsigned int type,
                             unsigned int value);

// Updates the pixel-fog state (D3DRS_FOGENABLE/FOGTABLEMODE/FOGSTART/
// FOGEND/FOGDENSITY, Xbox render states 92..96, re-asserted per draw from
// the deferred array). mode is the raw Xbox D3DFOGMODE (NONE=0, EXP=1,
// EXP2=2, LINEAR=3); the float arguments arrive as their bit patterns.
// Consumed by the interpreter's post-combiner fog blend together with
// RendererSetFogColor.
void RendererSetFogState(unsigned int enable, unsigned int mode,
                         unsigned int startBits, unsigned int endBits,
                         unsigned int densityBits);

// Updates D3DRS_FOGCOLOR (rgb; the alpha byte is not part of the blend).
void RendererSetFogColor(unsigned int color);

// Submits the pending batch, then copies the target draws currently land in
// (the bound render target, else the main target) into dst (row pitch in
// bytes). Safe to call with an empty batch.
bool RendererReadTarget(void* dst, unsigned int pitch);

// Dimensions of the target RendererReadTarget reads.
void RendererCurrentTargetSize(unsigned int* width, unsigned int* height);

// Submits the pending batch, then copies the main (present-source) target
// into dst regardless of the bound render target.
bool RendererReadMainTarget(void* dst, unsigned int pitch);

// Submits the pending batch, then copies a keyed registry target's pixels
// (BGRA8 rows, top-left origin) into dst. Returns false when no target is
// registered under the key or the extent exceeds the target.
bool RendererReadTargetPixels(void* key, void* dst, unsigned int pitch,
                              unsigned int width, unsigned int height);

// Opaque, full-backbuffer BGRA video overlay, independent of guest render
// state and the currently bound render target. Copies the supplied pixels
// before returning; subsequent draws retain all of their previous state.
bool RendererComposeOverlay(const void* pixels, unsigned int width,
                            unsigned int height, unsigned int pitch);

// Submits the pending batch, then copies the target into the given
// swapchain image (which transitions to present source). The image is a
// non-dispatchable Vulkan handle, which is a 64-bit integer on Windows.
// The copy submission consumes acquireSemaphore before touching the image,
// and completes before returning, so the presenter can safely queue Present.
bool RendererCopyToSwapchain(std::uint64_t swapchainImage,
                             unsigned int imageWidth,
                             unsigned int imageHeight,
                             std::uint64_t acquireSemaphore);

// P6 state blocks: capture/restore the guest-visible renderer state under a
// caller-chosen token (the host d3d8 state-block token; 0 is rejected).
// Capture stores a full snapshot (raster/depth/alpha, texture-stage state
// and bindings, combiner definition and constants, viewport); apply restores
// it. Texture bindings restore by cache key: a binding whose upload was
// evicted since the capture binds white until the next real SetTexture.
// Returns false when the render path is inactive or the token is unknown
// (delete is always a no-op success).
bool RendererStateBlockCapture(unsigned int token);
bool RendererStateBlockApply(unsigned int token);
void RendererStateBlockDelete(unsigned int token);

// Rebinds a cached texture upload to a stage without re-uploading (state
// block replay; the cache holds the content the block captured). Unbinds
// the stage for a null key. Returns false when the key is not in the cache;
// the stage then binds white.
bool RendererRebindStageTexture(unsigned int stage, void* key);

// P6 resource lifetime: drops backend state for a released host resource.
// The host d3d8 object is gone, so its pointer can be recycled by a new
// texture/surface at any time; cached uploads or registry entries keyed by
// it must not survive. Unknown keys are ignored. Pending GPU work is
// submitted before destruction, and stage bindings referencing the dropped
// resources revert to the white dummy / main target.
void RendererReleaseTexture(void* key);
void RendererReleaseRenderTarget(void* key);

bool RendererHasPendingFrame();

// Submits the pending batch (final step before the backend copies the
// target into the swapchain for present).
bool RendererEndFrameForPresent();

void RendererShutdownAfterDeviceLoss();

} // namespace vulkan
} // namespace d3d8
} // namespace cxbx

#endif // CXBX_HLE_D3D8_VULKAN_RENDERER_H
