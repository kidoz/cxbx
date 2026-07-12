#!/usr/bin/env python3
"""Force-kill the trace helper and validate every surviving decode anchor."""

from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
import time
from pathlib import Path

HEADER = struct.Struct("<8sHHHHQII")
RECORD_SIZE = 24


def wait_for(path: Path, process: subprocess.Popen[bytes], timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        if process.poll() is not None:
            raise RuntimeError(f"trace helper exited early with {process.returncode}")
        time.sleep(0.005)
    raise RuntimeError(f"timed out waiting for {path.name}")


def validate_text(path: Path) -> None:
    if path.exists() and path.stat().st_size:
        data = path.read_bytes()
        if not data.startswith(b"TRACE| v=2 start qpc_hz="):
            raise RuntimeError("surviving text trace lacks its decode anchor")
        if b"FLIGHT|" in data:
            raise RuntimeError("external termination unexpectedly ran crash dumping")


def validate_event(path: Path, require_records: bool) -> None:
    if not path.exists() or path.stat().st_size == 0:
        if require_records:
            raise RuntimeError("active trace left no event file")
        return
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise RuntimeError("surviving event file has a partial header")
    magic, file_version, grammar, schema, header_size, frequency, record_size, _ = (
        HEADER.unpack_from(data)
    )
    if (
        magic != b"CXBXEVT\0"
        or file_version != 1
        or grammar != 2
        or schema != 1
        or header_size != HEADER.size
        or frequency == 0
        or record_size != RECORD_SIZE
    ):
        raise RuntimeError("surviving event file has an invalid header")
    if (len(data) - header_size) % record_size:
        raise RuntimeError("surviving event file ends with a partial record")
    if require_records and len(data) == header_size:
        raise RuntimeError("active trace did not drain any records before termination")


def run_phase(helper: Path, root: Path, phase: str, wait_active: float) -> None:
    event = root / f"{phase}.evt"
    text = root / f"{phase}.log"
    ready = root / f"{phase}.ready"
    process = subprocess.Popen(
        [str(helper), str(event), str(text), str(ready), phase],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    require_records = phase == "active"
    try:
        if phase != "immediate":
            wait_for(ready, process)
            time.sleep(wait_active)
    finally:
        process.kill()
        process.wait(timeout=5)
        killed_at = time.time()
    validate_text(text)
    validate_event(event, require_records)
    if require_records:
        tail_age_ms = max(0.0, killed_at - event.stat().st_mtime) * 1000.0
        if tail_age_ms > 250.0:
            raise RuntimeError(f"active event tail age {tail_age_ms:.1f}ms exceeds 250ms")
        print(f"active event tail age: {tail_age_ms:.1f}ms")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("helper", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        run_phase(args.helper, root, "immediate", 0.0)
        run_phase(args.helper, root, "initialized", 0.0)
        run_phase(args.helper, root, "active", 0.1)
    print("trace durability anchors passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
