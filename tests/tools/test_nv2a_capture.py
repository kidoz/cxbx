#!/usr/bin/env python3

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


def build_capture(path: Path, bad_method: bool = False) -> None:
    pixels = struct.pack("<4I", 0xFF000000, 0xFFFF0000, 0xFF00FF00, 0xFF0000FF)
    crc = zlib.crc32(pixels) & 0xFFFFFFFF
    packet = (2 << 18) | 0x0100
    records = [
        record(
            nv2a_capture.RAMIN, struct.pack("<II", 4, zlib.crc32(b"RAM!") & 0xFFFFFFFF) + b"RAM!"
        ),
        record(nv2a_capture.PUSH_RUN, struct.pack("<9I", 0, 1, 0, 0, 12, 0, 0, 0, 0xFFFFFFFF)),
        record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, 0, packet)),
        record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, 4, 0x11223344)),
        record(nv2a_capture.METHOD, struct.pack("<4I", 0, 0, 0x0100, 0x11223344)),
        record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, 8, 0x55667788)),
        record(
            nv2a_capture.METHOD,
            struct.pack("<4I", 0, 0, 0x0104, 0 if bad_method else 0x55667788),
        ),
        record(
            nv2a_capture.SCANOUT,
            struct.pack("<6I", 0, 0x00200000, 2, 2, crc, len(pixels)) + pixels,
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


if __name__ == "__main__":
    unittest.main()
