#!/usr/bin/env python3
"""Report NV2A method coverage: which methods a title used vs. what Emu.cpp handles.

Feed it a run log captured with CXBX_NV2A_METHOD_STATS=1 (the "NV2A| stats"
histogram lines). Every observed method is named via tools/nv2a/methods.py and
checked against the set of methods the emulator's PGRAPH dispatcher actually
consumes — extracted by scanning Emu.cpp for `case NV097_*:` labels and
`Method >= X && Method < X + N` range tests. Unhandled hot methods are the NV2A
analogue of an unimplemented kernel export: a ranked to-do list for the title.

Usage:
  coverage.py run.log [--tree <repo-root>] [--top N] [--unhandled-only]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import methods

EMU_CPP = Path("src/cxbx/src/win32/CxbxKrnl/Emu.cpp")

DEFINE_RE = re.compile(r"#define\s+(\w+)\s+(0[xX][0-9A-Fa-f]+|\d+)u?\b")
CASE_RE = re.compile(r"case\s+(NV097_\w+)\s*:")
RANGE_RE = re.compile(
    r"Method\s*>=\s*([A-Za-z0-9_x]+)\s*&&\s*Method\s*<\s*([A-Za-z0-9_x]+)"
    r"(?:\s*\+\s*([^)\n]+))?\)",
    re.MULTILINE,
)


def _find_tree(start: Path) -> Path | None:
    for candidate in (start, *start.parents):
        if (candidate / EMU_CPP).is_file():
            return candidate
    return None


def _eval_expr(expr: str, defines: dict[str, int]) -> int | None:
    """Evaluate 'A * B + 4'-style expressions over integer literals and defines."""
    total: int | None = None
    for term in expr.split("+"):
        product = 1
        for factor in term.split("*"):
            token = factor.strip().rstrip("uU")
            if not token:
                return None
            if re.fullmatch(r"0[xX][0-9A-Fa-f]+|\d+", token):
                product *= int(token, 0)
            elif token in defines:
                product *= defines[token]
            else:
                return None
        total = product if total is None else total + product
    return total


def handled_methods(tree: Path) -> tuple[set[int], list[tuple[int, int]]]:
    """Extract (single methods, ranges) that Emu.cpp's KELVIN dispatch consumes."""
    source = (tree / EMU_CPP).read_text(encoding="utf-8", errors="replace")
    defines: dict[str, int] = {}
    for match in DEFINE_RE.finditer(source):
        defines[match.group(1)] = int(match.group(2), 0)

    singles: set[int] = set()
    for match in CASE_RE.finditer(source):
        value = defines.get(match.group(1))
        if value is not None:
            singles.add(value)

    ranges: list[tuple[int, int]] = []
    for match in RANGE_RE.finditer(source):
        base_name, end_name, extra = match.group(1), match.group(2), match.group(3)
        base = defines.get(base_name) if not base_name.startswith("0") else int(base_name, 0)
        if base is None:
            continue
        if extra is not None:
            if end_name != base_name:
                continue
            length = _eval_expr(extra, defines)
            if length is None:
                continue
            ranges.append((base, base + length))
        else:
            end = defines.get(end_name) if not end_name.startswith("0") else int(end_name, 0)
            if end is not None and end > base:
                ranges.append((base, end))
    return singles, ranges


def is_handled(method: int, singles: set[int], ranges: list[tuple[int, int]]) -> bool:
    if method in singles:
        return True
    return any(begin <= method < end for begin, end in ranges)


STATS_METHOD_RE = re.compile(
    r"NV2A\|\s+stats\s+class=0x([0-9A-Fa-f]+)\s+method=0x([0-9A-Fa-f]+)\s+count=(\d+)"
)
STATS_BIND_RE = re.compile(r"NV2A\|\s+stats\s+class=0x([0-9A-Fa-f]+)\s+bind\s+count=(\d+)")
STATS_BEGIN_RE = re.compile(r"NV2A\|\s+stats\s+begin_op=0x([0-9A-Fa-f]+)\s+count=(\d+)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", type=Path, help="run log with NV2A| stats lines")
    parser.add_argument(
        "--tree", type=Path, help="Cxbx repo root (auto-detected from this script otherwise)"
    )
    parser.add_argument("--top", type=int, default=0, help="show only the top N methods")
    parser.add_argument("--unhandled-only", action="store_true")
    args = parser.parse_args()

    tree = args.tree if args.tree is not None else _find_tree(Path(__file__).resolve().parent)
    if tree is None or not (tree / EMU_CPP).is_file():
        print("could not locate Emu.cpp; pass --tree <repo-root>", file=sys.stderr)
        return 1
    singles, ranges = handled_methods(tree)

    counts: dict[tuple[int, int], int] = {}
    binds: dict[int, int] = {}
    begin_ops: dict[int, int] = {}
    for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
        if (match := STATS_METHOD_RE.search(line)) is not None:
            key = (int(match.group(1), 16), int(match.group(2), 16))
            counts[key] = max(counts.get(key, 0), int(match.group(3)))
        elif (match := STATS_BIND_RE.search(line)) is not None:
            class_id = int(match.group(1), 16)
            binds[class_id] = max(binds.get(class_id, 0), int(match.group(2)))
        elif (match := STATS_BEGIN_RE.search(line)) is not None:
            op = int(match.group(1), 16)
            begin_ops[op] = max(begin_ops.get(op, 0), int(match.group(2)))

    if not counts:
        print(
            "no 'NV2A| stats' lines found; capture with CXBX_NV2A_METHOD_STATS=1", file=sys.stderr
        )
        return 1

    rows = sorted(counts.items(), key=lambda item: -item[1])
    if args.top > 0:
        rows = rows[: args.top]

    total = sum(counts.values())
    unhandled_total = 0
    print(f"{'class':<12} {'method':<8} {'count':>10}  {'handled':<9} name")
    for (class_id, method), count in rows:
        kelvin = (class_id & 0xFF) == 0x97
        handled = kelvin and is_handled(method, singles, ranges)
        if kelvin and not handled:
            unhandled_total += count
        if args.unhandled_only and (handled or not kelvin):
            continue
        flag = "yes" if handled else ("no" if kelvin else "n/a")
        print(
            f"{methods.class_name(class_id):<12} 0x{method:04X}   {count:>10}  "
            f"{flag:<9} {methods.method_name(class_id, method)}"
        )

    if binds:
        print()
        for class_id, count in sorted(binds.items()):
            print(f"bind {methods.class_name(class_id):<12} count={count}")
    if begin_ops:
        print()
        for op, count in sorted(begin_ops.items()):
            name = methods.BEGIN_OP_NAMES.get(op, f"0x{op:02X}")
            print(f"begin_op {name:<15} count={count}")

    print()
    print(
        f"total methods={total} unhandled-kelvin={unhandled_total} "
        f"({100.0 * unhandled_total / total:.1f}%)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
