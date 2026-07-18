#!/usr/bin/env python3
"""Validate and replay a CXBX NV2A pushbuffer capture bundle."""

from __future__ import annotations

import argparse
import json
import math
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
NV097_SET_CONTEXT_DMA_COLOR = 0x0194
NV097_SET_CONTEXT_DMA_ZETA = 0x0198
NV097_SET_CONTEXT_DMA_VERTEX = 0x019C
NV097_SET_BEGIN_END = 0x17FC
NV097_ARRAY_ELEMENT16 = 0x1800
NV097_ARRAY_ELEMENT32 = 0x1808
NV097_DRAW_ARRAYS = 0x1810
NV097_INLINE_ARRAY = 0x1818
NV097_SET_VERTEX4F = 0x1518
NV097_SET_VERTEX_DATA2F_M = 0x1880
NV097_SET_VERTEX_DATA4UB = 0x1940
NV097_SET_VERTEX_DATA4F_M = 0x1A00
NV097_SET_VERTEX_DATA_ARRAY_OFFSET = 0x1720
NV097_SET_VERTEX_DATA_ARRAY_FORMAT = 0x1760
NV097_SET_TRANSFORM_PROGRAM = 0x0B00
NV097_SET_TRANSFORM_CONSTANT = 0x0B80
NV097_SET_TRANSFORM_EXECUTION_MODE = 0x1E94
NV097_SET_TRANSFORM_PROGRAM_LOAD = 0x1E9C
NV097_SET_TRANSFORM_PROGRAM_START = 0x1EA0
NV097_SET_TRANSFORM_CONSTANT_LOAD = 0x1EA4
NV097_SET_SHADER_STAGE_PROGRAM = 0x1E70
NV097_SET_ZSTENCIL_CLEAR_VALUE = 0x1D8C
NV097_SET_COLOR_CLEAR_VALUE = 0x1D90
NV097_CLEAR_SURFACE = 0x1D94
NV097_SET_CLEAR_RECT_HORIZONTAL = 0x1D98
NV097_SET_CLEAR_RECT_VERTICAL = 0x1D9C

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
    indices: tuple[int, ...] = ()


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
        indices: tuple[int, ...] = (),
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
            indices=indices,
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
            result = self.checkpoint(
                record_index,
                method_index,
                frame,
                kind,
                method,
                data,
                count,
                tuple(self.element_indices) if kind == "indexed_batch" else (),
            )
            self.reset_batch()
            self.begin_op = 0
            return result
        return None


@dataclass
class PixelMemoryRegion:
    address: int
    data: bytearray
    known: bytearray
    observation_cursor: int = 0

    @property
    def end(self) -> int:
        return self.address + len(self.data)

    def overlay_unknown(self, address: int, data: bytes, minimum_address: int | None = None) -> int:
        begin = max(self.address, address)
        if minimum_address is not None:
            begin = max(begin, minimum_address)
        end = min(self.end, address + len(data))
        conflicts = 0
        for absolute in range(begin, end):
            target = absolute - self.address
            source = absolute - address
            if self.known[target]:
                conflicts += self.data[target] != data[source]
            else:
                self.data[target] = data[source]
                self.known[target] = 1
        return conflicts


@dataclass
class CapturedPixelMemory:
    observations: list[tuple[int, bytes]] = field(default_factory=list)
    observation_pages: dict[int, list[int]] = field(default_factory=dict)
    regions: dict[int, PixelMemoryRegion] = field(default_factory=dict)
    observation_conflicts: int = 0

    def observe(self, address: int, data: bytes) -> None:
        observation_index = len(self.observations)
        self.observations.append((address, data))
        if data:
            for page in range(address >> 12, ((address + len(data) - 1) >> 12) + 1):
                self.observation_pages.setdefault(page, []).append(observation_index)
        for region in self.regions.values():
            self.observation_conflicts += region.overlay_unknown(address, data)
            region.observation_cursor = len(self.observations)

    def ensure(self, address: int, size: int) -> PixelMemoryRegion:
        region = self.regions.get(address)
        if region is None:
            region = PixelMemoryRegion(address, bytearray(size), bytearray(size))
            self.regions[address] = region
            for observed_address, observed_data in self.observations:
                self.observation_conflicts += region.overlay_unknown(
                    observed_address, observed_data
                )
            region.observation_cursor = len(self.observations)
        elif len(region.data) < size:
            old_end = region.end
            growth = size - len(region.data)
            region.data.extend(bytes(growth))
            region.known.extend(bytes(growth))
            for observed_address, observed_data in self.observations[: region.observation_cursor]:
                self.observation_conflicts += region.overlay_unknown(
                    observed_address, observed_data, old_end
                )
        for observed_address, observed_data in self.observations[region.observation_cursor :]:
            self.observation_conflicts += region.overlay_unknown(observed_address, observed_data)
        region.observation_cursor = len(self.observations)
        return region

    def find(self, address: int, size: int) -> PixelMemoryRegion | None:
        for region in self.regions.values():
            if region.address <= address and address + size <= region.end:
                return region
        return None

    def read_observed(self, address: int, size: int) -> bytes | None:
        """Read the newest captured bytes without treating guest input as replay output."""
        result = bytearray(size)
        known = bytearray(size)
        aliases = (address, 0x80000000 | (address & 0x07FFFFFF))
        candidates: set[int] = set()
        for candidate in aliases:
            if size:
                for page in range(candidate >> 12, ((candidate + size - 1) >> 12) + 1):
                    candidates.update(self.observation_pages.get(page, ()))
        for observation_index in sorted(candidates, reverse=True):
            observed_address, observed_data = self.observations[observation_index]
            for candidate in aliases:
                begin = max(candidate, observed_address)
                end = min(candidate + size, observed_address + len(observed_data))
                for absolute in range(begin, end):
                    target = absolute - candidate
                    if not known[target]:
                        result[target] = observed_data[absolute - observed_address]
                        known[target] = 1
            if all(known):
                return bytes(result)
        return None

    def clear_masked(
        self,
        address: int,
        pitch: int,
        height: int,
        min_x: int,
        max_x: int,
        min_y: int,
        max_y: int,
        bytes_per_pixel: int,
        mask: int,
        value: int,
    ) -> None:
        region = self.ensure(address, pitch * height)
        pixel_count = max_x - min_x + 1
        for y in range(min_y, max_y + 1):
            row = y * pitch + min_x * bytes_per_pixel
            for component in range(bytes_per_pixel):
                component_mask = 0xFF << (component * 8)
                if mask & component_mask:
                    begin = row + component
                    end = begin + pixel_count * bytes_per_pixel
                    byte = (value >> (component * 8)) & 0xFF
                    region.data[begin:end:bytes_per_pixel] = bytes([byte]) * pixel_count
                    region.known[begin:end:bytes_per_pixel] = bytes([1]) * pixel_count

    def zero(self, address: int, size: int) -> PixelMemoryRegion:
        region = self.ensure(address, size)
        region.data[:size] = bytes(size)
        region.known[:size] = bytes([1]) * size
        return region

    def copy_rows(
        self,
        source_address: int,
        source_pitch: int,
        destination_address: int,
        destination_pitch: int,
        width_bytes: int,
        height: int,
    ) -> None:
        source = self.find(source_address, source_pitch * height)
        destination = self.ensure(destination_address, destination_pitch * height)
        if source is None:
            return
        for y in range(height):
            source_begin = source_address - source.address + y * source_pitch
            destination_begin = destination_address - destination.address + y * destination_pitch
            destination.data[destination_begin : destination_begin + width_bytes] = source.data[
                source_begin : source_begin + width_bytes
            ]
            destination.known[destination_begin : destination_begin + width_bytes] = source.known[
                source_begin : source_begin + width_bytes
            ]


def resolve_dma_base(
    handle_or_instance: int,
    ramin: bytes,
    objects: dict[int, tuple[int, int]],
) -> int:
    if handle_or_instance == 0:
        return 0
    instance = objects.get(handle_or_instance, (handle_or_instance, 0))[0]
    if instance + 12 > len(ramin):
        return 0
    flags, _limit, frame = struct.unpack_from("<III", ramin, instance)
    object_class = flags & 0xFFF
    if object_class not in (2, 3, 0x3D):
        return 0
    return (frame & 0xFFFFF000) + ((flags >> 20) & 0xFFF)


@dataclass
class AaColorSurface:
    address: int
    pitch: int
    width: int
    height: int
    frame: int


@dataclass(frozen=True)
class OfflineVertex:
    x: float
    y: float
    z: float
    w: float
    color: int


@dataclass
class OfflinePixelReplay:
    ramin: bytes
    objects: dict[int, tuple[int, int]]
    display_pitches: dict[int, int]
    memory: CapturedPixelMemory = field(default_factory=CapturedPixelMemory)
    aa_surface: AaColorSurface | None = None
    clears_executed: int = 0
    draws_executed: int = 0
    triangles_executed: int = 0
    presents_executed: int = 0
    unsupported: dict[str, int] = field(default_factory=dict)
    pending_present: tuple[int, int] | None = None

    def surface_address(self, state: PgraphReplayState, color: bool) -> int:
        context_method = NV097_SET_CONTEXT_DMA_COLOR if color else NV097_SET_CONTEXT_DMA_ZETA
        offset_method = NV097_SET_SURFACE_COLOR_OFFSET if color else NV097_SET_SURFACE_ZETA_OFFSET
        base = resolve_dma_base(state.registers[context_method // 4], self.ramin, self.objects)
        return (base + state.registers[offset_method // 4]) & 0xFFFFFFFF

    @staticmethod
    def surface_geometry(state: PgraphReplayState) -> tuple[int, int, int]:
        pitch = state.registers[NV097_SET_SURFACE_PITCH // 4] & 0xFFFF
        if pitch == 0:
            pitch = 640 * 4
        horizontal = state.registers[NV097_SET_SURFACE_CLIP_HORIZONTAL // 4]
        vertical = state.registers[NV097_SET_SURFACE_CLIP_VERTICAL // 4]
        width = (horizontal & 0xFFFF) + (horizontal >> 16)
        if width <= 0 or width > pitch // 4:
            width = pitch // 4
        height = (vertical & 0xFFFF) + (vertical >> 16)
        if height <= 0 or height > 4096:
            height = 480
        return pitch, width, height

    def vertex_address(self, state: PgraphReplayState, attribute: int, index: int) -> int:
        context = state.registers[NV097_SET_CONTEXT_DMA_VERTEX // 4]
        base = resolve_dma_base(context, self.ramin, self.objects)
        offset = state.registers[(NV097_SET_VERTEX_DATA_ARRAY_OFFSET + attribute * 4) // 4]
        vertex_format = state.registers[(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + attribute * 4) // 4]
        stride = (vertex_format >> 8) & 0xFF
        return (base + offset + index * stride) & 0xFFFFFFFF

    def read_vertex(self, state: PgraphReplayState, index: int) -> OfflineVertex | None:
        position_format = state.registers[NV097_SET_VERTEX_DATA_ARRAY_FORMAT // 4]
        position_type = position_format & 0xF
        position_count = (position_format >> 4) & 0xF
        position_stride = (position_format >> 8) & 0xFF
        if position_type != 2 or position_count not in (2, 3, 4) or position_stride == 0:
            return None
        raw_position = self.memory.read_observed(
            self.vertex_address(state, 0, index), position_count * 4
        )
        if raw_position is None:
            return None
        position = list(struct.unpack(f"<{position_count}f", raw_position))
        if position_count == 2:
            position.extend((0.0, 1.0))
        elif position_count == 3:
            position.append(1.0)

        color = 0xFFFFFFFF
        diffuse_format = state.registers[(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 3 * 4) // 4]
        diffuse_stride = (diffuse_format >> 8) & 0xFF
        if diffuse_stride:
            diffuse_type = diffuse_format & 0xF
            diffuse_count = (diffuse_format >> 4) & 0xF
            diffuse_size = 16 if diffuse_type == 2 else 4
            raw_diffuse = self.memory.read_observed(
                self.vertex_address(state, 3, index), diffuse_size
            )
            if raw_diffuse is None:
                return None
            if diffuse_type == 0 and diffuse_count == 4:
                color = struct.unpack("<I", raw_diffuse)[0]
            elif diffuse_type == 2 and diffuse_count == 4:
                channels = struct.unpack("<4f", raw_diffuse)
                packed = [max(0, min(255, int(value * 255.0 + 0.5))) for value in channels]
                color = (packed[3] << 24) | (packed[0] << 16) | (packed[1] << 8) | packed[2]
            else:
                return None

        matrix = [
            struct.unpack("<f", struct.pack("<I", word))[0] for word in state.composite_matrix
        ]
        x, y, z, w = position
        transformed = (
            x * matrix[0] + y * matrix[1] + z * matrix[2] + w * matrix[3],
            x * matrix[4] + y * matrix[5] + z * matrix[6] + w * matrix[7],
            x * matrix[8] + y * matrix[9] + z * matrix[10] + w * matrix[11],
            x * matrix[12] + y * matrix[13] + z * matrix[14] + w * matrix[15],
        )
        if not all(math.isfinite(value) for value in transformed) or transformed[3] <= 1.0e-5:
            return None
        viewport = [
            struct.unpack("<f", struct.pack("<I", word))[0] for word in state.viewport_offset
        ]
        inverse_w = 1.0 / transformed[3]
        return OfflineVertex(
            transformed[0] * inverse_w + viewport[0],
            transformed[1] * inverse_w + viewport[1],
            transformed[2] * inverse_w,
            transformed[3],
            color,
        )

    @staticmethod
    def triangle_indices(primitive: int, count: int) -> list[tuple[int, int, int]] | None:
        if primitive == 5:
            return [(index, index + 1, index + 2) for index in range(0, count - 2, 3)]
        if primitive == 6:
            return [(index, index + 1, index + 2) for index in range(count - 2)]
        if primitive in (7, 10):
            return [(0, index, index + 1) for index in range(1, count - 1)]
        if primitive == 8:
            return [
                triangle
                for index in range(0, count - 3, 4)
                for triangle in ((index, index + 1, index + 2), (index, index + 2, index + 3))
            ]
        if primitive == 9:
            return [
                triangle
                for index in range(0, count - 3, 2)
                for triangle in ((index, index + 1, index + 3), (index, index + 3, index + 2))
            ]
        return None

    @staticmethod
    def depth_pass(function: int, source: int, destination: int) -> bool:
        return {
            0x200: False,
            0x201: source < destination,
            0x202: source == destination,
            0x203: source <= destination,
            0x204: source > destination,
            0x205: source != destination,
            0x206: source >= destination,
        }.get(function, True)

    @staticmethod
    def depth_surface_known(
        region: PixelMemoryRegion, pitch: int, width: int, height: int, zeta_format: int
    ) -> bool:
        for y in range(height):
            row = y * pitch
            if zeta_format == 2:
                row_end = row + width * 4
                if any(
                    not all(region.known[row + component : row_end : 4]) for component in (1, 2, 3)
                ):
                    return False
            elif not all(region.known[row : row + width * 2]):
                return False
        return True

    def draw_unsupported_reason(
        self, state: PgraphReplayState, checkpoint: PgraphCheckpoint
    ) -> str | None:
        prefix = checkpoint.kind
        if checkpoint.kind not in ("draw_arrays", "indexed_batch"):
            return checkpoint.kind
        if state.vp_execution_mode == 2 and state.vp_instruction_count:
            return f"{prefix}_vertex_program"
        if state.registers[0x0300 // 4]:
            return f"{prefix}_alpha_test"
        if state.registers[0x0304 // 4]:
            return f"{prefix}_blend"
        if state.registers[0x032C // 4]:
            return f"{prefix}_stencil"
        for stage in range(4):
            stage_base = 0x1B00 + stage * 0x40
            texture_format = state.registers[(stage_base + 4) // 4]
            texture_control = state.registers[(stage_base + 12) // 4]
            texture_mode = (
                state.registers[NV097_SET_SHADER_STAGE_PROGRAM // 4] >> (stage * 5)
            ) & 0x1F
            if texture_format and texture_control & 0x40000000 and texture_mode:
                return f"{prefix}_texture"
        combiner_control = state.registers[0x1E60 // 4]
        diffuse_combiner = (
            (combiner_control & 0xFF) == 1
            and state.registers[0x0AC0 // 4] == 0x00002004
            and state.registers[0x0260 // 4] == 0x00002014
            and state.registers[0x1E40 // 4] == 0x00000C00
            and state.registers[0x0AA0 // 4] == 0x00000C00
        )
        final_cw0 = state.registers[0x0288 // 4]
        final_cw1 = state.registers[0x028C // 4]
        final_passthrough = final_cw0 == 0x0000000C and ((final_cw1 >> 8) & 0xFF) == 0x1C
        if (combiner_control and not diffuse_combiner) or (
            (final_cw0 or final_cw1) and not final_passthrough
        ):
            return f"{prefix}_combiner"
        if checkpoint.count < 3:
            return f"{prefix}_vertex_count"
        if self.triangle_indices(checkpoint.primitive, checkpoint.count) is None:
            return f"{prefix}_primitive"
        return None

    def write_fragment(
        self,
        state: PgraphReplayState,
        color_region: PixelMemoryRegion,
        depth_region: PixelMemoryRegion | None,
        color_pitch: int,
        depth_pitch: int,
        x: int,
        y: int,
        z: float,
        color: int,
        zeta_format: int,
    ) -> None:
        if state.registers[0x030C // 4] and depth_region is not None:
            depth_bytes = 4 if zeta_format == 2 else 2
            depth_max = 0xFFFFFF if zeta_format == 2 else 0xFFFF
            source_depth = int(max(0.0, min(float(depth_max), z)) + 0.5)
            depth_offset = y * depth_pitch + x * depth_bytes
            if zeta_format == 2:
                stored = struct.unpack_from("<I", depth_region.data, depth_offset)[0]
                destination_depth = stored >> 8
            else:
                stored = struct.unpack_from("<H", depth_region.data, depth_offset)[0]
                destination_depth = stored
            if not self.depth_pass(state.registers[0x0354 // 4], source_depth, destination_depth):
                return
            if state.registers[0x035C // 4]:
                if zeta_format == 2:
                    struct.pack_into(
                        "<I",
                        depth_region.data,
                        depth_offset,
                        (source_depth << 8) | (stored & 0xFF),
                    )
                else:
                    struct.pack_into("<H", depth_region.data, depth_offset, source_depth)
                depth_region.known[depth_offset : depth_offset + depth_bytes] = (
                    bytes([1]) * depth_bytes
                )

        color_offset = y * color_pitch + x * 4
        struct.pack_into("<I", color_region.data, color_offset, color)
        color_region.known[color_offset : color_offset + 4] = b"\x01\x01\x01\x01"

    def fill_triangle(
        self,
        state: PgraphReplayState,
        vertices: list[OfflineVertex],
        triangle: tuple[int, int, int],
        color_region: PixelMemoryRegion,
        depth_region: PixelMemoryRegion | None,
        color_pitch: int,
        depth_pitch: int,
        width: int,
        height: int,
        zeta_format: int,
    ) -> None:
        a, b, c = (vertices[index] for index in triangle)
        if min(a.w, b.w, c.w) <= 1.0e-5:
            return
        area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
        if -1.0e-3 < area < 1.0e-3:
            return
        low_x = min(a.x, b.x, c.x)
        high_x = max(a.x, b.x, c.x)
        low_y = min(a.y, b.y, c.y)
        high_y = max(a.y, b.y, c.y)
        if high_x < 0.0 or high_y < 0.0 or low_x >= width or low_y >= height:
            return
        low_x = max(0.0, low_x)
        low_y = max(0.0, low_y)
        high_x = min(float(width - 1), high_x)
        high_y = min(float(height - 1), high_y)
        inverse_area = 1.0 / area
        channels = [
            ((vertex.color >> shift) & 0xFF) for vertex in (a, b, c) for shift in (24, 16, 8, 0)
        ]
        uniform = a.color == b.color == c.color
        for y in range(int(low_y), int(high_y) + 1):
            for x in range(int(low_x), int(high_x) + 1):
                pixel_x = x + 0.5
                pixel_y = y + 0.5
                weight_a = (
                    (c.x - b.x) * (pixel_y - b.y) - (c.y - b.y) * (pixel_x - b.x)
                ) * inverse_area
                weight_b = (
                    (a.x - c.x) * (pixel_y - c.y) - (a.y - c.y) * (pixel_x - c.x)
                ) * inverse_area
                weight_c = (
                    (b.x - a.x) * (pixel_y - a.y) - (b.y - a.y) * (pixel_x - a.x)
                ) * inverse_area
                if weight_a < -1.0e-4 or weight_b < -1.0e-4 or weight_c < -1.0e-4:
                    continue
                if uniform:
                    color = a.color
                else:
                    output = []
                    for component in range(4):
                        value = (
                            weight_a * channels[component]
                            + weight_b * channels[4 + component]
                            + weight_c * channels[8 + component]
                        )
                        output.append(max(0, min(255, int(value + 0.5))))
                    color = (output[0] << 24) | (output[1] << 16) | (output[2] << 8) | output[3]
                z = weight_a * a.z + weight_b * b.z + weight_c * c.z
                self.write_fragment(
                    state,
                    color_region,
                    depth_region,
                    color_pitch,
                    depth_pitch,
                    x,
                    y,
                    z,
                    color,
                    zeta_format,
                )

    def execute_draw(self, state: PgraphReplayState, checkpoint: PgraphCheckpoint) -> None:
        reason = self.draw_unsupported_reason(state, checkpoint)
        if reason is not None:
            self.note_unsupported(reason)
            return
        source_indices = (
            checkpoint.indices
            if checkpoint.kind == "indexed_batch"
            else tuple(
                range(checkpoint.data & 0xFFFFFF, (checkpoint.data & 0xFFFFFF) + checkpoint.count)
            )
        )
        vertices: list[OfflineVertex] = []
        for index in source_indices:
            vertex = self.read_vertex(state, index)
            if vertex is None:
                self.note_unsupported(f"{checkpoint.kind}_vertex_input")
                return
            vertices.append(vertex)

        color_pitch, width, height = self.surface_geometry(state)
        color_address = self.surface_address(state, True)
        if not state.registers[NV097_SET_SURFACE_COLOR_OFFSET // 4]:
            self.note_unsupported(f"{checkpoint.kind}_color_surface")
            return
        color_region = self.memory.ensure(color_address, color_pitch * height)

        depth_region = None
        depth_pitch = 0
        zeta_format = (state.registers[NV097_SET_SURFACE_FORMAT // 4] >> 4) & 0xF
        if state.registers[0x030C // 4]:
            if zeta_format not in (1, 2) or not state.registers[NV097_SET_SURFACE_ZETA_OFFSET // 4]:
                self.note_unsupported(f"{checkpoint.kind}_depth_surface")
                return
            depth_pitch = state.registers[NV097_SET_SURFACE_PITCH // 4] >> 16
            if depth_pitch == 0:
                depth_pitch = width * (4 if zeta_format == 2 else 2)
            depth_region = self.memory.ensure(
                self.surface_address(state, False), depth_pitch * height
            )
            if not self.depth_surface_known(depth_region, depth_pitch, width, height, zeta_format):
                self.note_unsupported(f"{checkpoint.kind}_depth_input")
                return

        triangles = self.triangle_indices(checkpoint.primitive, len(vertices))
        assert triangles is not None
        for triangle in triangles:
            self.fill_triangle(
                state,
                vertices,
                triangle,
                color_region,
                depth_region,
                color_pitch,
                depth_pitch,
                width,
                height,
                zeta_format,
            )
        if (state.registers[NV097_SET_SURFACE_FORMAT // 4] >> 12) & 0xF:
            self.aa_surface = AaColorSurface(
                color_address, color_pitch, width, height, checkpoint.frame
            )
        self.draws_executed += 1
        self.triangles_executed += len(triangles)

    def execute_clear(self, state: PgraphReplayState, frame: int, flags: int) -> None:
        color_pitch, width, height = self.surface_geometry(state)

        horizontal = state.registers[NV097_SET_CLEAR_RECT_HORIZONTAL // 4]
        vertical = state.registers[NV097_SET_CLEAR_RECT_VERTICAL // 4]
        min_x, max_x = horizontal & 0xFFFF, horizontal >> 16
        min_y, max_y = vertical & 0xFFFF, vertical >> 16
        if min_x >= width or min_y >= height or max_x < min_x or max_y < min_y:
            return
        max_x = min(max_x, width - 1)
        max_y = min(max_y, height - 1)

        color_address = 0
        if flags & 0xF0 and state.registers[NV097_SET_SURFACE_COLOR_OFFSET // 4]:
            color_address = self.surface_address(state, True)
            mask = 0
            if flags & 0x10:
                mask |= 0x00FF0000
            if flags & 0x20:
                mask |= 0x0000FF00
            if flags & 0x40:
                mask |= 0x000000FF
            if flags & 0x80:
                mask |= 0xFF000000
            self.memory.clear_masked(
                color_address,
                color_pitch,
                height,
                min_x,
                max_x,
                min_y,
                max_y,
                4,
                mask,
                state.registers[NV097_SET_COLOR_CLEAR_VALUE // 4],
            )

        surface_format = state.registers[NV097_SET_SURFACE_FORMAT // 4]
        zeta_format = (surface_format >> 4) & 0xF
        if flags & 0x03 and state.registers[NV097_SET_SURFACE_ZETA_OFFSET // 4] and zeta_format:
            zeta_address = self.surface_address(state, False)
            zeta_pitch = state.registers[NV097_SET_SURFACE_PITCH // 4] >> 16
            clear_value = state.registers[NV097_SET_ZSTENCIL_CLEAR_VALUE // 4]
            if zeta_format == 2:
                if zeta_pitch == 0:
                    zeta_pitch = width * 4
                mask = (0xFFFFFF00 if flags & 1 else 0) | (0x000000FF if flags & 2 else 0)
                self.memory.clear_masked(
                    zeta_address,
                    zeta_pitch,
                    height,
                    min_x,
                    max_x,
                    min_y,
                    max_y,
                    4,
                    mask,
                    clear_value,
                )
            elif zeta_format == 1 and flags & 1:
                if zeta_pitch == 0:
                    zeta_pitch = width * 2
                self.memory.clear_masked(
                    zeta_address,
                    zeta_pitch,
                    height,
                    min_x,
                    max_x,
                    min_y,
                    max_y,
                    2,
                    0xFFFF,
                    clear_value,
                )

        if color_address and ((surface_format >> 12) & 0xF):
            self.aa_surface = AaColorSurface(color_address, color_pitch, width, height, frame)
        self.clears_executed += 1

    def execute_present(self, state: PgraphReplayState, frame: int) -> None:
        destination_address = self.surface_address(state, True) & 0x0FFFFFFF
        source = self.aa_surface
        display_pitch = self.display_pitches.get(frame, 640 * 4)
        if source is not None and source.frame == frame and source.address != destination_address:
            width = min(source.width, display_pitch // 4)
            height = min(source.height, 480)
            self.memory.zero(destination_address, display_pitch * 480)
            self.memory.copy_rows(
                source.address,
                source.pitch,
                destination_address,
                display_pitch,
                width * 4,
                height,
            )
        self.pending_present = (frame, destination_address)
        self.presents_executed += 1

    def note_unsupported(self, kind: str) -> None:
        self.unsupported[kind] = self.unsupported.get(kind, 0) + 1

    def validate_scanout(self, payload: bytes) -> dict[str, object]:
        frame, address, width, height, expected_crc, size = unpack_fields(payload, 6, "scanout")
        expected = payload[24:]
        region = self.memory.find(address, width * height * 4)
        actual = bytearray(size)
        known_rgb_bytes = 0
        if region is not None:
            offset = address - region.address
            for pixel in range(width * height):
                source = offset + pixel * 4
                target = pixel * 4
                actual[target : target + 3] = region.data[source : source + 3]
                actual[target + 3] = 0xFF
                known_rgb_bytes += sum(region.known[source : source + 3])
        else:
            actual[3::4] = bytes([0xFF]) * (width * height)
        actual_crc = zlib.crc32(actual) & 0xFFFFFFFF
        complete = known_rgb_bytes == width * height * 3
        present_matches = self.pending_present == (frame, address)
        return {
            "frame": frame,
            "address": address,
            "width": width,
            "height": height,
            "expected_crc32": expected_crc,
            "actual_crc32": actual_crc,
            "complete": complete,
            "match": complete and present_matches and actual == expected,
            "present_matches": present_matches,
            "known_rgb_bytes": known_rgb_bytes,
            "total_rgb_bytes": width * height * 3,
        }


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
        "index_count": len(checkpoint.indices),
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


def scanout_memory_records(path: Path) -> tuple[set[int], dict[int, int]]:
    excluded: set[int] = set()
    display_pitches: dict[int, int] = {}
    previous_memory: tuple[int, int] | None = None
    with path.open("rb") as stream:
        stream.seek(HEADER.size)
        record_index = 0
        while raw_header := stream.read(RECORD_HEADER.size):
            record_type, payload_size = RECORD_HEADER.unpack(raw_header)
            payload = stream.read(payload_size)
            if record_type == SCANOUT:
                frame, _address, width, _height, _crc, size = unpack_fields(payload, 6, "scanout")
                display_pitches[frame] = width * 4
                if previous_memory is not None and previous_memory[1] == size:
                    excluded.add(previous_memory[0])
            previous_memory = None
            if record_type == MEMORY:
                _address, size, _crc = unpack_fields(payload, 3, "memory")
                previous_memory = (record_index, size)
            record_index += 1
    return excluded, display_pitches


def replay_pixels(path: Path) -> dict[str, object]:
    analysis = analyze_capture(path)
    excluded_memory, display_pitches = scanout_memory_records(path)
    state = PgraphReplayState()
    backend = OfflinePixelReplay(b"", {}, display_pitches)
    scanouts: list[dict[str, object]] = []
    input_memory_records = 0
    input_memory_bytes = 0
    method_index = 0
    pending_draw: PgraphCheckpoint | None = None

    with path.open("rb") as stream:
        stream.seek(HEADER.size)
        record_index = 0
        while raw_header := stream.read(RECORD_HEADER.size):
            record_type, payload_size = RECORD_HEADER.unpack(raw_header)
            payload = stream.read(payload_size)
            if pending_draw is not None and record_type != MEMORY:
                backend.execute_draw(state, pending_draw)
                pending_draw = None
            if record_type == RAMIN:
                size, _crc = unpack_fields(payload, 2, "RAMIN")
                backend.ramin = payload[8 : 8 + size]
                backend.objects = discover_ramin_objects(backend.ramin)
            elif record_type == MEMORY and record_index not in excluded_memory:
                address, size, _crc = unpack_fields(payload, 3, "memory")
                backend.memory.observe(address, payload[12 : 12 + size])
                input_memory_records += 1
                input_memory_bytes += size
            elif record_type == METHOD:
                frame, subchannel, method, data = unpack_fields(payload, 4, "method")
                checkpoint = state.apply_method(
                    frame,
                    subchannel,
                    method,
                    data,
                    record_index,
                    method_index,
                    backend.objects,
                )
                method_index += 1
                if checkpoint is not None:
                    if checkpoint.kind == "clear":
                        backend.execute_clear(state, frame, checkpoint.data)
                    elif checkpoint.kind == "present":
                        backend.execute_present(state, frame)
                    elif checkpoint.kind in ("draw_arrays", "indexed_batch"):
                        pending_draw = checkpoint
                    else:
                        backend.note_unsupported(checkpoint.kind)
            elif record_type == SCANOUT:
                scanouts.append(backend.validate_scanout(payload))
            record_index += 1

    if pending_draw is not None:
        backend.execute_draw(state, pending_draw)

    complete = bool(scanouts) and all(scanout["complete"] for scanout in scanouts)
    matches = complete and all(scanout["match"] for scanout in scanouts)
    supported = not backend.unsupported and backend.memory.observation_conflicts == 0
    status = "pass" if matches and supported else "partial"
    return {
        "schema_version": 1,
        "capture": str(path.resolve()),
        "target_frame": analysis["target_frame"],
        "status": status,
        "methods": method_index,
        "clears_executed": backend.clears_executed,
        "draws_executed": backend.draws_executed,
        "triangles_executed": backend.triangles_executed,
        "presents_executed": backend.presents_executed,
        "unsupported_checkpoints": backend.unsupported,
        "input_memory_records": input_memory_records,
        "input_memory_bytes": input_memory_bytes,
        "excluded_scanout_reads": len(excluded_memory),
        "observation_conflicts": backend.memory.observation_conflicts,
        "conflict_free": backend.memory.observation_conflicts == 0,
        "complete": complete,
        "match": matches and supported,
        "scanouts": scanouts,
    }


def pixel_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="nv2a_capture pixels",
        description="Replay captured clear, fixed-function draw, and present pixels offline.",
    )
    parser.add_argument("capture", type=Path)
    parser.add_argument("--json", action="store_true", help="emit JSON pixel report")
    args = parser.parse_args(argv)
    try:
        result = replay_pixels(args.capture)
    except (OSError, CaptureError) as error:
        print(f"nv2a_capture pixels: INVALID: {error}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(
            f"nv2a_capture pixels: {result['status'].upper()} "
            f"clears={result['clears_executed']} "
            f"draws={result['draws_executed']} "
            f"triangles={result['triangles_executed']} "
            f"presents={result['presents_executed']} "
            f"scanouts={len(result['scanouts'])} "
            f"unsupported={sum(result['unsupported_checkpoints'].values())} "
            f"conflicts={result['observation_conflicts']}"
        )
        for scanout in result["scanouts"]:
            print(
                f"  frame={scanout['frame']} complete={scanout['complete']} "
                f"match={scanout['match']} actual=0x{scanout['actual_crc32']:08X} "
                f"expected=0x{scanout['expected_crc32']:08X}"
            )
    return 0 if result["match"] else 1


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
    if effective_argv and effective_argv[0] == "pixels":
        return pixel_main(effective_argv[1:])

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
