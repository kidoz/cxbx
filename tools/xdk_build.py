"""Shared XDK compiler, linker, asset and XBE pipeline for suite build.py scripts."""

import argparse
import os
import shutil
import subprocess
import sys
import wave
from dataclasses import dataclass
from pathlib import Path

from tool_config import config_path_value, load_config


@dataclass(frozen=True)
class BuildSpec:
    test_id: str
    libraries: tuple[str, ...]
    compiler_flags: tuple[str, ...] = ("/O2", "/DNDEBUG", "/ML")
    common_include: bool = True
    image_map: bool = True
    incremental: bool = False
    shader: bool = False
    sound_banks: bool = False
    video: bool = False
    makespace: bool = False


def run(command: list[str], directory: Path) -> None:
    subprocess.run(
        command,
        cwd=directory,
        check=True,
        timeout=120,
        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
    )


def c_array(path: Path, name: str) -> str:
    data = path.read_bytes()
    lines = [f"static const unsigned char {name}[] = {{"]
    for offset in range(0, len(data), 16):
        lines.append(
            "    " + ", ".join(f"0x{value:02X}" for value in data[offset : offset + 16]) + ","
        )
    return "\n".join([*lines, "};", ""])


def build(source: Path, output: Path, xdk: Path, spec: BuildSpec, cpp_makespace: bool) -> None:
    vc = xdk / "xbox/bin/vc71"
    if not (vc / "CL.Exe").is_file():
        vc = xdk / "xbox/bin/vc7"
    for tool in (vc / "CL.Exe", vc / "Link.Exe", xdk / "xbox/bin/imagebld.exe"):
        if not tool.is_file():
            raise FileNotFoundError(f"Missing XDK tool: {tool}")
    output.mkdir(parents=True, exist_ok=True)
    if spec.shader:
        run(
            [
                str(xdk / "xbox/bin/xsasm.exe"),
                "/nologo",
                "/h",
                "/hname",
                "g_CpuBridgeShader",
                str(source / "shader.vsh"),
                str(output / "cpu_bridge.h"),
            ],
            source,
        )
    if spec.sound_banks:
        with wave.open(str(output / "probe.wav"), "wb") as wav:
            wav.setparams((1, 2, 22050, 256, "NONE", "not compressed"))
            wav.writeframes(bytes(512))
        project = output / "probe.xap"
        project.write_text(
            (source / "probe.xap")
            .read_text(encoding="ascii")
            .replace(r"File = bin\probe.wav;", "File = probe.wav;"),
            encoding="ascii",
            newline="\n",
        )
        run([str(xdk / "xbox/bin/xactbld.exe"), "/L", str(project), str(output)], output)
        banks = "#pragma once\n\n" + c_array(output / "probe.xsb", "g_ProbeSoundBank")
        banks += "\n" + c_array(output / "probe.xwb", "g_ProbeWaveBank")
        (output / "probe_banks.h").write_text(banks, encoding="ascii", newline="\n")
    includes = [f"/I{xdk / 'xbox/include'}"]
    if spec.common_include:
        includes.append(f"/I{source.parents[1] / 'common'}")
    if spec.shader or spec.sound_banks:
        includes.append(f"/I{output}")
    defines = ["/DCXBX_MAKESPACE_CPP"] if spec.makespace and cpp_makespace else []
    run(
        [
            str(vc / "CL.Exe"),
            "/nologo",
            "/c",
            "/Gy",
            "/D_XBOX",
            "/W3",
            *spec.compiler_flags,
            *defines,
            *includes,
            f"/Fo{output / 'main.obj'}",
            str(source / "main.cpp"),
        ],
        source,
    )
    name = source.name
    executable = output / f"{name}.exe"
    map_path = output / f"{name}.map"
    run(
        [
            str(vc / "Link.Exe"),
            "/nologo",
            "/MACHINE:I386",
            "/FIXED:NO",
            "/SUBSYSTEM:XBOX",
            *([] if spec.incremental else ["/INCREMENTAL:NO"]),
            f"/LIBPATH:{xdk / 'xbox/lib'}",
            f"/OUT:{executable}",
            f"/MAP:{map_path}",
            str(output / "main.obj"),
            *spec.libraries,
        ],
        source,
    )
    run(
        [
            str(xdk / "xbox/bin/imagebld.exe"),
            "/NOLIBWARN",
            "/DONTMOUNTUD",
            "/STACK:0x10000",
            f"/TESTNAME:{name}",
            f"/TESTID:{spec.test_id}",
            f"/IN:{executable}",
            *([f"/MAP:{map_path}"] if spec.image_map else []),
            f"/OUT:{output / 'default.xbe'}",
        ],
        source,
    )
    if spec.video:
        media = output / "Media/Videos"
        media.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(
            xdk / "Samples/Xbox/Video/SimpleXMV/Media/Videos/test.xmv", media / "Test.xmv"
        )
    print(f"OK: {output / 'default.xbe'}")


def main(script: str, spec: BuildSpec) -> int:
    source = Path(script).resolve().parent
    parser = argparse.ArgumentParser(description=f"Build {source.name} with an Xbox XDK toolchain.")
    parser.add_argument(
        "--xdk", type=Path, help="XDK root; otherwise CXBX_XDK_ROOT, CXBX_XDK, or xdkaudit.xdk_root"
    )
    parser.add_argument("--output-dir", type=Path, default=source / "bin")
    if spec.makespace:
        parser.add_argument(
            "--cpp-makespace",
            action=argparse.BooleanOptionalAction,
            default=True,
            help="use the XDK 4627 D3D C++ MakeSpace export (default: enabled)",
        )
    args = parser.parse_args()
    try:
        xdk = args.xdk
        if xdk is None:
            value = os.environ.get("CXBX_XDK_ROOT") or os.environ.get("CXBX_XDK")
            xdk = (
                Path(value)
                if value
                else config_path_value(load_config(required=False), "xdkaudit", "xdk_root")
            )
        if xdk is None:
            raise ValueError("Pass --xdk or set CXBX_XDK_ROOT to the required Xbox XDK root.")
        build(
            source,
            args.output_dir.resolve(),
            xdk.expanduser().resolve(),
            spec,
            args.cpp_makespace if spec.makespace else False,
        )
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        return 1
    return 0
