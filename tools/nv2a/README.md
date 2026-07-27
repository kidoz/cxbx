# tools/nv2a — NV2A draw-level debug tooling

Host-side decoders for the emulator's NV2A (raw pushbuffer / software
rasterizer) debug output. The in-emulator switches they consume live in
`src/cxbx/src/win32/CxbxKrnl/Emu.cpp` and mirror the D3D8 HLE set
(`CXBX_D3D_*`):

| Variable | Effect |
|---|---|
| `CXBX_NV2A_DRAW_TRACE=1` | One `NVDRAW\|` line per rasterized draw or clear: per-frame draw index, kind, begin op, vertex count, VP mode, texture 0 format, blend/depth/alpha/stencil flags. |
| `CXBX_NV2A_SKIP_DRAWS=i[:j]` | Skip NV2A draws/clears with per-frame index in `[i,j)` — binary-search a broken frame. |
| `CXBX_NV2A_DUMP_DRAWS=i[:j]` | After each draw/clear in `[i,j)`, dump the color surface to `%TEMP%\cxbx_nv2a_draw\f<frame>_d<draw>.bmp` plus the full decoded pipeline state as a `.txt` sidecar (capped at 64 dumps). |
| `CXBX_NV2A_DUMP_FRAMES=i[:j]` | Limit draw dumps to frames in `[i,j)` (frame = FLIP_STALL present). |
| `CXBX_NV2A_CRC=1` | One `NVCRC\|` line per present: zlib-compatible CRC32 of the normalized scanout pixels — the frame-level regression signature. |

## Tools

- `drawreport.py [dump-dir]` — build a self-contained HTML contact sheet from a
  `CXBX_NV2A_DUMP_DRAWS` directory: every dumped draw as an image with its
  decoded state, combiner equations, and vertex-program disassembly inline.
- `vp_disasm.py <sidecar.txt|hex-file>` — disassemble NV2A vertex-program
  microcode (field layout mirrors `EmuVshDecoder.cpp`).
- `combiner.py <sidecar.txt>` / `--icw/--ocw/--final` — decode register-combiner
  words into readable equations (layout mirrors
  `include/cxbx/include/core/d3d_pixel_shader_translate.h`).
- `coverage.py <run.log>` — with a `CXBX_NV2A_METHOD_STATS=1` log, rank every
  method the title used and flag the ones the emulator's KELVIN dispatch does
  not handle (the NV2A analogue of the unimplemented-kernel-export trap).
- `methods.py` — shared KELVIN method/class/begin-op name tables.
- `texdump.py <cxbx_texN.argb>` / `--all` / `--png OUT` — decode the
  ground-truth sidecars `CXBX_NV2A_TEXTURE_DUMP=1` writes next to each
  `cxbx_tex<N>.bmp`: the `.argb` holds format-TRUE ARGB (alpha NOT forced —
  the `.bmp` forces alpha opaque for viewers and must never be used to argue
  transparency), the `.raw` holds the source bytes pre-deswizzle. Prints
  alpha/channel statistics and can emit a real-alpha PNG.

## Typical session: "the title renders black"

```powershell
$env:CXBX_NV2A_RASTER = "1"
$env:CXBX_NV2A_DRAW_TRACE = "1"          # count draws per frame first
$env:CXBX_NV2A_DUMP_DRAWS = "0:40"       # then dump a frame draw-by-draw
$env:CXBX_NV2A_DUMP_FRAMES = "5:6"
python tools/run_title.py ...            # or run cxbx directly
python tools/nv2a/drawreport.py          # contact sheet from %TEMP%\cxbx_nv2a_draw
```

Then step the report: the first draw whose output stops matching expectation
names the state to inspect — its combiner equations and vertex program are
already decoded on the card. `CXBX_NV2A_SKIP_DRAWS` confirms a suspect by
bisection; `coverage.py` shows whether the title leans on methods the
rasterizer ignores.

Self-tests: `python tools/nv2a/vp_disasm.py --self-test`,
`python tools/nv2a/combiner.py --self-test`.
