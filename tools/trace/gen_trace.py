#!/usr/bin/env python3
"""Generate the native trace registry and shared Python grammar."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import sys
import tempfile
import tomllib
from collections.abc import Hashable
from pathlib import Path
from typing import Any, NoReturn

GENERATOR_VERSION = 1
HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
DEFAULT_REGISTRY = HERE / "registry.toml"
DEFAULT_HEADER = REPO_ROOT / "include" / "cxbx" / "include" / "core" / "trace_registry.gen.h"
DEFAULT_GRAMMAR = REPO_ROOT / "tools" / "tracegrammar.py"


def fail(message: str) -> NoReturn:
    raise ValueError(message)


def load_registry(path: Path) -> tuple[dict[str, Any], str]:
    data = path.read_bytes()
    canonical_data = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    registry = tomllib.loads(canonical_data.decode("utf-8"))
    digest = hashlib.sha256(canonical_data).hexdigest()
    validate_registry(registry)
    return registry, digest


def validate_registry(registry: dict[str, Any]) -> None:
    grammar = registry.get("grammar")
    if not isinstance(grammar, dict) or not isinstance(grammar.get("version"), int):
        fail("grammar.version must be an integer")

    channels = registry.get("channels")
    events = registry.get("events")
    if not isinstance(channels, list) or not channels:
        fail("registry must contain channels")
    if not isinstance(events, list) or not events:
        fail("registry must contain events")

    channel_ids: set[int] = set()
    channel_keys: set[str] = set()
    channel_enums: set[str] = set()
    prefixes: set[str] = set()
    for channel in channels:
        require_fields(channel, "channel", "id", "enum", "key", "prefix", "default_tier")
        channel_id = channel["id"]
        if not isinstance(channel_id, int) or channel_id < 0 or channel_id >= 32:
            fail(f"invalid channel id: {channel_id!r}")
        add_unique(channel_ids, channel_id, "channel id")
        add_unique(channel_keys, channel["key"], "channel key")
        add_unique(channel_enums, channel["enum"], "channel enum")
        add_unique(prefixes, channel["prefix"], "channel prefix")
        if channel["default_tier"] not in {"text", "bin"}:
            fail(f"invalid tier for channel {channel['key']}")
    if sorted(channel_ids) != list(range(len(channels))):
        fail("channel ids must be contiguous from zero")

    value_sets = registry.get("value_sets", [])
    if not isinstance(value_sets, list):
        fail("value_sets must be an array")
    value_set_keys: set[str] = set()
    value_set_enums: set[str] = set()
    for value_set in value_sets:
        require_fields(value_set, "value set", "key", "enum", "values")
        add_unique(value_set_keys, value_set["key"], "value-set key")
        add_unique(value_set_enums, value_set["enum"], "value-set enum")
        if not isinstance(value_set["values"], list) or not value_set["values"]:
            fail(f"value set {value_set['key']} must contain values")
        ids: set[int] = set()
        enums: set[str] = set()
        keys: set[str] = set()
        for value in value_set["values"]:
            require_fields(value, "value", "id", "enum", "key")
            if not isinstance(value["id"], int) or value["id"] < 0:
                fail(f"invalid value id in {value_set['key']}: {value['id']!r}")
            add_unique(ids, value["id"], f"{value_set['key']} value id")
            add_unique(enums, value["enum"], f"{value_set['key']} value enum")
            add_unique(keys, value["key"], f"{value_set['key']} value key")

    event_ids: set[int] = set()
    event_enums: set[str] = set()
    event_keys: set[str] = set()
    for event in events:
        require_fields(
            event,
            "event",
            "id",
            "enum",
            "key",
            "channel",
            "argument",
            "argument_type",
        )
        event_id = event["id"]
        if not isinstance(event_id, int) or event_id <= 0 or event_id > 0xFFFF:
            fail(f"invalid event id: {event_id!r}")
        add_unique(event_ids, event_id, "event id")
        add_unique(event_enums, event["enum"], "event enum")
        add_unique(event_keys, event["key"], "event key")
        if event["channel"] not in channel_keys:
            fail(f"unknown event channel: {event['channel']}")
        if event["argument_type"] != "u32":
            fail(f"unsupported argument type: {event['argument_type']}")
        continuations = event.get("continuations", [])
        continuation_arguments = event.get("continuation_arguments", [])
        formats = event.get("formats", ["hex"])
        if not isinstance(continuations, list) or not all(
            isinstance(item, str) for item in continuations
        ):
            fail(f"invalid continuations for event {event['key']}")
        if not isinstance(continuation_arguments, list) or not all(
            isinstance(item, str) for item in continuation_arguments
        ):
            fail(f"invalid continuation arguments for event {event['key']}")
        if len(continuations) != len(continuation_arguments):
            fail(f"continuation metadata length mismatch for event {event['key']}")
        if not isinstance(formats, list) or len(formats) != len(continuations) + 1:
            fail(f"format metadata length mismatch for event {event['key']}")
        for value_format in formats:
            if value_format not in {"hex", "dec", "bool"} | value_set_keys:
                fail(f"unknown format {value_format!r} for event {event['key']}")
        if "verb" in event and not isinstance(event["verb"], str):
            fail(f"invalid verb for event {event['key']}")

    for event in events:
        for continuation in event.get("continuations", []):
            if continuation not in event_enums:
                fail(f"unknown continuation event {continuation!r}")


def require_fields(item: object, kind: str, *fields: str) -> None:
    if not isinstance(item, dict):
        fail(f"{kind} entry must be a table")
    missing = [field for field in fields if field not in item]
    if missing:
        fail(f"{kind} entry is missing: {', '.join(missing)}")


def add_unique[UniqueValue: Hashable](
    values: set[UniqueValue], value: UniqueValue, label: str
) -> None:
    if value in values:
        fail(f"duplicate {label}: {value!r}")
    values.add(value)


def cpp_string(value: str | None) -> str:
    if value is None:
        return "nullptr"
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def append_cpp_row(lines: list[str], fields: tuple[str, ...], indent: int = 4) -> None:
    leading = " " * indent
    continuation = " " * (indent + 2)
    row = leading + "{ " + ", ".join(fields) + " },"
    if len(row) <= 100:
        lines.append(row)
        return
    midpoint = max(2, len(fields) // 2)
    lines.append(leading + "{ " + ", ".join(fields[:midpoint]) + ",")
    lines.append(continuation + ", ".join(fields[midpoint:]) + " },")


def render_header(registry: dict[str, Any], digest: str) -> str:
    channels = sorted(registry["channels"], key=lambda item: item["id"])
    events = sorted(registry["events"], key=lambda item: item["id"])
    lines = [
        "// Generated by tools/trace/gen_trace.py; do not edit.",
        f"// generator={GENERATOR_VERSION} registry_sha256={digest}",
        "// Contract: numeric IDs are explicit; tables are sorted by ID; UTF-8/LF output.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
    ]
    availability = sorted(
        {channel["availability"] for channel in channels if "availability" in channel}
    )
    for macro in availability:
        lines.extend([f"#ifndef {macro}", f"#define {macro} 1", "#endif", ""])
    lines.extend(
        [
            "namespace cxbx::trace",
            "{",
            "",
            "inline constexpr std::uint16_t kTraceGrammarVersion = "
            f"{registry['grammar']['version']};",
            "",
            "enum class Channel : std::uint8_t",
            "{",
        ]
    )
    for channel in channels:
        lines.append(f"    {channel['enum']} = {channel['id']},")
    lines.extend(
        [f"    Count = {len(channels)},", "};", "", "enum class Event : std::uint16_t", "{"]
    )
    for event in events:
        lines.append(f"    {event['enum']} = {event['id']},")
    lines.extend(["};", ""])
    for value_set in sorted(registry.get("value_sets", []), key=lambda item: item["key"]):
        lines.extend([f"enum class {value_set['enum']} : std::uint32_t", "{"])
        for value in sorted(value_set["values"], key=lambda item: item["id"]):
            lines.append(f"    {value['enum']} = {value['id']},")
        lines.extend(["};", ""])
    lines.extend(
        [
            "struct ChannelDefinition",
            "{",
            "    Channel channel;",
            "    const char* key;",
            "    const char* prefix;",
            "    const char* environmentAlias;",
            "    const char* defaultTier;",
            "    bool available;",
            "};",
            "",
            "inline constexpr ChannelDefinition kChannelDefinitions[] = {",
        ]
    )
    for channel in channels:
        available = f"{channel['availability']} != 0" if "availability" in channel else "true"
        fields = (
            f"Channel::{channel['enum']}",
            cpp_string(channel["key"]),
            cpp_string(channel["prefix"]),
            cpp_string(channel.get("environment_alias")),
            cpp_string(channel["default_tier"]),
            available,
        )
        append_cpp_row(lines, fields)
    lines.extend(
        [
            "};",
            "",
            "struct EventDefinition",
            "{",
            "    Event event;",
            "    Channel channel;",
            "    const char* key;",
            "    const char* argument;",
            "};",
            "",
            "inline constexpr EventDefinition kEventDefinitions[] = {",
        ]
    )
    channel_by_key = {channel["key"]: channel for channel in channels}
    for event in events:
        channel = channel_by_key[event["channel"]]
        event_fields = (
            f"Event::{event['enum']}",
            f"Channel::{channel['enum']}",
            cpp_string(event["key"]),
            cpp_string(event["argument"]),
        )
        append_cpp_row(lines, event_fields, indent=4)
    lines.extend(
        [
            "};",
            "",
            "constexpr Channel EventChannel(Event event) noexcept",
            "{",
            "    switch(event)",
            "    {",
        ]
    )
    for event in events:
        channel = channel_by_key[event["channel"]]
        lines.append(f"        case Event::{event['enum']}: return Channel::{channel['enum']};")
    lines.extend(
        [
            "    }",
            "    return Channel::Count;",
            "}",
            "",
            "constexpr std::uint32_t ChannelBit(Channel channel) noexcept",
            "{",
            "    return std::uint32_t{ 1 } << static_cast<std::uint8_t>(channel);",
            "}",
            "",
            "static_assert(static_cast<std::uint8_t>(Channel::Count) <= 32,",
            '              "The runtime gate mask supports at most 32 channels");',
            "",
            "} // namespace cxbx::trace",
        ]
    )
    return "\n".join(lines) + "\n"


def python_repr(value: object) -> str:
    if isinstance(value, dict):
        lines = ["{"]
        for key in sorted(value, key=lambda item: (isinstance(item, str), item)):
            rendered_key = json.dumps(key) if isinstance(key, str) else str(key)
            rendered_value = python_repr(value[key])
            value_lines = rendered_value.splitlines()
            lines.append(f"    {rendered_key}: {value_lines[0]}")
            lines.extend("    " + line for line in value_lines[1:])
            lines[-1] += ","
        lines.append("}")
        return "\n".join(lines)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, list):
        return "[" + ", ".join(python_repr(item) for item in value) + "]"
    if value is None:
        return "None"
    if isinstance(value, bool):
        return "True" if value else "False"
    return str(value)


def render_grammar(registry: dict[str, Any], digest: str) -> str:
    channels = sorted(registry["channels"], key=lambda item: item["key"])
    events = sorted(registry["events"], key=lambda item: item["id"])
    channel_rows = {
        channel["key"]: {
            "id": channel["id"],
            "prefix": channel["prefix"],
            "tier": channel["default_tier"],
            "environment_alias": channel.get("environment_alias"),
            "availability": channel.get("availability"),
        }
        for channel in channels
    }
    event_rows = {
        event["id"]: {
            "enum": event["enum"],
            "key": event["key"],
            "channel": event["channel"],
            "argument": event["argument"],
            "argument_type": event["argument_type"],
            "arguments": [event["argument"], *event.get("continuation_arguments", [])],
            "continuations": event.get("continuations", []),
            "formats": event.get("formats", ["hex"]),
            "verb": event.get("verb"),
        }
        for event in events
    }
    value_set_rows = {
        value_set["key"]: {
            "enum": value_set["enum"],
            "values": {
                value["id"]: value["key"]
                for value in sorted(value_set["values"], key=lambda item: item["id"])
            },
        }
        for value_set in sorted(registry.get("value_sets", []), key=lambda item: item["key"])
    }
    return f'''# Generated by tools/trace/gen_trace.py; do not edit.
# generator={GENERATOR_VERSION} registry_sha256={digest}
"""Version-aware parser tables for CXBX structured traces."""

from __future__ import annotations

import re
from typing import Any

GRAMMAR_VERSION = {registry["grammar"]["version"]}
REGISTRY_SHA256 = "{digest}"
CHANNELS: dict[str, dict[str, Any]] = {python_repr(channel_rows)}
EVENTS: dict[int, dict[str, Any]] = {python_repr(event_rows)}
VALUE_SETS: dict[str, dict[str, Any]] = {python_repr(value_set_rows)}
PREFIX_TO_CHANNEL = {{str(info["prefix"]): key for key, info in sorted(CHANNELS.items())}}

_START_RE = re.compile(r"^TRACE\\| v=(\\d+) start qpc_hz=(\\d+)$")
_CHANNEL_RE = re.compile(
    r"^TRACE\\| v=(\\d+) channel=(\\S+) state=(enabled|disabled|unavailable) tier=(text|bin)$"
)
_V2_RE = re.compile(
    r"^(?P<prefix>[A-Z0-9]+)\\| (?:(?P<verb>[a-z][a-z0-9_-]*) )?"
    r"tick=(?P<tick>\\d+) tid=(?P<tid>0x[0-9A-Fa-f]+) lseq=(?P<lseq>\\d+)"
    r"(?: (?P<payload>.*))?$"
)
_FLIGHT_RE = re.compile(
    r"^FLIGHT\\| tick=(?P<tick>\\d+) tid_ix=(?P<tid_ix>\\d+) lseq=(?P<lseq>\\d+) "
    r"event=(?P<event>\\d+) arg=(?P<arg>0x[0-9A-Fa-f]{{8}})$"
)


def parse_line(line: str) -> dict[str, Any] | None:
    """Parse a control, canonical event, flight, or legacy channel line."""
    text = line.rstrip("\\r\\n")
    match = _START_RE.match(text)
    if match:
        return {{"kind": "start", "version": int(match[1]), "qpc_hz": int(match[2])}}
    match = _CHANNEL_RE.match(text)
    if match:
        return {{
            "kind": "channel",
            "version": int(match[1]),
            "channel": match[2],
            "state": match[3],
            "tier": match[4],
        }}
    match = _FLIGHT_RE.match(text)
    if match:
        event_id = int(match["event"])
        event = EVENTS.get(event_id)
        return {{
            "kind": "flight",
            "version": GRAMMAR_VERSION,
            "tick": int(match["tick"]),
            "thread_index": int(match["tid_ix"]),
            "lseq": int(match["lseq"]),
            "event_id": event_id,
            "event": event["key"] if event else None,
            "argument": int(match["arg"], 16),
        }}
    match = _V2_RE.match(text)
    if match and match["prefix"] in PREFIX_TO_CHANNEL:
        return {{
            "kind": "event",
            "version": GRAMMAR_VERSION,
            "channel": PREFIX_TO_CHANNEL[match["prefix"]],
            "prefix": match["prefix"],
            "verb": match["verb"],
            "tick": int(match["tick"]),
            "thread_id": int(match["tid"], 16),
            "lseq": int(match["lseq"]),
            "payload": match["payload"] or "",
        }}
    for prefix, channel in sorted(PREFIX_TO_CHANNEL.items()):
        marker = prefix + "|"
        if text.startswith(marker):
            return {{
                "kind": "legacy",
                "version": 1,
                "channel": channel,
                "prefix": prefix,
                "payload": text[len(marker) :].lstrip(),
            }}
    return None


def extract_prefixed_payload(line: str, prefix: str) -> str | None:
    """Return content following the last PREFIX| marker in a decorated line."""
    marker = prefix + "| "
    position = line.rfind(marker)
    return None if position < 0 else line[position + len(marker) :].rstrip("\\r\\n")


def self_test() -> None:
    start = parse_line(f"TRACE| v={{GRAMMAR_VERSION}} start qpc_hz=10000000")
    assert start == {{"kind": "start", "version": GRAMMAR_VERSION, "qpc_hz": 10000000}}
    for key, info in sorted(CHANNELS.items()):
        parsed = parse_line(f"{{info['prefix']}}| call tick=1 tid=0x00000002 lseq=3 sample=1")
        assert parsed is not None and parsed["channel"] == key and parsed["version"] == 2
    for event_id, event in sorted(EVENTS.items()):
        parsed = parse_line(f"FLIGHT| tick=1 tid_ix=2 lseq=3 event={{event_id}} arg=0x00000004")
        assert parsed is not None and parsed["event"] == event["key"]
    d3d = parse_line(
        "D3D| call tick=1 tid=0x00000002 lseq=3 api=Clear seq=3 flags=0x00000001 marker=0xD3D0BEEF"
    )
    assert d3d is not None and d3d["verb"] == "call" and d3d["channel"] == "d3d"
    assert extract_prefixed_payload("Emu (0x1): XT| CHK smoke PASS", "XT") == "CHK smoke PASS"
'''


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")


def check_text(path: Path, expected: str) -> bool:
    try:
        actual = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        print(f"missing generated file: {path}", file=sys.stderr)
        return False
    if actual != expected:
        print(f"generated file is stale: {path}", file=sys.stderr)
        return False
    return True


def run_self_test(grammar_path: Path) -> None:
    spec = importlib.util.spec_from_file_location("cxbx_tracegrammar_generated", grammar_path)
    if spec is None:
        fail(f"could not load generated grammar: {grammar_path}")
    loader = spec.loader
    if loader is None:
        fail(f"generated grammar has no loader: {grammar_path}")
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    module.self_test()


def verify_determinism(registry: dict[str, Any], digest: str) -> None:
    with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
        for root in (Path(first), Path(second)):
            write_text(root / "trace_registry.gen.h", render_header(registry, digest))
            write_text(root / "tracegrammar.py", render_grammar(registry, digest))
        for name in ("trace_registry.gen.h", "tracegrammar.py"):
            if (Path(first) / name).read_bytes() != (Path(second) / name).read_bytes():
                fail(f"nondeterministic output: {name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    parser.add_argument("--grammar", type=Path, default=DEFAULT_GRAMMAR)
    parser.add_argument("--check", action="store_true", help="fail if checked-in output differs")
    parser.add_argument("--self-test", action="store_true", help="run generated parser samples")
    args = parser.parse_args()

    registry, digest = load_registry(args.registry)
    header = render_header(registry, digest)
    grammar = render_grammar(registry, digest)
    verify_determinism(registry, digest)

    if args.check:
        valid = check_text(args.header, header) and check_text(args.grammar, grammar)
        if not valid:
            return 1
    else:
        write_text(args.header, header)
        write_text(args.grammar, grammar)

    if args.self_test:
        run_self_test(args.grammar)
    return 0


if __name__ == "__main__":
    sys.exit(main())
