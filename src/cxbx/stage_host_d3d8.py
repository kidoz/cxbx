# Copies the host d3d8 override dlls (DXVK, third_party/dxvk/<ver>/x86/) into
# <build-dir>/host-d3d8/ next to the built cxbx.exe; the launcher stages them
# from there next to the %TEMP% guest exe before spawn. See
# third_party/dxvk/3.0.2/README.md. Invoked by src/cxbx/meson.build.
import shutil
import sys
from pathlib import Path

outdir = Path(sys.argv[1])
dest = outdir / "host-d3d8"
dest.mkdir(parents=True, exist_ok=True)
for arg in sys.argv[2:]:
    src = Path(arg)
    shutil.copy2(src, dest / src.name)
(outdir / "host-d3d8.stamp").write_bytes(b"")
