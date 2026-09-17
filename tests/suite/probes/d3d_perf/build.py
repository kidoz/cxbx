"""Build d3d_perf with the Xbox XDK toolchain."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
from xdk_build import BuildSpec, main

SPEC = BuildSpec(
    test_id="0xFFFF0012",
    libraries=("xperf.lib", "xbdm.lib", "xapilib.lib", "d3d8i.lib", "xboxkrnl.lib"),
    compiler_flags=("/O2", "/DNDEBUG", "/DPROFILE", "/ML"),
)

if __name__ == "__main__":
    sys.exit(main(__file__, SPEC))
