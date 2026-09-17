# Title-run evidence

[Reference](README.md)

`tools/run_title.py` writes `summary.json` beside each `run.log`. It records:

- exact XBE, emulator, profile, and explicit environment overrides;
- guest startup and termination outcome;
- client-area screenshot metrics and capture source;
- the automatically selected representative screenshot;
- exception, fatal, and warning counts;
- `NVCRC|` records and per-frame `NVDRAW|` counts;
- whether the live NV2A overlay produced visible pixels.

The capture helper measures the real Win32 client rectangle. This avoids both
the old hard-coded title-bar offset and false image scores from desktop pixels.

## Visibility threshold

`--until-visible` stops after a frame has at least 1% non-black sampled pixels
and the requested color variation (`--visible-colors`, default 32). This is a
bring-up optimization, not a correctness verdict. Regression gates should use
frame CRCs or golden images.

For collection steps, see [Debug a title](../how-to/debug-title.md).

For replay contracts, see [NV2A capture reference](nv2a-capture.md).
