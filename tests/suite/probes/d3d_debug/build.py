"""Build d3d_debug with the Xbox XDK toolchain."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
from xdk_build import BuildSpec, main

SPEC = BuildSpec(
    test_id="0xFFFF0011",
    libraries=("xperf.lib", "xbdm.lib", "xapilibd.lib", "d3d8d.lib", "xboxkrnl.lib"),
    compiler_flags=("/Od", "/D_DEBUG", "/MLd"),
)

if __name__ == "__main__":
    sys.exit(main(__file__, SPEC))
