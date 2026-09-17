# Title-debugging design

[Explanation](README.md)

Title bring-up should answer three questions without manually reading a large
log or opening every screenshot:

1. Did the guest start and remain alive?
2. Did it produce a meaningful client frame?
3. Which frame or draw first diverged from a known-good run?

The low-level pieces already exist: structured traces, `NVCRC|` frame hashes,
`NVDRAW|` draw indices, state sidecars, method coverage, XDK probes, and OOVPA
analysis. The missing layer was a stable manifest joining those artifacts into
one machine-readable title-run result.

## Implemented evidence and replay

The [title-run manifest](../reference/title-run.md) joins startup, image,
fault, and draw evidence. [Capture replay](../reference/nv2a-capture.md)
compares PFIFO commands, PGRAPH checkpoints, and a bounded set of pixel
operations without booting the title. Its reference defines the supported
boundary and exit codes.

## Planned tooling

The following are development directions, not available commands.

### Manifest comparison

Compare two `summary.json` files by aligned frame CRC, draw count, faults, and
representative-image metrics. This should become the title-level CI verdict and
the predicate for automated `git bisect run`.

### State diffs at the first divergent draw

Store normalized state snapshots keyed by frame/draw and compare them field by
field. Surface identity, viewport, vertex program, constants, texture formats,
and combiner equations should be compared structurally instead of as log text.

### Golden XDK probes

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
