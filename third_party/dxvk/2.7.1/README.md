# DXVK 2.7.1 (x86) — host d3d8 override (superseded)

Superseded as the staged default by `third_party/dxvk/3.0.2/` (2026-08-29);
kept as the rollback target (`CXBX_HOST_D3D8_DIR` or a meson repoint).

`x86/d3d8.dll` and `x86/d3d9.dll` are the Windows-native build of DXVK 2.7.1
(taken from `dxvk-2.7.1.tar.gz`, `x32/` in the archive; d3d8 is implemented
over the shipped d3d9, translated to Vulkan — keep the pair together, do not
mix versions). `LICENSE` is DXVK's zlib license.

Why Cxbx ships this: Windows updates KB5123304/KB5121003/KB5120708
(2026-08-12) replaced `SysWOW64\d3d8.dll` (10.0.26100.8972). That system d3d8
accepts every call the HLE makes but rasterizes nothing — every title renders
black and runs CPU-bound slow. The cxbx launcher copies this dll pair next to
the `%TEMP%` guest exe before spawn (the guest loads d3d8 from its own
directory first), so the emulated Direct3D renders through DXVK/Vulkan
instead of the broken system dll. Verified 2026-08-29: Turok Evolution
renders again and the d3d conformance probes match their goldens.

Controls:

- `CXBX_HOST_D3D8_DIR=<dir>` — stage from an alternate directory
  (absolute, or relative to the cxbx.exe directory).
- `CXBX_NO_HOST_D3D8=1` — disable staging and run against the system d3d8
  (A/B diagnosis; expect black frames on the affected update).

The meson build copies `x86/*.dll` next to the built `cxbx.exe`, so every
build directory (and every launcher path: CLI, frontend, xtest) stages the
override automatically. A single first-chance `0xC0000005` inside
`nvoglv32.dll` (NVIDIA Vulkan ICD) during CreateDevice is the driver's own
handled probe — benign.
