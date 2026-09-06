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

// Releases everything the selected backend owns (the Vulkan presenter's
// device, swapchain, and instance).
void HostBackendShutdown();

// The flavor selected at initialization (D3D8 before the first call).
HostBackendFlavor HostBackendActive();

} // namespace d3d8
} // namespace cxbx

#endif // CXBX_HLE_D3D8_HOST_BACKEND_H
