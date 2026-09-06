# Vulkan Memory Allocator 3.4.0 (vendored)

Single-header GPU memory allocator reserved for the native Vulkan host
backend (`src/cxbx/src/hle/d3d8/vulkan/`). Not yet compiled into any target:
the P0 bootstrap performs no allocations, and wiring VMA in starts when the
backend takes over texture/image creation (migration phase P3/P5). It is
vendored now so the dependency provenance is pinned alongside volk.

- Source: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
- Tag: `v3.4.0` (fetched 2026-09-06)
- Files: `include/vk_mem_alloc.h` (unmodified), `LICENSE.txt` (MIT)
- When wired in, it must be compiled from exactly one translation unit with
  `VMA_IMPLEMENTATION` defined, and it requires the Khronos Vulkan headers on
  the include path (vendor them with this step).
