// Host render backend selection for the D3D8 HLE.
//
// The contract is intentionally owner-neutral: no Vulkan, Direct3D, or Win32
// types appear here. The HLE entry points keep calling the host d3d8 device;
// the Vulkan bootstrap module (vulkan/) is the only place that may touch
// Vulkan APIs and the native window handle. See the native-Vulkan-backend
// migration plan for the phase gates (P0 selects and bootstraps; presentation
// moves in P1 and later).

#ifndef CXBX_HLE_D3D8_HOST_BACKEND_H
#define CXBX_HLE_D3D8_HOST_BACKEND_H

#include <string.h>

namespace cxbx
{
namespace d3d8
{

enum class HostBackendFlavor
{
    D3D8,
    Vulkan,
};

// Pure mapping of a CXBX_HOST_BACKEND value to a backend flavor. Recognized
// values are "d3d8" and "vulkan", case-insensitive. Null, empty, and
// unrecognized values map to D3D8, the long-standing default, so a mistyped
// override can never disable the working presenter. Header-inline so the
// contract test can compile this file without linking the backend.
inline HostBackendFlavor HostBackendParseFlavor(const char* value)
{
    if(value == nullptr || value[0] == '\0')
    {
        return HostBackendFlavor::D3D8;
    }
    if(_stricmp(value, "vulkan") == 0)
    {
        return HostBackendFlavor::Vulkan;
    }
    return HostBackendFlavor::D3D8;
}

// Reads CXBX_HOST_BACKEND through the Win32 environment API (launcher-provided
// state is not always visible through a CRT environment view) and reports the
// selection.
HostBackendFlavor HostBackendFlavorFromEnvironment();

// P0/P1 backend bring-up: when CXBX_HOST_BACKEND=vulkan, creates the
// persistent Vulkan presenter (instance, Vulkan 1.3 device, Win32 surface,
// swapchain, and per-frame upload staging) on the render window and logs the
// outcome under the "VULKAN|" prefix. Rendering keeps flowing through d3d8;
// from P1 the swapchain owns the window flip. nativeWindow is a borrowed
// native window handle, mirroring the emulation-window capability contract
// (consumers do not destroy or retain it).
void HostBackendInitialize(const void* nativeWindow);

// P1 presentation handoff: when CXBX_HOST_BACKEND=vulkan and the presenter
// initialized successfully, the Vulkan swapchain owns the window surface and
// the d3d8 host device must not Present to the same window. Rendering keeps
// flowing through d3d8; only the final flip moved.
bool HostBackendPresents();

// Presents one full BGRA frame (top-left origin, pitch in bytes; the d3d8
// backbuffer as returned by LockRect) through the Vulkan swapchain. Returns
// false when the presenter cannot take the frame — the caller then falls
// back to the d3d8 host Present for that frame. A hard presenter failure
// latches off, and HostBackendPresents() starts returning false.
bool HostBackendPresentFrame(const void* pixels, unsigned int width,
                             unsigned int height, unsigned int pitch);

// P2 fixed-function rendering: when HostBackendRenders() is true, the HLE
// routes full-surface clears and pretransformed CPU-vertex draws into the
// backend's own color target (presented through the P1 present hook), and
// backbuffer reads are served from that target. d3d8 state that the render
// path does not consume yet (transforms, most render states) keeps flowing
// to the host d3d8 device as a shadow, which preserves Get semantics.
bool HostBackendRenders();

// Reports the emulated device's backbuffer dimensions once the host device
// exists; sizes (or lazily creates) the render target.
void HostBackendSetTargetSize(unsigned int width, unsigned int height);

// D3DCLEAR flag subset (bit 0 = z, bit 1 = stencil, bits 4..7 = target) with
// an X_D3DCOLOR value; only the target bit is consumed in P2.
void HostBackendClear(unsigned int flags, unsigned int color);

// Draws CPU vertices: position float4 (x, y, z, rhw) at offset 0, optional
// D3DCOLOR diffuse at diffuseOffset (0xFFFFFFFF = none), optional float2
// texcoord at texCoordOffset (0xFFFFFFFF = none). primitiveType uses the
// host D3DPRIMITIVETYPE enumeration and primitiveCount follows
// DrawPrimitiveUP semantics. Unsupported layouts are dropped with a
// one-time warning.
void HostBackendDrawUP(unsigned int primitiveType, unsigned int primitiveCount,
                       const void* data, unsigned int stride,
                       unsigned int diffuseOffset,
                       unsigned int texCoordOffset);

// Render target dimensions; false when the render path is not active.
bool HostBackendTargetSize(unsigned int* width, unsigned int* height);

// Binds (or unbinds, hostTexture == nullptr) the stage texture for the
// render path. The caller resolves the host d3d8 texture to pixels
// (LockRect) and passes the level-0 bytes with hostFormat = the host
// D3DFORMAT value; the renderer uploads, caches by hostTexture identity,
// and maps the format.
void HostBackendSetTexture(unsigned int stage, void* hostTexture,
                           const void* pixels, unsigned int pitch,
                           unsigned int width, unsigned int height,
                           unsigned int hostFormat);

// d3d8 texture-stage state, one component at a time (types documented in
// the renderer contract; mirrors the HLE deferred texture-state loop).
void HostBackendSetTextureOp(unsigned int stage, unsigned int type,
                             unsigned int value);
void HostBackendSetSamplerState(unsigned int stage, unsigned int type,
                                unsigned int value);

// Copies the render target into dst (row pitch in bytes) for backbuffer
// reads. Returns false when the render path is not active.
bool HostBackendReadFrame(void* dst, unsigned int pitch);

// Releases everything the selected backend owns (the Vulkan presenter's
// device, swapchain, and instance).
void HostBackendShutdown();

// The flavor selected at initialization (D3D8 before the first call).
HostBackendFlavor HostBackendActive();

} // namespace d3d8
} // namespace cxbx

#endif // CXBX_HLE_D3D8_HOST_BACKEND_H
