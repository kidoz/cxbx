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

// P0 smoke bootstrap: when CXBX_HOST_BACKEND=vulkan, creates and tears down a
// full Vulkan instance/device/swapchain on the render window and logs the
// outcome under the "VULKAN|" prefix. No rendering moves to Vulkan yet; the
// d3d8 backend stays the presenter in this phase. nativeWindow is a borrowed
// native window handle, mirroring the emulation-window capability contract
// (consumers do not destroy or retain it).
void HostBackendInitialize(const void* nativeWindow);

// Releases anything the selected backend still owns. A no-op in P0 (the
// smoke bootstrap tears itself down); the per-phase swapchain ownership
// lands with the backend that creates it.
void HostBackendShutdown();

// The flavor selected at initialization (D3D8 before the first call).
// In P0 a Vulkan result reports the selected flavor, not a presenting
// backend; the present-path switch is a later migration phase.
HostBackendFlavor HostBackendActive();

} // namespace d3d8
} // namespace cxbx

#endif // CXBX_HLE_D3D8_HOST_BACKEND_H
