#!/usr/bin/env python3
"""xdklib.py -- explore the extracted XDK library corpus and match .lib
function bodies against shipped title images.

The OOVPA workflow (gen_oovpa.py) needs to know: which XDK versions ship a
symbol, whether its body changed between versions, where the body lives in a
given title image, and where a shipped body diverges from the archived one
(LTCG / relinked builds). Answering those by hand means hexdumps and
one-symbol-at-a-time verify loops; this tool answers them in bulk from the
corpus under other/xbox-sdks/extracted.

Subcommands:
  versions                          discovered XDK lib roots
  libs     --version V              .lib files in one version
  find     PATTERN [--lib L]        symbol search across every version, with
                                    masked body hashes (byte-identity groups)
  body     --version V --lib L --sym S     hexdump + relocs (+ --disasm)
  diff     --lib L --sym S --versions A B  masked body diff between versions
  match    --version V --lib L --sym S --image XBE
                                    locate the body in a title image; --fuzzy
                                    reports prefix matches + first divergence
  map      --version V --lib L --image XBE
                                    match every code symbol in the lib against
                                    the image -> NAME = 0xVA map, call-graph
                                    cross-checked

All matching masks out relocated dwords (the .lib's relocations), so link-time
addresses never anchor or break a comparison.  Images: XBE files are laid out
by section vaddr (VAs are real guest addresses); PE files by section RVA; other
files are scanned raw at base 0.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_oovpa import (  # noqa: E402
    Function,
    _extract_from_member,
    _first_linker_member,
    _member_at,
    _xbe_vaddr_image,
    flat_image,
)

REPO = Path(__file__).resolve().parents[2]
DEFAULT_ROOTS = [REPO / "other" / "xbox-sdks" / "extracted", REPO / "other" / "sdk"]
CACHE = Path(os.environ.get("LOCALAPPDATA", tempfile.gettempdir())) / "cxbx-xdklib-libdirs.json"


# --------------------------------------------------------------------------- #
# Corpus discovery
# --------------------------------------------------------------------------- #


def _version_label(libdir: Path, root: Path) -> str:
    s = str(libdir)
    m = re.search(r"[Xx][Dd][Kk][Ss]etup(\d{4}(?:\.\d+)*)", s)
    if m:
        return m.group(1)
    try:
        top = libdir.relative_to(root).parts[0]
    except ValueError:
        top = libdir.parts[-4]
    m = re.search(r"_-_(\d{4}(?:\.\d+)?)", top)
    if m:
        return m.group(1)
    return top


def _find_libdirs(top: Path, max_depth: int = 6) -> list[Path]:
    """Dirs under `top` (inclusive) holding an XDK/xbox/lib tree. Stops
    descending a branch once found, so XDK payloads are never walked."""
    out: list[Path] = []
    frontier: list[tuple[Path, int]] = [(top, 0)]
    while frontier:
        d, depth = frontier.pop()
        cand = d / "XDK" / "xbox" / "lib"
        if cand.is_dir():
            out.append(cand)
            continue
        if depth >= max_depth:
            continue
        try:
            for c in d.iterdir():
                if c.is_dir():
                    frontier.append((c, depth + 1))
        except OSError:
            pass
    return out


def discover(rescan: bool = False) -> dict[str, Path]:
    """version label -> XDK/xbox/lib dir, cached (the corpus is static)."""
    if not rescan and CACHE.is_file():
        try:
            cached = json.loads(CACHE.read_text())
            got = {v: Path(p) for v, p in cached.items()}
            if got and all(p.is_dir() for p in got.values()):
                return got
        except (OSError, ValueError):
            pass
    found: dict[str, Path] = {}
    for root in DEFAULT_ROOTS:
        if not root.is_dir():
            continue
        for libdir in _find_libdirs(root):
            label = _version_label(libdir, root)
            if label not in found:  # first hit wins (5344 dup lives in other/sdk)
                found[label] = libdir
    try:
        CACHE.write_text(json.dumps({v: str(p) for v, p in found.items()}))
    except OSError:
        pass
    return found


def _version_key(label: str) -> tuple:
    nums = re.findall(r"\d+", label)
    return (0, tuple(int(n) for n in nums)) if nums else (1, label)


def pick_version(versions: dict[str, Path], want: str) -> tuple[str, Path]:
    if want in versions:
        return want, versions[want]
    pref = [v for v in versions if v.startswith(want)]
    if len(pref) == 1:
        return pref[0], versions[pref[0]]
    sys.exit(f"unknown --version {want!r}; have: "
             + " ".join(sorted(versions, key=_version_key)))


def lib_path(libdir: Path, name: str) -> Path:
    p = Path(name)
    if p.is_file():
        return p
    want = name.lower()
    if not want.endswith(".lib"):
        want += ".lib"
    for f in libdir.iterdir():
        if f.name.lower() == want:
            return f
    sys.exit(f"no {want} in {libdir}")


# --------------------------------------------------------------------------- #
# Bodies, masks, matching
# --------------------------------------------------------------------------- #


def safe_extract(lib: bytes, symbols: dict[str, int], sym: str) -> Function | None:
    off = symbols.get(sym)
    if off is None:
        return None
    try:
        member, _ = _member_at(lib, off)
        return _extract_from_member(member, sym)
    except (struct.error, IndexError, ValueError):
        return None


def masked_bytes(fn: Function) -> bytes:
    data = bytearray(fn.data)
    for r in fn.reloc_offsets:
        data[r : r + 4] = b"\0\0\0\0"
    return bytes(data)


def masked_hash(fn: Function) -> str:
    return f"{zlib.crc32(masked_bytes(fn)) & 0xFFFFFFFF:08x}"


def unmasked_runs(fn: Function) -> list[tuple[int, int]]:
    """Maximal (start, end) runs of bytes not covered by a relocation."""
    banned = set()
    for r in fn.reloc_offsets:
        banned.update(range(r, r + 4))
    runs, start = [], None
    for i in range(len(fn.data) + 1):
        if i < len(fn.data) and i not in banned:
            if start is None:
                start = i
        else:
            if start is not None:
                runs.append((start, i))
                start = None
    return runs


def exact_matches(img: bytes, fn: Function, limit: int = 8) -> list[int]:
    """Image offsets where every unmasked byte of the body matches."""
    runs = unmasked_runs(fn)
    if not runs:
        return []
    anchor = max(runs, key=lambda r: r[1] - r[0])
    a0, a1 = anchor
    needle = fn.data[a0:a1]
    if len(needle) < 5:
        return []
    out: list[int] = []
    pos = img.find(needle)
    while pos >= 0 and len(out) < limit:
        base = pos - a0
        if base >= 0 and base + len(fn.data) <= len(img):
            if all(img[base + s : base + e] == fn.data[s:e] for s, e in runs):
                out.append(base)
        pos = img.find(needle, pos + 1)
    return out


def _score(img: bytes, base: int, fn: Function,
           runs: list[tuple[int, int]], total: int) -> tuple[int, float]:
    """(first unmasked divergence offset, matched fraction of unmasked bytes)
    for the body laid at image offset `base`. Divergence == len(data) means a
    full match. Runs past the divergence still count toward the fraction, so a
    body whose prologue changed but whose tail survives scores high."""
    diverge = len(fn.data)
    matched = 0
    for s, e in runs:
        seg_end = min(e, len(img) - base)
        if seg_end <= s:
            break
        if img[base + s : base + seg_end] == fn.data[s:seg_end]:
            matched += seg_end - s
            continue
        good = s
        while good < seg_end and img[base + good] == fn.data[good]:
            good += 1
        matched += good - s
        diverge = min(diverge, good)
        # keep scanning: later runs may still match (counts toward fraction)
        rest = good + 1
        while rest < seg_end:
            if img[base + rest] == fn.data[rest]:
                matched += 1
            rest += 1
    return diverge, matched / total if total else 0.0


def fuzzy_matches(img: bytes, fn: Function, limit: int = 5) -> list[tuple[int, int, float]]:
    """Anchor-and-score candidates: (image offset, first-divergence offset,
    matched fraction of unmasked bytes). Several runs serve as anchors, so a
    body is found whether its prologue or its tail survived the relink."""
    runs = unmasked_runs(fn)
    total = sum(e - s for s, e in runs)
    anchors: list[tuple[int, int]] = []
    first = next((r for r in runs if r[1] - r[0] >= 5), None)
    if first:
        anchors.append(first)
    longest = max(runs, key=lambda r: r[1] - r[0], default=None)
    if longest and longest not in anchors:
        anchors.append(longest)
    for r in runs:
        if len(anchors) >= 6:
            break
        if r[1] - r[0] >= 8 and r not in anchors:
            anchors.append(r)

    cands: dict[int, tuple[int, float]] = {}
    for a0, a1 in anchors:
        needle = fn.data[a0 : min(a1, a0 + 24)]
        if len(needle) < 5:
            continue
        pos = img.find(needle)
        seen = 0
        while pos >= 0 and seen < 4096:
            seen += 1
            base = pos - a0
            if base >= 0 and base not in cands:
                cands[base] = _score(img, base, fn, runs, total)
            pos = img.find(needle, pos + 1)
    ranked = sorted(cands.items(), key=lambda kv: (-kv[1][1], -kv[1][0]))
    return [(b, d, f) for b, (d, f) in ranked[:limit]]


def load_image(path: Path) -> tuple[bytes, int]:
    data = path.read_bytes()
    if data[:4] == b"XBEH":
        return _xbe_vaddr_image(path)
    return flat_image(path), 0


# --------------------------------------------------------------------------- #
# Subcommands
# --------------------------------------------------------------------------- #


def cmd_versions(args) -> int:
    versions = discover(args.rescan)
    for v in sorted(versions, key=_version_key):
        libdir = versions[v]
        n = sum(1 for f in libdir.iterdir() if f.suffix.lower() == ".lib")
        print(f"{v:<10} {n:>3} libs  {libdir}")
    return 0


def cmd_libs(args) -> int:
    _, libdir = pick_version(discover(args.rescan), args.version)
    for f in sorted(libdir.iterdir()):
        if f.suffix.lower() == ".lib":
            print(f"{f.stat().st_size:>12,}  {f.name}")
    return 0


def _iter_libs(versions: dict[str, Path], only_version: str | None,
               only_lib: str | None):
    labels = sorted(versions, key=_version_key)
    if only_version:
        labels = [pick_version(versions, only_version)[0]]
    for v in labels:
        libdir = versions[v]
        for f in sorted(libdir.iterdir()):
            if f.suffix.lower() != ".lib":
                continue
            if only_lib and f.stem.lower() != only_lib.lower().removesuffix(".lib"):
                continue
            yield v, f


def cmd_find(args) -> int:
    versions = discover(args.rescan)
    if args.pattern.startswith("re:"):
        rx = re.compile(args.pattern[3:])
        match = rx.search
    else:
        pat = args.pattern.lower()
        match = lambda s: pat in s.lower()  # noqa: E731

    # symbol -> list of (version, libname, size, maskhash)
    hits: dict[str, list[tuple[str, str, str]]] = {}
    for v, f in _iter_libs(versions, args.version, args.lib):
        try:
            lib = f.read_bytes()
            symbols = _first_linker_member(lib)
        except (OSError, SystemExit):
            continue
        for sym in symbols:
            if not match(sym):
                continue
            note = ""
            if args.bodies:
                fn = safe_extract(lib, symbols, sym)
                note = f" {len(fn.data)}B h={masked_hash(fn)}" if fn else " (data/alias)"
            hits.setdefault(sym, []).append((v, f.stem, note))

    for sym in sorted(hits):
        print(sym)
        for v, libname, note in hits[sym]:
            print(f"    {v:<10} {libname}{note}")
    if not hits:
        print(f"no symbols matching {args.pattern!r}")
        return 1
    return 0


def cmd_body(args) -> int:
    versions = discover(args.rescan)
    v, libdir = pick_version(versions, args.version)
    f = lib_path(libdir, args.lib)
    lib = f.read_bytes()
    symbols = _first_linker_member(lib)
    fn = safe_extract(lib, symbols, args.sym)
    if fn is None:
        sys.exit(f"{args.sym}: not a code symbol in {f.name} ({v})")
    banned = set()
    for r in fn.reloc_offsets:
        banned.update(range(r, r + 4))
    print(f"{args.sym}  [{v} {f.name}]  {len(fn.data)} bytes, "
          f"{len(fn.reloc_offsets)} relocs, masked-hash {masked_hash(fn)}")
    for off in range(0, len(fn.data), 16):
        row = fn.data[off : off + 16]
        hexs = " ".join("??" if off + i in banned else f"{b:02X}"
                        for i, b in enumerate(row))
        print(f"  +0x{off:04X}  {hexs}")
    for off, target in fn.rel32_calls:
        print(f"  rel32 @ +0x{off:04X} -> {target}")
    if args.disasm:
        try:
            from capstone import CS_ARCH_X86, CS_MODE_32, Cs
        except ImportError:
            print("(capstone not installed; --disasm unavailable)")
            return 1
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        for ins in md.disasm(fn.data, 0):
            print(f"  +0x{ins.address:04X}  {ins.mnemonic} {ins.op_str}")
    return 0


def cmd_diff(args) -> int:
    versions = discover(args.rescan)
    fns = []
    for want in args.versions:
        v, libdir = pick_version(versions, want)
        f = lib_path(libdir, args.lib)
        lib = f.read_bytes()
        fn = safe_extract(lib, _first_linker_member(lib), args.sym)
        if fn is None:
            sys.exit(f"{args.sym}: not found in {f.name} ({v})")
        fns.append((v, fn))
    (va, fa), (vb, fb) = fns[0], fns[1]
    print(f"{args.sym}: {va}={len(fa.data)}B/{len(fa.reloc_offsets)}r "
          f"h={masked_hash(fa)}  {vb}={len(fb.data)}B/{len(fb.reloc_offsets)}r "
          f"h={masked_hash(fb)}")
    banned = set()
    for fn in (fa, fb):
        for r in fn.reloc_offsets:
            banned.update(range(r, r + 4))
    n = min(len(fa.data), len(fb.data))
    diffs = [i for i in range(n) if i not in banned and fa.data[i] != fb.data[i]]
    if not diffs and len(fa.data) == len(fb.data):
        print("identical (masked)")
        return 0
    if diffs:
        print(f"{len(diffs)} unmasked byte(s) differ; first divergence +0x{diffs[0]:X}")
        for i in diffs[:16]:
            print(f"  +0x{i:04X}  {va}:{fa.data[i]:02X}  {vb}:{fb.data[i]:02X}")
        if len(diffs) > 16:
            print(f"  ... {len(diffs) - 16} more")
    if len(fa.data) != len(fb.data):
        print(f"sizes differ: {len(fa.data)} vs {len(fb.data)}")
    return 1


def cmd_match(args) -> int:
    versions = discover(args.rescan)
    v, libdir = pick_version(versions, args.version)
    f = lib_path(libdir, args.lib)
    lib = f.read_bytes()
    symbols = _first_linker_member(lib)
    fn = safe_extract(lib, symbols, args.sym)
    if fn is None:
        sys.exit(f"{args.sym}: not a code symbol in {f.name} ({v})")
    img, base = load_image(Path(args.image))
    print(f"{args.sym}  [{v} {f.name}]  {len(fn.data)} bytes vs "
          f"{Path(args.image).name} ({len(img)} bytes, base 0x{base:08X})")

    hits = exact_matches(img, fn)
    for h in hits:
        print(f"  EXACT  0x{base + h:08X}")
    if hits and not args.fuzzy:
        return 0

    ranked = fuzzy_matches(img, fn)
    if not ranked:
        print("  no candidates (anchor prefix not present in image)")
        return 1
    for off, diverge, frac in ranked:
        state = "full" if diverge >= len(fn.data) else f"diverges at +0x{diverge:X}"
        print(f"  CAND   0x{base + off:08X}  prefix {state}, "
              f"{frac * 100:.0f}% of unmasked bytes match")
    return 0 if hits else 1


_SKIP_PREFIXES = ("__imp_", "__IMPORT_DESCRIPTOR", "__NULL_", "??_C", "??_7",
                  "??_R", "__real@", "__xmm@")


def cmd_map(args) -> int:
    versions = discover(args.rescan)
    v, libdir = pick_version(versions, args.version)
    f = lib_path(libdir, args.lib)
    lib = f.read_bytes()
    symbols = _first_linker_member(lib)
    img, base = load_image(Path(args.image))

    # Group identical masked bodies so twin families scan the image once.
    bodies: dict[bytes, tuple[Function, list[str]]] = {}
    skipped_small = skipped_data = 0
    for sym in symbols:
        if sym.startswith(_SKIP_PREFIXES):
            continue
        if args.filter and args.filter.lower() not in sym.lower():
            continue
        fn = safe_extract(lib, symbols, sym)
        if fn is None:
            skipped_data += 1
            continue
        if len(fn.data) < args.min_size:
            skipped_small += 1
            continue
        key = masked_bytes(fn)
        if key in bodies:
            bodies[key][1].append(sym)
        else:
            bodies[key] = (fn, [sym])

    mapped: list[tuple[int, Function, list[str]]] = []
    ambiguous: list[tuple[Function, list[str], list[int]]] = []
    missing = 0
    for fn, syms in bodies.values():
        hits = exact_matches(img, fn, limit=4)
        if len(hits) == 1 and len(syms) == 1:
            mapped.append((hits[0], fn, syms))
        elif len(hits) >= 1:
            ambiguous.append((fn, syms, hits))
        else:
            missing += 1

    # Call-graph cross-check: a mapped body whose rel32 callee is also mapped
    # must encode exactly that callee's VA.
    va_by_sym = {syms[0]: base + off for off, _, syms in mapped}
    checked = bad = 0
    verdicts: dict[str, str] = {}
    for off, fn, syms in mapped:
        marks = []
        for rel_off, callee in fn.rel32_calls:
            tgt = va_by_sym.get(callee)
            if tgt is None or rel_off + 4 > len(fn.data):
                continue
            checked += 1
            rel = int.from_bytes(img[off + rel_off : off + rel_off + 4], "little")
            got = (base + off + rel_off + 4 + rel) & 0xFFFFFFFF
            if got != tgt:
                bad += 1
                marks.append(f"call@+0x{rel_off:X}->{callee} encodes 0x{got:08X} "
                             f"not 0x{tgt:08X}")
        verdicts[syms[0]] = "; ".join(marks)

    out_lines = []
    for off, fn, syms in sorted(mapped):
        flag = "  !! " + verdicts[syms[0]] if verdicts[syms[0]] else ""
        out_lines.append(f"0x{base + off:08X} {syms[0]}  ({len(fn.data)}B){flag}")
    text = "\n".join(out_lines)
    if args.out:
        Path(args.out).write_text(text + "\n", encoding="utf-8")
        print(f"wrote {args.out}")
    else:
        print(text)

    print(f"\n{f.name} ({v}) vs {Path(args.image).name}: "
          f"{len(mapped)} unique, {len(ambiguous)} ambiguous, {missing} absent "
          f"of {len(bodies)} distinct bodies "
          f"({skipped_small} under {args.min_size}B and {skipped_data} data/alias skipped)")
    if checked:
        print(f"call-graph cross-check: {checked - bad}/{checked} rel32 edges consistent"
              + ("  (!! = suspect match)" if bad else ""))
    if ambiguous and args.verbose:
        print("\nambiguous:")
        for fn, syms, hits in ambiguous:
            where = " ".join(f"0x{base + h:08X}" for h in hits)
            print(f"  {' / '.join(syms[:3])}{' ...' if len(syms) > 3 else ''} "
                  f"({len(fn.data)}B) at {where}")
    return 0


# --------------------------------------------------------------------------- #
# Self-test
# --------------------------------------------------------------------------- #


def self_test() -> int:
    body = bytes(range(1, 97))
    fn = Function("t", body, [8, 40])  # two relocated dwords
    img = bytearray(4096)
    at = 1000
    img[at : at + 96] = body
    for r in (8, 40):  # relocations resolve to different link-time values
        img[at + r : at + r + 4] = b"\xde\xad\xbe\xef"
    assert exact_matches(bytes(img), fn) == [at]

    # a diverging copy: same prefix, breaks at +0x30
    at2 = 3000
    img2 = bytearray(img)
    img2[at2 : at2 + 96] = body
    for i in range(0x30, 96):
        img2[at2 + i] ^= 0xFF
    fz = fuzzy_matches(bytes(img2), fn)
    assert any(off == at2 and d == 0x30 for off, d, _ in fz), fz
    assert any(off == at and d == len(body) for off, d, _ in fz), fz

    assert unmasked_runs(fn) == [(0, 8), (12, 40), (44, 96)]
    assert masked_hash(fn) == masked_hash(Function("u", body, [8, 40]))
    print("self-test OK")
    return 0


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rescan", action="store_true",
                    help="rediscover XDK roots (ignore the cached index)")
    ap.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    sub = ap.add_subparsers(dest="cmd")

    sub.add_parser("versions", help="discovered XDK lib roots")

    sp = sub.add_parser("libs", help="list .lib files in one version")
    sp.add_argument("--version", required=True)

    sp = sub.add_parser("find", help="search symbols across all versions")
    sp.add_argument("pattern", help="substring (case-insensitive) or re:<regex>")
    sp.add_argument("--lib", help="restrict to one library (e.g. d3d8)")
    sp.add_argument("--version", help="restrict to one XDK version")
    sp.add_argument("--bodies", action="store_true",
                    help="extract each body: size + masked hash (identity groups)")

    sp = sub.add_parser("body", help="hexdump one function body")
    sp.add_argument("--version", required=True)
    sp.add_argument("--lib", required=True)
    sp.add_argument("--sym", required=True)
    sp.add_argument("--disasm", action="store_true")

    sp = sub.add_parser("diff", help="masked diff of one symbol between two versions")
    sp.add_argument("--lib", required=True)
    sp.add_argument("--sym", required=True)
    sp.add_argument("--versions", nargs=2, required=True, metavar=("A", "B"))

    sp = sub.add_parser("match", help="locate a lib body in a title image")
    sp.add_argument("--version", required=True)
    sp.add_argument("--lib", required=True)
    sp.add_argument("--sym", required=True)
    sp.add_argument("--image", required=True)
    sp.add_argument("--fuzzy", action="store_true",
                    help="also rank prefix matches + first divergence offset")

    sp = sub.add_parser("map", help="match every code symbol against an image")
    sp.add_argument("--version", required=True)
    sp.add_argument("--lib", required=True)
    sp.add_argument("--image", required=True)
    sp.add_argument("--min-size", type=int, default=24,
                    help="skip bodies smaller than this (default 24)")
    sp.add_argument("--filter", help="only symbols containing this substring")
    sp.add_argument("--out", help="write the NAME/VA map here")
    sp.add_argument("--verbose", action="store_true", help="also list ambiguous bodies")

    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.cmd:
        ap.print_help()
        return 2
    return {
        "versions": cmd_versions, "libs": cmd_libs, "find": cmd_find,
        "body": cmd_body, "diff": cmd_diff, "match": cmd_match, "map": cmd_map,
    }[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
