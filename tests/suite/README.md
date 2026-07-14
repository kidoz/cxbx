# Xbox-Original Emulator Conformance & Trace Suite

A set of small, **self-checking** test ROMs (nxdk XBEs) plus a cross-emulator
runner that make building and validating an original-Xbox (2001) emulator
tractable. Each probe exercises one subsystem, *knows the correct answer*, and
emits a structured, deterministic trace. Point the runner at any emulator to see
exactly which subsystem and behaviour diverges.

```
tests/suite/
  common/      xtrace.{h,c}  xtest.{h,c}  probe.mk   # trace harness + build glue
  probes/      smoke cpu_flags memory fileio kernel_cov kernel_trap
  golden/<emulator>/*.golden                          # per-target regression baselines
  build_probe.py                                       # build one probe (MSYS2 + scoop clang; paths from tools/config.toml)
tools/xtest/   xtest.py                                # the runner (build / run / diff / JUnit)
tools/config.toml                                      # local machine config (gitignored; copy config.toml.example)
```

## Quick start

```powershell
cd tools/xtest
python xtest.py list                       # discovered probes
python xtest.py run --emulator cxbx        # build + run all, report + JUnit
python xtest.py run --emulator cxbx --probe cpu_flags   # one probe
python xtest.py run --emulator cxbx --update-golden      # snapshot current results as baseline
```

Run against your own emulator with the custom adapter:

```powershell
python xtest.py run --emulator custom --cmd "myemu --run {xbe} --dvd {rundir}"
```

## Why self-checking?

Ground truth is **encoded in the probe** as assertions (e.g. "ADD 0x7F+0x01 must
set OF and clear CF"). So PASS/FAIL is meaningful without needing captures from
real hardware. This is stronger than diffing raw output against a golden that may
itself be wrong. Golden files are a *secondary* regression layer (per emulator).

## Trace protocol

Every probe writes a line-oriented, deterministic trace. Control lines start with
`#`; data lines are `EV`/`NOTE`; checks are `CHK`.

```
#suite xbox-conformance v1
#probe cpu_flags
NOTE ...informational...
EV   add8.7F+01 res=0x80 eflags=0x00000892      # observed data (for debugging)
CHK  add8.7F+01 expect=0x00000080 got=0x00000080 PASS
CHK  add8.7F+01.OF expect=1 got=1 PASS
...
#result cpu_flags verdict=PASS checks=85 fail=0
#end
```

The runner keys on `CHK ... PASS|FAIL` and the `#result` line. A probe also
returns `main() == fail-count`, so the process exit code corroborates the trace.

### Trace channels

Each line is written to **two** machine-readable channels simultaneously, so a
trace is captured no matter how complete the target emulator is:

1. **`D:\<probe>.trace`** — a clean file on the title's `D:` drive. Every Xbox
   emulator maps `D:` to the running title's directory, so this lands on the
   host. **Primary channel.**
2. **`DbgPrint` (kernel ordinal 8)** — each line mirrored with an `XT| ` prefix.
   Captured in the emulator's debug output even before its file I/O works.
   **Fallback channel** for early-stage emulators.

An optional on-screen channel (`xt_enable_screen()`) exists for visual
confirmation on real hardware / GPU-emulating targets; it is off by default so
probes stay portable to emulators without working video.

## Integrating your emulator

The suite is emulator-agnostic. To run it against your emulator you need only:

1. **A way to launch an XBE headlessly** and terminate when the guest exits
   (probes return from `main`, they do not loop).
2. **One output channel** the runner can read back, either:
   - map the guest `D:` drive to a host directory and let the runner read
     `<rundir>/<probe>.trace` (recommended — it is what real titles expect), or
   - capture kernel `DbgPrint` (ordinal 8) text to a log and point the runner at
     it (the `XT| `-prefixed lines are the trace).

Then add an adapter. The built-in `custom` adapter needs no code:

```powershell
python xtest.py run --emulator custom --cmd "myemu --run {xbe} --out {rundir}"
```

`{xbe}` `{rundir}` `{log}` are substituted per run. The runner locates
`<probe>.trace` anywhere under `{rundir}`. Results are baselined under
`golden/<emulator>/` so each target tracks its own progress independently.

## Capability tags (target filtering)

Some probes need an emulator feature that not every target has (e.g. an NV2A GPU
model that traps `0xFD000000`). A probe declares required tags in an optional
`probes/<name>/probe.toml`:

```toml
tags = ["nv2a"]
```

A target advertises what it supports via `capabilities` in `tools/config.toml`
(`[emulator.cxbx] capabilities = ["nv2a"]`) or `--capability nv2a` on the command
line. When running the full set, a probe whose tags aren't all covered is
**skipped** (reported, not failed) — so an `nv2a` probe won't hang an emulator
that lacks a GPU model. Bypass with `--all`, or force one with `--probe <name>`
(explicitly named probes always run). Untagged probes run everywhere.

## Adding a probe

1. `mkdir probes/<name>`; add `main.c` and a `Makefile`:
   ```make
   XBE_TITLE = xtest_<name>
   include ../../common/probe.mk
   ```
2. In `main.c`:
   ```c
   #include "xtest.h"
   int main(void) {
       xt_begin("v1", "<name>");           // trace name MUST match the dir name
       ...
       xt_check_u32("thing", expected, observed);
       return xt_end();                    // exit code == fail count
   }
   ```
3. `python xtest.py run --probe <name> --update-golden`.

Harness API: `xt_begin/xt_end`, `xt_ev/xt_note`,
`xt_check_u32/u64/bool/str`, `xt_check` (generic), `xt_check_flags` (EFLAGS),
`xt_enable_screen`. See `common/xtrace.h`.

### XDK-toolchain probes (`build.ps1` instead of a `Makefile`)

nxdk probes contain no XDK library code, so they can never exercise the HLE
layer (`EmuD3D8`/`EmuDSound`/`EmuXapi`) — OOVPA signatures have nothing to
match. A probe directory with a **`build.ps1`** is instead built with the real
Xbox XDK 5849 toolchain from `other/sdk/XDKSetup5849.15_extracted/XDK`:

```
xbox\bin\vc71\CL.Exe   (/D_XBOX /ML, /I xbox\include)
xbox\bin\vc71\Link.Exe (/MACHINE:I386 /FIXED:NO /SUBSYSTEM:XBOX, xbox\lib)
xbox\bin\imagebld.exe  (/IN:probe.exe /OUT:bin\default.xbe)
```

so the image links genuine `xapilib`/`d3d8`/`dsound` code — exactly what real
titles ship. Such probes are tagged `xdk` (skipped on targets that can't boot
XAPI images) and declare the run-time env recipe in `probe.toml`'s `[env]`
table (on Cxbx: `CXBX_KERNEL_SKIP_INIT=1` + `CXBX_FS_SWAP=1`). End the probe
with `HalReturnToFirmware(2)` — returning from `main()` parks in the XAPI
dashboard-relaunch path under emulation. See `probes/xdk_smoke/` for the
template; `common/xdk_xtrace.h` is the shared trace harness for XDK probes.

The SDK also ships distinct `d3d8d.lib` (Debug) and `d3d8i.lib` (Profile)
images. Their instrumented function bodies need their own HLE signatures; the
retail `d3d8.lib` OOVPAs are not assumed to match. `d3d_debug` and `d3d_perf`
check this resolution boundary before calling CreateDevice, so unsupported
variants produce a short named FAIL rather than entering raw NV2A code.

Pixel-readback discipline for D3D probes: the HLE `Surface_LockRect` leaves
the host surface locked (Xbox D3D has no UnlockRect in the HLE table), so do
ALL rendering first and a single readback at the end — never draw or present
after locking the backbuffer.

## Probes (v1)

| Probe        | Subsystem            | What it checks                                              |
|--------------|----------------------|------------------------------------------------------------|
| `smoke`      | harness              | trace channels + clean exit                                |
| `cpu_flags`  | CPU / x86            | value + EFLAGS (CF/PF/AF/ZF/SF/OF) for add/sub/and/or/xor   |
| `memory`     | RAM / allocator      | contiguous alloc, page alignment, byte/word write-readback |
| `fileio`     | FATX / file system   | create/write/reopen/size/read/seek/delete on `D:`          |
| `fileio_vfs` | FATX / VFS metadata  | gap probe: `GetFileSize` (works) vs the broken VFS layer above leaf I/O -- `CreateDirectory` never materializes a dir, `GetFileAttributes` returns INVALID for an existing file, `MoveFile` fails; `FindFirstFile`/`NtQueryDirectoryFile` hangs (not exercised) |
| `kernel_cov` | kernel HLE           | time sources monotonic, `Rtl` string/memory ops            |
| `kernel_trap`| kernel HLE (missing) | calling an unimplemented export is trapped, not crashed     |
| `kernel_thread` | kernel HLE (threading) | `KeGetCurrentThread`, event lifecycle (`KeInitializeEvent`/`Set`/`Reset`), non-blocking `KeWaitForSingleObject`, `KeDelayExecutionThread`; documents the `PsCreateSystemThread` spawn gap (spawned threads get no Xbox FS selector on Cxbx) |
| `gfx`        | graphics / video mem | framebuffer get/write/readback + FNV-1a fingerprint         |
| `nv2a_pmc`   | NV2A PMC / RAMIN     | `PMC_BOOT_0` chip ID, `PMC_ENABLE`, RAMIN write/readback (0xFD MMIO) |
| `nv2a_intr`  | NV2A interrupts      | `INTR_EN` latch, `PMC_INTR_0` aggregation, write-1-to-clear |
| `nv2a_pfifo` | NV2A PFIFO / PGRAPH  | DMA object + pushbuffer → pusher → PGRAPH method dispatch   |
| `nv2a_pvideo`| NV2A PVIDEO (gap)    | documents the video-port overlay/capture gap: registers are cache-backed (round-trip) but not modeled (expectations flip when PVIDEO lands) |
| `xdk_smoke`  | XDK runtime (HLE)    | XDK-5849-built XBE boots via xapilib, file I/O, clean exit  |
| `hle_resolve`| OOVPA/HLE database   | per-function: the HLE pass patched this real d3d8/dsound.lib function (expect=0 entries = documented signature debt) |
| `d3d_clear_present` | D3D8 HLE (host GPU) | CreateDevice → Clear → Swap → pixel-exact backbuffer readback |
| `d3d_makespace` | XDK D3D8 push cursor | MakeSpace HLE resolution plus non-null, writable eight-DWORD return storage |
| `d3d_draw`   | D3D8 HLE draw paths  | DrawVerticesUP triangles + immediate-mode Begin/End quad, pixel-exact |
| `d3d_texture`| D3D8 HLE texture path | CreateTexture2 → LockRect upload → SetTexture → textured draws (both paths), pixel-exact |
| `d3d_state`  | D3D8 HLE state       | Set/GetTransform bit-exact round-trips, SetRenderState_* family survival, GetDisplayMode sanity |
| `d3d_shader_lifecycle` | XDK 4627 D3D8 shaders | Create/set/delete pixel shader, repeated delete, and zero-handle safety |
| `d3d_rendertarget` | XDK 4627 D3D8 surfaces | SetRenderTarget HLE interception, NULL-depth handling, and render/depth restoration |
| `d3d_stencil_state` | XDK 4627 D3D8 state | StencilFail, variable-count constants, and immediate-mode HLE interception |
| `d3d_tex_swizzle` | D3D8 HLE fidelity | documents the swizzled-texture gap: Morton-order uploads render linear (expectations flip when unswizzle lands) |
| `d3d_debug` | XDK debug D3D8 | `d3d8d` HLE resolution, debug-marker state/reset, and `D3D__SingleStepPusher` idle barrier |
| `d3d_perf` | XDK instrumented D3D8 | `d3d8i` HLE resolution, API counters/reset, PIX event nesting, and push-buffer accounting |
| `d3d_pushbuffer` | XDK D3D8 command stream | push-buffer record/offset/replay, kick/idle, fences, display field, and pixel readback |
| `ds_buffer`  | DSOUND HLE (host audio) | create → PCM upload → Play → play cursor advances in real time → Stop |
| `ds_status`  | DSOUND HLE (buffer state) | GetStatus state machine (stopped → PLAYING+LOOPING → stopped → PLAYING-only) + Lock/Unlock write-readback |
| `ds_stream`  | DSOUND HLE (stream packets) | XDK 4627 queue → pending → host playback completion/callback → flush → release lifecycle |
| `xinput_state` | XAPI input HLE      | device enumeration + XInputGetState returns the CXBX_INPUT_STATE-injected pad state (headless input) |
| `ds_nestopia`| DSOUND HLE (title pattern) | NestopiaX's soundNES.cpp lifecycle API-for-API: dual buffers, SetMixBins, Lock/Unlock ring updates, GetStatus |

The `nv2a_*` probes reach the GPU through the `0xFD000000` MMIO aperture (and, for
`nv2a_pfifo`, guest physical memory at `0x80000000`), which CXBX trap-and-emulates
into its NV2A model. They are tagged `nv2a` (see capability filtering) so they are
skipped on targets that don't advertise a GPU model.

## Emulator-side support in Cxbx (the "warned trap")

Stock Cxbx stored an unimplemented kernel ordinal's own number in the thunk
table, so calling it jumped to a bogus address and crashed with no diagnostic.
`KernelThunk.cpp` now (default `CXBX_TRAP_UNIMPLEMENTED=1`) points each
unimplemented ordinal at a per-ordinal template stub that logs

```
KTRACE| UNIMPLEMENTED ordinal=<n> (0x<nnn>) caller=<addr>
```

and returns 0, so a title survives long enough to tell you *which export to
implement next*. `kernel_trap` verifies this end-to-end. Caveat: the Xbox kernel
ABI is `__stdcall`; for a multi-argument unimplemented export the stub cannot
restore the caller stack, so the title may still destabilise after the warning —
but you get the diagnostic first. `EmuKrnlLogging.h` also provides `KTRACE()` to
standardise per-thunk tracing.

## Graphics / NV2A — scope note

This Cxbx tree now has a **register-level NV2A model** (`Emu.cpp`): it
trap-and-emulates the `0xFD000000` MMIO aperture — PMC, PFIFO with a DMA pusher,
PGRAPH method dispatch, and 1 MiB of RAMIN with RAMHT lookup. The `nv2a_pmc` /
`nv2a_intr` / `nv2a_pfifo` probes validate exactly that layer and pass on this
build.

What it is **not** is a rendering GPU: Cxbx still forwards retail Direct3D8 to host
D3D and does not rasterize the NV2A pipeline, so it produces no scanned-out
framebuffer for nxdk homebrew — which is why `gfx.fb_nonnull` (via `XVideoGetFB`)
fails here. Full rendering/framebuffer conformance remains meaningful only on
GPU-rasterizing targets (xemu, Cxbx-Reloaded) or real hardware. The
CPU/memory/file/kernel probes are fully headless and target-independent.

## Current findings (target: cxbx)

Running the suite against this repo's Cxbx build (**8 PASS, 2 FAIL of 10**) — the
three `nv2a_*` probes pass against the new NV2A model — surfaces two real
divergences:

- **`memory / mm.alloc_page_aligned` FAIL** — `MmAllocateContiguousMemory`
  returns a non-page-aligned pointer (observed offset `0x1E0`). A real Xbox
  always returns page-aligned (4 KiB) contiguous memory.
- **`gfx / gfx.fb_nonnull` FAIL** — `XVideoGetFB()` returns NULL: the NV2A model
  is register-level only (no rasterizer / scanned-out framebuffer), so nxdk's
  framebuffer isn't stood up. On a GPU-rasterizing target this probe returns a
  valid framebuffer and runs the readback + hash checks.

Both are exactly the kind of subsystem divergence the suite exists to surface.
`smoke`, `cpu_flags`, `fileio`, `kernel_cov`, `kernel_trap`, and the three
`nv2a_*` probes all PASS.
