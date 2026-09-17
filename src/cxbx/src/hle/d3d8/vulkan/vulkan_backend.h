// Vulkan presenter entry points for the D3D8 HLE host backend.
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

// Creates the persistent presenter: Vulkan instance, Vulkan 1.3 physical
// device, Win32 surface and swapchain on nativeWindow (a borrowed native
// window handle), plus the upload staging and per-frame sync objects.
// Everything is logged under the "VULKAN|" prefix. Returns true when the
// presenter is ready to take Present frames; on false the caller stays with
// the d3d8 presenter. Until Shutdown, the presenter owns no guest state.
bool Initialize(const void* nativeWindow, bool validationLayers);

// Uploads one full BGRA frame (top-left origin, pitch in bytes) into the
// staging buffer, copies it into the acquired swapchain image, and queues a
// FIFO present. Handles out-of-date swapchains by recreating it. Returns
// false when presentation is impossible this frame (the caller then falls
// back to the d3d8 presenter); a hard failure latches the presenter off.
bool PresentFrame(const void* pixels, unsigned int width, unsigned int height,
                  unsigned int pitch);

// Whether the presenter is still operational. A hard failure inside
// PresentFrame latches this off, permanently handing the window back to the
// d3d8 presenter for the rest of the session.
bool PresenterValid();

// Validation errors observed since Initialize, including resource teardown.
// Zero alone does not imply validation was enabled (missing layers log once).
unsigned int ValidationErrorCount();

// Resizes (or lazily creates) the fixed-function render target to the
// emulated device's backbuffer dimensions; the HLE calls this once its host
// device exists. No-op unless the presenter is valid.
void SetTargetSize(unsigned int width, unsigned int height);

// Destroys every Vulkan object the presenter owns.
void Shutdown();

} // namespace vulkan
} // namespace d3d8
} // namespace cxbx

#endif // CXBX_HLE_D3D8_VULKAN_BACKEND_H
