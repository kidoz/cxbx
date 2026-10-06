# Inspect an NV2A capture offline

[How-to guides](README.md)

Use this guide to inspect reconstructed draw state and determine whether a
capture's scanouts can be reproduced by the offline pixel renderer.

Prerequisites: Python 3.14 or later with uv and a completed `.nv2acap` bundle.
[Capture and validate a bundle](capture-nv2a.md) first. Run commands from the
repository root and replace the example capture path with your own.

## Inspect PGRAPH state checkpoints

```powershell
uv run python tools/nv2a_capture.py pgraph frame00000.nv2acap
uv run python tools/nv2a_capture.py pgraph frame00000.nv2acap --json
```

The text report gives method and checkpoint counts and the final state CRC.
Use the JSON report to inspect individual draw, clear, and present checkpoints
and relate their frame and source indices to the recorded execution.

A single-input PGRAPH replay exits `0` when valid and `2` when the capture is
invalid. See the [PGRAPH replay contract](../reference/nv2a-capture.md#replay-pgraph-state)
for the state represented by each checkpoint. To find a checkpoint that changed
between runs, use [Compare NV2A captures](compare-nv2a-captures.md).

## Check independently replayed pixels

```powershell
uv run python tools/nv2a_capture.py pixels frame00000.nv2acap
uv run python tools/nv2a_capture.py pixels frame00000.nv2acap --json
```

Inspect coverage and actual versus expected CRCs for every scanout. If the
report is `PARTIAL`, inspect its unsupported checkpoints and observation
conflicts before treating a CRC difference as a renderer regression.

`PASS` with exit `0` requires complete independently produced output and
matching scanout CRCs. `PARTIAL` exits `1`; corrupt input exits `2`. See the
[pixel replay contract](../reference/nv2a-capture.md#replay-deterministic-pixels)
and [supported boundary](../reference/nv2a-capture.md#current-boundary) for the
operations that can be replayed.
