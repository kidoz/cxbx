#!/usr/bin/env python3
"""Build an HTML contact sheet from a CXBX_NV2A_DUMP_DRAWS dump directory.

Pairs each f<frame>_d<draw>.bmp with its .txt state sidecar, decodes the
combiner setup and vertex program inline, and writes a single self-contained
HTML file (images embedded as PNG data URIs). Open it in a browser and step
through the frame draw by draw to find where the image breaks.

Usage:
  drawreport.py                    # reads %TEMP%/cxbx_nv2a_draw, writes report.html there
  drawreport.py <dump-dir> [-o report.html]
"""

from __future__ import annotations

import argparse
import base64
import html
import re
import struct
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import combiner
import vp_disasm


def read_bmp(path: Path) -> tuple[int, int, bytes] | None:
    """Read a 32bpp BI_RGB BMP (as written by the emulator) -> (w, h, BGRA rows)."""
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        return None
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 32 or width <= 0 or width > 4096 or abs(height) > 4096:
        return None
    top_down = height < 0
    height = abs(height)
    row_bytes = width * 4
    if len(data) < offset + row_bytes * height:
        return None
    rows = []
    for y in range(height):
        source_y = y if top_down else height - 1 - y
        start = offset + source_y * row_bytes
        rows.append(data[start : start + row_bytes])
    return width, height, b"".join(rows)


def bgra_to_png(width: int, height: int, bgra: bytes) -> bytes:
    """Encode BGRA pixel rows as an RGB PNG (alpha dropped; dumps force 0xFF)."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type: none
        row = bgra[y * width * 4 : (y + 1) * width * 4]
        for x in range(width):
            raw += bytes((row[x * 4 + 2], row[x * 4 + 1], row[x * 4]))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 6)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


SUMMARY_KEYS = ("frame=", "surface ", "depth ", "alpha ", "blend ", "vp ")


def sidecar_summary(text: str) -> list[str]:
    lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith(SUMMARY_KEYS):
            lines.append(stripped)
    return lines


def build_report(dump_dir: Path, output: Path) -> int:
    pattern = re.compile(r"^f(\d+)_d(\d+)\.bmp$")
    entries = []
    for bmp_path in sorted(dump_dir.glob("f*_d*.bmp")):
        match = pattern.match(bmp_path.name)
        if match:
            entries.append((int(match.group(1)), int(match.group(2)), bmp_path))
    if not entries:
        print(f"no f*_d*.bmp dumps in {dump_dir}", file=sys.stderr)
        return 1

    cards: list[str] = []
    for frame, draw, bmp_path in entries:
        image = read_bmp(bmp_path)
        if image is None:
            image_html = "<p class='err'>unreadable BMP</p>"
        else:
            png = bgra_to_png(*image)
            uri = base64.b64encode(png).decode("ascii")
            image_html = f"<img src='data:image/png;base64,{uri}' alt='frame {frame} draw {draw}'>"

        sidecar = bmp_path.with_suffix(".txt")
        state_html = "<p class='err'>no state sidecar</p>"
        if sidecar.is_file():
            text = sidecar.read_text(encoding="utf-8", errors="replace")
            summary = "<br>".join(html.escape(s) for s in sidecar_summary(text))
            try:
                comb = "\n".join(combiner.decode_state_file(sidecar))
            except Exception as error:
                comb = f"combiner decode failed: {error}"
            program = vp_disasm.parse_program(sidecar)
            vp = "\n".join(vp_disasm.disassemble(program)) if program else "(no program)"
            state_html = (
                f"<p class='state'>{summary}</p>"
                f"<details><summary>combiners</summary><pre>{html.escape(comb)}</pre></details>"
                f"<details><summary>vertex program ({len(program)} instr)</summary>"
                f"<pre>{html.escape(vp)}</pre></details>"
            )

        cards.append(
            f"<div class='card'><h3>frame {frame} &middot; draw {draw}</h3>"
            f"{image_html}{state_html}</div>"
        )

    output.write_text(
        "<!doctype html><meta charset='utf-8'><title>NV2A draw dump report</title>"
        "<style>"
        "body{font-family:system-ui,sans-serif;background:#1b1b1f;color:#ddd;margin:1rem}"
        ".card{display:inline-block;vertical-align:top;margin:.5rem;padding:.5rem;"
        "background:#26262c;border-radius:8px;max-width:660px}"
        ".card img{max-width:640px;height:auto;display:block;border:1px solid #444}"
        ".card h3{margin:.2rem 0}.state{font-family:monospace;font-size:.75rem}"
        "pre{font-size:.75rem;overflow-x:auto;background:#111;padding:.5rem}"
        ".err{color:#f88}"
        "</style>"
        f"<h1>NV2A draw dumps &mdash; {html.escape(str(dump_dir))}</h1>" + "".join(cards),
        encoding="utf-8",
    )
    print(f"wrote {output} ({len(entries)} draws)")
    return 0


def main() -> int:
    default_dir = Path(tempfile.gettempdir()) / "cxbx_nv2a_draw"
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dump_dir", nargs="?", type=Path, default=default_dir)
    parser.add_argument(
        "-o", "--output", type=Path, help="output HTML path (default <dump-dir>/report.html)"
    )
    args = parser.parse_args()
    if not args.dump_dir.is_dir():
        print(f"dump directory not found: {args.dump_dir}", file=sys.stderr)
        return 1
    output = args.output if args.output is not None else args.dump_dir / "report.html"
    return build_report(args.dump_dir, output)


if __name__ == "__main__":
    raise SystemExit(main())
