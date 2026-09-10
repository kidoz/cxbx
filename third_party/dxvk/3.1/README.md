# DXVK 3.1 (x86) — host d3d8 override

The matched `x86/d3d8.dll` and `x86/d3d9.dll` pair comes from the `x32/`
directory of the official [DXVK 3.1 release](https://github.com/doitsujin/dxvk/releases/tag/v3.1),
published 2026-08-28. D3D8 uses the shipped D3D9 implementation, which
translates to Vulkan; keep both DLLs on the same version.

## Provenance

- Archive: [dxvk-3.1.tar.gz](https://github.com/doitsujin/dxvk/releases/download/v3.1/dxvk-3.1.tar.gz).
- Archive SHA-256: `30f9cc326874be344285582275446968cfa4c069db31ce56df312d6644179154`.
- Digest checked against the official GitHub release asset metadata on 2026-09-08.
- Both DLLs retain their upstream bytes and use the PE i386 architecture.
- `d3d8.dll` SHA-256: `6b8730179ac210d1c57243baeaaba8a1ddbc602c4bce9e9aee3fb8b9d77c9179`.
- `d3d9.dll` SHA-256: `2efb4c004abed6028349010f1a647d5b09616c67fd7f40fa8175ddb7b5f16e58`.
- `LICENSE` is the unmodified zlib/libpng notice from the upstream `v3.1` tag.

Only the D3D8/D3D9 pair is bundled; this update adds no D3D10, D3D11 or DXGI
DLLs. DXVK 3.1 retains the 3.x [Vulkan 1.4 driver requirement](https://github.com/doitsujin/dxvk/wiki/Driver-support).
Relevant upstream
changes include a D3D9 fog regression fix and a workaround for a specific
NVIDIA out-of-memory crash with descriptor heaps enabled. These release notes
do not establish that any particular emulator crash is fixed.

## Integration

Meson's `host_d3d8_stage` target stages this pair in the launcher's `host-d3d8`
subdirectory. The launcher copies it beside the generated guest executable.
The pair supports the D3D8 backend and remaining host D3D8 operations used by
the native Vulkan backend.

This version is the staged default. To use an externally supplied matched
DLL pair without rebuilding, set `CXBX_HOST_D3D8_DIR` to its directory.
`CXBX_NO_HOST_D3D8=1` disables staging for system-D3D8 diagnostics.
