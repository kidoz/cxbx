#!/usr/bin/env python3
"""Decode versioned CXBX binary trace event files."""

from __future__ import annotations

import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path
from typing import Any

import tracegrammar

HEADER = struct.Struct("<8sHHHHQII")
RECORD = struct.Struct("<QQHHI")
MAGIC = b"CXBXEVT\0"
FILE_VERSION = 1
RECORD_SCHEMA_VERSION = 1


def event_id(key: str) -> int:
    for identifier, event in tracegrammar.EVENTS.items():
        if event["key"] == key:
            return identifier
    raise ValueError(f"registry does not define {key}")


def format_argument(value_format: str, value: int) -> str:
    if value_format == "hex":
        return f"0x{value:08X}"
    if value_format == "dec":
        return str(value)
    if value_format == "bool":
        return "1" if value else "0"
    value_set = tracegrammar.VALUE_SETS[value_format]
    return str(value_set["values"].get(value, f"0x{value:08X}"))


def assemble_logical_records(
    records: list[dict[str, int]],
) -> tuple[list[dict[str, Any]], list[str]]:
    enum_ids = {event["enum"]: identifier for identifier, event in tracegrammar.EVENTS.items()}
    continuation_ids = {
        enum_ids[enum] for event in tracegrammar.EVENTS.values() for enum in event["continuations"]
    }
    logical: list[dict[str, Any]] = []
    violations: list[str] = []
    index = 0
    while index < len(records):
        record = records[index]
        event = tracegrammar.EVENTS.get(record["event_id"])
        if event is None:
            logical.append({"event": None, "values": [record["argument"]], **record})
            index += 1
            continue
        if record["event_id"] in continuation_ids:
            violations.append(f"orphan continuation event {record['event_id']} at record {index}")
            index += 1
            continue

        expected = [enum_ids[enum] for enum in event["continuations"]]
        values = [record["argument"]]
        valid = True
        for offset, expected_id in enumerate(expected, start=1):
            continuation_index = index + offset
            if continuation_index >= len(records):
                violations.append(f"event {event['key']} ends with a partial record group")
                valid = False
                break
            continuation = records[continuation_index]
            if (
                continuation["event_id"] != expected_id
                or continuation["tick"] != record["tick"]
                or continuation["lseq"] != record["lseq"]
                or continuation["thread_index"] != record["thread_index"]
            ):
                violations.append(f"event {event['key']} has a malformed record group")
                valid = False
                break
            values.append(continuation["argument"])
        if valid:
            logical.append({"event": event, "values": values, **record})
            index += len(expected) + 1
        else:
            index += 1
    return logical, violations


def read_event_file(path: Path) -> tuple[dict[str, int], list[dict[str, int]]]:
    data = path.read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("event file is shorter than its header")
    magic, file_version, grammar_version, schema_version, header_size, frequency, record_size, _ = (
        HEADER.unpack_from(data)
    )
    if magic != MAGIC:
        raise ValueError("invalid event-file magic")
    if file_version != FILE_VERSION:
        raise ValueError(f"unsupported event-file version {file_version}")
    if grammar_version != tracegrammar.GRAMMAR_VERSION:
        raise ValueError(f"unsupported grammar version {grammar_version}")
    if schema_version != RECORD_SCHEMA_VERSION or record_size != RECORD.size:
        raise ValueError("unsupported record schema")
    if header_size < HEADER.size or header_size > len(data):
        raise ValueError("invalid event-file header size")
    payload = data[header_size:]
    if len(payload) % record_size != 0:
        raise ValueError("event file ends with a partial record")

    records = [
        {
            "tick": tick,
            "lseq": sequence,
            "event_id": event,
            "thread_index": thread_index,
            "argument": argument,
        }
        for tick, sequence, event, thread_index, argument in RECORD.iter_unpack(payload)
    ]
    header = {
        "file_version": file_version,
        "grammar_version": grammar_version,
        "record_schema_version": schema_version,
        "tick_frequency": frequency,
        "record_size": record_size,
    }
    return header, records


def decode_event_file(path: Path) -> dict[str, Any]:
    header, records = read_event_file(path)
    attach_id = event_id("thread_attach")
    dropped_id = event_id("binary_dropped")
    thread_ids = {
        record["thread_index"]: record["argument"]
        for record in records
        if record["event_id"] == attach_id
    }

    logical_records, violations = assemble_logical_records(records)
    previous_sequence: dict[int, int] = {}
    for record in logical_records:
        if record["event_id"] in {attach_id, dropped_id}:
            continue
        thread_index = record["thread_index"]
        sequence = record["lseq"]
        previous = previous_sequence.get(thread_index)
        if previous is not None and sequence <= previous:
            violations.append(f"thread {thread_index}: lseq {sequence} follows {previous}")
        previous_sequence[thread_index] = sequence

    decoded: list[dict[str, Any]] = []
    for record in sorted(
        logical_records,
        key=lambda item: (
            item["tick"],
            thread_ids.get(item["thread_index"], item["thread_index"]),
            item["lseq"],
        ),
    ):
        if record["event_id"] == attach_id:
            continue
        if record["event_id"] == dropped_id:
            decoded.append({"kind": "dropped", **record, "count": record["argument"]})
            continue

        thread_index = record["thread_index"]
        sequence = record["lseq"]
        event = record["event"]
        if event is None:
            decoded.append({"kind": "unknown", **record})
            continue
        channel = tracegrammar.CHANNELS[event["channel"]]
        thread_id = thread_ids.get(thread_index, thread_index)
        arguments = dict(zip(event["arguments"], record["values"], strict=True))
        payload = " ".join(
            f"{name}={format_argument(value_format, value)}"
            for name, value_format, value in zip(
                event["arguments"], event["formats"], record["values"], strict=True
            )
        )
        verb = event["verb"]
        if verb is None:
            verb = "event"
            payload = f"event={event['key']} {payload}"
        decoded.append(
            {
                "kind": "event",
                **record,
                "event": event["key"],
                "channel": event["channel"],
                "argument_name": event["argument"],
                "arguments": arguments,
                "thread_id": thread_id,
                "time_us": record["tick"] * 1_000_000 // header["tick_frequency"],
                "text": (
                    f"{channel['prefix']}| {verb} tick={record['tick']} "
                    f"tid=0x{thread_id:08X} lseq={sequence} {payload}"
                ),
            }
        )
    return {
        "header": header,
        "thread_ids": thread_ids,
        "records": decoded,
        "sequence_violations": violations,
    }


def self_test() -> None:
    attach_id = event_id("thread_attach")
    d3d_id = event_id("d3d_boundary")
    d3d_call_id = event_id("d3d_call")
    continuation_ids = [event_id(f"continuation_{index}") for index in range(3)]
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "sample.evt"
        header = HEADER.pack(
            MAGIC,
            FILE_VERSION,
            tracegrammar.GRAMMAR_VERSION,
            RECORD_SCHEMA_VERSION,
            HEADER.size,
            10_000_000,
            RECORD.size,
            0,
        )
        records = b"".join(
            (
                RECORD.pack(1, 0, attach_id, 1, 0x1234),
                RECORD.pack(2, 2, d3d_call_id, 1, 1),
                RECORD.pack(2, 2, continuation_ids[0], 1, 2),
                RECORD.pack(2, 2, continuation_ids[1], 1, 0x10),
                RECORD.pack(2, 2, continuation_ids[2], 1, 0xD3D0BEEF),
                RECORD.pack(3, 1, d3d_id, 1, 0x20),
            )
        )
        path.write_bytes(header + records)
        result = decode_event_file(path)
        assert result["header"]["tick_frequency"] == 10_000_000
        assert result["thread_ids"] == {1: 0x1234}
        assert len(result["sequence_violations"]) == 1
        assert result["records"][0]["text"].endswith(
            "api=Clear seq=2 flags=0x00000010 marker=0xD3D0BEEF"
        )

        path.write_bytes(header + records[: -2 * RECORD.size])
        malformed = decode_event_file(path)
        assert any("partial record group" in item for item in malformed["sequence_violations"])

        path.write_bytes(header + records + b"x")
        try:
            read_event_file(path)
        except ValueError:
            pass
        else:
            raise AssertionError("partial record was accepted")

        wrap_ticks = (1 << 32) * 10
        ordering = b"".join(
            (
                RECORD.pack(1, 0, attach_id, 1, 0x2000),
                RECORD.pack(1, 0, attach_id, 2, 0x1000),
                RECORD.pack(wrap_ticks * 3, (1 << 32) + 1, d3d_id, 1, 0x11),
                RECORD.pack(wrap_ticks * 3, (1 << 32) + 1, d3d_id, 2, 0x22),
                RECORD.pack(wrap_ticks * 3 + 10, (1 << 32) + 2, d3d_id, 1, 0x33),
                RECORD.pack(wrap_ticks * 3 + 10, (1 << 32) + 2, d3d_id, 2, 0x44),
            )
        )
        path.write_bytes(header + ordering)
        ordered = decode_event_file(path)
        assert ordered["sequence_violations"] == []
        assert [item["thread_id"] for item in ordered["records"][:2]] == [0x1000, 0x2000]
        assert ordered["records"][0]["time_us"] == (wrap_ticks * 3) // 10
        assert ordered["records"][-1]["lseq"] == (1 << 32) + 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path, nargs="?")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.file is None:
        parser.error("file is required unless --self-test is used")
    try:
        result = decode_event_file(args.file)
    except (OSError, ValueError) as error:
        print(f"traceevt: error: {error}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        for record in result["records"]:
            if record["kind"] == "event":
                print(record["text"])
            elif record["kind"] == "dropped":
                print(
                    f"TRACE| v={tracegrammar.GRAMMAR_VERSION} dropped "
                    f"tid_ix={record['thread_index']} count={record['count']}"
                )
        for violation in result["sequence_violations"]:
            print(f"TRACE| warning={violation}", file=sys.stderr)
    return 1 if result["sequence_violations"] else 0


if __name__ == "__main__":
    sys.exit(main())
