"""Behavior checks for the Python build, CI and capture entry points."""

import ctypes
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import wave
import zlib
from contextlib import redirect_stdout
from pathlib import Path
from typing import TextIO, cast
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
for directory in (
    ROOT / "tools",
    ROOT / ".github/scripts",
    ROOT / "tests/host",
    ROOT / "tools/xtest",
):
    sys.path.insert(0, str(directory))

import clang_tidy_diff  # noqa: E402
import run_native_asan  # noqa: E402
import run_vulkan_validation  # noqa: E402

import capture_window  # noqa: E402
import xdk_build  # noqa: E402
import xtest  # noqa: E402


class ScriptTests(unittest.TestCase):
    def test_diff_ranges_exclude_deletions_and_headers(self) -> None:
        self.assertEqual(
            clang_tidy_diff.changed_ranges(
                "+++ b/src/a.cpp\n@@ -1 +4,2 @@\n@@ -8,2 +9,0 @@\n@@ -11 +12 @@\n"
                "+++ b/src/a.h\n@@ -1 +2 @@\n+++ b/tests/empty.cpp\n@@ -1 +0,0 @@\n"
            ),
            {"src/a.cpp": [[4, 5], [12, 12]]},
        )

    def test_tidy_receives_unescaped_json_and_only_compiled_files(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cxbx scripts ") as temp:
            root = Path(temp)
            (root / "compile_commands.json").write_text(
                json.dumps([{"directory": str(root), "file": "src/a.cpp"}]), encoding="utf-8"
            )
            diff = "+++ b/src/a.cpp\n@@ -1 +2,3 @@\n+++ b/src/b.cpp\n@@ -1 +1 @@\n"
            with patch.object(clang_tidy_diff.subprocess, "run") as run:
                run.return_value = subprocess.CompletedProcess([], 0, diff)
                clang_tidy_diff.analyze(root, "base", "HEAD", root)
                command = run.call_args_list[-1].args[0]
                filters = json.loads(command[2].removeprefix("--line-filter="))
                self.assertEqual(filters, [{"name": str(root / "src/a.cpp"), "lines": [[2, 4]]}])
                self.assertEqual(command[4:], [str(root / "src/a.cpp")])

    def test_xdk_assets_flags_and_vc7_fallback(self) -> None:
        with tempfile.TemporaryDirectory(prefix="cxbx xdk ") as temp:
            root = Path(temp)
            source, output, xdk = root / "probes/example", root / "output", root / "sdk"
            source.mkdir(parents=True)
            (source / "probe.xap").write_text(r"File = bin\probe.wav;", encoding="ascii")
            for tool in ("vc7/CL.Exe", "vc7/Link.Exe", "imagebld.exe"):
                path = xdk / "xbox/bin" / tool
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            output.mkdir()
            (output / "probe.xsb").write_bytes(b"\x01\xff")
            (output / "probe.xwb").write_bytes(b"\x02\x00")
            media = xdk / "Samples/Xbox/Video/SimpleXMV/Media/Videos/test.xmv"
            media.parent.mkdir(parents=True)
            media.write_bytes(b"movie")
            spec = xdk_build.BuildSpec(
                "0xFFFF0001",
                ("d3d8.lib",),
                shader=True,
                sound_banks=True,
                video=True,
                makespace=True,
                image_map=False,
            )
            with patch.object(xdk_build, "run") as run:
                xdk_build.build(source, output, xdk, spec, True)
                commands = [call.args[0] for call in run.call_args_list]
            self.assertEqual(len(commands), 5)
            self.assertIn("/DCXBX_MAKESPACE_CPP", commands[2])
            self.assertIn(f"/I{root / 'common'}", commands[2])
            self.assertIn(f"/I{output}", commands[2])
            self.assertTrue(
                commands[2][0].endswith("vc7\\CL.Exe")
                if os.name == "nt"
                else commands[2][0].endswith("vc7/CL.Exe")
            )
            self.assertFalse(any(arg.startswith("/MAP:") for arg in commands[-1]))
            self.assertEqual((output / "Media/Videos/Test.xmv").read_bytes(), b"movie")
            self.assertIn("0x01, 0xFF,", (output / "probe_banks.h").read_text())
            with wave.open(str(output / "probe.wav"), "rb") as wav:
                self.assertEqual(
                    (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()), (1, 2, 22050)
                )
                self.assertEqual(wav.readframes(256), bytes(512))

    def test_xdk_compiler_failure_stops_before_link(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ("vc71/CL.Exe", "vc71/Link.Exe", "imagebld.exe"):
                path = root / "xbox/bin" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            with patch.object(
                xdk_build, "run", side_effect=subprocess.CalledProcessError(2, "cl")
            ) as run:
                with self.assertRaises(subprocess.CalledProcessError):
                    xdk_build.build(
                        root, root / "output", root, xdk_build.BuildSpec("1", ()), False
                    )
                self.assertEqual(run.call_count, 1)

    def test_probe_discovery_and_python_launch(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            probe = root / "probes/example"
            (probe / "bin").mkdir(parents=True)
            (probe / "build.py").touch()
            (probe / "bin/default.xbe").touch()
            self.assertEqual(xtest.discover_probes(root), ["example"])
            with patch.object(xtest.subprocess, "run") as run:
                run.return_value = subprocess.CompletedProcess([], 0, "OK", "")
                self.assertEqual(
                    xtest.build_probe({"paths": {"suite_dir": str(root)}}, "example"), (True, "OK")
                )
                self.assertEqual(run.call_args.args[0], [sys.executable, str(probe / "build.py")])
                run.side_effect = subprocess.TimeoutExpired("builder", 600)
                self.assertFalse(
                    xtest.build_probe({"paths": {"suite_dir": str(root)}}, "example")[0]
                )

    def test_png_channels_orientation_and_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "capture.png"
            pixels = bytes((0, 0, 255, 0, 0, 255, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0))
            capture_window.save_png(path, 2, 2, pixels)
            png = path.read_bytes()
            self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
            offset, compressed = 8, b""
            while offset < len(png):
                length = struct.unpack_from(">I", png, offset)[0]
                kind, data = png[offset + 4 : offset + 8], png[offset + 8 : offset + 8 + length]
                self.assertEqual(
                    struct.unpack_from(">I", png, offset + 8 + length)[0], zlib.crc32(kind + data)
                )
                if kind == b"IDAT":
                    compressed += data
                offset += length + 12
            self.assertEqual(zlib.decompress(compressed), b"\0\xff\0\0\0\xff\0\0\0\0\xff\0\0\0")
            self.assertEqual(
                capture_window.client_metrics(pixels, 2, 0, 0, 2, 2),
                "samples=1 nonblack=1.000000 colors=1 bbox=0,0,0,0",
            )

    @unittest.skipUnless(sys.platform == "win32", "Win32 capture integration")
    def test_capture_owned_offscreen_window(self) -> None:
        user = ctypes.WinDLL("user32", use_last_error=True)
        user.CreateWindowExW.restype = ctypes.c_void_p
        user.CreateWindowExW.argtypes = [
            ctypes.c_ulong,
            ctypes.c_wchar_p,
            ctypes.c_wchar_p,
            ctypes.c_ulong,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
        ]
        user.DestroyWindow.argtypes = [ctypes.c_void_p]
        hwnd = user.CreateWindowExW(
            0, "STATIC", "Capture test", 0x90000006, -30000, -30000, 128, 64, None, None, None, None
        )
        self.assertTrue(hwnd)
        try:
            with tempfile.TemporaryDirectory() as temp:
                destination = Path(temp) / "capture.png"
                result = capture_window.capture(os.getpid(), destination)
                self.assertTrue(result.startswith("SAVED "), result)
                self.assertIn("client=", result)
                self.assertTrue(destination.read_bytes().startswith(b"\x89PNG"))
                self.assertEqual(capture_window.capture(0x7FFFFFFF, destination), "NOWINDOW")
        finally:
            user.DestroyWindow(hwnd)

    def test_asan_failure_writes_result_and_retains_log(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with patch.object(
                run_native_asan.subprocess,
                "run",
                side_effect=subprocess.CalledProcessError(3, "meson"),
            ):
                with self.assertRaises(subprocess.CalledProcessError):
                    run_native_asan.logged(["meson"], root / "compile.log", {})
            self.assertTrue((root / "compile.log").is_file())
            with patch.object(
                sys,
                "argv",
                [
                    "asan",
                    "--source-directory",
                    temp,
                    "--build-directory",
                    temp,
                    "--diagnostics-directory",
                    temp,
                ],
            ):
                with patch.object(run_native_asan, "run_lane", side_effect=OSError("tool missing")):
                    self.assertEqual(run_native_asan.main(), 1)
            self.assertIn("status=failed\nmessage=tool missing", (root / "result.txt").read_text())

    def test_vulkan_requires_active_layer_and_continues_after_timeout(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "VkLayer_khronos_validation.json").touch()
            environment = dict(os.environ)

            def run(command: list[str], **kwargs: object) -> subprocess.CompletedProcess[str]:
                if command[-1] == "vertex":
                    raise subprocess.TimeoutExpired(command, 30)
                if "stdout" in kwargs and command[-1] != "index":
                    cast(TextIO, kwargs["stdout"]).write("VULKAN| Khronos validation active\n")
                return subprocess.CompletedProcess(command, 0)

            with patch.object(run_vulkan_validation.subprocess, "run", side_effect=run):
                with redirect_stdout(io.StringIO()) as output:
                    self.assertEqual(run_vulkan_validation.run_validation(root, root), 1)
            self.assertIn("FAIL: vertex", output.getvalue())
            self.assertIn("FAIL: index", output.getvalue())
            self.assertIn("PASS: present", output.getvalue())
            self.assertTrue((root / "present.log").is_file())
            self.assertEqual(dict(os.environ), environment)


if __name__ == "__main__":
    unittest.main()
