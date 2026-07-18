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
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nv2a_capture
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
DUMP_GLOBS = [
    "cxbx_draw",
    "cxbx_nv2a_draw",
    "cxbx_frame*.bmp",
    "cxbx_fb*.bmp",
    "cxbx_tex",
]
CAPTURE_RE = re.compile(
    r"^SAVED width=(?P<width>\d+) height=(?P<height>\d+) "
    r"source=(?P<source>\w+) client=(?P<client_width>\d+)x(?P<client_height>\d+) "
    r"samples=(?P<samples>\d+) nonblack=(?P<nonblack>[0-9.]+) "
    r"colors=(?P<colors>\d+) bbox=(?P<bbox>\S+)"
    r"(?: screen=(?P<screen_x>-?\d+),(?P<screen_y>-?\d+)"
    r" window=(?P<window_x>-?\d+),(?P<window_y>-?\d+))?$"
)
NVCRC_RE = re.compile(
    r"NVCRC\| frame=(\d+) addr=0x([0-9A-Fa-f]+) w=(\d+) h=(\d+) crc=0x([0-9A-Fa-f]+)"
)
NVDRAW_RE = re.compile(r"NVDRAW\| frame=(\d+) draw=(\d+)\s+(\w+)")


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


def parse_capture_result(raw):
    match = CAPTURE_RE.match(raw)
    if match is None:
        return {"saved": False, "status": raw or "capture-failed"}
    values = match.groupdict()
    bbox = (None if values["bbox"] == "none"
            else [int(v) for v in values["bbox"].split(",")])
    result = {
        "saved": True,
        "status": "saved",
        "source": values["source"],
        "width": int(values["width"]),
        "height": int(values["height"]),
        "client_width": int(values["client_width"]),
        "client_height": int(values["client_height"]),
        "samples": int(values["samples"]),
        "nonblack_fraction": float(values["nonblack"]),
        "sampled_colors": int(values["colors"]),
        "nonblack_bbox": bbox,
    }
    if values["screen_x"] is not None:
        result["client_screen_origin"] = [
            int(values["screen_x"]), int(values["screen_y"])]
        result["window_screen_origin"] = [
            int(values["window_x"]), int(values["window_y"])]
    return result


def capture_window(helper, guest_pid, out_png):
    if guest_pid is None:
        return {"saved": False, "status": "no-guest"}
    try:
        r = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(helper), "-TargetPid", str(guest_pid), "-Out", str(out_png)],
            capture_output=True, text=True, timeout=30)
        raw = (r.stdout.strip().splitlines() or ["capture-failed"])[-1]
        return parse_capture_result(raw)
    except (OSError, subprocess.SubprocessError):
        return {"saved": False, "status": "capture-failed"}


def capture_is_visible(capture, minimum_colors):
    return (capture.get("saved", False) and
            capture.get("sampled_colors", 0) >= minimum_colors and
            capture.get("nonblack_fraction", 0.0) >= 0.01)


def nv2a_capture_complete(path):
    if path is None or not path.is_file() or path.stat().st_size < 28:
        return False
    try:
        with path.open("rb") as stream:
            stream.seek(-28, os.SEEK_END)
            return struct.unpack("<II", stream.read(8)) == (6, 20)
    except (OSError, struct.error):
        return False


def select_representative_shot(shots):
    saved = [shot for shot in shots if shot.get("saved", False)]
    if not saved:
        return None
    return max(saved, key=lambda shot: (
        shot.get("sampled_colors", 0),
        shot.get("nonblack_fraction", 0.0),
    ))


def collect_dumps(dest):
    """Copy this run's emulator BMP dumps out of %TEMP% into the run dir."""
    t = temp_dir()
    n = 0
    for g in DUMP_GLOBS:
        for m in sorted(t.glob(g)):
            try:
                if m.is_dir():
                    if m.name == "cxbx_nv2a_draw":
                        draw_dest = dest / m.name
                        draw_dest.mkdir(exist_ok=True)
                        for f in m.iterdir():
                            if f.is_file() and f.suffix.lower() in {".bmp", ".txt"}:
                                shutil.copy2(f, draw_dest / f.name)
                                n += f.suffix.lower() == ".bmp"
                    else:
                        for f in m.glob("*.bmp"):
                            shutil.copy2(f, dest / f"{m.name}_{f.name}")
                            n += 1
                elif m.suffix.lower() == ".bmp":
                    shutil.copy2(m, dest / m.name)
                    n += 1
            except OSError:
                pass
    return n


def summarize_log(logpath):
    summary = {
        "lines": 0,
        "exception_lines": 0,
        "fatal_lines": 0,
        "warning_lines": 0,
        "overlay_visible": False,
        "nv2a_crc": [],
        "nv2a_draws_per_frame": {},
    }
    try:
        lines = logpath.open(encoding="utf-8", errors="replace")
    except OSError:
        return summary
    with lines:
        for line in lines:
            summary["lines"] += 1
            if "EXC| " in line or "Vectored exception" in line:
                summary["exception_lines"] += 1
            if "Terminating Process" in line or "Fatal Message" in line:
                summary["fatal_lines"] += 1
            if "*Warning*" in line or "*WARNING*" in line:
                summary["warning_lines"] += 1
            if "NV2A overlay received visible pixels" in line:
                summary["overlay_visible"] = True
            if (match := NVCRC_RE.search(line)) is not None:
                summary["nv2a_crc"].append({
                    "frame": int(match.group(1)),
                    "address": f"0x{int(match.group(2), 16):08X}",
                    "width": int(match.group(3)),
                    "height": int(match.group(4)),
                    "crc32": f"0x{int(match.group(5), 16):08X}",
                })
            if (match := NVDRAW_RE.search(line)) is not None:
                frame = match.group(1)
                draw_count = int(match.group(2)) + 1
                summary["nv2a_draws_per_frame"][frame] = max(
                    summary["nv2a_draws_per_frame"].get(frame, 0), draw_count)
    return summary


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="run_title", description="Boot an Xbox title in Cxbx and collect artifacts.")
    ap.add_argument("target", help="an .xbe, a folder holding default.xbe, or a name under other/games/")
    ap.add_argument("--seconds", type=float, default=30, help="run duration (default 30)")
    ap.add_argument("--shots", type=int, default=3, help="window screenshots, evenly spread (0=none)")
    ap.add_argument("--until-visible", action="store_true",
                    help="stop after a captured client frame has meaningful color variation")
    ap.add_argument("--visible-colors", type=int, default=32, metavar="N",
                    help="sampled client colors required by --until-visible (default 32)")
    ap.add_argument("--profile", choices=sorted(PROFILES), default="default", help="env recipe")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra env var (repeatable)")
    ap.add_argument("--dump-frames", action="store_true",
                    help="enable the emulator's backbuffer BMP dumps and collect them (ground truth)")
    ap.add_argument("--capture-pushbuffer", type=int, default=None, metavar="FRAME",
                    help="capture PFIFO commands/resources through FRAME to a replay bundle")
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
    env_overrides = {}
    for kv in args.env:
        if "=" not in kv:
            die(f"--env expects K=V, got {kv!r}")
        k, v = kv.split("=", 1)
        env[k] = v
        env_overrides[k] = v
    if args.dump_frames:
        env.setdefault("CXBX_D3D_DUMP_DRAWS", "0:1")
        env.setdefault("CXBX_D3D_DUMP_FRAMES", "1:100000")
    capture_path = None
    if args.capture_pushbuffer is not None:
        if args.capture_pushbuffer < 0:
            die("--capture-pushbuffer expects a non-negative frame index")
        capture_path = outdir / f"frame{args.capture_pushbuffer:05d}.nv2acap"
        env["CXBX_NV2A_CAPTURE"] = str(capture_path.resolve())
        env["CXBX_NV2A_CAPTURE_FRAME"] = str(args.capture_pushbuffer)

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

    started_at = datetime.now().astimezone().isoformat(timespec="seconds")
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

    shots = []
    shots_taken, exited_early, stopped_on_visible = 0, False, False
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
            capture = capture_window(helper, guest, png)
            capture["at_seconds"] = round(time.time() - started, 3)
            capture["path"] = str(png.relative_to(outdir))
            shots.append(capture)
            shots_taken += capture.get("saved", False)
            if capture.get("saved", False):
                detail = (f"{capture['source']} nonblack={capture['nonblack_fraction']:.3f} "
                          f"colors={capture['sampled_colors']}")
            else:
                detail = capture["status"]
            print(f"  [{time.time()-started:4.0f}s] shot -> {png.name}  ({detail})")
            if args.until_visible and capture_is_visible(capture, args.visible_colors):
                if capture_path is not None and not nv2a_capture_complete(capture_path):
                    print(f"  [{time.time()-started:4.0f}s] visible frame; waiting for capture.")
                    continue
                stopped_on_visible = True
                print(f"  [{time.time()-started:4.0f}s] visible-frame threshold reached.")
                break

    alive = time.time() - started
    if p.poll() is None:
        print(f"  [{alive:4.0f}s] stopping (killing pid tree {p.pid})...")
    kill_tree(p.pid, guest)
    clear_temp_exe()

    n_frames = collect_dumps(frames_dir) if args.dump_frames else 0
    log_summary = summarize_log(logpath)
    representative = select_representative_shot(shots)
    capture_summary = None
    if capture_path is not None:
        capture_summary = {
            "path": str(capture_path.relative_to(outdir)),
            "saved": capture_path.is_file(),
            "complete": nv2a_capture_complete(capture_path),
            "bytes": capture_path.stat().st_size if capture_path.is_file() else 0,
            "target_frame": args.capture_pushbuffer,
        }
        if capture_summary["complete"]:
            try:
                replay = nv2a_capture.analyze_capture(capture_path)
                capture_summary.update({
                    "replay": replay["replay"],
                    "push_runs": replay["push_runs"],
                    "push_words": replay["push_words"],
                    "methods": replay["methods"],
                    "memory_bytes": replay["memory_bytes"],
                    "output_crc32": replay["output_crc32"],
                })
            except (OSError, nv2a_capture.CaptureError) as error:
                capture_summary["replay"] = "fail"
                capture_summary["replay_error"] = str(error)

    if guest is None:
        outcome = "guest never started (temp-exe contention or launch error)"
    elif exited_early:
        outcome = f"guest exited on its own after {alive:.0f}s"
    elif stopped_on_visible:
        outcome = f"visible frame captured after {alive:.0f}s"
    else:
        outcome = f"stopped after the {args.seconds:g}s timeout (guest still running)"
    summary = {
        "schema_version": 1,
        "title": xbe.parent.name,
        "xbe": str(xbe),
        "emulator": str(exe),
        "profile": args.profile,
        "profile_environment": PROFILES[args.profile],
        "environment_overrides": env_overrides,
        "started_at": started_at,
        "elapsed_seconds": round(alive, 3),
        "outcome": outcome,
        "guest_found": guest is not None,
        "guest_exited_early": exited_early,
        "stopped_on_visible": stopped_on_visible,
        "shots": shots,
        "representative_shot": representative["path"] if representative else None,
        "log": log_summary,
        "collected_frames": n_frames,
        "pushbuffer_capture": capture_summary,
    }
    summary_path = outdir / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n",
                            encoding="utf-8")
    print("\nrun_title: done")
    print(f"  outcome  : {outcome}")
    print(f"  log      : {logpath}  ({log_summary['lines']} lines)")
    print(f"  summary  : {summary_path}")
    if representative:
        print(f"  best shot: {representative['path']}  "
              f"colors={representative['sampled_colors']} "
              f"nonblack={representative['nonblack_fraction']:.3f}")
    if capture_summary is not None:
        status = "incomplete"
        if capture_summary["complete"]:
            status = f"complete, replay-{capture_summary.get('replay', 'not-run')}"
        print(f"  capture  : {capture_path}  ({status}, {capture_summary['bytes']} bytes)")
        if capture_summary["complete"]:
            print(f"             replay: python tools/nv2a_capture.py \"{capture_path}\"")
    if log_summary["exception_lines"] or log_summary["fatal_lines"]:
        print(f"  faults   : {log_summary['exception_lines']} exception line(s), "
              f"{log_summary['fatal_lines']} fatal/terminate marker(s)")
        print(f"             triage: cxbxdbg locate \"{xbe}\" --log \"{logpath}\"")
    print(f"  shots    : {shots_taken}/{len(shot_times)} saved in {shots_dir}")
    if args.dump_frames:
        print(f"  frames   : {n_frames} backbuffer BMP(s) in {frames_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
