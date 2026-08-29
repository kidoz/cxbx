# DXVK 3.0.2 (x86) — host d3d8 override

`x86/d3d8.dll` and `x86/d3d9.dll` are the Windows-native build of DXVK 3.0.2
(taken from `dxvk-3.0.2.tar.gz`, `x32/` in the archive; d3d8 is implemented
over the shipped d3d9, translated to Vulkan — keep the pair together, do not
mix versions). `LICENSE` is DXVK's zlib license. The tarball's SHA-256 was
checked against the digest published on the release page
(9c538924110a7cdef871ca36dee218c0774124374ffdeb38af4b76be55bdf7c2).

Why Cxbx ships this: Windows updates KB5123304/KB5121003/KB5120708
(2026-08-12) replaced `SysWOW64\d3d8.dll` (10.0.26100.8972). That system d3d8
accepts every call the HLE makes but rasterizes nothing — every title renders
black and runs CPU-bound slow. The cxbx launcher copies this dll pair next to
the `%TEMP%` guest exe before spawn (the guest loads d3d8 from its own
directory first), so the emulated Direct3D renders through DXVK/Vulkan
instead of the broken system dll. 3.0.2 replaced 2.7.1 as the staged default
(2026-08-29); `third_party/dxvk/2.7.1/` stays as the rollback target.

Controls:

- `CXBX_HOST_D3D8_DIR=<dir>` — stage from an alternate directory
  (absolute, or relative to the cxbx.exe directory). Rollback without a
  rebuild: point this at `third_party/dxvk/2.7.1/x86` from a checkout.
- `CXBX_NO_HOST_D3D8=1` — disable staging and run against the system d3d8
  (A/B diagnosis; expect black frames on the affected update).

Driver requirements moved with the 3.0 line: DXVK 3.0 needs a Vulkan 1.4
driver (2.7.1 needed 1.3). On NVIDIA the 3.0 descriptor binding model only
enables from driver 595.84; older drivers run it disabled automatically.

The meson build copies `x86/*.dll` next to the built `cxbx.exe`, so every
build directory (and every launcher path: CLI, frontend, xtest) stages the
override automatically. A single first-chance `0xC0000005` inside
`nvoglv32.dll` (NVIDIA Vulkan ICD) during CreateDevice is the driver's own
handled probe — benign.

Verified 2026-08-29 on 3.0.2: the d3d conformance probes match their goldens
(`d3d_clear_present`, `d3d_state`, `d3d_draw`, `d3d_texture`,
`d3d_shader_lifecycle` plus the rest of the d3d set; the one failing check in
`d3d_pixel_shader_fallbacks` reproduces byte-identically with 2.7.1 and comes
from in-flight HLE work, not the DXVK version), and Turok Evolution renders
as before.
