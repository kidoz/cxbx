#!/usr/bin/env python3
"""Validate and replay a CXBX NV2A pushbuffer capture bundle."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
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


class CaptureError(RuntimeError):
    """Raised when a capture is corrupt or cannot be replayed exactly."""


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
        if frame != self.frame:
            self.packet_errors.append(
                f"push word frame {frame} does not match run frame {self.frame}"
            )
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Validate a CXBX NV2A capture and replay its PFIFO command stream."
    )
    parser.add_argument("capture", type=Path, help=".nv2acap bundle")
    parser.add_argument("--json", action="store_true", help="emit JSON summary")
    parser.add_argument(
        "--allow-truncated", action="store_true", help="accept a byte-limited bundle"
    )
    args = parser.parse_args(argv)

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
