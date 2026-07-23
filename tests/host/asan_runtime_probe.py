from __future__ import annotations

import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: asan_runtime_probe.py <probe-executable>", file=sys.stderr)
        return 2

    try:
        result = subprocess.run(
            [sys.argv[1], "8"],
            capture_output=True,
            check=False,
            text=True,
            timeout=10,
        )
    except subprocess.TimeoutExpired:
        print("ASan probe timed out", file=sys.stderr)
        return 1
    output = result.stdout + result.stderr

    if result.returncode == 0:
        print("ASan probe unexpectedly succeeded", file=sys.stderr)
        return 1

    expected_diagnostics = ("AddressSanitizer", "heap-buffer-overflow")
    missing = [
        diagnostic
        for diagnostic in expected_diagnostics
        if diagnostic not in output
    ]
    if missing:
        print(
            "ASan probe failed without expected diagnostics: "
            + ", ".join(missing),
            file=sys.stderr,
        )
        print(output, file=sys.stderr)
        return 1

    print("PASS ASan detected the deliberate heap buffer overflow")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
