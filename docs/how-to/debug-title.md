# Collect and narrow down a title failure

[How-to guides](README.md)

Prerequisites: a built CXBX, a title XBE and its data, Python 3.14 or later with
uv, and a desktop session for screenshots. Use the tracked
[configuration example](../../tools/config.toml.example) to configure tooling,
or pass the emulator path with `--exe`. Examples run from the repository root;
replace title paths with your own.

## Collect a bounded run

```powershell
uv run python tools/run_title.py 'C:/xbox-titles/example/default.xbe' `
  --profile nv2a --seconds 30 --shots 30 --until-visible
```

Use `--profile nv2a` when investigating the raw NV2A path. For ordinary title
startup, choose `--profile default`. The runner writes `summary.json` beside
the run log. Inspect startup/termination, fault counts, and the representative
screenshot before narrowing the graphics investigation.

`--until-visible` is a startup shortcut. A visible frame does not prove correct
rendering; use CRCs or golden images for a regression verdict. The
[evidence reference](../reference/title-run.md) defines the recorded fields
and visibility threshold.

## Narrow a raw NV2A rendering failure

1. Run once with `--until-visible` to prove that startup reaches rendering.
2. Enable `CXBX_NV2A_CRC=1` and `CXBX_NV2A_DRAW_TRACE=1` for stable frame and
   draw identities.
3. Restrict `CXBX_NV2A_DUMP_FRAMES` and `CXBX_NV2A_DUMP_DRAWS` to one bad frame.
4. Use `tools/nv2a/drawreport.py` to identify the first bad draw.
5. Confirm it with `CXBX_NV2A_SKIP_DRAWS` and replace the focused title case
   with an XDK conformance probe where possible.

To compare command streams and isolate state divergence without repeated
title boots, [capture and compare NV2A execution](capture-nv2a.md).
See [Title-debugging design](../explanation/title-debugging.md) for rationale
and planned tooling.
