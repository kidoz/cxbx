"""Build d3d_state_block with the Xbox XDK toolchain."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools"))
from xdk_build import BuildSpec, main

SPEC = BuildSpec(
    test_id="0xFFFF0052",
    libraries=("xapilib.lib", "d3d8.lib", "xboxkrnl.lib"),
    # The Xbox d3d8.h guards Begin/EndStateBlock declarations behind this
    # flag (titles that record state blocks define it); the library always
    # contains the functions.
    compiler_flags=("/O2", "/DNDEBUG", "/ML", "/DD3DCOMPILE_BEGINSTATEBLOCK=1"),
)

if __name__ == "__main__":
    sys.exit(main(__file__, SPEC))
