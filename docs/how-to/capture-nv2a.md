# Capture an NV2A frame for offline analysis

[How-to guides](README.md)

Use this guide to collect a complete `.nv2acap` bundle and check that it is
valid before using it in a graphics investigation.

Prerequisites: Python 3.14 or later with uv, a built CXBX, and a configured
title runner. See [title debugging](debug-title.md) for runner setup.
Run commands from the repository root. Replace example title and capture
paths with your own.

## 1. Capture through the selected frame

```powershell
uv run python tools/run_title.py path/to/default.xbe `
  --profile nv2a --capture-pushbuffer 0 --seconds 20 --shots 20
```

Replace `0` with the zero-based frame number you want to investigate. The
runner prints its output directory and records the bundle path and replay
verdict in `summary.json`. Capture includes activity from reset through the
selected frame; allow enough time for the completion footer to be written.
For output limits and direct runtime controls, see
[Capture controls](../reference/nv2a-capture.md#capture-controls).

## 2. Validate the completed bundle

```powershell
uv run python tools/nv2a_capture.py path/to/frame00000.nv2acap
uv run python tools/nv2a_capture.py path/to/frame00000.nv2acap --json
```

A zero exit confirms that the bundle passes structural and PFIFO replay
checks. A nonzero exit indicates corruption or divergence; inspect the report
before using the capture as a baseline. If the bundle is incomplete, collect
it again with more time. If it reached the byte limit, choose a larger limit
within the supported range in the capture reference. `--json` emits a
machine-readable report.

See [validation semantics](../reference/nv2a-capture.md#replay-and-validate)
for the exact checks. Validation establishes that the recorded command stream
can be replayed; to investigate rendering, continue with
[Inspect an NV2A capture offline](inspect-nv2a-capture.md). To locate a
regression between two runs, use
[Compare NV2A captures](compare-nv2a-captures.md).
