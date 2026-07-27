#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""xiso_extract - list or extract files from an Xbox ISO (XDVDFS image).

Bringing up a title starts by getting its files off the disc image; this walks
the XDVDFS directory tree and pulls out `default.xbe` + the game data. Handles the
raw XISO layout (partition base 0) and the redump/XGD offsets. Stdlib only.

    python tools/xiso_extract.py game.iso                 # -> ./game/ (all files)
    python tools/xiso_extract.py game.iso other/games/Foo # -> that dir (all files)
    python tools/xiso_extract.py game.iso --list          # just list contents
    python tools/xiso_extract.py game.iso --file default.xbe -o out/  # one file
"""

import argparse
import struct
import sys
from pathlib import Path

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game-partition base offsets: raw XISO, then the common redump/XGD1-3 offsets.
BASES = (0, 0x18300000, 0x1FB20000, 0x30600000, 0xFD90000)


def find_base(f):
    for base in BASES:
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    sys.exit("xiso_extract: no XDVDFS volume descriptor found (not an Xbox ISO?)")


def walk_dir(f, base, dir_sector, dir_size, prefix=""):
    """Left/right binary-tree of directory entries; returns [(path, sector, size)]
    for files (directories are recursed into)."""
    f.seek(base + dir_sector * SECTOR)
    data = f.read(dir_size)
    entries, stack, seen = [], [0], set()
    while stack:
        off = stack.pop()
        if off in seen or off * 4 + 14 > len(data):
            continue
        seen.add(off)
        left, right, sector, size = struct.unpack_from("<HHII", data, off * 4)
        attr = data[off * 4 + 12]
        nlen = data[off * 4 + 13]
        if left == 0xFFFF and right == 0xFFFF:
            continue
        name = data[off * 4 + 14: off * 4 + 14 + nlen].decode("ascii", "replace")
        if left:
            stack.append(left)
        if right:
            stack.append(right)
        if attr & 0x10:                       # directory
            if size:
                entries += walk_dir(f, base, sector, size, prefix + name + "/")
        else:
            entries.append((prefix + name, sector, size))
    return entries


def read_toc(f):
    base = find_base(f)
    f.seek(base + 32 * SECTOR + 20)
    root_sector, root_size = struct.unpack("<II", f.read(8))
    return base, walk_dir(f, base, root_sector, root_size)


def extract_one(f, base, sector, size, out):
    out.parent.mkdir(parents=True, exist_ok=True)
    f.seek(base + sector * SECTOR)
    remaining = size
    with out.open("wb") as o:
        while remaining > 0:
            chunk = f.read(min(1 << 22, remaining))
            if not chunk:
                sys.exit(f"xiso_extract: short read extracting {out.name}")
            o.write(chunk)
            remaining -= len(chunk)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="xiso_extract",
                                 description="List or extract files from an Xbox ISO.")
    ap.add_argument("iso", help="Xbox ISO (XDVDFS) image")
    ap.add_argument("dest", nargs="?", default=None,
                    help="output directory (default: <iso-stem>/ next to the iso)")
    ap.add_argument("--list", action="store_true", help="list contents, don't extract")
    ap.add_argument("--file", metavar="NAME",
                    help="extract only entries whose path ends with NAME (e.g. default.xbe)")
    ap.add_argument("-o", "--out", help="output path/dir for --file (default: dest or cwd)")
    args = ap.parse_args(argv)

    iso = Path(args.iso)
    if not iso.is_file():
        sys.exit(f"xiso_extract: no such file: {iso}")

    with iso.open("rb") as f:
        base, entries = read_toc(f)
        total = sum(e[2] for e in entries)
        print(f"{iso.name}: {len(entries)} files, {total / 1e6:.0f} MB, partition base 0x{base:X}")

        if args.list:
            for name, _sector, size in sorted(entries):
                print(f"  {size:>12,}  {name}")
            return 0

        if args.file:
            want = args.file.lower()
            matched = [e for e in entries if e[0].lower().endswith(want)]
            if not matched:
                sys.exit(f"xiso_extract: no entry matching {args.file!r}")
            outbase = Path(args.out) if args.out else (Path(args.dest) if args.dest else Path("."))
            for name, sector, size in matched:
                out = outbase / Path(name).name if len(matched) == 1 and outbase.suffix == "" \
                    else (outbase if outbase.suffix else outbase / name)
                extract_one(f, base, sector, size, out)
                print(f"  extracted {name} ({size:,} bytes) -> {out}")
            return 0

        dest = Path(args.dest) if args.dest else iso.with_suffix("")
        for name, sector, size in entries:
            extract_one(f, base, sector, size, dest / name)
        print(f"done -> {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
