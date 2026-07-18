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


def triangle_pixels(color: int) -> bytes:
    pixels = [0xFF000000] * 64
    for y in range(1, 6):
        for x in range(1, 6):
            if x + y <= 6:
                pixels[y * 8 + x] = color
    return struct.pack("<64I", *pixels)


def vertex_bytes(color: int, z: float = 0.0) -> bytes:
    return b"".join(
        struct.pack("<fffI", x, y, z, color) for x, y in ((1.0, 1.0), (6.0, 1.0), (1.0, 6.0))
    )


def build_draw_capture(
    path: Path,
    *,
    indexed: bool = False,
    depth: bool = False,
    depth_format: int = 1,
    include_vertex_memory: bool = True,
) -> None:
    depth_bytes = 4 if depth_format == 2 else 2
    methods: list[tuple[int, int, bytes | None]] = [
        (0x0200, 8 << 16, None),
        (0x0204, 8 << 16, None),
        (0x0208, (depth_format << 4) if depth else 0, None),
        (0x020C, ((8 * depth_bytes) << 16) | (8 * 4), None),
        (0x0210, 0x2000, None),
        (0x1D98, 7 << 16, None),
        (0x1D9C, 7 << 16, None),
        (0x1D8C, 0xFFFFFF00 if depth_format == 2 else 0xFFFF, None),
        (0x1D90, 0xFF000000, None),
        (0x1D94, 0xF1 if depth else 0xF0, None),
        (0x1720, 0x4000, None),
        (0x1760, (16 << 8) | (3 << 4) | 2, None),
        (0x172C, 0x400C, None),
        (0x176C, (16 << 8) | (4 << 4), None),
    ]
    if depth:
        methods[5:5] = [
            (0x0214, 0x3000, None),
            (0x030C, 1, None),
            (0x0354, 0x201, None),
        ]

    def append_draw(color: int, z: float) -> None:
        memory = vertex_bytes(color, z) if include_vertex_memory else None
        methods.append((0x17FC, 5, None))
        if indexed:
            methods.extend(((0x1808, 0, None), (0x1808, 1, None), (0x1808, 2, None)))
            methods.append((0x17FC, 0, memory))
        else:
            methods.append((0x1810, 0x02000000, memory))
            methods.append((0x17FC, 0, None))

    if depth:
        append_draw(0xFFFF0000, 100.0)
        append_draw(0xFF00FF00, 200.0)
        expected = triangle_pixels(0xFFFF0000)
    else:
        append_draw(0xFF00FF00, 0.0)
        expected = triangle_pixels(0xFF00FF00)
    methods.append((0x0130, 0, None))

    crc = zlib.crc32(expected) & 0xFFFFFFFF
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
    for method, data, memory in methods:
        packet = 0x40000000 | (1 << 18) | method
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, packet)))
        address += 4
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, data)))
        records.append(record(nv2a_capture.METHOD, struct.pack("<4I", 0, 0, method, data)))
        address += 4
        if memory is not None:
            records.append(
                record(
                    nv2a_capture.MEMORY,
                    struct.pack("<III", 0x80004000, len(memory), zlib.crc32(memory) & 0xFFFFFFFF)
                    + memory,
                )
            )
    records.append(
        record(
            nv2a_capture.SCANOUT,
            struct.pack("<6I", 0, 0x2000, 8, 8, crc, len(expected)) + expected,
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


def texture_pixels(bilinear: bool) -> bytes:
    if bilinear:
        rows = (
            ((255, 0, 0), (191, 64, 0), (64, 191, 0), (0, 255, 0)),
            ((191, 0, 64), (159, 64, 64), (96, 191, 64), (64, 255, 64)),
            ((64, 0, 191), (96, 64, 191), (159, 191, 191), (191, 255, 191)),
            ((0, 0, 255), (64, 64, 255), (191, 191, 255), (255, 255, 255)),
        )
    else:
        rows = (
            ((255, 0, 0), (255, 0, 0), (0, 255, 0), (0, 255, 0)),
            ((255, 0, 0), (255, 0, 0), (0, 255, 0), (0, 255, 0)),
            ((0, 0, 255), (0, 0, 255), (255, 255, 255), (255, 255, 255)),
            ((0, 0, 255), (0, 0, 255), (255, 255, 255), (255, 255, 255)),
        )
    colors = [
        0xFF000000 | red << 16 | green << 8 | blue for row in rows for red, green, blue in row
    ]
    return struct.pack("<16I", *colors)


def build_texture_capture(path: Path, *, paletted: bool, bilinear: bool) -> None:
    vertices = b"".join(
        struct.pack("<fffff", x, y, 0.0, u, v)
        for x, y, u, v in (
            (0.0, 0.0, 0.0, 0.0),
            (4.0, 0.0, 1.0, 0.0),
            (4.0, 4.0, 1.0, 1.0),
            (0.0, 4.0, 0.0, 1.0),
        )
    )
    texels = struct.pack("<4I", 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF)
    palette = bytearray(256 * 4)
    for index, color in enumerate((0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF)):
        struct.pack_into("<I", palette, index * 4, color)
    source = bytes(range(4)) if paletted else texels
    texture_format = (
        (1 << 24) | (1 << 20) | (0x0B << 8) | (2 << 4) if paletted else (0x12 << 8) | (2 << 4)
    )
    methods: list[tuple[int, int, list[tuple[int, bytes]] | None]] = [
        (0x0200, 4 << 16, None),
        (0x0204, 4 << 16, None),
        (0x020C, 16, None),
        (0x0210, 0x2000, None),
        (0x1D98, 3 << 16, None),
        (0x1D9C, 3 << 16, None),
        (0x1D90, 0xFF000000, None),
        (0x1D94, 0xF0, None),
        (0x1B00, 0x5000, None),
        (0x1B04, texture_format, None),
        (0x1B08, 0x00030303, None),
        (0x1B0C, 0x4003FFC0, None),
        (0x1B10, 8 << 16, None),
        (0x1B14, (2 if bilinear else 1) << 24, None),
        (0x1B1C, (2 << 16) | 2, None),
        (0x1B20, 0x6000, None),
        (0x1E70, 1, None),
        (0x1720, 0x4000, None),
        (0x1760, (20 << 8) | (3 << 4) | 2, None),
        (0x1744, 0x400C, None),
        (0x1784, (20 << 8) | (2 << 4) | 2, None),
        (0x17FC, 8, None),
        (
            0x1810,
            0x03000000,
            [(0x80004000, vertices), (0x80005000, source)]
            + ([(0x80006000, bytes(palette))] if paletted else []),
        ),
        (0x17FC, 0, None),
        (0x0130, 0, None),
    ]
    expected = texture_pixels(bilinear)
    crc = zlib.crc32(expected) & 0xFFFFFFFF
    base = 0x10000000
    records = [
        record(
            nv2a_capture.RAMIN,
            struct.pack("<II", 4, zlib.crc32(b"RAM!") & 0xFFFFFFFF) + b"RAM!",
        ),
        record(
            nv2a_capture.PUSH_RUN,
            struct.pack("<9I", 0, 1, base, base, base + len(methods) * 8, 0, 0, 0, 0xFFFFFFFF),
        ),
    ]
    address = base
    for method, data, memory_records in methods:
        packet = 0x40000000 | (1 << 18) | method
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, packet)))
        address += 4
        records.append(record(nv2a_capture.PUSH_WORD, struct.pack("<3I", 0, address, data)))
        records.append(record(nv2a_capture.METHOD, struct.pack("<4I", 0, 0, method, data)))
        address += 4
        for memory_address, memory in memory_records or ():
            records.append(
                record(
                    nv2a_capture.MEMORY,
                    struct.pack(
                        "<III",
                        memory_address,
                        len(memory),
                        zlib.crc32(memory) & 0xFFFFFFFF,
                    )
                    + memory,
                )
            )
    records.append(
        record(
            nv2a_capture.SCANOUT,
            struct.pack("<6I", 0, 0x2000, 4, 4, crc, len(expected)) + expected,
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

    def test_reads_newest_captured_bytes_through_physical_mirror(self) -> None:
        memory = nv2a_capture.CapturedPixelMemory()
        memory.observe(0x80001000, b"\x11\x22\x33\x44")
        memory.observe(0x80001001, b"\x99")
        self.assertEqual(memory.read_observed(0x1000, 4), b"\x11\x99\x33\x44")
        memory.observe(0x82F93D50, b"\x55\x66\x77\x88")
        self.assertEqual(memory.read_observed(0x0EF93D50, 4), b"\x55\x66\x77\x88")
        self.assertIsNone(memory.read_observed(0x2000, 4))

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

    def test_replays_fixed_function_draw_arrays_pixels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "draw-arrays.nv2acap"
            build_draw_capture(path)
            result = nv2a_capture.replay_pixels(path)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["draws_executed"], 1)
        self.assertEqual(result["triangles_executed"], 1)
        self.assertEqual(result["unsupported_checkpoints"], {})
        self.assertTrue(result["scanouts"][0]["match"])

    def test_replays_retained_indexed_batch_pixels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "indexed.nv2acap"
            build_draw_capture(path, indexed=True)
            pixels = nv2a_capture.replay_pixels(path)
            pgraph = nv2a_capture.replay_pgraph(path)
        indexed = next(item for item in pgraph["checkpoints"] if item["kind"] == "indexed_batch")
        self.assertEqual(indexed["index_count"], 3)
        self.assertEqual(pixels["status"], "pass")
        self.assertEqual(pixels["draws_executed"], 1)
        self.assertTrue(pixels["scanouts"][0]["match"])

    def test_replays_linear_point_and_paletted_bilinear_textures(self) -> None:
        cases = ((False, False), (True, True))
        for paletted, bilinear in cases:
            with (
                self.subTest(paletted=paletted, bilinear=bilinear),
                tempfile.TemporaryDirectory() as directory,
            ):
                path = Path(directory) / "texture.nv2acap"
                build_texture_capture(path, paletted=paletted, bilinear=bilinear)
                result = nv2a_capture.replay_pixels(path)
            self.assertEqual(result["status"], "pass")
            self.assertEqual(result["draws_executed"], 1)
            self.assertEqual(result["textured_draws_executed"], 1)
            self.assertEqual(result["triangles_executed"], 2)
            self.assertEqual(result["unsupported_checkpoints"], {})
            self.assertTrue(result["scanouts"][0]["match"])

    def test_texture_addressing_and_bilinear_sampling(self) -> None:
        source = struct.pack("<4I", 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF)
        backend = nv2a_capture.OfflinePixelReplay(b"", {}, {})

        def sampler(address: int, bilinear: bool) -> nv2a_capture.OfflineSampler:
            return nv2a_capture.OfflineSampler(
                source=source,
                palette=tuple([0] * 256),
                width=2,
                height=2,
                pitch=8,
                bytes_per_pixel=4,
                kind=0,
                swizzled=False,
                address=address,
                bilinear=bilinear,
            )

        self.assertEqual(backend.sample_texture(sampler(0x00000303, False), 1.25, 0.25), 0xFF00FF00)
        self.assertEqual(backend.sample_texture(sampler(0x00000101, False), 1.25, 0.25), 0xFFFF0000)
        self.assertEqual(backend.sample_texture(sampler(0x00000303, True), 0.5, 0.5), 0xFF808080)

        palette = tuple(0xFF000000 | index for index in range(256))
        swizzled = nv2a_capture.OfflineSampler(
            source=bytes(range(8)),
            palette=palette,
            width=4,
            height=2,
            pitch=4,
            bytes_per_pixel=1,
            kind=5,
            swizzled=True,
            address=0x00000303,
            bilinear=False,
        )
        self.assertEqual(backend.fetch_texel(swizzled, 2, 0), 0xFF000004)
        self.assertEqual(backend.fetch_texel(swizzled, 0, 1), 0xFF000002)

    def test_combines_texture_with_factor_rgb_and_diffuse_alpha(self) -> None:
        state = nv2a_capture.PgraphReplayState()
        state.registers[0x0AC0 // 4] = 0x08010000
        state.registers[0x0A60 // 4] = 0xFF4080C0
        color = nv2a_capture.OfflinePixelReplay.combine_texture(state, 0x80402010, 0xFF804020)
        self.assertEqual(color, 0x80202018)

    def test_replays_z16_and_z24_depth_ordering(self) -> None:
        for depth_format in (1, 2):
            with (
                self.subTest(depth_format=depth_format),
                tempfile.TemporaryDirectory() as directory,
            ):
                path = Path(directory) / "depth.nv2acap"
                build_draw_capture(path, depth=True, depth_format=depth_format)
                result = nv2a_capture.replay_pixels(path)
            self.assertEqual(result["status"], "pass")
            self.assertEqual(result["draws_executed"], 2)
            self.assertEqual(result["triangles_executed"], 2)
            self.assertTrue(result["scanouts"][0]["match"])

    def test_applies_fixed_function_matrix_and_viewport_transform(self) -> None:
        state = nv2a_capture.PgraphReplayState()
        state.registers[nv2a_capture.NV097_SET_VERTEX_DATA_ARRAY_OFFSET // 4] = 0x4000
        state.registers[nv2a_capture.NV097_SET_VERTEX_DATA_ARRAY_FORMAT // 4] = (
            (16 << 8) | (4 << 4) | 2
        )
        state.composite_matrix[3] = struct.unpack("<I", struct.pack("<f", 10.0))[0]
        state.composite_matrix[7] = struct.unpack("<I", struct.pack("<f", 20.0))[0]
        state.viewport_offset[0] = struct.unpack("<I", struct.pack("<f", 1.0))[0]
        state.viewport_offset[1] = struct.unpack("<I", struct.pack("<f", 2.0))[0]
        backend = nv2a_capture.OfflinePixelReplay(b"", {}, {})
        backend.memory.observe(0x80004000, struct.pack("<4f", 1.0, 2.0, 3.0, 1.0))
        vertex = backend.read_vertex(state, 0)
        assert vertex is not None
        self.assertEqual((vertex.x, vertex.y, vertex.z, vertex.w), (12.0, 24.0, 3.0, 1.0))

        state.composite_matrix[15] = struct.unpack("<I", struct.pack("<f", -1.0))[0]
        clipped_vertex = backend.read_vertex(state, 0)
        assert clipped_vertex is not None
        self.assertEqual(clipped_vertex.w, -1.0)

    def test_accepts_diffuse_and_final_passthrough_combiners(self) -> None:
        state = nv2a_capture.PgraphReplayState()
        state.apply_method(0, 0, 0x17FC, 5, 0, 0, {})
        checkpoint = state.apply_method(0, 0, 0x1810, 0x02000000, 1, 1, {})
        assert checkpoint is not None
        state.registers[0x1E60 // 4] = 1
        state.registers[0x0AC0 // 4] = 0x00002004
        state.registers[0x0260 // 4] = 0x00002014
        state.registers[0x1E40 // 4] = 0x00000C00
        state.registers[0x0AA0 // 4] = 0x00000C00
        state.registers[0x0288 // 4] = 0x0000000C
        state.registers[0x028C // 4] = 0x00001C00
        backend = nv2a_capture.OfflinePixelReplay(b"", {}, {})
        self.assertIsNone(backend.draw_unsupported_reason(state, checkpoint))
        state.registers[0x0AC0 // 4] = 0
        self.assertEqual(backend.draw_unsupported_reason(state, checkpoint), "draw_arrays_combiner")

    def test_missing_vertex_memory_stays_partial(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "missing-vertices.nv2acap"
            build_draw_capture(path, include_vertex_memory=False)
            result = nv2a_capture.replay_pixels(path)
        self.assertEqual(result["status"], "partial")
        self.assertEqual(result["draws_executed"], 0)
        self.assertEqual(result["unsupported_checkpoints"], {"draw_arrays_vertex_input": 1})
        self.assertFalse(result["match"])

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
