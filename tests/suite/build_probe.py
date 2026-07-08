#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build one conformance probe to an XBE using the MSYS2 make + scoop toolchain.

Toolchain paths come from the local tools/config.toml (see tools/config.toml.example)
rather than being hardcoded, so this script carries no machine-specific paths.

    python build_probe.py <probe-dir> [make-args...]
    python build_probe.py tests/suite/probes/smoke -j4

The nxdk build requires the specific toolchain mix (MSYS2 cygwin make + scoop
clang/lld + scoop g++ for host tools) documented in tests/suite/README.md, so the
actual build still runs `make` inside MSYS2 bash; this script just assembles the
right environment from config and invokes it.
"""

import os
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from tool_config import config_path_value, load_config, repo_root


def win_to_posix(p: Path) -> str:
    """C:\\Users\\x -> /c/Users/x, for an MSYS2/Cygwin shell."""
    s = str(p).replace("\\", "/")
    if len(s) >= 2 and s[1] == ":":
        s = "/" + s[0].lower() + s[2:]
    return s


def main(argv: list[str]) -> int:
    if not argv:
        print("usage: build_probe.py <probe-dir> [make-args...]", file=sys.stderr)
        return 2

    probe_dir = Path(argv[0]).expanduser()
    if not probe_dir.is_absolute():
        probe_dir = (repo_root() / probe_dir).resolve()
    if not probe_dir.is_dir():
        print(f"build_probe.py: no such probe directory: {probe_dir}", file=sys.stderr)
        return 2
    make_args = argv[1:]

    try:
        cfg = load_config(required=True)
    except (FileNotFoundError, OSError) as e:
        print(str(e), file=sys.stderr)
        return 2

    nxdk = config_path_value(cfg, "paths", "nxdk_dir", required=True)
    bash = config_path_value(cfg, "paths", "msys2_bash", required=True)
    scoop = config_path_value(cfg, "paths", "scoop", required=True)

    nxdk_posix = win_to_posix(nxdk)
    scoop_posix = win_to_posix(scoop)
    probe_posix = win_to_posix(probe_dir)

    # nxdk/bin plus the scoop clang/lld and g++ (host tools) directories.
    toolchain = ":".join(
        [
            f"{nxdk_posix}/bin",
            f"{scoop_posix}/apps/llvm/current/bin",
            f"{scoop_posix}/apps/gcc/current/bin",
            f"{scoop_posix}/shims",
        ]
    )

    script = (
        'set -e; '
        'export MSYSTEM=MINGW64; '
        f'export NXDK_DIR="{nxdk_posix}"; '
        f'export PATH="$PATH:{toolchain}"; '
        f'cd "{probe_posix}"; '
        f'exec /usr/bin/make NXDK_DIR="{nxdk_posix}" {" ".join(make_args)}'
    )

    env = dict(os.environ, MSYSTEM="MINGW64")
    return subprocess.run([str(bash), "-lc", script], env=env).returncode


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
