# Run kernel conformance probes

[How-to guides](README.md)

Prerequisites: a built emulator, uv with Python 3.14 or later, and the toolchain
required by the selected probe. Configure local runner settings using the
tracked [configuration example](../../tools/config.toml.example); the
`CXBX_TOOLS_CONFIG` environment variable can select your configuration file.
Set its emulator executable and toolchain paths for your machine.

From the repository root, discover probes and run the kernel checks:

```powershell
uv run python tools/xtest/xtest.py list
uv run python tools/xtest/xtest.py run --emulator cxbx --probe kernel_cov
uv run python tools/xtest/xtest.py run --emulator cxbx --probe kernel_trap
```

Inspect the console verdicts. A nonzero exit indicates at least one non-PASS
probe or a malformed requested host capture. Add `--show-trace` to inspect the
full probe trace; failed checks include expected and actual values.

To run the default probe selection:

```powershell
uv run python tools/xtest/xtest.py run --emulator cxbx
```

See the [suite guide](../../tests/suite/README.md) for toolchain requirements
and probe authoring, and [runner documentation](../../tools/xtest/README.md)
for capability filtering, goldens, and CI behavior. The
[legacy kernel-test record](../reference/history/kernel-test-suite.md)
describes an earlier manual test and is not a current suite verdict.
