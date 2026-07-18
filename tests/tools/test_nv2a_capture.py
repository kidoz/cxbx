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


def build_pgraph_capture(path: Path, blend_enable: int = 0) -> None:
    pixels = struct.pack("<4I", *([0xFF000000] * 4))
    crc = zlib.crc32(pixels) & 0xFFFFFFFF
    methods = [
        (0x0200, (2 << 16) | 0),
        (0x0204, (2 << 16) | 0),
        (0x020C, 8),
        (0x0304, blend_enable),
        (0x1D90, 0xFF000000),
        (0x1D94, 0x00000003),
        (0x17FC, 0x00000005),
        (0x1810, 0x02000000),
        (0x17FC, 0),
        (0x0130, 0),
    ]
    base = 0x10000000
    command_words = len(methods) * 2
    records = [
        record(
            nv2a_capture.RAMIN,
            struct.pack("<II", 4, zlib.crc32(b"RAM!") & 0xFFFFFFFF) + b"RAM!",
        ),
        record(
            nv2a_capture.PUSH_RUN,
            struct.pack(
                "<9I",
                0,
                1,
                base,
                base,
                base + command_words * 4,
                0,
                0,
                0,
                0xFFFFFFFF,
            ),
        ),
    ]
    address = base
    for method, data in methods:
        packet = 0x40000000 | (1 << 18) | method
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, packet)))
        address += 4
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, data)))
        records.append(record(nv2a_capture.METHOD, struct.pack("<4I", 0, 0, method, data)))
        address += 4
    records.append(
        record(
            nv2a_capture.SCANOUT,
            struct.pack("<6I", 0, 0x00200000, 2, 2, crc, len(pixels)) + pixels,
        )
    )
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


def build_pixel_capture(
    path: Path, execute_clear: bool = True, include_scanout_read: bool = False
) -> None:
    pixel = 0xFF112233
    pixels = struct.pack("<4I", *([pixel] * 4))
    crc = zlib.crc32(pixels) & 0xFFFFFFFF
    methods = [
        (0x0200, 2 << 16),
        (0x0204, 2 << 16),
        (0x0208, 0x00001000),
        (0x020C, 16),
        (0x0210, 0x00001000),
        (0x1D98, 1 << 16),
        (0x1D9C, 1 << 16),
        (0x1D90, 0x00112233),
    ]
    if execute_clear:
        methods.append((0x1D94, 0xF0))
    methods.extend(
        (
            (0x0208, 0),
            (0x020C, 8),
            (0x0210, 0x00002000),
            (0x0130, 0),
        )
    )
    base = 0x10000000
    records = [
        record(
            nv2a_capture.RAMIN,
            struct.pack("<II", 4, zlib.crc32(b"RAM!") & 0xFFFFFFFF) + b"RAM!",
        ),
        record(
            nv2a_capture.PUSH_RUN,
            struct.pack(
                "<9I",
                0,
                1,
                base,
                base,
                base + len(methods) * 8,
                0,
                0,
                0,
                0xFFFFFFFF,
            ),
        ),
    ]
    address = base
    for method, data in methods:
        packet = 0x40000000 | (1 << 18) | method
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, packet)))
        address += 4
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, data)))
        records.append(record(nv2a_capture.METHOD, struct.pack("<4I", 0, 0, method, data)))
        address += 4
    if include_scanout_read:
        raw_pixels = struct.pack("<4I", *([0x00112233] * 4))
        records.append(
            record(
                nv2a_capture.MEMORY,
                struct.pack(
                    "<III", 0x00002000, len(raw_pixels), zlib.crc32(raw_pixels) & 0xFFFFFFFF
                )
                + raw_pixels,
            )
        )
    records.append(
        record(
            nv2a_capture.SCANOUT,
            struct.pack("<6I", 0, 0x00002000, 2, 2, crc, len(pixels)) + pixels,
        )
    )
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
    def test_pixel_memory_applies_each_observation_once(self) -> None:
        memory = nv2a_capture.CapturedPixelMemory()
        memory.observe(0x1000, b"\x11\x22\x33\x44")
        region = memory.ensure(0x1000, 4)
        self.assertEqual(region.data, b"\x11\x22\x33\x44")
        region.data[0] = 0x99
        memory.ensure(0x1000, 4)
        self.assertEqual(memory.observation_conflicts, 0)
        memory.observe(0x1000, b"\x11\x22\x33\x44")
        self.assertEqual(memory.observation_conflicts, 1)

    def test_replays_clear_and_aa_resolve_pixels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pixels.nv2acap"
            build_pixel_capture(path)
            result = nv2a_capture.replay_pixels(path)
            with contextlib.redirect_stdout(io.StringIO()):
                exit_code = nv2a_capture.pixel_main([str(path)])
        self.assertEqual(exit_code, 0)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["clears_executed"], 1)
        self.assertEqual(result["presents_executed"], 1)
        self.assertTrue(result["conflict_free"])
        self.assertTrue(result["scanouts"][0]["complete"])
        self.assertTrue(result["scanouts"][0]["match"])

    def test_pixel_replay_excludes_scanout_read_from_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "scanout-only.nv2acap"
            build_pixel_capture(path, execute_clear=False, include_scanout_read=True)
            result = nv2a_capture.replay_pixels(path)
            with contextlib.redirect_stdout(io.StringIO()):
                exit_code = nv2a_capture.pixel_main([str(path)])
        self.assertEqual(exit_code, 1)
        self.assertEqual(result["excluded_scanout_reads"], 1)
        self.assertEqual(result["input_memory_records"], 0)
        self.assertFalse(result["scanouts"][0]["complete"])
        self.assertFalse(result["scanouts"][0]["match"])

    def test_resolves_dma_surface_base(self) -> None:
        ramin = bytearray(0x200)
        struct.pack_into("<III", ramin, 0x100, 0x3450003D, 0xFFFF, 0x01234003)
        self.assertEqual(
            nv2a_capture.resolve_dma_base(7, bytes(ramin), {7: (0x100, 0x3D)}),
            0x01234345,
        )

    def test_pusher_allows_monotonic_frame_transition_inside_run(self) -> None:
        replay = nv2a_capture.PusherReplay()
        replay.begin_run((0, 1, 0, 0, 8, 0, 0, 0, 0xFFFFFFFF))
        replay.push_word(0, 0, (1 << 18) | 0x0100)
        replay.push_word(1, 4, 0x11223344)
        self.assertEqual(replay.methods, [(1, 0, 0x0100, 0x11223344)])
        self.assertEqual(replay.packet_errors, [])

        backwards = nv2a_capture.PusherReplay()
        backwards.begin_run((1, 1, 0, 0, 4, 0, 0, 0, 0xFFFFFFFF))
        backwards.push_word(0, 0, 0)
        self.assertEqual(
            backwards.packet_errors,
            ["push word frame moved backwards from 1 to 0"],
        )

    def test_pgraph_state_keeps_immediate_vertex_history(self) -> None:
        def replay(first_x: int) -> nv2a_capture.PgraphCheckpoint:
            state = nv2a_capture.PgraphReplayState()
            state.apply_method(0, 0, 0x17FC, 5, 0, 0, {})
            method_index = 1
            for vertex_x in (first_x, 0x40000000):
                for component, value in enumerate((vertex_x, 0x3F800000, 0x00000000, 0x3F800000)):
                    state.apply_method(
                        0,
                        0,
                        0x1518 + component * 4,
                        value,
                        method_index,
                        method_index,
                        {},
                    )
                    method_index += 1
            checkpoint = state.apply_method(0, 0, 0x17FC, 0, method_index, method_index, {})
            assert checkpoint is not None
            return checkpoint

        baseline = replay(0x3F800000)
        candidate = replay(0x40400000)
        self.assertEqual(baseline.kind, "immediate_batch")
        self.assertEqual(baseline.count, 2)
        self.assertNotEqual(baseline.state_crc32, candidate.state_crc32)
        self.assertEqual(nv2a_capture.PgraphReplayState().registers[0x0354 // 4], 0x0201)

    def test_pgraph_state_matches_live_batch_capacities(self) -> None:
        state = nv2a_capture.PgraphReplayState()
        state.inline_words = [0] * nv2a_capture.PGRAPH_INLINE_WORD_CAPACITY
        state.apply_method(0, 0, 0x1818, 0xDEADBEEF, 0, 0, {})
        self.assertEqual(len(state.inline_words), nv2a_capture.PGRAPH_INLINE_WORD_CAPACITY)
        self.assertTrue(state.inline_overflow)

        state.element_indices = [0] * nv2a_capture.PGRAPH_ELEMENT_INDEX_CAPACITY
        state.apply_method(0, 0, 0x1808, 7, 1, 1, {})
        self.assertEqual(len(state.element_indices), nv2a_capture.PGRAPH_ELEMENT_INDEX_CAPACITY)

    def test_discovers_ramin_object_class(self) -> None:
        ramin = bytearray(0x200)
        struct.pack_into("<II", ramin, 0x40, 13, 0x80000010)
        struct.pack_into("<I", ramin, 0x100, 0x00000097)
        self.assertEqual(
            nv2a_capture.discover_ramin_objects(bytes(ramin)),
            {13: (0x100, nv2a_capture.NV_CLASS_KELVIN)},
        )

    def test_replays_pgraph_checkpoints(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pgraph.nv2acap"
            build_pgraph_capture(path)
            result = nv2a_capture.replay_pgraph(path)
            with contextlib.redirect_stdout(io.StringIO()):
                exit_code = nv2a_capture.pgraph_main([str(path)])
        self.assertEqual(exit_code, 0)
        self.assertEqual(result["methods"], 10)
        self.assertEqual(result["checkpoint_count"], 3)
        self.assertEqual(
            [checkpoint["kind"] for checkpoint in result["checkpoints"]],
            ["clear", "draw_arrays", "present"],
        )
        self.assertEqual(result["checkpoints"][1]["primitive"], 5)
        self.assertEqual(result["checkpoints"][1]["count"], 3)

    def test_pgraph_comparison_reports_state_divergence(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            baseline = Path(directory) / "baseline.nv2acap"
            candidate = Path(directory) / "candidate.nv2acap"
            build_pgraph_capture(baseline)
            build_pgraph_capture(candidate, blend_enable=1)
            result = nv2a_capture.compare_pgraph_replays(baseline, candidate)
            with contextlib.redirect_stdout(io.StringIO()):
                different_exit = nv2a_capture.pgraph_main([str(baseline), str(candidate)])
                match_exit = nv2a_capture.pgraph_main([str(baseline), str(baseline)])
        self.assertFalse(result["equal"])
        self.assertEqual(result["first_divergence"]["index"], 0)
        self.assertEqual(result["first_divergence"]["candidate"]["pipeline"]["blend"], 1)
        self.assertEqual(different_exit, 1)
        self.assertEqual(match_exit, 0)

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
