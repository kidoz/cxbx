# xtest — Xbox conformance suite runner

Builds and runs the probes in `tests/suite/probes/` against an emulator, applies
each probe's self-check verdicts, diffs against per-emulator goldens, and writes a
console report + JUnit XML.

## Usage

```powershell
python xtest.py list                                   # discovered probes
python xtest.py build [probe ...]                      # build XBEs only
python xtest.py run [--emulator cxbx] [--probe NAME]   # build (unless --no-build) + run
python xtest.py run --update-golden                    # snapshot current results as baseline
python xtest.py run --emulator custom --cmd "emu {xbe} {rundir}"
```

Options: `--no-build`, `--timeout N`, `--probe NAME` (repeatable),
`--update-golden`, `--capability NAME`, `--all`, `--show-trace`. Exit code is
non-zero if any probe is not PASS (CI-friendly).

Failed checks are reported with their full `expect=`/`got=` detail (not just the
check name), so a divergence is actionable from the console. `--show-trace` prints
each probe's complete trace after the report for deeper inspection.

## Configuration — `config.toml`

- `paths.suite_dir` / `nxdk_dir` / `msys2_bash` — build toolchain locations.
- `emulator.cxbx` — the built-in adapter: `exe`, `run_args` (with `{xbe}`/`{log}`),
  `timeout`, `d_drive` (where the guest `D:` maps, relative to the XBE dir), and
  `kill_names` (processes to kill if a run times out).

## How a run works

1. Build each probe to `probes/<name>/bin/default.xbe` (MSYS2 make + scoop clang).
2. Launch it headlessly via the adapter with a timeout watchdog.
3. Read the probe's `D:\<name>.trace` (for Cxbx, that is `bin/<name>.trace`).
4. Parse `CHK`/`#result`, apply verdicts, diff vs `golden/<emulator>/<name>.golden`.
5. Print a table + write `out/results.xml` (JUnit).

## Adding an emulator adapter

For most emulators the `custom` adapter suffices (no code). For a first-class
adapter, add a class in `xtest.py` mirroring `CxbxAdapter.run(name, timeout) ->
RunResult(trace_path, log_path, exit_code, timed_out)` and select it in
`make_adapter`.
