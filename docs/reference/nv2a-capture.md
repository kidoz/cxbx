# NV2A capture and replay reference

[Reference](README.md)

CXBX can record a bounded raw-NV2A frame bundle and replay its PFIFO packet
control flow without booting the title again. The bundle joins the evidence that
was previously spread across logs, draw dumps, and guest memory:

- PFIFO state at the beginning of every pusher run;
- every fetched command word and its guest address;
- every normalized method dispatch;
- the RAMIN object/DMA snapshot;
- exact successful guest-memory reads made while processing the frame;
- normalized scanout pixels and their expected CRC32.

For commands and workflow, see [Capture and compare NV2A execution](../how-to/capture-nv2a.md).

## Capture controls

`FRAME` is zero-based. A capture includes GPU activity from reset through that
frame, rather than starting at the selected frame, so persistent PGRAPH state is
not missing. The runner does not honor `--until-visible` until the requested
capture has a complete footer.

| Variable | Meaning |
|---|---|
| `CXBX_NV2A_CAPTURE=<path>` | Enable capture and write the bundle to this path. |
| `CXBX_NV2A_CAPTURE_FRAME=<n>` | Complete after zero-based frame `n`; default `0`. |
| `CXBX_NV2A_CAPTURE_LIMIT_MB=<n>` | Bound output to 64-1024 MiB; default 256 MiB. |

Capture is disabled unless `CXBX_NV2A_CAPTURE` is set. Reaching the byte limit
marks the footer as truncated; replay rejects it unless explicitly allowed.

## Replay and validate

Replay checks:

1. Header, record sizes, byte limit, and completion footer.
2. CRC32 of every RAMIN, memory, and scanout payload.
3. PFIFO `GET` progression through increasing/non-increasing packets, jumps,
   calls, and returns.
4. Exact equality between replayed and runtime-observed method dispatches.
5. Equality between the final scanout CRC and footer CRC.

The command exits nonzero on corruption or divergence, so it can be used as a
focused regression predicate.

## Compare captures

Comparison validates and replays both inputs before comparing them. It reports
the first divergence in the complete record stream and independently for PFIFO
runs, fetched words, methods, memory observations, RAMIN, scanouts, and the
completion footer.

Host allocations can move between processes. The default comparison therefore
uses pusher addresses relative to each run's base, normalizes embedded jump/call
targets, and ignores absolute memory and scanout addresses. Method data,
non-control command words, PFIFO state, sizes, payload CRCs, and output CRCs
remain exact. Use `--strict-addresses` when raw address identity is part of the
test.

Exit codes form a stable automation contract:

| Code | Meaning |
|---:|---|
| `0` | Captures match. |
| `1` | A valid capture differs. |
| `2` | An input is missing, corrupt, truncated, or otherwise invalid. |

## Replay PGRAPH state

PGRAPH replay resolves subchannel object bindings from the captured RAMIN image,
filters non-Kelvin methods, and applies the Kelvin state stream without starting
the emulator. It emits deterministic checkpoints for clears, `DRAW_ARRAYS`,
indexed batches, inline batches, immediate-mode batches, and presents.

Each checkpoint records its source record/method index, frame, primitive and
vertex/word count, surface bindings, important pipeline fields, and a canonical
state CRC. The CRC covers method registers, reset defaults, object bindings,
transform program and constants, viewport and composite transforms, index and
inline data, and complete immediate-vertex history. Comparing two replays
reports the first checkpoint whose command or pixel-relevant state differs.

The PGRAPH command uses the same automation exit codes as capture comparison:
`0` for a valid replay or match, `1` for a valid replay difference, and `2` for
an invalid capture.

## Replay deterministic pixels

Pixel replay reconstructs sparse surfaces from ordered captured-memory
observations and resolves color, zeta, and vertex DMA objects through RAMIN. It
executes color/depth/stencil clear masks; fixed-function `DRAW_ARRAYS` and
indexed batches with float positions plus optional packed or float diffuse
color; triangle, strip, fan, quad, quad-strip, and polygon assembly; Gouraud
color interpolation; Z16/Z24 depth tests and writes; and multisampled color
resolve into the presented scanout surface.

The bounded texture path supports one active stage in 2D-projective mode. It
replays the rasterizer's uncompressed 8-, 16-, and 32-bit formats, including
swizzled P8 surfaces and their captured palettes; point and bilinear
magnification; wrap, mirror, and clamp addressing; and texture-only, diffuse
modulate, or factor-RGB/diffuse-alpha register combiners with a passthrough
final combiner. Texture coordinates use the captured projective Q and
perspective-correct interpolation. Direct-host texture, palette, and vertex
reads are recorded under their NV2A-visible physical-mirror addresses so replay
does not depend on process-specific host pointers.

Draw execution is deferred until the memory records caused by the submission
have been consumed. Indexed checkpoints retain the submitted indices, and
physical-memory mirror addresses are normalized when fetching captured vertex
bytes. Scanout-source reads are explicitly excluded from replay inputs, so the
expected framebuffer cannot make an unimplemented pixel path pass accidentally.

Every scanout reports known-byte coverage plus actual and expected CRC32 values.
The command reports `PASS` and exits `0` only when every output byte was produced
or initialized independently, all scanout CRCs match, captured observations do
not conflict with replay-owned bytes, and the capture contains no unsupported
draw checkpoints. A valid but incomplete replay reports `PARTIAL` and exits `1`;
corrupt input exits `2`.

## Version 1 binary format

All integers are unsigned little-endian values. The 32-byte header is:

| Field | Size |
|---|---:|
| Magic `CXNVCAP\0` | 8 bytes |
| Format version | 4 bytes |
| Endian marker `0x01020304` | 4 bytes |
| Header size | 4 bytes |
| Target frame | 4 bytes |
| Declared byte limit | 8 bytes |

Each record begins with a 32-bit type and 32-bit payload size. Version 1 record
types are `PushRun`, `PushWord`, `Method`, `Memory`, `Scanout`, `Finish`, and
`Ramin`. Variable byte payloads carry their own size and CRC32. Readers must
skip unknown record types by payload size so later versions can add evidence
without invalidating version 1 parsers.

## Current boundary

The host tool independently replays PFIFO, pixel-relevant PGRAPH state, DMA
surface resolution, clears, fixed-function vertex-array draws, bounded stage-0
texture sampling and register combining, triangle rasterization, depth,
draw-surface writes, and multisample presents. Vertex programs, multitexture and
non-projective texture modes, scaled or otherwise unrecognized register
combiners, non-passthrough final combiners, alpha/stencil tests during draws,
blending, and inline/immediate vertex layouts remain explicit unsupported
checkpoints. The bundle contains the ordered memory observations and expected
scanouts needed for those extractions. Checkpoint CRCs isolate command/state
divergence; known-byte coverage prevents partial pixel replay from being
mistaken for a complete rendering result.
