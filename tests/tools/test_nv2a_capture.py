#!/usr/bin/env python3

import contextlib
import io
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import nv2a_capture  # noqa: E402


def record(record_type: int, payload: bytes) -> bytes:
    return struct.pack("<II", record_type, len(payload)) + payload


def build_capture(
    path: Path,
    bad_method: bool = False,
    method_value: int = 0x55667788,
    base: int = 0,
    memory_address: int = 0x00100000,
    scanout_address: int = 0x00200000,
) -> None:
    pixels = struct.pack("<4I", 0xFF000000, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF)
    crc = zlib.crc32(pixels) & 0xFFFFFFFF
    packet = (2 << 18) | 0x0100
    records = [
        record(
            nv2a_capture.RAMIN, struct.pack("<II", 4, zlib.crc32(b"RAM!") & 0xFFFFFFFF) + b"RAM!"
        ),
        record(
            nv2a_capture.PUSH_RUN,
            struct.pack("<9I", 0, 1, base, base, base + 12, 0, 0, 0, 0xFFFFFFFF),
        ),
        record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, base, packet)),
        record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, base + 4, 0x11223344)),
        record(nv2a_capture.METHOD, struct.pack("<4I", 0, 0, 0x0100, 0x11223344)),
        record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, base + 8, method_value)),
        record(
            nv2a_capture.METHOD,
            struct.pack("<4I", 0, 0, 0x0104, 0 if bad_method else method_value),
        ),
        record(
            nv2a_capture.MEMORY,
            struct.pack("<III", memory_address, len(pixels), crc) + pixels,
        ),
        record(
            nv2a_capture.SCANOUT,
            struct.pack("<6I", 0, scanout_address, 2, 2, crc, len(pixels)) + pixels,
        ),
    ]
    records.append(record(nv2a_capture.FINISH, struct.pack("<5I", 0, 0, len(records), 0, crc)))
    header = nv2a_capture.HEADER.pack(
        nv2a_capture.MAGIC,
        nv2a_capture.VERSION,
        nv2a_capture.ENDIAN_MARKER,
        nv2a_capture.HEADER.size,
        0,
        64 * 1024 * 1024,
    )
    path.write_bytes(header + b"".join(records))


class Nv2aCaptureTests(unittest.TestCase):
    def test_normalizes_host_control_targets_only(self) -> None:
        base = 0x10000000
        self.assertEqual(
            nv2a_capture.normalize_control_word(0x30000040, True, base, False),
            0x20000040,
        )
        self.assertEqual(
            nv2a_capture.normalize_control_word(0x10000041, True, base, False),
            0x00000041,
        )
        self.assertEqual(
            nv2a_capture.normalize_control_word(0x10000042, True, base, False),
            0x00000042,
        )
        self.assertEqual(
            nv2a_capture.normalize_control_word(0x30000040, True, base, True),
            0x30000040,
        )

    def test_replays_packet_stream_and_validates_scanout(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "frame.nv2acap"
            build_capture(path)
            result = nv2a_capture.analyze_capture(path)
        self.assertEqual(result["replay"], "pass")
        self.assertEqual(result["methods"], 2)
        self.assertEqual(result["scanouts"][0]["width"], 2)

    def test_detects_method_divergence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.nv2acap"
            build_capture(path, bad_method=True)
            with self.assertRaisesRegex(nv2a_capture.CaptureError, "PFIFO replay diverged"):
                nv2a_capture.analyze_capture(path)

    def test_comparison_normalizes_relocated_addresses(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            baseline = Path(directory) / "baseline.nv2acap"
            candidate = Path(directory) / "candidate.nv2acap"
            build_capture(baseline, base=0x10000000)
            build_capture(
                candidate,
                base=0x20000000,
                memory_address=0x00300000,
                scanout_address=0x00400000,
            )
            semantic = nv2a_capture.compare_captures(baseline, candidate)
            strict = nv2a_capture.compare_captures(baseline, candidate, strict_addresses=True)
            with contextlib.redirect_stdout(io.StringIO()):
                match_exit = nv2a_capture.compare_main([str(baseline), str(candidate)])
                strict_exit = nv2a_capture.compare_main(
                    [str(baseline), str(candidate), "--strict-addresses"]
                )
        self.assertTrue(semantic["equal"])
        self.assertFalse(strict["equal"])
        self.assertEqual(match_exit, 0)
        self.assertEqual(strict_exit, 1)
        self.assertEqual(strict["first_divergence"]["baseline"]["category"], "push_runs")

    def test_comparison_reports_valid_method_change(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            baseline = Path(directory) / "baseline.nv2acap"
            candidate = Path(directory) / "candidate.nv2acap"
            build_capture(baseline)
            build_capture(candidate, method_value=0xDEADBEEF)
            result = nv2a_capture.compare_captures(baseline, candidate)
            with contextlib.redirect_stdout(io.StringIO()):
                exit_code = nv2a_capture.compare_main([str(baseline), str(candidate)])
            with contextlib.redirect_stderr(io.StringIO()):
                invalid_exit = nv2a_capture.compare_main(
                    [str(baseline), str(Path(directory) / "missing.nv2acap")]
                )
        self.assertFalse(result["equal"])
        self.assertFalse(result["categories"]["methods"]["equal"])
        self.assertEqual(
            result["categories"]["methods"]["first_divergence"]["candidate"]["data"],
            0xDEADBEEF,
        )
        self.assertEqual(exit_code, 1)
        self.assertEqual(invalid_exit, 2)


if __name__ == "__main__":
    unittest.main()
