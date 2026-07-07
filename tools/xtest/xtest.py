#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
xtest - build and run the Xbox-Original conformance/trace suite against any
emulator, and report where it diverges.

    python xtest.py list
    python xtest.py build [probe ...]
    python xtest.py run [--emulator cxbx] [--probe NAME ...] [--update-golden]
    python xtest.py run --emulator custom --cmd "myemu --run {xbe} --dout {rundir}"

Each probe is a self-checking nxdk test ROM that writes a structured trace to its
D: drive (see tests/suite/README.md). The runner parses that trace, applies the
probe's own PASS/FAIL verdicts, optionally diffs against a golden baseline, and
emits a console table + JUnit XML (out/results.xml).

The suite is emulator-agnostic: the built-in `cxbx` adapter is just one target.
Point `--emulator custom --cmd ...` at your own emulator to see exactly which
subsystem/behaviour is wrong.
"""

import argparse
import difflib
import os
import re
import subprocess
import sys
import tomllib
import xml.etree.ElementTree as ET
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, cast

HERE = Path(__file__).resolve().parent
OUT_DIR = HERE / "out"
Config = dict[str, Any]
CommandFunc = Callable[[Config, argparse.Namespace], int]


# --------------------------------------------------------------------------- #
# Config & discovery
# --------------------------------------------------------------------------- #


def load_config() -> Config:
    with open(HERE / "config.toml", "rb") as f:
        return tomllib.load(f)


def win_to_posix(p: str | Path) -> str:
    """C:/Users/x -> /c/Users/x  (for MSYS2 command lines)."""
    p = str(p).replace("\\", "/")
    m = re.match(r"^([A-Za-z]):/(.*)$", p)
    return f"/{m.group(1).lower()}/{m.group(2)}" if m else p


def discover_probes(suite_dir: Path) -> list[str]:
    """Every subdir of probes/ that has a Makefile, sorted by name."""
    pdir = suite_dir / "probes"
    return sorted(d.name for d in pdir.iterdir() if d.is_dir() and (d / "Makefile").exists())


def probe_tags(suite_dir: Path, name: str) -> list[str]:
    """Capability tags a probe requires, from optional probes/<name>/probe.toml
    (`tags = ["nv2a", ...]`). No file -> no tags -> runs on every target."""
    p = suite_dir / "probes" / name / "probe.toml"
    if not p.exists():
        return []
    with open(p, "rb") as f:
        return [str(t) for t in tomllib.load(f).get("tags", [])]


def target_capabilities(cfg: Config, args: argparse.Namespace) -> set[str] | None:
    """Capabilities the target advertises: --capability flags, else the
    emulator's config `capabilities`. None => no filtering (run everything)."""
    if args.capability:
        return set(args.capability)
    ecfg = cfg.get("emulator", {}).get(args.emulator, {})
    caps = ecfg.get("capabilities")
    return set(caps) if caps is not None else None


# --------------------------------------------------------------------------- #
# Building
# --------------------------------------------------------------------------- #


def build_probe(cfg: Config, name: str) -> tuple[bool, str]:
    suite_dir = Path(cfg["paths"]["suite_dir"])
    bash = cfg["paths"]["msys2_bash"]
    probe_posix = win_to_posix(suite_dir / "probes" / name)
    script_posix = win_to_posix(suite_dir / "build-probe.sh")
    env = dict(os.environ, MSYSTEM="MINGW64")
    cmd = [bash, "-lc", f"sh {script_posix} {probe_posix} -j4"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    xbe = suite_dir / "probes" / name / "bin" / "default.xbe"
    ok = r.returncode == 0 and xbe.exists()
    tail = (r.stdout + r.stderr).strip().splitlines()[-4:]
    return ok, "\n".join(tail)


# --------------------------------------------------------------------------- #
# Emulator adapters
# --------------------------------------------------------------------------- #


class RunResult:
    def __init__(
        self, trace_path: Path, log_path: Path, exit_code: int | None, timed_out: bool
    ) -> None:
        self.trace_path = trace_path
        self.log_path = log_path
        self.exit_code = exit_code
        self.timed_out = timed_out


def _kill(names: Sequence[str]) -> None:
    for n in names:
        subprocess.run(["taskkill", "/IM", n, "/F"], capture_output=True, text=True)


class CxbxAdapter:
    """Built-in adapter for the Cxbx emulator's headless --run/--log mode."""

    def __init__(self, cfg: Config) -> None:
        self.ecfg = cfg["emulator"]["cxbx"]
        self.suite_dir = Path(cfg["paths"]["suite_dir"])

    def run(self, name: str, timeout: int) -> RunResult:
        bindir = self.suite_dir / "probes" / name / "bin"
        xbe = bindir / "default.xbe"
        log = bindir / "run.log"
        if log.exists():
            log.unlink()
        # D:\<name>.trace maps to the XBE directory for Cxbx.
        d_drive = (bindir / self.ecfg.get("d_drive", ".")).resolve()
        trace = d_drive / f"{name}.trace"
        if trace.exists():
            trace.unlink()

        args = [self.ecfg["exe"]]
        for a in self.ecfg["run_args"]:
            args.append(a.replace("{xbe}", str(xbe)).replace("{log}", str(log)))

        timed_out = False
        code = None
        p = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            code = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            p.kill()
        _kill(self.ecfg.get("kill_names", []))
        return RunResult(trace, log, code, timed_out)


class CustomAdapter:
    """Adapter for a third-party emulator via a command template.

    Placeholders: {xbe} {rundir} {log}. The emulator must write the probe's
    D:\\<name>.trace somewhere under {rundir}; we locate it by basename.
    """

    def __init__(self, cfg: Config, cmd_template: str) -> None:
        self.suite_dir = Path(cfg["paths"]["suite_dir"])
        self.cmd_template = cmd_template

    def run(self, name: str, timeout: int) -> RunResult:
        bindir = self.suite_dir / "probes" / name / "bin"
        xbe = bindir / "default.xbe"
        rundir = bindir
        log = bindir / "run.log"
        trace = bindir / f"{name}.trace"
        if trace.exists():
            trace.unlink()
        cmd = self.cmd_template.format(xbe=str(xbe), rundir=str(rundir), log=str(log))
        timed_out = False
        code = None
        p = subprocess.Popen(cmd, shell=True)
        try:
            code = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            p.kill()
        # Find the trace anywhere under rundir (emulator chooses D: location).
        if not trace.exists():
            found = list(rundir.rglob(f"{name}.trace"))
            if found:
                trace = found[0]
        return RunResult(trace, log, code, timed_out)


# --------------------------------------------------------------------------- #
# Trace parsing, normalization, golden diff
# --------------------------------------------------------------------------- #


class ProbeResult:
    def __init__(self, name: str) -> None:
        self.name = name
        self.verdict = "NOTRACE"  # PASS | FAIL | NOTRACE | TIMEOUT
        self.checks = 0
        self.fails = 0
        self.fail_names: list[str] = []
        self.fail_lines: list[str] = []  # full failing CHK lines (with expect=/got=)
        self.golden = "-"  # OK | DIFF | NEW | -
        self.diff = ""
        self.trace_text = ""
        self.timed_out = False


CHK_RE = re.compile(r"^CHK (\S+) .*?(PASS|FAIL)\s*$")
RESULT_RE = re.compile(r"^#result (\S+) verdict=(\S+) checks=(\d+) fail=(\d+)")


def parse_trace(text: str, res: ProbeResult) -> None:
    for line in text.splitlines():
        m = CHK_RE.match(line)
        if m and m.group(2) == "FAIL":
            res.fail_names.append(m.group(1))
            res.fail_lines.append(line.strip())
        r = RESULT_RE.match(line)
        if r:
            res.verdict = r.group(2)
            res.checks = int(r.group(3))
            res.fails = int(r.group(4))
    if res.verdict == "NOTRACE" and text.strip():
        # Trace exists but no #result line -> probe crashed mid-run.
        res.verdict = "PARTIAL"


def normalize_for_golden(text: str) -> list[str]:
    """Stable, value-independent projection used for regression diffing:
    keep header, CHK verdicts (name+verdict only), and the #result line."""
    out = []
    for line in text.splitlines():
        if line.startswith("#suite") or line.startswith("#probe"):
            out.append(line)
        elif line.startswith("CHK "):
            m = CHK_RE.match(line)
            if m:
                out.append(f"CHK {m.group(1)} {m.group(2)}")
        elif line.startswith("#result"):
            out.append(line)
    return out


def golden_compare(suite_dir: Path, emulator: str, res: ProbeResult, update: bool) -> None:
    # Goldens are per-emulator baselines (golden/<emulator>/<probe>.golden), so
    # each target tracks its own progress without clobbering another's.
    gdir = suite_dir / "golden" / emulator
    gpath = gdir / f"{res.name}.golden"
    actual = normalize_for_golden(res.trace_text)
    if update:
        gdir.mkdir(parents=True, exist_ok=True)
        gpath.write_text("\n".join(actual) + "\n", encoding="utf-8")
        res.golden = "WROTE"
        return
    if not gpath.exists():
        res.golden = "NEW"
        return
    golden = gpath.read_text(encoding="utf-8").splitlines()
    if golden == actual:
        res.golden = "OK"
    else:
        res.golden = "DIFF"
        res.diff = "\n".join(
            difflib.unified_diff(
                golden,
                actual,
                fromfile=f"{res.name}.golden",
                tofile=f"{res.name}.actual",
                lineterm="",
            )
        )


# --------------------------------------------------------------------------- #
# Reporting
# --------------------------------------------------------------------------- #


def print_report(results: Sequence[ProbeResult], emulator: str) -> bool:
    print()
    print(f"  Xbox conformance suite -- target: {emulator}")
    print("  " + "-" * 62)
    print(f"  {'PROBE':<14}{'VERDICT':<10}{'CHECKS':<8}{'FAIL':<6}{'GOLDEN':<8}")
    print("  " + "-" * 62)
    n_pass = n_fail = 0
    for r in results:
        mark = {
            "PASS": "PASS",
            "FAIL": "FAIL",
            "NOTRACE": "NO-TRACE",
            "PARTIAL": "PARTIAL",
            "TIMEOUT": "TIMEOUT",
        }.get(r.verdict, r.verdict)
        if r.verdict == "PASS":
            n_pass += 1
        else:
            n_fail += 1
        print(f"  {r.name:<14}{mark:<10}{r.checks:<8}{r.fails:<6}{r.golden:<8}")
    print("  " + "-" * 62)
    print(f"  {n_pass} passed, {n_fail} not-passed, {len(results)} total")

    for r in results:
        if r.fail_lines:
            # Show the full failing checks (with expect=/got=) so a divergence is
            # actionable without opening the .trace file.
            print(f"\n  [{r.name}] failed checks:")
            for line in r.fail_lines:
                print("    " + line)
        elif r.fail_names:
            print(f"\n  [{r.name}] failed checks: {', '.join(r.fail_names)}")
        if r.diff:
            print(f"\n  [{r.name}] golden diff:")
            for line in r.diff.splitlines():
                print("    " + line)
    print()
    return n_fail == 0


def write_junit(results: Sequence[ProbeResult], emulator: str) -> None:
    OUT_DIR.mkdir(exist_ok=True)
    ts = ET.Element(
        "testsuite",
        name=f"xbox-conformance:{emulator}",
        tests=str(len(results)),
        failures=str(sum(1 for r in results if r.verdict != "PASS")),
    )
    for r in results:
        tc = ET.SubElement(
            ts,
            "testcase",
            classname=emulator,
            name=r.name,
            assertions=str(r.checks),
        )
        if r.verdict != "PASS":
            msg = f"verdict={r.verdict} fails={r.fails}"
            if r.fail_names:
                msg += " (" + ", ".join(r.fail_names) + ")"
            fail = ET.SubElement(tc, "failure", message=msg, type=r.verdict)
            fail.text = (r.diff or r.trace_text)[:4000]
    path = OUT_DIR / "results.xml"
    ET.ElementTree(ts).write(path, encoding="utf-8", xml_declaration=True)
    print(f"  JUnit: {path}")


# --------------------------------------------------------------------------- #
# Commands
# --------------------------------------------------------------------------- #


def cmd_list(cfg: Config, _args: argparse.Namespace) -> int:
    suite_dir = Path(cfg["paths"]["suite_dir"])
    for name in discover_probes(suite_dir):
        print(name)
    return 0


def cmd_build(cfg: Config, args: argparse.Namespace) -> int:
    suite_dir = Path(cfg["paths"]["suite_dir"])
    names = args.probe or discover_probes(suite_dir)
    rc = 0
    for name in names:
        print(f"[build] {name} ...", end=" ", flush=True)
        ok, tail = build_probe(cfg, name)
        print("OK" if ok else "FAILED")
        if not ok:
            rc = 1
            print("    " + tail.replace("\n", "\n    "))
    return rc


def make_adapter(cfg: Config, args: argparse.Namespace) -> CxbxAdapter | CustomAdapter:
    if args.emulator == "cxbx":
        return CxbxAdapter(cfg)
    if args.emulator == "custom":
        if not args.cmd:
            sys.exit('--emulator custom requires --cmd "...{xbe}..."')
        return CustomAdapter(cfg, args.cmd)
    sys.exit(f"unknown emulator: {args.emulator}")


def cmd_run(cfg: Config, args: argparse.Namespace) -> int:
    suite_dir = Path(cfg["paths"]["suite_dir"])
    explicit = bool(args.probe)
    names = args.probe or discover_probes(suite_dir)
    adapter = make_adapter(cfg, args)
    timeout = args.timeout or cfg["emulator"].get("cxbx", {}).get("timeout", 25)

    # Capability filtering: skip probes whose required tags the target does not
    # advertise (e.g. an "nv2a" probe on an emulator with no GPU model, which
    # would hang on an untrapped 0xFD... access). Explicitly named probes and
    # --all bypass the filter.
    skipped: list[tuple[str, list[str]]] = []
    caps = target_capabilities(cfg, args)
    if not explicit and not args.all and caps is not None:
        kept = []
        for n in names:
            missing = [t for t in probe_tags(suite_dir, n) if t not in caps]
            if missing:
                skipped.append((n, missing))
            else:
                kept.append(n)
        names = kept

    if not args.no_build:
        if cmd_build(cfg, argparse.Namespace(probe=names)) != 0:
            print("build failed; aborting run")
            return 1

    results = []
    for name in names:
        print(f"[run] {name} ...", end=" ", flush=True)
        rr = adapter.run(name, timeout)
        res = ProbeResult(name)
        res.timed_out = rr.timed_out
        if rr.trace_path and Path(rr.trace_path).exists():
            res.trace_text = Path(rr.trace_path).read_text(encoding="utf-8", errors="replace")
            parse_trace(res.trace_text, res)
            golden_compare(suite_dir, args.emulator, res, args.update_golden)
        elif rr.timed_out:
            res.verdict = "TIMEOUT"
        print(res.verdict + (f" ({res.golden})" if res.golden not in ("-",) else ""))
        results.append(res)

    ok = print_report(results, args.emulator)
    for n, miss in skipped:
        print(f"  skipped {n} (target lacks capability: {', '.join(miss)})")

    if getattr(args, "show_trace", False):
        for r in results:
            print(f"\n  === trace: {r.name} ===")
            for line in r.trace_text.splitlines():
                print("    " + line)

    write_junit(results, args.emulator)
    return 0 if ok else 1


def cmd_gate(cfg: Config, args: argparse.Namespace) -> int:
    """One-command CI gate: audit the kernel thunk table, (re)build the emulator,
    build every probe, run the full suite. Exit 0 only if the audit passes and
    every probe passes and matches its golden."""
    # Fail-fast source check: the kernel thunk table must match the Xbox ABI
    # ordinals (a shift there crashes every title at startup with no obvious cause).
    audit = Path(__file__).resolve().parents[1] / "kernelaudit" / "check_kernel_thunks.py"
    if audit.exists():
        print("[gate] auditing kernel thunk ordinals ...", flush=True)
        r = subprocess.run([sys.executable, str(audit)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode != 0:
            sys.stdout.write(r.stderr)
            print("[gate] kernel-thunk audit FAILED")
            return 1

    ecfg = cfg["emulator"].get(args.emulator, {})
    build_dir = ecfg.get("build_dir")
    if build_dir:
        if not (Path(build_dir) / "build.ninja").exists():
            print(f"[gate] configuring emulator build ({build_dir}) ...", flush=True)
            r = subprocess.run(["meson", "setup", build_dir], capture_output=True, text=True)
            if r.returncode != 0:
                print((r.stdout + r.stderr)[-2000:])
                print("[gate] emulator configure FAILED")
                return 1
        print(f"[gate] building emulator ({build_dir}) ...", flush=True)
        r = subprocess.run(["ninja", "-C", build_dir], capture_output=True, text=True)
        if r.returncode != 0:
            print((r.stdout + r.stderr)[-2000:])
            print("[gate] emulator build FAILED")
            return 1
        print("[gate] emulator build OK")

    run_args = argparse.Namespace(
        emulator=args.emulator,
        cmd=args.cmd,
        probe=None,
        timeout=args.timeout,
        no_build=False,
        capability=None,
        all=True,
        update_golden=False,
        show_trace=False,
    )
    return cmd_run(cfg, run_args)


def main() -> int:
    ap = argparse.ArgumentParser(description="Xbox conformance suite runner")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_list = sub.add_parser("list", help="list discovered probes")
    p_list.set_defaults(func=cmd_list)

    b = sub.add_parser("build", help="build probe XBEs")
    b.add_argument("probe", nargs="*", help="probe names (default: all)")
    b.set_defaults(func=cmd_build)

    r = sub.add_parser("run", help="build+run probes against an emulator")
    r.add_argument("--emulator", default="cxbx", help="cxbx | custom")
    r.add_argument("--cmd", help="custom emulator command template")
    r.add_argument("--probe", action="append", help="probe name (repeatable)")
    r.add_argument("--timeout", type=int, help="per-probe timeout seconds")
    r.add_argument("--no-build", action="store_true", help="skip building")
    r.add_argument(
        "--capability", action="append",
        help="declare a target capability, e.g. nv2a (repeatable); overrides config",
    )
    r.add_argument("--all", action="store_true", help="run all probes, ignore capability filter")
    r.add_argument(
        "--update-golden", action="store_true", help="write current normalized traces as goldens"
    )
    r.add_argument(
        "--show-trace", action="store_true", help="print each probe's full trace after the report"
    )
    r.set_defaults(func=cmd_run)

    g = sub.add_parser(
        "gate", help="CI gate: build emulator + every probe, run the full suite"
    )
    g.add_argument("--emulator", default="cxbx", help="cxbx | custom")
    g.add_argument("--cmd", help="custom emulator command template")
    g.add_argument("--timeout", type=int, help="per-probe timeout seconds")
    g.set_defaults(func=cmd_gate)

    args = ap.parse_args()
    cfg = load_config()
    func = cast(CommandFunc, args.func)
    return func(cfg, args)


if __name__ == "__main__":
    sys.exit(main() or 0)
