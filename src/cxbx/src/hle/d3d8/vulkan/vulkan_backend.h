// Vulkan smoke-bootstrap entry point for the D3D8 HLE host backend.
//
// This module is the only place in the tree that may include Vulkan headers
// or volk. The header itself stays type-clean so host_backend.cpp can
// dispatch to it without pulling any Vulkan or Win32 declaration into the
// HLE-owned contract.

#ifndef CXBX_HLE_D3D8_VULKAN_BACKEND_H
#define CXBX_HLE_D3D8_VULKAN_BACKEND_H

namespace cxbx
{
namespace d3d8
{
namespace vulkan
{

// Creates a Vulkan instance, requires a Vulkan 1.3 physical device, opens a
// Win32 surface and swapchain on nativeWindow (a borrowed native window
// handle), logs what it found under the "VULKAN|" prefix, then destroys
// everything before returning. Returns true only when every step succeeded.
bool SmokeBootstrap(const void* nativeWindow, bool validationLayers);

} // namespace vulkan
} // namespace d3d8
} // namespace cxbx

#endif // CXBX_HLE_D3D8_VULKAN_BACKEND_H
