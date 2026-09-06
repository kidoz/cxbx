# Khronos Vulkan Headers 1.4.350 (vendored)

The Khronos `vulkan/` header directory used by the native Vulkan host backend
(`src/cxbx/src/hle/d3d8/vulkan/`). volk's `volk.h` consumes
`vulkan/vk_platform.h` from here; Vulkan Memory Allocator
(`third_party/vma/3.4.0`) will consume the full set when it is wired in.

- Source: https://github.com/KhronosGroup/Vulkan-Headers
- Tag: `v1.4.350` (fetched 2026-09-06)
- Files: `vulkan/` (unmodified); licensing per upstream `LICENSES/`
  (Apache-2.0, with MIT for the spirv-agnostic fragments)
- Only `src/cxbx/src/hle/d3d8/vulkan/` and the vendored `cxbx_volk` target may
  put this directory on their include path.
