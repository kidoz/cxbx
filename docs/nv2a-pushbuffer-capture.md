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

The host tool replays and verifies the PFIFO command processor. It does not yet
rerun the software PGRAPH rasterizer independently: that implementation still
lives in `Emu.cpp` and depends on emulator-owned mutable state. The bundle
already contains the method stream, RAMIN, memory observations, and expected
scanout needed for that next extraction. Until then, scanout CRC validation
proves capture integrity, while title/probe execution remains the rendering
oracle.
