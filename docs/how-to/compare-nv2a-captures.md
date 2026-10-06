# Compare NV2A captures

[How-to guides](README.md)

Use this guide to locate the first command or graphics-state difference
between a baseline run and a candidate run.

Prerequisites: Python 3.14 or later with uv and two completed `.nv2acap`
bundles collected for the same title and target frame. Use
[Capture an NV2A frame for offline analysis](capture-nv2a.md) to collect them.
Run commands from the repository root and replace the example bundle paths
with your own.

## 1. Compare the recorded execution

```powershell
uv run python tools/nv2a_capture.py compare baseline.nv2acap candidate.nv2acap
```

The command validates both inputs and reports `MATCH` or the first differing
event. Inspect the baseline and candidate event descriptions and the list of
differing categories. To obtain the first difference for each category, request
the full JSON report:

```powershell
uv run python tools/nv2a_capture.py compare baseline.nv2acap candidate.nv2acap --json
```

Use `--strict-addresses` when address identity is part of the test. Otherwise,
keep the default address normalization so relocated allocations do not appear
as regressions alongside the command or payload difference you are investigating.

Exit `0` means the captures match; exit `1` means valid captures differ;
exit `2` means an input is invalid. Correct an invalid capture before drawing
a regression conclusion. See the
[comparison contract](../reference/nv2a-capture.md#compare-captures) for the
normalization rules.

## 2. Locate the first differing PGRAPH checkpoint

When the investigation needs draw or present state, compare the reconstructed
PGRAPH checkpoints:

```powershell
uv run python tools/nv2a_capture.py pgraph baseline.nv2acap candidate.nv2acap
uv run python tools/nv2a_capture.py pgraph baseline.nv2acap candidate.nv2acap --json
```

Inspect the first differing checkpoint and its frame, primitive, and state
CRC. A match here narrows the investigation to evidence outside the compared
checkpoint state; it does not establish a complete pixel match. See the
[PGRAPH replay contract](../reference/nv2a-capture.md#replay-pgraph-state)
for checkpoint contents and exit codes. To check pixel coverage in either
bundle, use [offline capture inspection](inspect-nv2a-capture.md).

## Use the comparison during a Git bisect

Prepare a Python wrapper that builds the checked-out revision, produces a
candidate capture with the same inputs, and returns the comparator's result.
Keep the baseline outside output directories that the wrapper replaces.

After starting a bisect and marking known good and bad revisions, run the
wrapper. The path below is a placeholder for your own script:

```powershell
git bisect run uv run python path/to/run_capture_bisect.py
```

Have the wrapper handle comparator exit `2` explicitly. If a revision cannot
produce a usable capture and should be skipped, return Git's skip code `125`.
Inspect the identified revision and run `git bisect reset` when finished.
