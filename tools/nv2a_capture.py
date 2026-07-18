#!/usr/bin/env python3
"""Validate and replay a CXBX NV2A pushbuffer capture bundle."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = b"CXNVCAP\0"
VERSION = 1
ENDIAN_MARKER = 0x01020304
HEADER = struct.Struct("<8sIIIIQ")
RECORD_HEADER = struct.Struct("<II")

PUSH_RUN = 1
PUSH_WORD = 2
METHOD = 3
MEMORY = 4
SCANOUT = 5
FINISH = 6
RAMIN = 7

NV_CLASS_KELVIN = 0x97
NV097_SET_FLIP_STALL = 0x0130
NV097_SET_SURFACE_CLIP_HORIZONTAL = 0x0200
NV097_SET_SURFACE_CLIP_VERTICAL = 0x0204
NV097_SET_SURFACE_FORMAT = 0x0208
NV097_SET_SURFACE_PITCH = 0x020C
NV097_SET_SURFACE_COLOR_OFFSET = 0x0210
NV097_SET_SURFACE_ZETA_OFFSET = 0x0214
NV097_SET_BEGIN_END = 0x17FC
NV097_ARRAY_ELEMENT16 = 0x1800
NV097_ARRAY_ELEMENT32 = 0x1808
NV097_DRAW_ARRAYS = 0x1810
NV097_INLINE_ARRAY = 0x1818
NV097_SET_VERTEX4F = 0x1518
NV097_SET_VERTEX_DATA2F_M = 0x1880
NV097_SET_VERTEX_DATA4UB = 0x1940
NV097_SET_VERTEX_DATA4F_M = 0x1A00
NV097_SET_TRANSFORM_PROGRAM = 0x0B00
NV097_SET_TRANSFORM_CONSTANT = 0x0B80
NV097_SET_TRANSFORM_EXECUTION_MODE = 0x1E94
NV097_SET_TRANSFORM_PROGRAM_LOAD = 0x1E9C
NV097_SET_TRANSFORM_PROGRAM_START = 0x1EA0
NV097_SET_TRANSFORM_CONSTANT_LOAD = 0x1EA4
NV097_SET_SHADER_STAGE_PROGRAM = 0x1E70
NV097_CLEAR_SURFACE = 0x1D94

PGRAPH_REGISTER_COUNT = 0x2000 // 4
PGRAPH_VERTEX_ATTRIBUTE_COUNT = 16
PGRAPH_VERTEX_PROGRAM_WORDS = 136 * 4
PGRAPH_TRANSFORM_CONSTANT_WORDS = 192 * 4
PGRAPH_ELEMENT_INDEX_CAPACITY = 4096
PGRAPH_INLINE_WORD_CAPACITY = 65536


def default_pgraph_registers() -> list[int]:
    registers = [0] * PGRAPH_REGISTER_COUNT
    registers[0x033C // 4] = 0x0207
    registers[0x0344 // 4] = 0x0001
    registers[0x0350 // 4] = 0x8006
    registers[0x0354 // 4] = 0x0201
    registers[0x035C // 4] = 1
    registers[0x0360 // 4] = 0xFF
    registers[0x0364 // 4] = 0x0207
    registers[0x036C // 4] = 0xFF
    registers[0x0370 // 4] = 0x1E00
    registers[0x0374 // 4] = 0x1E00
    registers[0x0378 // 4] = 0x1E00
    return registers


def identity_matrix_words() -> list[int]:
    return [0x3F800000 if index % 5 == 0 else 0 for index in range(16)]


class CaptureError(RuntimeError):
    """Raised when a capture is corrupt or cannot be replayed exactly."""


@dataclass(frozen=True)
class ComparisonEvent:
    category: str
    record_index: int
    signature: tuple[object, ...]
    details: dict[str, object]


@dataclass(frozen=True)
class PgraphCheckpoint:
    sequence: int
    record_index: int
    method_index: int
    frame: int
    kind: str
    method: int
    data: int
    primitive: int
    count: int
    state_crc32: int
    surface: dict[str, int]
    pipeline: dict[str, int]


@dataclass
class PgraphReplayState:
    registers: list[int] = field(default_factory=default_pgraph_registers)
    subchannel_classes: list[int] = field(default_factory=lambda: [0] * 8)
    vertex_program: list[int] = field(default_factory=lambda: [0] * PGRAPH_VERTEX_PROGRAM_WORDS)
    transform_constants: list[int] = field(
        default_factory=lambda: [0] * PGRAPH_TRANSFORM_CONSTANT_WORDS
    )
    immediate_attributes: list[int] = field(
        default_factory=lambda: [0] * (PGRAPH_VERTEX_ATTRIBUTE_COUNT * 4)
    )
    immediate_formats: list[int] = field(
        default_factory=lambda: [0] * PGRAPH_VERTEX_ATTRIBUTE_COUNT
    )
    composite_matrix: list[int] = field(default_factory=identity_matrix_words)
    viewport_offset: list[int] = field(default_factory=lambda: [0] * 4)
    viewport_scale: list[int] = field(default_factory=lambda: [0x3F800000] * 4)
    element_indices: list[int] = field(default_factory=list)
    inline_words: list[int] = field(default_factory=list)
    immediate_words: list[int] = field(default_factory=list)
    begin_op: int = 0
    immediate_vertices: int = 0
    inline_overflow: bool = False
    immediate_overflow: bool = False
    vp_write_word: int = 0
    vp_instruction_count: int = 0
    vp_start: int = 0
    vp_execution_mode: int = 0
    constant_write_word: int = 0
    checkpoint_count: int = 0

    def reset_batch(self) -> None:
        self.element_indices.clear()
        self.inline_words.clear()
        self.immediate_words.clear()
        self.immediate_vertices = 0
        self.inline_overflow = False
        self.immediate_overflow = False

    def state_crc32(self) -> int:
        crc = 0
        scalar = (
            1,
            self.begin_op,
            self.immediate_vertices,
            self.inline_overflow,
            self.immediate_overflow,
            self.vp_write_word,
            self.vp_instruction_count,
            self.vp_start,
            self.vp_execution_mode,
            self.constant_write_word,
        )
        for values in (
            scalar,
            self.subchannel_classes,
            self.registers,
            self.vertex_program,
            self.transform_constants,
            self.immediate_attributes,
            self.immediate_formats,
            self.composite_matrix,
            self.viewport_offset,
            self.viewport_scale,
            self.element_indices,
            self.inline_words,
            self.immediate_words,
        ):
            crc = zlib.crc32(struct.pack(f"<{len(values)}I", *values), crc)
        return crc & 0xFFFFFFFF

    def checkpoint(
        self,
        record_index: int,
        method_index: int,
        frame: int,
        kind: str,
        method: int,
        data: int,
        count: int,
    ) -> PgraphCheckpoint:
        surface = {
            "clip_horizontal": self.registers[NV097_SET_SURFACE_CLIP_HORIZONTAL // 4],
            "clip_vertical": self.registers[NV097_SET_SURFACE_CLIP_VERTICAL // 4],
            "format": self.registers[NV097_SET_SURFACE_FORMAT // 4],
            "pitch": self.registers[NV097_SET_SURFACE_PITCH // 4],
            "color_offset": self.registers[NV097_SET_SURFACE_COLOR_OFFSET // 4],
            "zeta_offset": self.registers[NV097_SET_SURFACE_ZETA_OFFSET // 4],
        }
        pipeline = {
            "alpha_test": self.registers[0x0300 // 4],
            "blend": self.registers[0x0304 // 4],
            "depth_test": self.registers[0x030C // 4],
            "stencil_test": self.registers[0x032C // 4],
            "texture0_format": self.registers[0x1B04 // 4],
            "shader_stage_program": self.registers[NV097_SET_SHADER_STAGE_PROGRAM // 4],
            "vp_execution_mode": self.vp_execution_mode,
            "vp_start": self.vp_start,
            "vp_instruction_count": self.vp_instruction_count,
        }
        result = PgraphCheckpoint(
            sequence=self.checkpoint_count,
            record_index=record_index,
            method_index=method_index,
            frame=frame,
            kind=kind,
            method=method,
            data=data,
            primitive=self.begin_op,
            count=count,
            state_crc32=self.state_crc32(),
            surface=surface,
            pipeline=pipeline,
        )
        self.checkpoint_count += 1
        return result

    def apply_method(
        self,
        frame: int,
        subchannel: int,
        method: int,
        data: int,
        record_index: int,
        method_index: int,
        objects: dict[int, tuple[int, int]],
    ) -> PgraphCheckpoint | None:
        channel = subchannel & 7
        if method == 0:
            self.subchannel_classes[channel] = objects.get(data, (0, NV_CLASS_KELVIN))[1]
            return None

        class_id = self.subchannel_classes[channel] or NV_CLASS_KELVIN
        if class_id != NV_CLASS_KELVIN:
            return None
        if 0x180 <= method < 0x200 and data in objects:
            data = objects[data][0]

        method &= 0x1FFC
        self.registers[method // 4] = data

        if method == NV097_SET_TRANSFORM_EXECUTION_MODE:
            self.vp_execution_mode = data & 3
        elif method == NV097_SET_TRANSFORM_PROGRAM_LOAD:
            self.vp_write_word = data * 4
        elif method == NV097_SET_TRANSFORM_PROGRAM_START:
            self.vp_start = data
        elif method == NV097_SET_TRANSFORM_CONSTANT_LOAD:
            self.constant_write_word = data * 4
        elif NV097_SET_TRANSFORM_PROGRAM <= method < NV097_SET_TRANSFORM_PROGRAM + 0x80:
            if self.vp_write_word < len(self.vertex_program):
                self.vertex_program[self.vp_write_word] = data
                self.vp_write_word += 1
                self.vp_instruction_count = max(
                    self.vp_instruction_count, (self.vp_write_word + 3) // 4
                )
        elif NV097_SET_TRANSFORM_CONSTANT <= method < NV097_SET_TRANSFORM_CONSTANT + 0x80:
            if self.constant_write_word < len(self.transform_constants):
                self.transform_constants[self.constant_write_word] = data
                self.constant_write_word += 1
        elif 0x0680 <= method < 0x0680 + 16 * 4:
            self.composite_matrix[(method - 0x0680) // 4] = data
        elif 0x0A20 <= method < 0x0A20 + 4 * 4:
            self.viewport_offset[(method - 0x0A20) // 4] = data
        elif 0x0AF0 <= method < 0x0AF0 + 4 * 4:
            self.viewport_scale[(method - 0x0AF0) // 4] = data
        elif method == NV097_ARRAY_ELEMENT16:
            for index in (data & 0xFFFF, data >> 16):
                if len(self.element_indices) < PGRAPH_ELEMENT_INDEX_CAPACITY:
                    self.element_indices.append(index)
        elif method == NV097_ARRAY_ELEMENT32:
            if len(self.element_indices) < PGRAPH_ELEMENT_INDEX_CAPACITY:
                self.element_indices.append(data)
        elif method == NV097_INLINE_ARRAY:
            if len(self.inline_words) < PGRAPH_INLINE_WORD_CAPACITY:
                self.inline_words.append(data)
            else:
                self.inline_overflow = True
        elif NV097_SET_VERTEX_DATA2F_M <= method < NV097_SET_VERTEX_DATA2F_M + 16 * 8:
            relative = method - NV097_SET_VERTEX_DATA2F_M
            attribute = relative // 8
            component = (relative & 7) // 4
            self.immediate_attributes[attribute * 4 + component] = data
            self.immediate_formats[attribute] = 0x22
        elif NV097_SET_VERTEX_DATA4UB <= method < NV097_SET_VERTEX_DATA4UB + 16 * 4:
            attribute = (method - NV097_SET_VERTEX_DATA4UB) // 4
            self.immediate_attributes[attribute * 4] = data
            self.immediate_formats[attribute] = 0x40
        elif NV097_SET_VERTEX_DATA4F_M <= method < NV097_SET_VERTEX_DATA4F_M + 16 * 16:
            relative = method - NV097_SET_VERTEX_DATA4F_M
            attribute = relative // 16
            component = (relative & 15) // 4
            self.immediate_attributes[attribute * 4 + component] = data
            self.immediate_formats[attribute] = 0x42
        elif NV097_SET_VERTEX4F <= method < NV097_SET_VERTEX4F + 16:
            component = (method - NV097_SET_VERTEX4F) // 4
            self.immediate_attributes[component] = data
            self.immediate_formats[0] = 0x42
            if component == 3:
                if (
                    len(self.immediate_words) + PGRAPH_VERTEX_ATTRIBUTE_COUNT * 4
                    <= PGRAPH_INLINE_WORD_CAPACITY
                ):
                    self.immediate_words.extend(self.immediate_attributes)
                    self.immediate_vertices += 1
                else:
                    self.immediate_overflow = True

        if method == NV097_CLEAR_SURFACE:
            return self.checkpoint(record_index, method_index, frame, "clear", method, data, 0)
        if method == NV097_DRAW_ARRAYS:
            count = ((data >> 24) & 0xFF) + 1
            return self.checkpoint(
                record_index, method_index, frame, "draw_arrays", method, data, count
            )
        if method == NV097_SET_FLIP_STALL:
            return self.checkpoint(record_index, method_index, frame, "present", method, data, 0)
        if method == NV097_SET_BEGIN_END:
            if data != 0:
                self.reset_batch()
                self.begin_op = data
                return None
            if self.immediate_vertices:
                kind = "immediate_batch"
                count = self.immediate_vertices
            elif self.inline_words:
                kind = "inline_batch"
                count = len(self.inline_words)
            elif self.element_indices:
                kind = "indexed_batch"
                count = len(self.element_indices)
            else:
                self.begin_op = 0
                return None
            result = self.checkpoint(record_index, method_index, frame, kind, method, data, count)
            self.reset_batch()
            self.begin_op = 0
            return result
        return None


def unpack_fields(payload: bytes, count: int, name: str) -> tuple[int, ...]:
    size = count * 4
    if len(payload) < size:
        raise CaptureError(f"short {name} record: {len(payload)} < {size}")
    return struct.unpack_from(f"<{count}I", payload)


class PusherReplay:
    def __init__(self) -> None:
        self.state = 0
        self.get = 0
        self.put = 0
        self.subroutine = 0
        self.active = False
        self.methods: list[tuple[int, int, int, int]] = []
        self.address_errors: list[str] = []
        self.packet_errors: list[str] = []

    def begin_run(self, fields: tuple[int, ...]) -> None:
        frame, _host_mode, _base, get, put, state, _dcount, subroutine, _limit = fields
        self.frame = frame
        self.get = get
        self.put = put
        self.state = state
        self.subroutine = subroutine
        self.active = True

    def push_word(self, frame: int, address: int, word: int) -> None:
        if not self.active:
            self.packet_errors.append("push word appeared before a push-run record")
            return
        if frame < self.frame:
            self.packet_errors.append(
                f"push word frame moved backwards from {self.frame} to {frame}"
            )
        else:
            self.frame = frame
        if address != self.get:
            self.address_errors.append(
                f"frame {frame}: fetched 0x{address:08X}, expected 0x{self.get:08X}"
            )

        self.get = (address + 4) & 0xFFFFFFFF
        count = (self.state & 0x1FFC0000) >> 18
        if count:
            method = self.state & 0x1FFC
            subchannel = (self.state & 0xE000) >> 13
            self.methods.append((frame, subchannel, method, word))
            if (self.state & 1) == 0:
                method += 4
            count -= 1
            self.state &= ~(0x1FFC | 0x1FFC0000)
            self.state |= method & 0x1FFC
            self.state |= (count << 18) & 0x1FFC0000
            return

        if (word & 0xE0000003) == 0x20000000:
            self.get = word & 0x1FFFFFFC
        elif (word & 3) == 1:
            self.get = word & 0xFFFFFFFC
        elif (word & 3) == 2:
            if self.subroutine & 1:
                self.packet_errors.append("nested pusher call")
            else:
                self.subroutine = (self.get & 0xFFFFFFFC) | 1
                self.get = word & 0xFFFFFFFC
        elif word == 0x00020000:
            if (self.subroutine & 1) == 0:
                self.packet_errors.append("pusher return without a call")
            else:
                self.get = self.subroutine & 0xFFFFFFFC
                self.subroutine = 0
        elif (word & 0xE0030003) in (0, 0x40000000):
            incrementing = (word & 0xE0030003) == 0
            self.state &= 0xE0000000
            self.state |= word & (0x1FFC | 0xE000 | 0x1FFC0000)
            if not incrementing:
                self.state |= 1
        else:
            self.packet_errors.append(f"reserved packet word 0x{word:08X}")


def analyze_capture(path: Path, allow_truncated: bool = False) -> dict[str, object]:
    actual_size = path.stat().st_size
    record_counts: dict[int, int] = {}
    recorded_methods: list[tuple[int, int, int, int]] = []
    replay = PusherReplay()
    scanouts: list[dict[str, int]] = []
    memory_bytes = 0
    memory_records = 0
    finish: tuple[int, ...] | None = None

    with path.open("rb") as stream:
        header_bytes = stream.read(HEADER.size)
        if len(header_bytes) != HEADER.size:
            raise CaptureError("capture header is truncated")
        magic, version, endian, header_size, target_frame, byte_limit = HEADER.unpack(header_bytes)
        if magic != MAGIC:
            raise CaptureError(f"bad capture magic: {magic!r}")
        if version != VERSION:
            raise CaptureError(f"unsupported capture version {version}")
        if endian != ENDIAN_MARKER or header_size != HEADER.size:
            raise CaptureError("capture byte order or header size is invalid")
        if actual_size > byte_limit:
            raise CaptureError(f"capture size {actual_size} exceeds declared limit {byte_limit}")

        records_before_finish = 0
        while True:
            raw_header = stream.read(RECORD_HEADER.size)
            if not raw_header:
                break
            if len(raw_header) != RECORD_HEADER.size:
                raise CaptureError("record header is truncated")
            record_type, payload_size = RECORD_HEADER.unpack(raw_header)
            payload = stream.read(payload_size)
            if len(payload) != payload_size:
                raise CaptureError(
                    f"record {record_type} is truncated: {len(payload)} < {payload_size}"
                )
            if finish is not None:
                raise CaptureError("capture contains records after its finish footer")
            record_counts[record_type] = record_counts.get(record_type, 0) + 1

            if record_type == PUSH_RUN:
                fields = unpack_fields(payload, 9, "push-run")
                if len(payload) != 36:
                    raise CaptureError("push-run record has trailing data")
                replay.begin_run(fields)
            elif record_type == PUSH_WORD:
                fields = unpack_fields(payload, 3, "push-word")
                if len(payload) != 12:
                    raise CaptureError("push-word record has trailing data")
                replay.push_word(*fields)
            elif record_type == METHOD:
                fields = unpack_fields(payload, 4, "method")
                if len(payload) != 16:
                    raise CaptureError("method record has trailing data")
                recorded_methods.append(fields)
            elif record_type == MEMORY:
                address, size, expected_crc = unpack_fields(payload, 3, "memory")
                data = payload[12:]
                if len(data) != size:
                    raise CaptureError(
                        f"memory 0x{address:08X} size is {len(data)}, expected {size}"
                    )
                if zlib.crc32(data) & 0xFFFFFFFF != expected_crc:
                    raise CaptureError(f"memory 0x{address:08X} failed CRC validation")
                memory_bytes += size
                memory_records += 1
            elif record_type == SCANOUT:
                frame, address, width, height, expected_crc, size = unpack_fields(
                    payload, 6, "scanout"
                )
                pixels = payload[24:]
                if len(pixels) != size or size != width * height * 4:
                    raise CaptureError(f"frame {frame} scanout dimensions do not match payload")
                actual_crc = zlib.crc32(pixels) & 0xFFFFFFFF
                if actual_crc != expected_crc:
                    raise CaptureError(f"frame {frame} scanout failed CRC validation")
                scanouts.append(
                    {
                        "frame": frame,
                        "address": address,
                        "width": width,
                        "height": height,
                        "crc32": expected_crc,
                    }
                )
            elif record_type == RAMIN:
                size, expected_crc = unpack_fields(payload, 2, "RAMIN")
                data = payload[8:]
                if len(data) != size or zlib.crc32(data) & 0xFFFFFFFF != expected_crc:
                    raise CaptureError("RAMIN snapshot failed size or CRC validation")
            elif record_type == FINISH:
                finish = unpack_fields(payload, 5, "finish")
                if len(payload) != 20:
                    raise CaptureError("finish record has trailing data")
                if finish[2] != records_before_finish:
                    raise CaptureError(
                        f"finish reports {finish[2]} records, read {records_before_finish}"
                    )
            records_before_finish += 1

    if finish is None:
        raise CaptureError("capture has no finish record")
    footer_target, completed_frame, _count, truncated, output_crc = finish
    if footer_target != target_frame or completed_frame != target_frame:
        raise CaptureError(
            f"capture completed frame {completed_frame}, expected target {target_frame}"
        )
    if truncated and not allow_truncated:
        raise CaptureError("capture reached its byte limit; rerun with a larger limit")

    method_mismatches = []
    for index, (actual, expected) in enumerate(zip(replay.methods, recorded_methods, strict=False)):
        if actual != expected:
            method_mismatches.append(f"method {index}: replay {actual!r}, capture {expected!r}")
            if len(method_mismatches) == 8:
                break
    if len(replay.methods) != len(recorded_methods):
        method_mismatches.append(
            f"replayed {len(replay.methods)} methods, captured {len(recorded_methods)}"
        )

    replay_errors = replay.address_errors + replay.packet_errors + method_mismatches
    if replay_errors:
        raise CaptureError("PFIFO replay diverged:\n  " + "\n  ".join(replay_errors[:16]))
    if scanouts and scanouts[-1]["crc32"] != output_crc:
        raise CaptureError(
            f"footer CRC 0x{output_crc:08X} does not match final scanout "
            f"0x{scanouts[-1]['crc32']:08X}"
        )

    return {
        "schema_version": 1,
        "capture": str(path.resolve()),
        "capture_bytes": actual_size,
        "byte_limit": byte_limit,
        "target_frame": target_frame,
        "truncated": bool(truncated),
        "push_runs": record_counts.get(PUSH_RUN, 0),
        "push_words": record_counts.get(PUSH_WORD, 0),
        "methods": len(recorded_methods),
        "memory_records": memory_records,
        "memory_bytes": memory_bytes,
        "ramin_snapshots": record_counts.get(RAMIN, 0),
        "scanouts": scanouts,
        "output_crc32": f"0x{output_crc:08X}",
        "replay": "pass",
    }


def discover_ramin_objects(ramin: bytes) -> dict[int, tuple[int, int]]:
    objects: dict[int, tuple[int, int]] = {}
    for offset in range(0, len(ramin) - 7, 8):
        handle, context = struct.unpack_from("<II", ramin, offset)
        if handle == 0 or (context & 0x80000000) == 0:
            continue
        instance = (context & 0xFFFF) << 4
        if instance + 4 > len(ramin):
            continue
        class_id = struct.unpack_from("<I", ramin, instance)[0] & 0xFF
        if class_id != 0:
            objects.setdefault(handle, (instance, class_id))
    return objects


def checkpoint_json(checkpoint: PgraphCheckpoint | None) -> dict[str, object] | None:
    if checkpoint is None:
        return None
    return {
        "sequence": checkpoint.sequence,
        "record_index": checkpoint.record_index,
        "method_index": checkpoint.method_index,
        "frame": checkpoint.frame,
        "kind": checkpoint.kind,
        "method": checkpoint.method,
        "data": checkpoint.data,
        "primitive": checkpoint.primitive,
        "count": checkpoint.count,
        "state_crc32": checkpoint.state_crc32,
        "surface": checkpoint.surface,
        "pipeline": checkpoint.pipeline,
    }


def replay_pgraph(path: Path) -> dict[str, object]:
    analysis = analyze_capture(path)
    ramin = b""
    methods: list[tuple[int, int, int, int, int]] = []

    with path.open("rb") as stream:
        stream.seek(HEADER.size)
        record_index = 0
        while raw_header := stream.read(RECORD_HEADER.size):
            record_type, payload_size = RECORD_HEADER.unpack(raw_header)
            payload = stream.read(payload_size)
            if record_type == RAMIN:
                size, _crc = unpack_fields(payload, 2, "RAMIN")
                ramin = payload[8 : 8 + size]
            elif record_type == METHOD:
                frame, subchannel, method, data = unpack_fields(payload, 4, "method")
                methods.append((record_index, frame, subchannel, method, data))
            record_index += 1

    objects = discover_ramin_objects(ramin)
    state = PgraphReplayState()
    checkpoints: list[PgraphCheckpoint] = []
    kelvin_methods = 0
    for method_index, (record_index, frame, subchannel, method, data) in enumerate(methods):
        class_before = state.subchannel_classes[subchannel & 7] or NV_CLASS_KELVIN
        checkpoint = state.apply_method(
            frame,
            subchannel,
            method,
            data,
            record_index,
            method_index,
            objects,
        )
        if method == 0:
            class_before = state.subchannel_classes[subchannel & 7]
        if class_before == NV_CLASS_KELVIN:
            kelvin_methods += 1
        if checkpoint is not None:
            checkpoints.append(checkpoint)

    counts: dict[str, int] = {}
    for checkpoint in checkpoints:
        counts[checkpoint.kind] = counts.get(checkpoint.kind, 0) + 1
    return {
        "schema_version": 1,
        "capture": str(path.resolve()),
        "target_frame": analysis["target_frame"],
        "methods": len(methods),
        "kelvin_methods": kelvin_methods,
        "ramin_objects": len(objects),
        "memory_records": analysis["memory_records"],
        "memory_bytes": analysis["memory_bytes"],
        "checkpoint_count": len(checkpoints),
        "checkpoint_counts": counts,
        "final_state_crc32": state.state_crc32(),
        "checkpoints": [checkpoint_json(checkpoint) for checkpoint in checkpoints],
    }


def pgraph_checkpoint_signature(checkpoint: dict[str, object]) -> tuple[object, ...]:
    return (
        checkpoint["frame"],
        checkpoint["kind"],
        checkpoint["method"],
        checkpoint["data"],
        checkpoint["primitive"],
        checkpoint["count"],
        checkpoint["state_crc32"],
    )


def compare_pgraph_replays(baseline_path: Path, candidate_path: Path) -> dict[str, object]:
    baseline = replay_pgraph(baseline_path)
    candidate = replay_pgraph(candidate_path)
    baseline_checkpoints = baseline["checkpoints"]
    candidate_checkpoints = candidate["checkpoints"]
    assert isinstance(baseline_checkpoints, list)
    assert isinstance(candidate_checkpoints, list)
    shared = min(len(baseline_checkpoints), len(candidate_checkpoints))
    divergence = None
    for index in range(shared):
        if pgraph_checkpoint_signature(baseline_checkpoints[index]) != pgraph_checkpoint_signature(
            candidate_checkpoints[index]
        ):
            divergence = {
                "index": index,
                "baseline": baseline_checkpoints[index],
                "candidate": candidate_checkpoints[index],
            }
            break
    if divergence is None and len(baseline_checkpoints) != len(candidate_checkpoints):
        divergence = {
            "index": shared,
            "baseline": (
                baseline_checkpoints[shared] if shared < len(baseline_checkpoints) else None
            ),
            "candidate": (
                candidate_checkpoints[shared] if shared < len(candidate_checkpoints) else None
            ),
        }
    if divergence is None and baseline["final_state_crc32"] != candidate["final_state_crc32"]:
        divergence = {
            "index": shared,
            "baseline": {"final_state_crc32": baseline["final_state_crc32"]},
            "candidate": {"final_state_crc32": candidate["final_state_crc32"]},
        }
    return {
        "schema_version": 1,
        "equal": divergence is None,
        "baseline": baseline,
        "candidate": candidate,
        "first_divergence": divergence,
    }


def format_pgraph_checkpoint(checkpoint: dict[str, object] | None) -> str:
    if checkpoint is None:
        return "<missing>"
    if "kind" not in checkpoint:
        return f"final_state_crc32=0x{checkpoint['final_state_crc32']:08X}"
    return (
        f"{checkpoint['kind']} frame={checkpoint['frame']} "
        f"method=0x{checkpoint['method']:04X} data=0x{checkpoint['data']:08X} "
        f"primitive=0x{checkpoint['primitive']:02X} count={checkpoint['count']} "
        f"state_crc32=0x{checkpoint['state_crc32']:08X}"
    )


def pgraph_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="nv2a_capture pgraph",
        description="Replay captured PGRAPH state and emit deterministic checkpoints.",
    )
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path, nargs="?")
    parser.add_argument("--json", action="store_true", help="emit JSON replay report")
    args = parser.parse_args(argv)
    try:
        if args.candidate is None:
            result = replay_pgraph(args.baseline)
        else:
            result = compare_pgraph_replays(args.baseline, args.candidate)
    except (OSError, CaptureError) as error:
        print(f"nv2a_capture pgraph: INVALID: {error}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    elif args.candidate is None:
        print(
            "nv2a_capture pgraph: PASS "
            f"methods={result['methods']} checkpoints={result['checkpoint_count']} "
            f"state_crc32=0x{result['final_state_crc32']:08X}"
        )
    elif result["equal"]:
        baseline = result["baseline"]
        print(
            "nv2a_capture pgraph: MATCH "
            f"checkpoints={baseline['checkpoint_count']} "
            f"state_crc32=0x{baseline['final_state_crc32']:08X}"
        )
    else:
        divergence = result["first_divergence"]
        assert isinstance(divergence, dict)
        print(f"nv2a_capture pgraph: DIFFER at checkpoint {divergence['index']}")
        print(f"  baseline : {format_pgraph_checkpoint(divergence['baseline'])}")
        print(f"  candidate: {format_pgraph_checkpoint(divergence['candidate'])}")
    return 0 if args.candidate is None or result["equal"] else 1


def normalize_run_address(value: int, host_mode: bool, base: int) -> int:
    if not host_mode or value == 0:
        return value
    return (value - base) & 0xFFFFFFFF


def normalize_control_word(word: int, host_mode: bool, base: int, method_data: bool) -> int:
    if not host_mode or method_data:
        return word
    if (word & 0xE0000003) == 0x20000000:
        target = normalize_run_address(word & 0x1FFFFFFC, True, base)
        return 0x20000000 | (target & 0x1FFFFFFC)
    if (word & 3) == 1:
        target = normalize_run_address(word & 0xFFFFFFFC, True, base)
        return (target & 0xFFFFFFFC) | 1
    if (word & 3) == 2:
        target = normalize_run_address(word & 0xFFFFFFFC, True, base)
        return (target & 0xFFFFFFFC) | 2
    return word


def comparison_events(path: Path, strict_addresses: bool = False) -> list[ComparisonEvent]:
    # Validate the complete bundle and its internal PFIFO replay before using it
    # as either side of a comparison.
    analyze_capture(path)
    events: list[ComparisonEvent] = []
    host_mode = False
    run_base = 0
    replay = PusherReplay()

    with path.open("rb") as stream:
        stream.seek(HEADER.size)
        record_index = 0
        while raw_header := stream.read(RECORD_HEADER.size):
            record_type, payload_size = RECORD_HEADER.unpack(raw_header)
            payload = stream.read(payload_size)

            if record_type == PUSH_RUN:
                fields = unpack_fields(payload, 9, "push-run")
                frame, host, base, get, put, state, dcount, subroutine, limit = fields
                host_mode = bool(host)
                run_base = base
                replay.begin_run(fields)
                get_offset = normalize_run_address(get, host_mode, base)
                put_offset = normalize_run_address(put, host_mode, base)
                subroutine_offset = normalize_run_address(subroutine, host_mode, base)
                signature: tuple[object, ...] = (
                    frame,
                    host_mode,
                    get_offset,
                    put_offset,
                    state,
                    dcount,
                    subroutine_offset,
                    limit,
                )
                if strict_addresses:
                    signature += (base, get, put, subroutine)
                details = {
                    "frame": frame,
                    "host_mode": host_mode,
                    "base": base,
                    "get": get,
                    "put": put,
                    "get_offset": get_offset,
                    "put_offset": put_offset,
                    "state": state,
                    "dcount": dcount,
                    "subroutine": subroutine,
                    "limit": limit,
                }
                category = "push_runs"
            elif record_type == PUSH_WORD:
                frame, address, word = unpack_fields(payload, 3, "push-word")
                address_offset = normalize_run_address(address, host_mode, run_base)
                method_data = ((replay.state & 0x1FFC0000) >> 18) != 0
                normalized_word = normalize_control_word(word, host_mode, run_base, method_data)
                replay.push_word(frame, address, word)
                signature = (frame, address_offset, normalized_word)
                if strict_addresses:
                    signature += (address, word)
                details = {
                    "frame": frame,
                    "address": address,
                    "address_offset": address_offset,
                    "word": word,
                }
                if normalized_word != word:
                    details["normalized_word"] = normalized_word
                category = "push_words"
            elif record_type == METHOD:
                frame, subchannel, method, data = unpack_fields(payload, 4, "method")
                signature = (frame, subchannel, method, data)
                details = {
                    "frame": frame,
                    "subchannel": subchannel,
                    "method": method,
                    "data": data,
                }
                category = "methods"
            elif record_type == MEMORY:
                address, size, crc = unpack_fields(payload, 3, "memory")
                signature = (size, crc)
                if strict_addresses:
                    signature += (address,)
                details = {"address": address, "size": size, "crc32": crc}
                category = "memory"
            elif record_type == SCANOUT:
                frame, address, width, height, crc, size = unpack_fields(payload, 6, "scanout")
                signature = (frame, width, height, crc, size)
                if strict_addresses:
                    signature += (address,)
                details = {
                    "frame": frame,
                    "address": address,
                    "width": width,
                    "height": height,
                    "crc32": crc,
                    "size": size,
                }
                category = "scanouts"
            elif record_type == RAMIN:
                size, crc = unpack_fields(payload, 2, "RAMIN")
                signature = (size, crc)
                details = {"size": size, "crc32": crc}
                category = "ramin"
            elif record_type == FINISH:
                target, completed, count, truncated, output_crc = unpack_fields(
                    payload, 5, "finish"
                )
                signature = (target, completed, truncated, output_crc)
                details = {
                    "target_frame": target,
                    "completed_frame": completed,
                    "record_count": count,
                    "truncated": bool(truncated),
                    "output_crc32": output_crc,
                }
                category = "finish"
            else:
                crc = zlib.crc32(payload) & 0xFFFFFFFF
                signature = (record_type, payload_size, crc)
                details = {"type": record_type, "size": payload_size, "crc32": crc}
                category = "unknown"

            events.append(
                ComparisonEvent(
                    category=category,
                    record_index=record_index,
                    signature=signature,
                    details=details,
                )
            )
            record_index += 1
    return events


def event_json(event: ComparisonEvent | None) -> dict[str, object] | None:
    if event is None:
        return None
    return {
        "category": event.category,
        "record_index": event.record_index,
        **event.details,
    }


def first_divergence(
    baseline: list[ComparisonEvent], candidate: list[ComparisonEvent]
) -> dict[str, object] | None:
    shared = min(len(baseline), len(candidate))
    for index in range(shared):
        if (
            baseline[index].category != candidate[index].category
            or baseline[index].signature != candidate[index].signature
        ):
            return {
                "index": index,
                "baseline": event_json(baseline[index]),
                "candidate": event_json(candidate[index]),
            }
    if len(baseline) != len(candidate):
        return {
            "index": shared,
            "baseline": event_json(baseline[shared] if shared < len(baseline) else None),
            "candidate": event_json(candidate[shared] if shared < len(candidate) else None),
        }
    return None


def compare_captures(
    baseline_path: Path, candidate_path: Path, strict_addresses: bool = False
) -> dict[str, object]:
    baseline = comparison_events(baseline_path, strict_addresses)
    candidate = comparison_events(candidate_path, strict_addresses)
    categories = sorted({event.category for event in baseline + candidate})
    category_results: dict[str, object] = {}
    for category in categories:
        baseline_category = [event for event in baseline if event.category == category]
        candidate_category = [event for event in candidate if event.category == category]
        divergence = first_divergence(baseline_category, candidate_category)
        category_results[category] = {
            "equal": divergence is None,
            "baseline_count": len(baseline_category),
            "candidate_count": len(candidate_category),
            "first_divergence": divergence,
        }

    divergence = first_divergence(baseline, candidate)
    return {
        "schema_version": 1,
        "equal": divergence is None,
        "strict_addresses": strict_addresses,
        "baseline": str(baseline_path.resolve()),
        "candidate": str(candidate_path.resolve()),
        "baseline_records": len(baseline),
        "candidate_records": len(candidate),
        "first_divergence": divergence,
        "categories": category_results,
    }


def format_event(event: dict[str, object] | None) -> str:
    if event is None:
        return "<missing>"
    fields = []
    hexadecimal = {
        "address",
        "address_offset",
        "base",
        "crc32",
        "data",
        "get",
        "get_offset",
        "limit",
        "method",
        "normalized_word",
        "output_crc32",
        "put",
        "put_offset",
        "state",
        "subroutine",
        "word",
    }
    for key, value in event.items():
        if key in {"category", "record_index"}:
            continue
        if key in hexadecimal and isinstance(value, int):
            fields.append(f"{key}=0x{value:08X}")
        else:
            fields.append(f"{key}={value}")
    return f"{event['category']} record={event['record_index']} " + " ".join(fields)


def compare_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="nv2a_capture compare",
        description="Report the first semantic divergence between two capture bundles.",
    )
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--json", action="store_true", help="emit JSON comparison")
    parser.add_argument(
        "--strict-addresses",
        action="store_true",
        help="compare raw host, memory, and scanout addresses",
    )
    args = parser.parse_args(argv)
    try:
        result = compare_captures(args.baseline, args.candidate, args.strict_addresses)
    except (OSError, CaptureError) as error:
        print(f"nv2a_capture compare: INVALID: {error}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    elif result["equal"]:
        print(
            "nv2a_capture compare: MATCH "
            f"records={result['baseline_records']} strict_addresses={args.strict_addresses}"
        )
    else:
        divergence = result["first_divergence"]
        assert isinstance(divergence, dict)
        print(f"nv2a_capture compare: DIFFER at event {divergence['index']}")
        print(f"  baseline : {format_event(divergence['baseline'])}")
        print(f"  candidate: {format_event(divergence['candidate'])}")
        differing = [
            name for name, category in result["categories"].items() if not category["equal"]
        ]
        print(f"  categories: {', '.join(differing)}")
    return 0 if result["equal"] else 1


def main(argv: list[str] | None = None) -> int:
    effective_argv = list(sys.argv[1:] if argv is None else argv)
    if effective_argv and effective_argv[0] == "compare":
        return compare_main(effective_argv[1:])
    if effective_argv and effective_argv[0] == "pgraph":
        return pgraph_main(effective_argv[1:])

    parser = argparse.ArgumentParser(
        description="Validate a CXBX NV2A capture and replay its PFIFO command stream."
    )
    parser.add_argument("capture", type=Path, help=".nv2acap bundle")
    parser.add_argument("--json", action="store_true", help="emit JSON summary")
    parser.add_argument(
        "--allow-truncated", action="store_true", help="accept a byte-limited bundle"
    )
    args = parser.parse_args(effective_argv)

    try:
        result = analyze_capture(args.capture, args.allow_truncated)
    except (OSError, CaptureError) as error:
        print(f"nv2a_capture: FAIL: {error}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            "nv2a_capture: PASS "
            f"frame={result['target_frame']} runs={result['push_runs']} "
            f"words={result['push_words']} methods={result['methods']} "
            f"memory={result['memory_bytes']} crc={result['output_crc32']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
