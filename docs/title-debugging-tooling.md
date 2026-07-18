# Title debugging tooling

## Goal

Title bring-up should answer three questions without manually reading a large
log or opening every screenshot:

1. Did the guest start and remain alive?
2. Did it produce a meaningful client frame?
3. Which frame or draw first diverged from a known-good run?

The low-level pieces already exist: structured traces, `NVCRC|` frame hashes,
`NVDRAW|` draw indices, state sidecars, method coverage, XDK probes, and OOVPA
analysis. The missing layer was a stable manifest joining those artifacts into
one machine-readable title-run result.

## Title-run manifests

`tools/run_title.py` now writes `summary.json` beside each `run.log`. It records:

- exact XBE, emulator, profile, and explicit environment overrides;
- guest startup and termination outcome;
- client-area screenshot metrics and capture source;
- the automatically selected representative screenshot;
- exception, fatal, and warning counts;
- `NVCRC|` records and per-frame `NVDRAW|` counts;
- whether the live NV2A overlay produced visible pixels.

The capture helper measures the real Win32 client rectangle. This avoids both
the old hard-coded title-bar offset and false image scores from desktop pixels.

```powershell
python tools/run_title.py "other/games/Beyond Good And Evil" `
  --profile nv2a --seconds 30 --shots 30 --until-visible
```

`--until-visible` stops after a frame has at least 1% non-black sampled pixels
and the requested color variation (`--visible-colors`, default 32). This is a
bring-up optimization, not a correctness verdict. Regression gates should use
frame CRCs or golden images.

## Recommended development loop

1. Run once with `--until-visible` to prove that startup reaches rendering.
2. Enable `CXBX_NV2A_CRC=1` and `CXBX_NV2A_DRAW_TRACE=1` for stable frame and
   draw identities.
3. Restrict `CXBX_NV2A_DUMP_FRAMES` and `CXBX_NV2A_DUMP_DRAWS` to one bad frame.
4. Use `tools/nv2a/drawreport.py` to identify the first bad draw.
5. Confirm it with `CXBX_NV2A_SKIP_DRAWS` and replace the focused title case
   with an XDK conformance probe where possible.

## Next tooling investments

### 1. Capture comparison (implemented)

`tools/nv2a_capture.py compare` validates and replays two `.nv2acap` bundles,
then reports the first global and per-category divergence. It normalizes
relocated host addresses by default, supports strict address comparison, emits
JSON, and returns stable match/differ/invalid exit codes for bisect wrappers.

### 2. Manifest comparison

Compare two `summary.json` files by aligned frame CRC, draw count, faults, and
representative-image metrics. This should become the title-level CI verdict and
the predicate for automated `git bisect run`.

### 3. Pushbuffer capture and replay (implemented for PFIFO)

`--capture-pushbuffer FRAME` now records a bounded versioned bundle containing
PFIFO run state, fetched words/addresses, method dispatches, RAMIN, exact memory
reads, and the normalized scanout/CRC. `tools/nv2a_capture.py` replays packet
control flow and verifies every captured payload. See
`docs/nv2a-pushbuffer-capture.md`.

Independent PGRAPH raster replay remains the next extraction: move the mutable
raster state out of `Emu.cpp` behind a reusable state object, then feed the
captured method and memory records into it.

### 4. State diffs at the first divergent draw

Store normalized state snapshots keyed by frame/draw and compare them field by
field. Surface identity, viewport, vertex program, constants, texture formats,
and combiner equations should be compared structurally instead of as log text.

### 5. Golden XDK probes

Use official-XDK probes for isolated texture, shader, clipping, input, and
audio semantics. Title traces should discover missing cases; probes should own
the permanent regression contract.

## External precedents

- xemu exposes named NV2A method, DMA, surface lifecycle, and flip trace events:
  <https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/trace-events>
- apitrace supports trimmed call sets, per-draw snapshots, state diffs, image
  regression testing, and automated Git bisection:
  <https://github.com/apitrace/apitrace/blob/master/docs/USAGE.markdown>
- RenderDoc exposes capture replay through a Python controller, demonstrating
  why an offline replay object is more useful than screenshots alone:
  <https://github.com/baldurk/renderdoc/blob/v1.x/docs/python_api/examples/renderdoc_intro.py>
- Dolphin's FIFO Player records and replays a bounded GPU command stream for
  graphics debugging:
  <https://github.com/dolphin-emu/dolphin/wiki/FIFO-Player-Overview>

Direct RenderDoc integration is not the first priority for the raw NV2A path:
the software rasterizer does not issue a one-to-one host graphics API draw for
each guest method. A guest-level pushbuffer replay bundle preserves the state
that CXBX actually needs to debug.
