"""Run renderer regressions with an installed x64 Khronos validation layer.

This renderer-only host variant does not establish guest ABI/TLS correctness.
The canonical i686 tests remain in Meson. Build products and logs are retained.
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SCENARIOS = (
    "vertex",
    "index",
    "target",
    "combiner",
    "combiner_logic",
    "depth",
    "sampling",
    "descriptors",
    "raster",
    "present",
)
FLAGS = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0


def run_validation(layer: Path, output: Path) -> int:
    root = Path(__file__).resolve().parents[2]
    if not (layer / "VkLayer_khronos_validation.json").is_file():
        raise ValueError(
            "Layer directory must contain the x64 Khronos validation manifest and DLL."
        )
    output.mkdir(parents=True, exist_ok=True)
    volk = output / "volk.obj"
    executable = output / "renderer_test.exe"
    subprocess.run(
        [
            "clang",
            "--target=x86_64-pc-windows-msvc",
            "-DVK_USE_64_BIT_PTR_DEFINES=0",
            "-DVK_USE_PLATFORM_WIN32_KHR",
            "-Ithird_party/vulkan-headers/1.4.350",
            "-c",
            "third_party/volk/1.4.350/volk.c",
            "-o",
            str(volk),
        ],
        cwd=root,
        check=True,
        timeout=120,
        creationflags=FLAGS,
    )
    # Match the i686 module's opaque handle representation with the x64 loader ABI.
    subprocess.run(
        [
            "clang++",
            "--target=x86_64-pc-windows-msvc",
            "-std=c++20",
            "-fms-extensions",
            "-DVK_USE_64_BIT_PTR_DEFINES=0",
            "-Ithird_party/vulkan-headers/1.4.350",
            "-Ithird_party/volk/1.4.350",
            "-Isrc/cxbx/src/hle/d3d8",
            "tests/host/vulkan_renderer.cpp",
            "src/cxbx/src/hle/d3d8/vulkan/vulkan_backend.cpp",
            "src/cxbx/src/hle/d3d8/vulkan/vulkan_renderer.cpp",
            str(volk),
            "-luser32",
            "-o",
            str(executable),
        ],
        cwd=root,
        check=True,
        timeout=120,
        creationflags=FLAGS,
    )
    environment = dict(
        os.environ, CXBX_TEST_VULKAN="1", VK_LAYER_PATH=str(layer), VK_LAYER_VALIDATE_SYNC="1"
    )
    environment.pop("VK_LAYER_ENABLES", None)
    failures = 0
    for scenario in SCENARIOS:
        log = output / f"{scenario}.log"
        try:
            with log.open("w", encoding="utf-8") as stream:
                result = subprocess.run(
                    [str(executable), scenario],
                    cwd=root,
                    env=environment,
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                    timeout=30,
                    creationflags=FLAGS,
                )
            passed = (
                result.returncode == 0
                and "VULKAN| Khronos validation active"
                in log.read_text(encoding="utf-8", errors="replace")
            )
        except subprocess.TimeoutExpired:
            passed = False
        if passed:
            print(f"PASS: {scenario} (validation active)")
        else:
            failures += 1
            print(f"FAIL: {scenario} (see {log})")
    print(f"Artifacts: {output}")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layer-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path)
    args = parser.parse_args()
    try:
        output = args.output_directory or Path(tempfile.mkdtemp(prefix="cxbx-vulkan-validation-"))
        return run_validation(args.layer_directory.resolve(), output.resolve())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"Vulkan validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
