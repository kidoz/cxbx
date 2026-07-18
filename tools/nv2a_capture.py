#!/usr/bin/env python3
"""Validate and replay a CXBX NV2A pushbuffer capture bundle."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from dataclasses import dataclass
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


@dataclass(frozen=True)
class ComparisonEvent:
    category: str
    record_index: int
    signature: tuple[object, ...]
    details: dict[str, object]


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
