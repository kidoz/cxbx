"""Run the bounded native x64 ASan lane and retain reproducible diagnostics."""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

FLAGS = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def command_output(command: list[str], environment: dict[str, str]) -> str:
    return subprocess.run(
        command,
        env=environment,
        check=True,
        capture_output=True,
        text=True,
        timeout=30,
        creationflags=FLAGS,
    ).stdout.strip()


def logged(command: list[str], log: Path, environment: dict[str, str]) -> None:
    print(f"Running {subprocess.list2cmdline(command)}", flush=True)
    try:
        with log.open("w", encoding="utf-8") as output:
            subprocess.run(
                command,
                env=environment,
                stdout=output,
                stderr=subprocess.STDOUT,
                check=True,
                timeout=900,
                creationflags=FLAGS,
            )
    finally:
        print(log.read_text(encoding="utf-8", errors="replace"), end="", flush=True)


def run_lane(source: Path, build: Path, diagnostics: Path) -> None:
    environment = dict(os.environ, CC="clang", CXX="clang++", CC_LD="lld-link", CXX_LD="lld-link")
    tools = {}
    for name in ("clang", "clang++", "lld-link", "llvm-symbolizer", "meson", "ninja"):
        path = shutil.which(name)
        if path is None:
            raise FileNotFoundError(f"Required tool not found: {name}")
        tools[name] = path
    resource = command_output([tools["clang++"], "-print-resource-dir"], environment)
    if not resource:
        raise ValueError("Unable to resolve the Clang resource directory.")
    runtime = Path(resource) / "lib/windows"
    clang_version = command_output([tools["clang++"], "--version"], environment).splitlines()[0]
    report = [
        f"source_directory={source}",
        f"build_directory={build}",
        f"clang_resource_directory={resource}",
        f"asan_runtime_directory={runtime}",
        f"llvm_symbolizer={tools['llvm-symbolizer']}",
        f"meson_version={command_output([tools['meson'], '--version'], environment)}",
        f"ninja_version={command_output([tools['ninja'], '--version'], environment)}",
        f"clang_version={clang_version}",
    ]
    report.extend(f"{name}={path}" for name, path in tools.items())
    for name in (
        "clang_rt.asan_dynamic-x86_64.dll",
        "clang_rt.asan_dynamic-x86_64.lib",
        "clang_rt.asan_static_runtime_thunk-x86_64.lib",
    ):
        data = (runtime / name).read_bytes()
        report.append(
            f"{name} Length={len(data)} SHA256={hashlib.sha256(data).hexdigest().upper()}"
        )
    inventory = "\n".join(report) + "\n"
    (diagnostics / "toolchain.txt").write_text(inventory, encoding="utf-8")
    print(inventory, end="")
    logged(
        [
            tools["meson"],
            "setup",
            str(build),
            str(source),
            "-Dbuild_host_asan=true",
            "-Db_sanitize=address",
            "-Db_vscrt=static_from_buildtype",
            "--buildtype=debugoptimized",
        ],
        diagnostics / "configure.log",
        environment,
    )
    logged([tools["meson"], "compile", "-C", str(build)], diagnostics / "compile.log", environment)
    logged(
        [
            tools["meson"],
            "test",
            "-C",
            str(build),
            "--print-errorlogs",
            "--suite",
            "cxbx:sanitizer",
        ],
        diagnostics / "test-console.log",
        environment,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-directory", required=True, type=Path)
    parser.add_argument("--build-directory", required=True, type=Path)
    parser.add_argument("--diagnostics-directory", required=True, type=Path)
    args = parser.parse_args()
    diagnostics = args.diagnostics_directory.resolve()
    try:
        diagnostics.mkdir(parents=True, exist_ok=True)
        run_lane(args.source_directory.resolve(), args.build_directory.resolve(), diagnostics)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        message = f"status=failed\nmessage={error}\n"
        if diagnostics.is_dir():
            (diagnostics / "result.txt").write_text(message, encoding="utf-8")
        print(message, file=sys.stderr, end="")
        return 1
    (diagnostics / "result.txt").write_text("status=passed\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
