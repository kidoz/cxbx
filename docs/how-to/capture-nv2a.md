# Capture and compare NV2A execution

[How-to guides](README.md)

Prerequisites: Python 3.14 or later with uv; a configured title runner and
built CXBX for capture; completed `.nv2acap` bundles for offline replay.
Run commands from the repository root. Replace example title and capture
paths with your own. See [title debugging](debug-title.md) for runner setup.

## Capture a frame

```powershell
uv run python tools/run_title.py path/to/default.xbe `
  --profile nv2a --capture-pushbuffer 0 --seconds 20 --shots 20
```

Choose the zero-based frame number. The runner records the output bundle and
replay verdict in `summary.json`. Capture includes activity from reset through
the selected frame; allow enough time for the completion footer to be written.
For output limits and direct runtime controls, see
[Capture controls](../reference/nv2a-capture.md#capture-controls).

## Replay and validate

```powershell
uv run python tools/nv2a_capture.py path/to/frame00000.nv2acap
uv run python tools/nv2a_capture.py path/to/frame00000.nv2acap --json
```

A nonzero exit indicates corruption or divergence. Inspect the report before
using the capture as a baseline. `--json` emits a machine-readable report.

See [replay semantics](../reference/nv2a-capture.md#replay-and-validate) for the exact contract.

## Compare captures

```powershell
uv run python tools/nv2a_capture.py compare baseline.nv2acap candidate.nv2acap
uv run python tools/nv2a_capture.py compare baseline.nv2acap candidate.nv2acap --json
```

The report locates the first global and per-category difference. Use
`--strict-addresses` only when address identity is part of the test; default
comparison normalizes relocated addresses. Exit codes are `0` (match),
`1` (difference), and `2` (invalid input).

A bisect wrapper should generate `candidate.nv2acap` for the checked-out commit
and then return the comparator's result:

```powershell
git bisect run uv run python path/to/run_capture_bisect.py
```

Keep the baseline outside build/run output that the wrapper replaces. Treat
exit code `2` explicitly in the wrapper if an unbuildable commit should be
reported to Git as `125` (skip) instead of bad.

See [replay semantics](../reference/nv2a-capture.md#compare-captures) for the exact contract.

## Replay PGRAPH state

```powershell
uv run python tools/nv2a_capture.py pgraph frame00005.nv2acap
uv run python tools/nv2a_capture.py pgraph frame00005.nv2acap --json
uv run python tools/nv2a_capture.py pgraph baseline.nv2acap candidate.nv2acap
```

Use one input to inspect state checkpoints, or two to locate the first
different checkpoint. Exit codes are `0` (valid replay or match),
`1` (difference), and `2` (invalid input).

See [replay semantics](../reference/nv2a-capture.md#replay-pgraph-state) for the exact contract.

## Replay deterministic pixels

```powershell
uv run python tools/nv2a_capture.py pixels frame00000.nv2acap
uv run python tools/nv2a_capture.py pixels frame00000.nv2acap --json
```

Inspect coverage and CRCs for every scanout. `PASS` / exit `0` requires
complete independent output; `PARTIAL` / exit `1` is not a rendering pass.
Corrupt input exits `2`.

See [replay semantics](../reference/nv2a-capture.md#replay-deterministic-pixels) for the exact contract.
