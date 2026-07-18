#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""run_title - boot an arbitrary Xbox title in Cxbx, watch it, collect artifacts.

The conformance runner (tools/xtest) only boots suite probes; bringing up a real
game was an ad-hoc loop of `Start-Process` / `Start-Sleep` / a PrintWindow script
/ `taskkill /IM default.exe`. That last step is a hazard: killing the guest *by
image name* also kills any UNRELATED Cxbx session on the machine, and a
half-killed guest leaves `%TEMP%\\default.exe` locked so the next launch -- or a
`ninja` link of Cxbx.dll -- fails with "permission denied".

This harness fixes both. It launches `cxbx.exe --run <xbe> --log`, finds the guest
`default.exe` as the *child of this launcher* (by parent PID, never by name),
captures periodic window screenshots and (optionally) the emulator's ground-truth
backbuffer BMP dumps, and on exit kills only its own process tree
(`taskkill /PID <launcher> /T`) and clears the leftover temp exe. Every run writes
to its own timestamped directory -- nothing is overwritten.

    python tools/run_title.py "other/games/Arx Fatalis" --seconds 40 --shots 4
    python tools/run_title.py path/to/default.xbe --dump-frames --profile trace
    python tools/run_title.py "Samurai Showdown V"        # name under other/games/

Feed the run log straight into crash triage:
    cxbxdbg locate <xbe> --log <run-dir>/run.log
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tool_config

GUEST_IMAGE = "default.exe"

# Named environment recipes. `default` is the standard bring-up combo that lets
# titles survive the FS-swap and IRQL-assert classes (see the bring-up notes).
PROFILES = {
    "raw": {},
    "default": {"CXBX_FS_SWAP": "1", "CXBX_SURVIVE_BUGCHECK": "1"},
    "nv2a": {"CXBX_FS_SWAP": "1", "CXBX_SURVIVE_BUGCHECK": "1",
              "CXBX_NV2A_RASTER": "1"},
    "trace": {"CXBX_FS_SWAP": "1", "CXBX_SURVIVE_BUGCHECK": "1",
              "CXBX_EXC_TRACE": "1"},
}
# Backbuffer/scanout dumps the emulator writes to %TEMP% (ground truth for
# "what did it render", unlike PrintWindow which can come back black).
DUMP_GLOBS = ["cxbx_draw", "cxbx_frame*.bmp", "cxbx_fb*.bmp", "cxbx_tex"]


def die(msg):
    print(f"run_title: error: {msg}", file=sys.stderr)
    sys.exit(2)


def resolve_xbe(repo, target):
    p = Path(target)
    if p.suffix.lower() == ".xbe" and p.is_file():
        return p.resolve()
    for cand in (p, p / "default.xbe",
                 repo / "other" / "games" / target,
                 repo / "other" / "games" / target / "default.xbe"):
        if cand.is_file():
            return cand.resolve()
        if cand.is_dir() and (cand / "default.xbe").is_file():
            return (cand / "default.xbe").resolve()
    die(f"could not resolve a default.xbe from {target!r} "
        "(pass an .xbe, a folder, or a name under other/games/)")


def slugify(name):
    return "".join(c if c.isalnum() else "-" for c in name).strip("-").lower() or "title"


def temp_dir():
    return Path(os.environ.get("TEMP") or os.environ.get("TMP") or "/tmp")


def clear_temp_exe(seconds=3.0):
    """Delete the leftover %TEMP%\\default.exe (retry through the lock)."""
    p = temp_dir() / GUEST_IMAGE
    deadline = time.time() + seconds
    while p.exists() and time.time() < deadline:
        try:
            p.unlink()
        except OSError:
            time.sleep(0.1)


def clear_dumps():
    t = temp_dir()
    for g in DUMP_GLOBS:
        for m in t.glob(g):
            try:
                if m.is_dir():
                    shutil.rmtree(m, ignore_errors=True)
                else:
                    m.unlink()
            except OSError:
                pass


def child_guest_pid(parent_pid):
    """The guest default.exe spawned by our launcher, by PARENT PID (never by
    name -- so a concurrent unrelated Cxbx session is never touched)."""
    ps = (f"(Get-CimInstance Win32_Process -Filter \"ParentProcessId={parent_pid}\" "
          f"| Where-Object {{ $_.Name -eq '{GUEST_IMAGE}' }} "
          f"| Select-Object -First 1 -ExpandProperty ProcessId)")
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                           capture_output=True, text=True, timeout=15)
    except (OSError, subprocess.SubprocessError):
        return None
    out = r.stdout.strip()
    return int(out) if out.isdigit() else None


def pid_alive(pid):
    r = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"],
                       capture_output=True, text=True)
    return str(pid) in r.stdout


def kill_tree(launcher_pid, guest_pid):
    """Kill only our own processes, by PID. /T takes the launcher's child tree
    (the guest); the explicit guest kill is a belt-and-suspenders backup."""
    subprocess.run(["taskkill", "/PID", str(launcher_pid), "/T", "/F"],
                   capture_output=True, text=True)
    if guest_pid:
        subprocess.run(["taskkill", "/PID", str(guest_pid), "/F"],
                       capture_output=True, text=True)
    deadline = time.time() + 4.0
    while time.time() < deadline:
        if not (pid_alive(launcher_pid) or (guest_pid and pid_alive(guest_pid))):
            break
        time.sleep(0.15)


def capture_window(helper, guest_pid, out_png):
    if guest_pid is None:
        return "no-guest"
    try:
        r = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(helper), "-TargetPid", str(guest_pid), "-Out", str(out_png)],
            capture_output=True, text=True, timeout=30)
        return (r.stdout.strip().splitlines() or ["?"])[-1]
    except (OSError, subprocess.SubprocessError):
        return "capture-failed"


def collect_dumps(dest):
    """Copy this run's emulator BMP dumps out of %TEMP% into the run dir."""
    t = temp_dir()
    n = 0
    for g in DUMP_GLOBS:
        for m in sorted(t.glob(g)):
            try:
                if m.is_dir():
                    for f in m.glob("*.bmp"):
                        shutil.copy2(f, dest / f"{m.name}_{f.name}")
                        n += 1
                elif m.suffix.lower() == ".bmp":
                    shutil.copy2(m, dest / m.name)
                    n += 1
            except OSError:
                pass
    return n


def log_exception_count(logpath):
    try:
        text = logpath.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0, 0
    exc = sum(1 for ln in text.splitlines()
              if "EXC| " in ln or "Vectored exception" in ln)
    fatal = sum(1 for ln in text.splitlines()
                if "Terminating Process" in ln or "Fatal Message" in ln)
    return exc, fatal


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="run_title", description="Boot an Xbox title in Cxbx and collect artifacts.")
    ap.add_argument("target", help="an .xbe, a folder holding default.xbe, or a name under other/games/")
    ap.add_argument("--seconds", type=float, default=30, help="run duration (default 30)")
    ap.add_argument("--shots", type=int, default=3, help="window screenshots, evenly spread (0=none)")
    ap.add_argument("--profile", choices=sorted(PROFILES), default="default", help="env recipe")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra env var (repeatable)")
    ap.add_argument("--dump-frames", action="store_true",
                    help="enable the emulator's backbuffer BMP dumps and collect them (ground truth)")
    ap.add_argument("--out", default=None, help="base output dir (default: tools/run)")
    ap.add_argument("--exe", default=None, help="cxbx.exe path (default: from tools/config.toml)")
    ap.add_argument("--keep-temp", action="store_true", help="don't pre-clean %%TEMP%%\\default.exe")
    args = ap.parse_args(argv)

    repo = tool_config.repo_root()
    xbe = resolve_xbe(repo, args.target)

    exe = args.exe
    if not exe:
        try:
            cfg = tool_config.load_config(required=True)
            exe = tool_config.config_value(cfg, "emulator", "cxbx", "exe", required=True)
        except (FileNotFoundError, KeyError) as e:
            die(f"{e}\n(or pass --exe <path-to-cxbx.exe>)")
    exe = Path(exe)
    if not exe.is_file():
        die(f"emulator exe not found: {exe}")

    helper = Path(__file__).resolve().parent / "run" / "capture_window.ps1"

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    base = Path(args.out) if args.out else (repo / "tools" / "run")
    outdir = base / f"{slugify(xbe.parent.name)}-{stamp}"
    shots_dir = outdir / "shots"
    frames_dir = outdir / "frames"
    outdir.mkdir(parents=True, exist_ok=True)
    shots_dir.mkdir(exist_ok=True)
    if args.dump_frames:
        frames_dir.mkdir(exist_ok=True)
    logpath = outdir / "run.log"

    env = dict(os.environ)
    env.update(PROFILES[args.profile])
    for kv in args.env:
        if "=" not in kv:
            die(f"--env expects K=V, got {kv!r}")
        k, v = kv.split("=", 1)
        env[k] = v
    if args.dump_frames:
        env.setdefault("CXBX_D3D_DUMP_DRAWS", "0:1")
        env.setdefault("CXBX_D3D_DUMP_FRAMES", "1:100000")

    if not args.keep_temp:
        clear_temp_exe()
    if args.dump_frames:
        clear_dumps()

    cmd = [str(exe), "--run", str(xbe), "--log", str(logpath)]
    print(f"run_title: {xbe.parent.name}")
    print(f"  exe      : {exe}")
    print(f"  profile  : {args.profile}  {PROFILES[args.profile] or '(none)'}"
          + ("  +dump-frames" if args.dump_frames else ""))
    print(f"  out dir  : {outdir}")
    print(f"  duration : {args.seconds:g}s, {args.shots} shot(s)\n")

    started = time.time()
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    print(f"  launcher pid {p.pid}; locating guest child...")

    guest = None
    for _ in range(20):                       # guest spawns after XBE->exe convert (~1-2s)
        if p.poll() is not None:
            break
        guest = child_guest_pid(p.pid)
        if guest:
            break
        time.sleep(0.5)
    print(f"  guest pid    {guest if guest else '(not found -- window shots disabled)'}")
    if guest is None and p.poll() is not None:
        print("  WARNING: the guest never started and the launcher already exited --")
        print("           likely another Cxbx instance holds %TEMP%\\default.exe (only one")
        print("           title runs at a time), or the launch errored. Check run.log.")

    # Schedule shots evenly across the run; always sample near the end too.
    shot_times = []
    if args.shots > 0:
        step = args.seconds / (args.shots + 1)
        shot_times = [step * (i + 1) for i in range(args.shots)]

    shots_taken, exited_early = 0, False
    for i, when in enumerate(shot_times + [args.seconds]):
        remaining = when - (time.time() - started)
        if remaining > 0:
            time.sleep(remaining)
        if p.poll() is not None:
            exited_early = True
            print(f"  [{time.time()-started:4.0f}s] guest exited on its own (code {p.returncode}).")
            break
        if i < len(shot_times):
            png = shots_dir / f"shot{i + 1:03d}-t{int(when):03d}s.png"
            status = capture_window(helper, guest, png)
            shots_taken += png.is_file()
            print(f"  [{time.time()-started:4.0f}s] shot -> {png.name}  ({status})")

    alive = time.time() - started
    if p.poll() is None:
        print(f"  [{alive:4.0f}s] stopping (killing pid tree {p.pid})...")
    kill_tree(p.pid, guest)
    clear_temp_exe()

    n_frames = collect_dumps(frames_dir) if args.dump_frames else 0
    exc, fatal = log_exception_count(logpath)
    log_lines = sum(1 for _ in logpath.open(encoding="utf-8", errors="replace")) if logpath.is_file() else 0

    if guest is None:
        outcome = "guest never started (temp-exe contention or launch error)"
    elif exited_early:
        outcome = f"guest exited on its own after {alive:.0f}s"
    else:
        outcome = f"stopped after the {args.seconds:g}s timeout (guest still running)"
    print("\nrun_title: done")
    print(f"  outcome  : {outcome}")
    print(f"  log      : {logpath}  ({log_lines} lines)")
    if exc or fatal:
        print(f"  faults   : {exc} exception line(s), {fatal} fatal/terminate marker(s)")
        print(f"             triage: cxbxdbg locate \"{xbe}\" --log \"{logpath}\"")
    print(f"  shots    : {shots_taken}/{len(shot_times)} saved in {shots_dir}")
    if args.dump_frames:
        print(f"  frames   : {n_frames} backbuffer BMP(s) in {frames_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
