#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Inventory an Xbox XDK and compare its consumers with CXBX HLE coverage."""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path
from typing import Any

TOOLS_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR))

import tool_config  # noqa: E402

HLE_ENTRY_RE = re.compile(
    r"\{\s*\"(?P<library>[A-Z0-9]+)\"\s*,\s*"
    r"(?P<major>\d+)\s*,\s*(?P<minor>\d+)\s*,\s*(?P<build>\d+)\s*,\s*"
    r"(?P<table>[A-Za-z0-9_]+)\s*,",
    re.DOTALL,
)
LIBRARY_RE = re.compile(r"(?i)(?<![A-Za-z0-9_.-])([A-Za-z0-9_.-]+\.lib)\b")
HLE_ALIASES = {"D3DX8": "D3D8"}
NON_HLE_PREFIXES = ("LIBC", "LIBCP")
NON_HLE_LIBRARIES = {"XBOXKRNL"}


class AuditError(RuntimeError):
    """Raised when the input cannot be audited safely."""


def parse_hle_database(repo_root: Path) -> list[dict[str, Any]]:
    source = repo_root / "src/cxbx/src/win32/CxbxKrnl/HLEDataBase.cpp"
    try:
        text = source.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        raise AuditError(f"cannot read HLE database: {source}") from exc

    entries: list[dict[str, Any]] = []
    for match in HLE_ENTRY_RE.finditer(text):
        major = int(match.group("major"))
        minor = int(match.group("minor"))
        build = int(match.group("build"))
        entries.append(
            {
                "library": match.group("library"),
                "version": f"{major}.{minor}.{build}",
                "table": match.group("table"),
            }
        )
    return entries


def parse_project_dependencies(project: Path) -> set[str]:
    try:
        root = ET.parse(project).getroot()
    except (OSError, ET.ParseError):
        return set()

    dependencies: set[str] = set()
    for element in root.iter():
        raw = element.attrib.get("AdditionalDependencies", "")
        dependencies.update(match.group(1).lower() for match in LIBRARY_RE.finditer(raw))
    return dependencies


def parse_xbe_libraries(path: Path) -> list[dict[str, Any]]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise AuditError(f"cannot read XBE: {path}") from exc
    if len(data) < 0x16C or data[:4] != b"XBEH":
        raise AuditError(f"invalid XBE header: {path}")

    image_base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x160)[0]
    versions_address = struct.unpack_from("<I", data, 0x164)[0]
    if versions_address < image_base:
        raise AuditError(f"invalid XBE library-version address: {path}")
    versions_offset = versions_address - image_base
    if count > 4096 or versions_offset + count * 16 > len(data):
        raise AuditError(f"truncated XBE library-version table: {path}")

    libraries: list[dict[str, Any]] = []
    for index in range(count):
        offset = versions_offset + index * 16
        raw_name, major, minor, build, flags = struct.unpack_from("<8sHHHH", data, offset)
        name = raw_name.split(b"\0", 1)[0].decode("ascii", "replace").strip().upper()
        libraries.append(
            {
                "library": name,
                "version": f"{major}.{minor}.{build}",
                "debug": bool(flags & 0x8000),
            }
        )
    return libraries


def classify_requirement(
    library: str, version: str, coverage: dict[tuple[str, str], str]
) -> tuple[str, str | None]:
    if library in NON_HLE_LIBRARIES or library.startswith(NON_HLE_PREFIXES):
        return "not-hle", None
    lookup_library = HLE_ALIASES.get(library, library)
    table = coverage.get((lookup_library, version))
    return ("covered", table) if table else ("candidate", None)


def audit_xdk(xdk_root: Path, repo_root: Path = REPO_ROOT) -> dict[str, Any]:
    if not xdk_root.is_dir():
        raise AuditError(f"XDK root is not a directory: {xdk_root}")

    library_dir = xdk_root / "xbox/lib"
    sample_dir = xdk_root / "Samples/Xbox"
    if not library_dir.is_dir() or not sample_dir.is_dir():
        raise AuditError("XDK root must contain xbox/lib and Samples/Xbox")

    library_files = sorted(path.name.lower() for path in library_dir.glob("*.lib"))
    projects = sorted(sample_dir.rglob("*.vcproj"))
    dependency_counts: Counter[str] = Counter()
    for project in projects:
        dependency_counts.update(parse_project_dependencies(project))

    hle_entries = parse_hle_database(repo_root)
    coverage = {(entry["library"], entry["version"]): entry["table"] for entry in hle_entries}

    requirements: Counter[tuple[str, str, bool]] = Counter()
    invalid_xbes = 0
    xbes = sorted(xdk_root.rglob("*.xbe"))
    for xbe in xbes:
        try:
            for library in parse_xbe_libraries(xbe):
                requirements[(library["library"], library["version"], library["debug"])] += 1
        except AuditError:
            invalid_xbes += 1

    requirement_rows: list[dict[str, Any]] = []
    for (library, version, debug), images in sorted(requirements.items()):
        classification, table = classify_requirement(library, version, coverage)
        row: dict[str, Any] = {
            "library": library,
            "version": version,
            "debug": debug,
            "images": images,
            "classification": classification,
        }
        if table is not None:
            row["table"] = table
        requirement_rows.append(row)

    return {
        "schema_version": 1,
        "inventory": {
            "libraries": len(library_files),
            "sample_projects": len(projects),
            "prebuilt_xbes": len(xbes),
            "invalid_xbes": invalid_xbes,
            "hle_entries": len(hle_entries),
        },
        "library_files": library_files,
        "sample_dependencies": [
            {"library": library, "projects": count}
            for library, count in sorted(dependency_counts.items())
        ],
        "xbe_requirements": requirement_rows,
    }


def print_text(report: dict[str, Any]) -> None:
    inventory = report["inventory"]
    print(
        "XDK audit: "
        f"{inventory['libraries']} libraries, "
        f"{inventory['sample_projects']} sample projects, "
        f"{inventory['prebuilt_xbes']} prebuilt XBEs, "
        f"{inventory['hle_entries']} HLE registrations"
    )
    print("\nXBE library requirements:")
    for row in report["xbe_requirements"]:
        suffix = f" -> {row['table']}" if "table" in row else ""
        print(
            f"  {row['classification']:9} {row['library']:8} "
            f"{row['version']:10} images={row['images']}{suffix}"
        )


def resolve_xdk_root(argument: str | None) -> Path:
    if argument:
        return Path(argument).expanduser().resolve()
    config = tool_config.load_config(required=False)
    configured = tool_config.config_path_value(config, "xdkaudit", "xdk_root")
    if configured is None:
        raise AuditError(
            "provide --xdk-root or configure xdkaudit.xdk_root in the local tools config"
        )
    return configured


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Inventory an Xbox XDK and compare XBE requirements with CXBX HLE coverage"
    )
    parser.add_argument("--xdk-root", help="XDK root containing xbox/lib and Samples/Xbox")
    parser.add_argument("--repo-root", default=str(REPO_ROOT), help=argparse.SUPPRESS)
    parser.add_argument("--json", action="store_true", help="emit stable JSON metadata")
    args = parser.parse_args(argv)

    try:
        report = audit_xdk(resolve_xdk_root(args.xdk_root), Path(args.repo_root).resolve())
    except AuditError as exc:
        print(f"xdkaudit: {exc}", file=sys.stderr)
        return 2

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_text(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
