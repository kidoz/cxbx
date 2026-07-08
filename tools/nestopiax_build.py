#!/usr/bin/env python3
"""Rebuild NestopiaX 1.0 from its released source against XDK 5849 using the
XDK's own vc71 toolchain (CL/Link) + imagebld -- the same pipeline as the
suite's XDK probes. Honors the vcproj Release|Xbox configuration (defines,
include dirs, per-file excludes)."""

import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

SRC = Path(r"D:\projects\cxbx\other\source\NestopiaX_1.0_Source")
XDK = Path(r"D:\projects\cxbx\other\sdk\XDKSetup5849.15_extracted\XDK")
VC = XDK / "xbox" / "bin" / "vc71"
OUT = Path(r"C:\Users\ALEKSA~1\AppData\Local\Temp\claude\D--projects-cxbx\475e1110-42b6-4414-877d-6984a4cb8dcb\scratchpad\nx_build")
CONFIG = "Release|Xbox"

DEFINES = ["NDEBUG", "_XBOX", "EMU_A68K", "MMX", "_CONSOLE", "_SECURE_SCL=0", "CXBX_NO_SPLASH_MP3"]
INCLUDES = [
    r".\src\cpu\s2650", r".\src\alt\xbox", r".\src\alt\mp3", r".\src\burn\cps3",
    r".\src\cpu\m6809", r".\src\cpu\hd6309", r".\src\alt", r".\src\cpu\m6800",
    r".\src\cpu\m68k", r".\src\burn\konami", r".\src\burn", r".\src\burner",
    r".\src\cpu\konami", r".\src\vc\include", r".\src\interface",
    r".\src\cpu\i8039", r".\src\cpu\arm7", r".\src\cpu\z80", r".\src\burn\misc",
    r".\src\cpu\sh2", r".\src\cpu\nec", r".\src\cpu\m6502", r".\src\generated",
    r".\src\cpu\doze", r".\src\Nestopia\source\zlib",
    r".\src\Nestopia\source\linux\unzip", r".\src\Nestopia\source\linux\7zip",
    r".\src\Nestopia\source\linux", r".\src\Nestopia\source\core\vssystem",
    r".\src\Nestopia\source\core\input", r".\src\Nestopia\source\core\board",
    r".\src\Nestopia\source\core\api", r".\src\Nestopia\source\core",
]
LIBS = ("xapilib.lib d3d8.lib d3dx8.lib xgraphics.lib xboxkrnl.lib dsound.lib "
        "xacteng.lib xsndtrk.lib xvoice.lib xonlines.lib zlibstat.lib xmv.lib").split()


def collect_sources():
    """(compile_list, prebuilt_obj_list) honoring Release|Xbox per-file
    ExcludedFromBuild. .asm entries use the prebuilt sibling .obj the source
    tree ships (no MASM in the XDK toolchain)."""
    tree = ET.parse(SRC / "NestopiaX.vcproj")
    compile_list, prebuilt = [], []
    for f in tree.iter("File"):
        rel = f.get("RelativePath")
        if not rel:
            continue
        low = rel.lower()
        excluded = False
        for fc in f.iter("FileConfiguration"):
            if fc.get("Name") == CONFIG and fc.get("ExcludedFromBuild") == "TRUE":
                excluded = True
        if excluded:
            continue
        if low.endswith((".cpp", ".c")):
            compile_list.append(rel)
        elif low.endswith(".obj"):
            prebuilt.append(SRC / rel)
        elif low.endswith(".asm"):
            obj = (SRC / rel).with_suffix(".obj")
            if obj.exists():
                prebuilt.append(obj)
            else:
                print(f"WARNING: no prebuilt obj for {rel}")
    return compile_list, prebuilt


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    objdir = OUT / "obj"
    objdir.mkdir(exist_ok=True)

    sources, prebuilt = collect_sources()
    print(f"{len(sources)} sources, {len(prebuilt)} prebuilt objs")

    inc = [f'/I{XDK / "xbox" / "include"}'] + [f"/I{SRC / i}" for i in INCLUDES]
    defs = [f"/D{d}" for d in DEFINES]
    base = [str(VC / "CL.Exe"), "/nologo", "/c", "/O2", "/Oy", "/GB", "/W0",
            "/MT", "/GF", "/Gy"] + defs + inc

    objs, failed = [], []
    for i, rel in enumerate(sources):
        src = (SRC / rel).resolve()
        # Unique object names: mirror the relative path with __ separators.
        objname = rel.strip(".\\").replace("\\", "__").replace("/", "__")
        objname = objname.rsplit(".", 1)[0] + ".obj"
        obj = objdir / objname
        objs.append(obj)
        if obj.exists() and obj.stat().st_mtime >= src.stat().st_mtime:
            continue
        r = subprocess.run(base + [f"/Fo{obj}", str(src)],
                           capture_output=True, text=True, cwd=SRC)
        if r.returncode != 0:
            failed.append((rel, (r.stdout + r.stderr).strip()))
            print(f"[{i+1}/{len(sources)}] FAIL {rel}")
        elif (i + 1) % 50 == 0:
            print(f"[{i+1}/{len(sources)}] ok")

    if failed:
        print(f"\n{len(failed)} FAILED:")
        for rel, err in failed[:8]:
            print(f"--- {rel} ---")
            print("\n".join(err.splitlines()[:12]))
        sys.exit(1)

    print("linking...")
    exe = OUT / "NestopiaX.exe"
    rsp = OUT / "link.rsp"
    rsp_lines = ["/nologo", "/MACHINE:I386", "/FIXED:NO", "/SUBSYSTEM:XBOX",
                 "/INCREMENTAL:NO", "/NODEFAULTLIB:LIBC",
                 f'/LIBPATH:"{XDK / "xbox" / "lib"}"',
                 f'/LIBPATH:"{SRC / "src" / "Nestopia" / "lib"}"',
                 f'/OUT:"{exe}"', f'/MAP:"{OUT / "NestopiaX.map"}"',
                 ] + [f'"{o}"' for o in objs + prebuilt] + LIBS
    rsp.write_text("\n".join(rsp_lines), encoding="ascii")
    r = subprocess.run([str(VC / "Link.Exe"), f"@{rsp}"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print((r.stdout + r.stderr)[-4000:])
        sys.exit(1)

    print("imagebld...")
    r = subprocess.run([str(XDK / "xbox" / "bin" / "imagebld.exe"),
                        "/NOLIBWARN", "/DONTMOUNTUD", "/STACK:0xC0000",
                        "/TESTNAME:NestopiaX", "/TESTID:0xFFFF0780",
                        f"/IN:{exe}", f"/MAP:{OUT / 'NestopiaX.map'}",
                        f"/OUT:{OUT / 'default.xbe'}"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print((r.stdout + r.stderr)[-2000:])
        sys.exit(1)
    print(f"OK: {OUT / 'default.xbe'}")


if __name__ == "__main__":
    main()
