"""Build xmv_play with the Xbox XDK toolchain."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
from xdk_build import BuildSpec, main

SPEC = BuildSpec(
    test_id="0xFFFF0011",
    libraries=("xmv.lib", "xapilib.lib", "d3d8.lib", "dsound.lib", "xboxkrnl.lib"),
    video=True,
)

if __name__ == "__main__":
    sys.exit(main(__file__, SPEC))
