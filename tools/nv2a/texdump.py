#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
texdump - decode the emulator's ground-truth texture sidecars.

CXBX_NV2A_TEXTURE_DUMP=1 writes, per dumped texture, three files to %TEMP%:
  cxbx_tex<N>.bmp   32-bit BMP with alpha FORCED opaque (viewer convenience)
  cxbx_tex<N>.argb  'XTEX' header + format-TRUE ARGB dwords (alpha not forced)
  cxbx_tex<N>.raw   source bytes exactly as read from guest memory

The .bmp lies about alpha by design; investigations about transparency must
read the .argb. This tool prints per-channel statistics from it (and the raw
bytes' first dwords), and can write a PNG that preserves the real alpha over
a checkerboard-free straight encode.

  python tools/nv2a/texdump.py %TEMP%/cxbx_tex0.argb
  python tools/nv2a/texdump.py %TEMP%/cxbx_tex0.argb --png out.png
  python tools/nv2a/texdump.py --all            # every cxbx_tex*.argb in %TEMP%
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

MAGIC = 0x58455458  # 'XTEX'

KIND_NAMES = {0: "A8R8G8B8", 1: "R5G6B5", 2: "A1R5G5B5/X1R5G5B5",
              3: "A4R4G4B4", 4: "Y8", 5: "P8"}


def load(path: Path) -> tuple[int, int, int, int, int, list[int]]:
    data = path.read_bytes()
    if len(data) < 24:
        sys.exit(f"{path.name}: too short for an XTEX sidecar")
    magic, width, height, color, kind, swizzled = struct.unpack_from("<6I", data)
    if magic != MAGIC:
        sys.exit(f"{path.name}: bad magic 0x{magic:08X} (want 'XTEX')")
    n = width * height
    texels = list(struct.unpack_from(f"<{n}I", data, 24))
    return width, height, color, kind, swizzled, texels


def stats(path: Path, png_out: Path | None, scale: int = 1) -> None:
    width, height, color, kind, swizzled, texels = load(path)
    alpha = Counter((t >> 24) & 0xFF for t in texels)
    a0 = alpha.get(0, 0)
    aff = alpha.get(0xFF, 0)
    amid = len(texels) - a0 - aff
    distinct = len(set(texels))
    nonblack = sum(1 for t in texels if t & 0x00FFFFFF)

    print(f"{path.name}: {width}x{height} color=0x{color:02X} "
          f"kind={kind}({KIND_NAMES.get(kind, '?')}) "
          f"{'swizzled' if swizzled else 'linear'}")
    print(f"  texels     : {len(texels)}  distinct={distinct}  "
          f"nonblack-rgb={nonblack} ({nonblack * 100 // max(1, len(texels))}%)")
    print(f"  TRUE alpha : a=0x00:{a0}  a=0xFF:{aff}  mid:{amid}"
          + ("   <-- fully transparent!" if aff == 0 and amid == 0 and a0 else ""))
    print(f"  first texel: 0x{texels[0]:08X}" if texels else "  (empty)")

    raw = path.with_suffix(".raw")
    if raw.is_file():
        head = raw.read_bytes()[:16]
        words = " ".join(f"{w:08X}" for w in struct.unpack_from(
            f"<{len(head) // 4}I", head))
        print(f"  raw source : {raw.stat().st_size} bytes, first dwords {words}")

    if png_out is not None:
        write_png(png_out, width, height, texels, scale)
        print(f"  png        : {png_out} (real alpha preserved)")


def write_png(path: Path, width: int, height: int, argb: list[int],
              scale: int = 1) -> None:
    # Nearest-neighbour upscale: font atlases are often 32 px tall, which is
    # unreadable at 1:1 when the glyphs must be identified by eye.
    rows = bytearray()
    for y in range(height):
        for _ in range(scale):
            rows.append(0)  # filter: none
            for x in range(width):
                t = argb[y * width + x]
                px = bytes(((t >> 16) & 0xFF, (t >> 8) & 0xFF,
                            t & 0xFF, (t >> 24) & 0xFF))
                rows += px * scale

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", width * scale, height * scale, 8, 6, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))
    path.write_bytes(png)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="texdump", description="Decode cxbx_tex<N>.argb ground-truth texture sidecars.")
    ap.add_argument("sidecar", nargs="?", help="a cxbx_tex<N>.argb file")
    ap.add_argument("--all", action="store_true", help="every cxbx_tex*.argb in %%TEMP%%")
    ap.add_argument("--png", metavar="OUT", help="also write a real-alpha PNG")
    ap.add_argument("--scale", type=int, default=1, metavar="N",
                    help="nearest-neighbour upscale for --png (read small font atlases)")
    ap.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args(argv)

    if args.self_test:
        import tempfile
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "cxbx_tex0.argb"
            texels = [0x00FF0000, 0xFF00FF00, 0x80FFFFFF, 0x00000000]
            p.write_bytes(struct.pack("<6I", MAGIC, 2, 2, 0x06, 0, 1) +
                          struct.pack("<4I", *texels))
            w, h, _c, _k, _s, got = load(p)
            assert (w, h, got) == (2, 2, texels)
            out = Path(td) / "t.png"
            write_png(out, 2, 2, texels)
            assert out.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
        print("self-test OK")
        return 0

    targets: list[Path] = []
    if args.all:
        temp = Path(os.environ.get("TEMP") or os.environ.get("TMP") or "/tmp")
        targets = sorted(temp.glob("cxbx_tex*.argb"))
        if not targets:
            print(f"no cxbx_tex*.argb in {temp}")
            return 1
    elif args.sidecar:
        targets = [Path(args.sidecar)]
    else:
        ap.error("give a sidecar path or --all")

    for i, t in enumerate(targets):
        if i:
            print()
        png = Path(args.png) if args.png and len(targets) == 1 else None
        stats(t, png, max(1, args.scale))
    return 0


if __name__ == "__main__":
    sys.exit(main())
