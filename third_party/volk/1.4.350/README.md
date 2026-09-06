# volk 1.4.350 (vendored)

Meta-loader for the Vulkan API used by the native Vulkan host backend
(`src/cxbx/src/hle/d3d8/vulkan/`). Loads `vulkan-1.dll` at runtime through
`volkInitialize`, so the build carries no import-library dependency on a
Vulkan SDK or the loader library, and a machine without a Vulkan runtime
fails closed with a logged reason instead of failing to start.

- Source: https://github.com/zeux/volk
- Tag: `1.4.350` (fetched 2026-09-06)
- Files: `volk.h`, `volk.c` (unmodified), `LICENSE.md` (MIT)
- Consumers compile with `-DVK_NO_PROTOTYPES` and link `cxbx_volk`
  (`src/cxbx/meson.build`); only `src/cxbx/src/hle/d3d8/vulkan/` may include
  `volk.h`.
