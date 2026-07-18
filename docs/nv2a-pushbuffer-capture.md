# NV2A pushbuffer capture and replay

CXBX can record a bounded raw-NV2A frame bundle and replay its PFIFO packet
control flow without booting the title again. The bundle joins the evidence that
was previously spread across logs, draw dumps, and guest memory:

- PFIFO state at the beginning of every pusher run;
- every fetched command word and its guest address;
- every normalized method dispatch;
- the RAMIN object/DMA snapshot;
- exact successful guest-memory reads made while processing the frame;
- normalized scanout pixels and their expected CRC32.

## Capture a frame

The title runner chooses the output path and records the replay verdict in its
`summary.json`:

```powershell
python tools/run_title.py path/to/default.xbe `
  --profile nv2a --capture-pushbuffer 0 --seconds 20 --shots 20
```

`FRAME` is zero-based. A capture includes GPU activity from reset through that
frame, rather than starting at the selected frame, so persistent PGRAPH state is
not missing. The runner does not honor `--until-visible` until the requested
capture has a complete footer.

The runtime interface is also available directly:

| Variable | Meaning |
|---|---|
| `CXBX_NV2A_CAPTURE=<path>` | Enable capture and write the bundle to this path. |
| `CXBX_NV2A_CAPTURE_FRAME=<n>` | Complete after zero-based frame `n`; default `0`. |
| `CXBX_NV2A_CAPTURE_LIMIT_MB=<n>` | Bound output to 64-1024 MiB; default 256 MiB. |

Capture is disabled unless `CXBX_NV2A_CAPTURE` is set. Reaching the byte limit
marks the footer as truncated; replay rejects it unless explicitly allowed.

## Replay and validate

```powershell
python tools/nv2a_capture.py path/to/frame00000.nv2acap
python tools/nv2a_capture.py path/to/frame00000.nv2acap --json
```

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

```powershell
python tools/nv2a_capture.py compare baseline.nv2acap candidate.nv2acap
python tools/nv2a_capture.py compare baseline.nv2acap candidate.nv2acap --json
```

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

A bisect wrapper should generate `candidate.nv2acap` for the checked-out commit
and then return the comparator's result:

```powershell
git bisect run powershell -File path/to/run_capture_bisect.ps1
```

Keep the baseline outside build/run output that the wrapper replaces. Treat
exit code `2` explicitly in the wrapper if an unbuildable commit should be
reported to Git as `125` (skip) instead of bad.

## Replay PGRAPH state

```powershell
python tools/nv2a_capture.py pgraph frame00005.nv2acap
python tools/nv2a_capture.py pgraph frame00005.nv2acap --json
python tools/nv2a_capture.py pgraph baseline.nv2acap candidate.nv2acap
```

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

The host tool independently replays the PFIFO command processor and the
pixel-relevant PGRAPH state machine through draw checkpoints. It does not yet
execute the software pixel backend: triangle setup, texture sampling, depth and
stencil access, combiners, and surface writes still live in `Emu.cpp` and use
emulator-owned memory services. The bundle contains the ordered memory
observations and expected scanouts needed for that extraction. Until then,
checkpoint CRCs isolate command/state divergence and scanout CRC validation
proves capture integrity, while title/probe execution remains the pixel oracle.
