# Explore a conformance probe

[Tutorials](README.md)

In this exercise you will connect a small CPU test to its recorded result.
By the end, you will know how to find a probe, recognize a check, and locate
the runner option that selects that probe.

You need a checkout, PowerShell, uv, and Python 3.14 or later. Run the commands
from the repository root. This exercise reads source and a checked-in baseline;
it does not run guest code or establish a new emulator test result.

## 1. Discover the runner

```powershell
uv run python tools/xtest/xtest.py --help
```

Find `list`, `build`, `run`, and `gate` in the help output. These are the
runner's entry points for discovering probes, building them, executing them,
and running the regression gate.

## 2. Find a CPU check

Open [the cpu_flags source](../../tests/suite/probes/cpu_flags/main.c) and search
for `add8.7F+01`. You will find an 8-bit addition of `0x7F` and `0x01` whose
expected result is `0x80`. The vector also records the expected CPU flags.

Each named vector gives the trace a stable name. Next, use that name to find
the corresponding baseline checks:

```powershell
Select-String -Path tests/suite/golden/cxbx/cpu_flags.golden -SimpleMatch 'CHK add8.7F+01'
```

You should see the result check and six flag checks, all marked `PASS` in this
baseline. The suffixes `CF`, `PF`, `AF`, `ZF`, `SF`, and `OF` identify the flags.

## 3. Inspect the complete verdict

```powershell
Get-Content tests/suite/golden/cxbx/cpu_flags.golden | Select-Object -Last 1
```

The checked-in trace ends with:

```text
#result cpu_flags verdict=PASS checks=85 fail=0
```

This is a recorded baseline. Comparing a new execution against it is a separate
step that needs a configured emulator and probe toolchain.

## 4. Find how to select this probe

```powershell
uv run python tools/xtest/xtest.py run --help
```

Find `--probe` and `--show-trace`. Together they let you select a probe and
inspect its complete trace during a real run.

You have now followed one test from its source vector through individual
checks to the suite verdict. To execute probes, continue with
[Run kernel conformance probes](../how-to/run-kernel-tests.md), which describes
the required setup and uses the same runner with kernel probes.
