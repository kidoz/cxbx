"""Build xonline_startup with the Xbox XDK toolchain."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
from xdk_build import BuildSpec, main

SPEC = BuildSpec(
    test_id="0xFFFF0040",
    libraries=("xapilib.lib", "xonline.lib", "xboxkrnl.lib"),
)

if __name__ == "__main__":
    sys.exit(main(__file__, SPEC))
