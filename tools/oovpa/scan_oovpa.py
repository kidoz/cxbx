#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
scan_oovpa - report which OOVPA signatures of an HLE table resolve in an image.

gen_oovpa.py authors signatures from an XDK static library. That is impossible
for the LTCG libraries (d3d8ltcg.lib and friends hold link-time-codegen IL --
ANON_OBJECT members, Sig1=0x0000/Sig2=0xFFFF -- not x86; the machine code only
exists once the *title's* linker runs). Signatures for an LTCG title therefore
have to be derived from shipped images, and the first thing that needs
establishing is the gap: of the functions an existing table claims to cover,
which actually resolve in the target image, which are missing, and which are
ambiguous.

This tool answers that. It parses the SOOVPA/LOOVPA definitions and the
OOVPATable array out of the .inl sources, lays an XBE out by section virtual
address (what the HLE scan sees at runtime), and reports per function:

    OK     matched exactly once   -> already covered by the existing table
    MISS   no match               -> needs an LTCG-specific signature
    MULTI  matched more than once -> ambiguous; the HLE pass would patch a
                                     wrong prologue, so it is worse than a MISS

XRef pairs (the leading XRefCount pairs, whose "value" is an XREF enum rather
than a byte) cannot be byte-matched, so only the byte pairs are scanned. That
matches how the engine filters candidates before resolving XRefs, and it means
a MISS here is a genuine miss: no candidate site exists at all.

    python scan_oovpa.py --table D3D8_1_0_5849 \\
        --inl src/cxbx/src/win32/CxbxKrnl/*.inl \\
        --image "other/games/Samurai Showdown V/default.xbe" \\
        --image "other/games/King of Fighters 2002/default.xbe"
"""

import argparse
import glob
import re
import struct
import sys
from pathlib import Path

# SOOVPA<8> Name =
# {
#     0, 8, -1, 0,
#     {
#         { 0x00, 0x8B },
#         ...
_SIG_RE = re.compile(
    r"\b[SL]OOVPA<\s*(\d+)\s*>\s+(\w+)\s*=\s*\{(.*?)\}\s*;",
    re.DOTALL,
)
_PAIR_RE = re.compile(r"\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*([^\s,}]+)\s*\}")


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def parse_signatures(paths: list[Path]) -> dict[str, dict]:
    """name -> {'xrefcount': int, 'pairs': [(offset, byte)], 'xrefs': [(offset, enum)]}"""
    sigs: dict[str, dict] = {}
    for p in paths:
        text = _strip_comments(p.read_text(encoding="utf-8", errors="replace"))
        for m in _SIG_RE.finditer(text):
            name = m.group(2)
            body = m.group(3)
            # Header fields come before the inner pair brace-block.
            inner = body[body.find("{"):]
            head = body[: body.find("{")]
            fields = [f.strip() for f in head.split(",") if f.strip()]
            if len(fields) < 4:
                continue
            try:
                xrefcount = int(fields[3], 0)
            except ValueError:
                continue

            pairs, xrefs = [], []
            for i, pm in enumerate(_PAIR_RE.finditer(inner)):
                off = int(pm.group(1), 0)
                val = pm.group(2)
                if i < xrefcount:
                    xrefs.append((off, val))
                    continue
                try:
                    pairs.append((off, int(val, 0)))
                except ValueError:
                    # A byte pair whose value is not numeric: unparseable, skip
                    # the whole signature rather than scan a wrong pattern.
                    pairs = []
                    break
            if pairs:
                sigs[name] = {"xrefcount": xrefcount, "pairs": pairs, "xrefs": xrefs}
    return sigs


# OOVPATable D3D8_1_0_5849[] = { { (OOVPA*)&Sig, XTL::EmuFn, "EmuFn" }, ... };
def parse_table(paths: list[Path], table: str) -> list[tuple[str, str, bool]]:
    """[(signature var, emu function, patch_all)] in table order."""
    for p in paths:
        text = _strip_comments(p.read_text(encoding="utf-8", errors="replace"))
        m = re.search(
            r"OOVPATable\s+" + re.escape(table) + r"\s*\[\s*\]\s*=\s*\{(.*?)\n\}\s*;",
            text,
            re.DOTALL,
        )
        if not m:
            continue
        body = m.group(1)
        matches = list(re.finditer(
            r"\(\s*OOVPA\s*\*\s*\)\s*&\s*(\w+)\s*,\s*([\w:]+)", m.group(1)
        ))
        out = []
        for index, em in enumerate(matches):
            end = matches[index + 1].start() if index + 1 < len(matches) else len(body)
            patch_all = "OOVPA_FLAG_PATCH_ALL" in body[em.start():end]
            out.append((em.group(1), em.group(2), patch_all))
        return out
    sys.exit(f"table not found in the given .inl files: {table}")


def xbe_image(path: Path) -> tuple[bytes, int]:
    """Lay an XBE out by section virtual address. Returns (image, base)."""
    d = path.read_bytes()
    if d[:4] != b"XBEH":
        # Not an XBE: scan the file bytes as-is.
        return d, 0
    base, = struct.unpack_from("<I", d, 0x104)
    size_of_image, = struct.unpack_from("<I", d, 0x10C)
    nsec, = struct.unpack_from("<I", d, 0x11C)
    psec, = struct.unpack_from("<I", d, 0x120)
    img = bytearray(size_of_image)
    off = psec - base
    for i in range(nsec):
        h = d[off + 0x38 * i: off + 0x38 * (i + 1)]
        vaddr, vsize, raw, rawsize = struct.unpack_from("<IIII", h, 4)
        n = min(rawsize, len(d) - raw)
        start = vaddr - base
        if 0 <= start < len(img):
            img[start: start + n] = d[raw: raw + n]
    return bytes(img), base


def find_matches(img: bytes, pairs: list[tuple[int, int]], limit: int = 8) -> list[int]:
    """Every base offset where all byte pairs hold. Anchored on the first pair."""
    first_off, first_val = pairs[0]
    last = pairs[-1][0]
    out: list[int] = []
    pos = img.find(bytes([first_val]), first_off)
    end = len(img) - last
    while pos >= 0:
        base = pos - first_off
        if base >= end:
            break
        if base >= 0 and all(img[base + o] == v for o, v in pairs):
            out.append(base)
            if len(out) >= limit:
                break
        pos = img.find(bytes([first_val]), pos + 1)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Report which OOVPAs of a table resolve in an image"
    )
    ap.add_argument("--table", required=True, help="OOVPATable array name, e.g. D3D8_1_0_5849")
    ap.add_argument("--inl", action="append", required=True,
                    help="glob of .inl sources holding the signatures/table (repeatable)")
    ap.add_argument("--image", action="append", required=True,
                    help="XBE (or raw binary) to scan (repeatable)")
    ap.add_argument("--quiet", action="store_true", help="only print the summary")
    ap.add_argument("--located-out",
                    help="write the addresses of OK matches (one 0x-address plus "
                         "the emu function per line) for xbe_api_usage.py "
                         "--located; requires exactly one --image")
    args = ap.parse_args()
    if args.located_out and len(args.image) != 1:
        sys.exit("--located-out requires exactly one --image")

    inls = [Path(p) for g in args.inl for p in glob.glob(g, recursive=True)]
    if not inls:
        sys.exit("no .inl files matched")

    sigs = parse_signatures(inls)
    entries = parse_table(inls, args.table)
    print(f"{args.table}: {len(entries)} entries; parsed {len(sigs)} signatures "
          f"from {len(inls)} .inl files\n")

    for ipath in args.image:
        p = Path(ipath)
        img, base = xbe_image(p)
        ok = miss = multi = unparsed = 0
        rows = []
        for signame, emufn, patch_all in entries:
            sig = sigs.get(signame)
            if sig is None:
                unparsed += 1
                rows.append(("????", signame, emufn, ""))
                continue
            hits = find_matches(img, sig["pairs"])
            if len(hits) == 1 or (patch_all and hits):
                ok += 1
                status = "ALL" if len(hits) > 1 else "OK"
                rows.append((status, signame, emufn,
                             [f"0x{base + hit:08X}" for hit in hits]))
            elif not hits:
                miss += 1
                rows.append(("MISS", signame, emufn, []))
            else:
                multi += 1
                rows.append(("MULTI", signame, emufn,
                             [f"0x{base + h:08X}" for h in hits[:4]]))

        total = len(entries)
        print(f"=== {p.name}  ({total} entries)")
        print(f"    OK={ok}  MISS={miss}  MULTI={multi}  unparsed={unparsed}")
        if not args.quiet:
            for status, signame, emufn, where in rows:
                if status not in ("OK", "ALL"):
                    print(f"    {status:5s} {emufn:52s} {signame}")
        print()

        if args.located_out:
            located_count = sum(
                len(addresses)
                for st, _sig, _emufn, addresses in rows if st in ("OK", "ALL")
            )
            Path(args.located_out).write_text("".join(
                f"{address}  {emufn}\n"
                for st, _sig, emufn, addresses in rows if st in ("OK", "ALL")
                for address in addresses
            ))
            print(f"    -> wrote {located_count} located address(es) to "
                  f"{args.located_out}\n")

        # Machine-readable miss list: the functions an LTCG table must supply.
        misses = [s for st, s, _, _ in rows if st in ("MISS", "MULTI")]
        if misses and not args.quiet:
            print(f"    -> {len(misses)} signature(s) need an LTCG-specific "
                  f"replacement for {p.name}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
