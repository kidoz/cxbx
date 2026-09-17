"""Build xdk_smoke with the Xbox XDK toolchain."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
from xdk_build import BuildSpec, main

SPEC = BuildSpec(
    test_id="0xFFFF0001",
    libraries=("xapilib.lib", "xboxkrnl.lib"),
    common_include=False,
    incremental=True,
)

if __name__ == "__main__":
    sys.exit(main(__file__, SPEC))
