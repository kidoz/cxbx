#!/usr/bin/env python3
"""Run the native trace benchmark and enforce its checked-in reference profile."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path
from typing import Any


def powershell(command: str) -> str:
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", command],
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout.strip()


def host_profile() -> dict[str, str]:
    cpu = powershell("(Get-CimInstance Win32_Processor | Select-Object -First 1).Name")
    power = powershell("powercfg /getactivescheme")
    match = re.search(r"[0-9a-fA-F-]{36}", power)
    return {"cpu": cpu, "power_scheme_guid": match.group(0).lower() if match else ""}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument(
        "--profile",
        type=Path,
        default=Path(__file__).with_name("trace") / "benchmark_profile.toml",
    )
    args = parser.parse_args()
    profile = tomllib.loads(args.profile.read_text(encoding="utf-8"))
    reference = profile["reference"]
    result = subprocess.run([str(args.executable)], capture_output=True, text=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(result.stdout)
        return result.returncode
    measurements: dict[str, Any] = json.loads(result.stdout)
    host = host_profile()
    matches = (
        host["cpu"] == reference["cpu"]
        and host["power_scheme_guid"] == reference["power_scheme_guid"]
        and measurements["compiler"] == reference["compiler"]
        and measurements["iterations"] >= reference["iterations"]
        and measurements["optimization"] == reference["optimization"]
    )
    print(json.dumps({"reference_match": matches, "host": host, **measurements}, indent=2))
    if not matches:
        print("tracebench: reporting only; host does not match the reference profile")
        return 0
    targets = profile["targets_ns"]
    failures = [
        f"{name}={measurements[name + '_ns']:.3f}ns > {limit:.3f}ns"
        for name, limit in targets.items()
        if measurements[name + "_ns"] > limit
    ]
    if failures:
        print("tracebench: " + "; ".join(failures), file=sys.stderr)
        return 1
    print("tracebench: reference budgets passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
