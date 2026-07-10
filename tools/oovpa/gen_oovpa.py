#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
gen_oovpa - generate Cxbx OOVPA signatures from an XDK static library.

For each requested symbol, the COFF archive member holding it is located, the
function's section bytes and relocations are extracted, and (offset, value)
pairs are chosen at relocation-free offsets (relocated dwords differ per title
image, so they can never be part of a signature). The result is an
`SOOVPA<N>` (or, for offsets beyond 0xFF, `LOOVPA<N>`) initializer ready for a
D3D8/DSound `.inl` file (struct layout: include/cxbx/include/win32/cxbxkrnl/OOVPA.h,
matcher: EmuLocateFunction in Emu.cpp).

Every signature is validated against verification binaries: it must match
EXACTLY ONCE in images that contain the function and never more than once
anywhere (a multi-match would make the HLE pass patch the wrong prologue).
PE images (reconstructed %TEMP%\\default.exe, probe .exe) are laid out by
section RVA before scanning; XBE/raw files are scanned as-is (function bytes
are contiguous within a section either way).

    python gen_oovpa.py --lib <d3d8.lib> \\
        --func _D3DDevice_SetLight@8=IDirect3DDevice8_SetLight_1_0_5849 \\
        --verify-one bin/hle_resolve.exe --verify other/xbes/**/*.xbe \\
        [--pairs 8] [--out sigs.inl]
"""

import argparse
import glob
import struct
import sys
from pathlib import Path

IMAGE_SCN_CNT_CODE = 0x00000020


# --------------------------------------------------------------------------- #
# COFF archive (.lib) parsing
# --------------------------------------------------------------------------- #


IMAGE_REL_I386_REL32 = 0x0014


class Function:
    def __init__(self, name: str, data: bytes, reloc_offsets: list[int],
                 rel32_calls: list[tuple[int, str]] | None = None) -> None:
        self.name = name
        self.data = data
        self.reloc_offsets = reloc_offsets  # start offset of each relocated dword
        # (operand offset, target symbol) for each REL32 relocation
        self.rel32_calls = rel32_calls or []


def _member_at(lib: bytes, off: int) -> tuple[bytes, int]:
    """Archive member at `off` -> (payload, next member offset)."""
    size = int(lib[off + 48 : off + 58].split(b"\0")[0].strip() or b"0")
    payload = lib[off + 60 : off + 60 + size]
    nxt = off + 60 + size
    if nxt % 2:
        nxt += 1
    return payload, nxt


def _first_linker_member(lib: bytes) -> dict[str, int]:
    """Symbol name -> member offset, from the archive's first linker member."""
    if lib[:8] != b"!<arch>\n":
        sys.exit("not a COFF archive")
    payload, _ = _member_at(lib, 8)
    (count,) = struct.unpack(">I", payload[:4])
    offsets = struct.unpack(f">{count}I", payload[4 : 4 + 4 * count])
    names = payload[4 + 4 * count :].split(b"\0")[:count]
    return {n.decode("ascii", "replace"): o for n, o in zip(names, offsets)}


def _symbol_name(entry: bytes, strtab: bytes) -> str:
    if entry[:4] == b"\0\0\0\0":
        (stroff,) = struct.unpack("<I", entry[4:8])
        end = strtab.find(b"\0", stroff)
        return strtab[stroff:end].decode("ascii", "replace")
    return entry[:8].rstrip(b"\0").decode("ascii", "replace")


def _iter_object_members(lib: bytes):
    """Yield every COFF object member payload in the archive (skipping the
    linker members and the longnames member)."""
    off = 8
    while off + 60 <= len(lib):
        name = lib[off : off + 16].rstrip()
        payload, nxt = _member_at(lib, off)
        if name not in (b"/", b"//") and len(payload) > 20:
            machine, = struct.unpack("<H", payload[0:2])
            if machine == 0x014C:  # i386 object
                yield payload
        off = nxt


def extract_function_all(lib: bytes, symbols: dict[str, int], sym: str) -> list[Function]:
    """All distinct copies of `sym` across archive members. Old-MSVC archives
    can carry differently-compiled copies of the same COMDAT in several
    members, and the linker's pick need not be the archive index's pick."""
    out: list[Function] = []
    seen: set[bytes] = set()
    for member in _iter_object_members(lib):
        fn = _extract_from_member(member, sym)
        if fn is not None and fn.data not in seen:
            seen.add(fn.data)
            out.append(fn)
    if not out:
        sys.exit(f"symbol not defined in any archive member: {sym}")
    return out


def extract_function(lib: bytes, symbols: dict[str, int], sym: str) -> Function:
    if sym not in symbols:
        sys.exit(f"symbol not in archive index: {sym}")
    member, _ = _member_at(lib, symbols[sym])
    fn = _extract_from_member(member, sym)
    if fn is None:
        sys.exit(f"{sym}: not found in its archive-index member")
    return fn


def _extract_from_member(member: bytes, sym: str) -> Function | None:
    n_sections, = struct.unpack("<H", member[2:4])
    ptr_symtab, n_symbols = struct.unpack("<II", member[8:16])
    strtab = member[ptr_symtab + 18 * n_symbols :]

    # Find the symbol's section and offset within it. Old-MSVC members can
    # list an undefined reference entry for the name AHEAD of the definition
    # in the same symbol table, so keep scanning past undefined hits.
    sect_idx = value = None
    i = 0
    while i < n_symbols:
        e = member[ptr_symtab + 18 * i : ptr_symtab + 18 * (i + 1)]
        if _symbol_name(e, strtab) == sym:
            v, sect_no = struct.unpack("<Ih", e[8:14])
            if sect_no > 0:
                value = v
                sect_idx = sect_no - 1
                break
        i += 1 + e[17]  # skip aux symbols
    if sect_idx is None:
        return None

    sh = member[20 + 40 * sect_idx : 20 + 40 * (sect_idx + 1)]
    (size_raw, ptr_raw, ptr_reloc, _ptr_ln, n_reloc, _n_ln, flags) = struct.unpack(
        "<IIIIHHI", sh[16:40]
    )
    if not flags & IMAGE_SCN_CNT_CODE:
        return None  # data symbol

    data = member[ptr_raw : ptr_raw + size_raw]
    relocs = []
    rel32_calls = []
    for r in range(n_reloc):
        va, symidx, rtype = struct.unpack(
            "<IIH", member[ptr_reloc + 10 * r : ptr_reloc + 10 * r + 10]
        )
        relocs.append(va)
        if rtype == IMAGE_REL_I386_REL32:
            e = member[ptr_symtab + 18 * symidx : ptr_symtab + 18 * (symidx + 1)]
            rel32_calls.append((va, _symbol_name(e, strtab)))

    # COMDAT sections hold one function; a nonzero Value would mean the symbol
    # is an alias inside a packed section, which this simple extractor rejects.
    if value != 0:
        return None
    return Function(sym, data, relocs, rel32_calls)


# --------------------------------------------------------------------------- #
# Pair selection
# --------------------------------------------------------------------------- #


def pick_pairs(fn: Function, want: int, max_offset: int,
               allow_fewer: bool = False) -> list[tuple[int, int]]:
    """Evenly spread `want` (offset, value) pairs over the function, skipping
    relocated dwords. Offset 0 is always included (anchors the scan).
    `allow_fewer` permits tiny mostly-relocated functions (thin call stubs)
    to yield fewer pairs -- only safe for XRef signatures, where the rel32
    call-target pair carries the discrimination."""
    banned = set()
    for r in fn.reloc_offsets:
        banned.update(range(r, r + 4))
    limit = min(len(fn.data) - 1, max_offset)

    usable = [o for o in range(limit + 1) if o not in banned]
    if len(usable) < want:
        if allow_fewer and usable:
            want = len(usable)
        else:
            sys.exit(f"{fn.name}: only {len(usable)} usable offsets, need {want}")

    picked = [0] if 0 in usable else [usable[0]]
    # Ideal evenly-spaced targets; snap each to the nearest usable offset.
    for k in range(1, want):
        target = k * limit // (want - 1)
        best = min(usable, key=lambda o: abs(o - target))
        if best not in picked:
            picked.append(best)
    # Top up if snapping collided.
    for o in usable:
        if len(picked) >= want:
            break
        if o not in picked:
            picked.append(o)
    picked = sorted(picked[:want])
    return [(o, fn.data[o]) for o in picked]


def render(name: str, pairs: list[tuple[int, int]], save_index: str = "-1",
           xref_pairs: list[tuple[int, str]] | None = None) -> str:
    """Emit an OOVPA initializer. `xref_pairs` (offset, XREF enum name) come
    first per the matcher's convention (pairs [0..XRefCount) are XRefs);
    `save_index` is an XREF enum name for save-signatures, "-1" otherwise."""
    xref_pairs = xref_pairs or []
    count = len(xref_pairs) + len(pairs)
    max_off = max([o for o, _ in pairs] + [o for o, _ in xref_pairs])
    if pairs[-1][0] != max_off:
        sys.exit(f"{name}: last byte pair must hold the max offset "
                 f"(matcher uses Sovp[count-1].Offset as the scan bound)")
    large = max_off > 0xFF
    kind = "LOOVPA" if large else "SOOVPA"
    lines = [f"{kind}<{count}> {name} =", "{",
             f"    {int(large)}, {count}, {save_index}, {len(xref_pairs)},", "    {"]
    rows = [f"        {{ 0x{o:02X}, {v} }}" for o, v in xref_pairs]
    rows += [f"        {{ 0x{o:02X}, 0x{v:02X} }}" for o, v in pairs]
    return "\n".join(lines) + "\n" + ",\n".join(rows) + "\n    }\n};\n"


# --------------------------------------------------------------------------- #
# Verification
# --------------------------------------------------------------------------- #


def flat_image(path: Path) -> bytes:
    """PE files are laid out by section RVA (that is what the HLE scan sees);
    anything else (XBE, raw) is scanned as file bytes."""
    data = path.read_bytes()
    if data[:2] != b"MZ":
        return data
    (e_lfanew,) = struct.unpack("<I", data[0x3C:0x40])
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        return data
    coff = e_lfanew + 4
    n_sections, = struct.unpack("<H", data[coff + 2 : coff + 4])
    opt_size, = struct.unpack("<H", data[coff + 16 : coff + 18])
    (size_of_image,) = struct.unpack("<I", data[coff + 20 + 56 : coff + 20 + 60])
    img = bytearray(size_of_image)
    sh0 = coff + 20 + opt_size
    for s in range(n_sections):
        sh = data[sh0 + 40 * s : sh0 + 40 * (s + 1)]
        vsize, va, rawsize, rawptr = struct.unpack("<IIII", sh[8:24])
        n = min(rawsize, vsize if vsize else rawsize)
        img[va : va + n] = data[rawptr : rawptr + n]
    return bytes(img)


def count_matches(img: bytes, pairs: list[tuple[int, int]]) -> int:
    last = pairs[-1][0]
    n = 0
    end = len(img) - last
    first_off, first_val = pairs[0]
    pos = img.find(bytes([first_val]), first_off)
    # Anchor on the first pair's byte for speed.
    while 0 <= pos < end + first_off:
        base = pos - first_off
        if base >= 0 and all(img[base + o] == v for o, v in pairs):
            n += 1
        pos = img.find(bytes([first_val]), pos + 1)
    return n


def find_matches(img: bytes, pairs: list[tuple[int, int]], limit: int = 64) -> list[int]:
    out = []
    end = len(img) - pairs[-1][0]
    for base in range(end):
        if all(img[base + o] == v for o, v in pairs):
            out.append(base)
            if len(out) >= limit:
                break
    return out


def count_xref_matches(img: bytes, pairs: list[tuple[int, int]],
                       xref_off: int, internal_addr: int) -> int:
    """Simulate the engine's XRef matching: among all byte-pair matches, count
    those whose rel32 operand at xref_off resolves to internal_addr."""
    n = 0
    for base in find_matches(img, pairs):
        if base + xref_off + 4 > len(img):
            continue
        rel = int.from_bytes(img[base + xref_off : base + xref_off + 4], "little")
        if (rel + base + xref_off + 4) & 0xFFFFFFFF == internal_addr:
            n += 1
    return n


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #


def main() -> int:
    ap = argparse.ArgumentParser(description="Generate Cxbx OOVPA signatures from an XDK .lib")
    ap.add_argument("--lib", required=True, help="XDK static library (e.g. d3d8.lib)")
    ap.add_argument(
        "--func", action="append", default=[], metavar="SYMBOL=SIGNAME",
        help="decorated symbol and the OOVPA variable name to emit (repeatable)",
    )
    ap.add_argument(
        "--xref-func", action="append", default=[],
        metavar="SYMBOL=WRAPSIG:INTSIG:XREF_ENUM",
        help="thin-wrapper symbol whose body is byte-identical to its siblings: "
             "auto-discovers the internal function it calls (single REL32), "
             "emits an XRef-save signature for the internal (INTSIG, saved to "
             "XREF_ENUM) plus an XRef-consuming signature for the wrapper "
             "(WRAPSIG, first pair = rel32 operand vs XREF_ENUM) (repeatable)",
    )
    ap.add_argument(
        "--verify-one", action="append", default=[],
        help="binary that MUST contain the function exactly once (repeatable)",
    )
    ap.add_argument(
        "--verify", action="append", default=[],
        help="glob of binaries that must match at most once (repeatable)",
    )
    ap.add_argument("--pairs", type=int, default=8, help="pairs per signature (default 8)")
    ap.add_argument("--max-offset", type=lambda v: int(v, 0), default=0xFF,
                    help="highest offset considered (default 0xFF -> SOOVPA)")
    ap.add_argument("--out", help="write the .inl snippet here (default stdout)")
    args = ap.parse_args()

    lib = Path(args.lib).read_bytes()
    symbols = _first_linker_member(lib)

    must = [Path(p) for p in args.verify_one]
    may = [Path(p) for g in args.verify for p in glob.glob(g, recursive=True)]
    must_imgs = [(p, flat_image(p)) for p in must]
    may_imgs = [(p, flat_image(p)) for p in may]

    if not args.func and not args.xref_func:
        sys.exit("need at least one --func or --xref-func")

    snippets = []
    failed = False
    for spec in args.func:
        sym, _, signame = spec.partition("=")
        if not signame:
            sys.exit(f"--func needs SYMBOL=SIGNAME, got: {spec}")
        fn = extract_function(lib, symbols, sym)
        pairs = pick_pairs(fn, args.pairs, args.max_offset)

        ok = True
        for p, img in must_imgs:
            n = count_matches(img, pairs)
            if n != 1:
                print(f"FAIL {signame}: {n} matches in {p.name} (need exactly 1)")
                ok = False
                failed = True
        for p, img in may_imgs:
            n = count_matches(img, pairs)
            if n > 1:
                print(f"FAIL {signame}: {n} matches in {p.name} (collision)")
                ok = False
                failed = True
        if ok:
            print(f"OK   {signame}: {sym} ({len(fn.data)} bytes, "
                  f"{len(fn.reloc_offsets)} relocs, unique in {len(must_imgs) + len(may_imgs)} images)")
            snippets.append(f"// {sym} ({Path(args.lib).name}, {len(fn.data)} bytes)\n" + render(signame, pairs))

    def sig_with_suffix(signame: str, suffix: str) -> str:
        return (signame.replace("_1_0_", f"{suffix}_1_0_")
                if "_1_0_" in signame else signame + suffix)

    def resolve_chain(sym: str, signame: str, enum_name: str,
                      target_match: str | None, depth: int, quiet: bool = False):
        """Build the XRef chain for `sym` bottom-up. Returns
        (snippets, addr_by_must_image, addr_by_may_image, enum_names) or None
        on failure. A level that is byte-unique ends the chain; a colliding
        level is discriminated by one of its REL32 callees, tried tail-first
        with backtracking (the tail call is usually the real worker; shared
        helpers like DirectSoundEnterCriticalSection fail verification and
        are skipped naturally)."""
        if depth > 4:
            if not quiet:
                print(f"FAIL {signame}: XRef chain deeper than 4 levels")
            return None
        # The archive can hold multiple differently-compiled copies of the
        # symbol; the linker's pick need not be the archive index's. Try each
        # copy until one verifies against the images.
        copies = extract_function_all(lib, symbols, sym)
        result = None
        for fn in copies:
            result = _resolve_chain_copy(fn, signame, enum_name, target_match, depth)
            if result is not None:
                return result
        if not quiet:
            print(f"FAIL {signame}: no archive copy of {sym} "
                  f"({len(copies)} tried) verified against the images")
        return None

    def _resolve_chain_copy(fn: Function, signame: str, enum_name: str,
                            target_match: str | None, depth: int):
        sym = fn.name
        pairs = pick_pairs(fn, args.pairs, args.max_offset)

        must_hits = {p: find_matches(img, pairs, limit=3) for p, img in must_imgs}
        may_hits = {p: find_matches(img, pairs, limit=3) for p, img in may_imgs}
        byte_unique = (all(len(h) == 1 for h in must_hits.values())
                       and all(len(h) <= 1 for h in may_hits.values()))

        if byte_unique:
            snip = (f"// {sym} ({Path(args.lib).name}, {len(fn.data)} bytes)\n"
                    f"// XRef-save signature (chain leaf, byte-unique).\n"
                    + render(signame, pairs, save_index=enum_name))
            return ([snip],
                    {p: h[0] for p, h in must_hits.items()},
                    {p: h[0] for p, h in may_hits.items() if h},
                    [enum_name])

        # Not byte-unique: discriminate by a callee, tail call first.
        calls = list(reversed(fn.rel32_calls))
        if target_match is not None:
            calls = [c for c in calls if target_match in c[1]]
        if not calls:
            return None

        for xref_off, callee_sym in calls:
            child = resolve_chain(callee_sym, sig_with_suffix(signame, "T"),
                                  enum_name + "_T", None, depth + 1, quiet=True)
            if child is None:
                continue
            child_snips, child_must, child_may, child_enums = child

            must_addr = {}
            ok = True
            for p, img in must_imgs:
                cands = [b for b in must_hits[p]
                         if (int.from_bytes(img[b + xref_off : b + xref_off + 4], "little")
                             + b + xref_off + 4) & 0xFFFFFFFF == child_must[p]]
                if len(cands) != 1:
                    ok = False
                    break
                must_addr[p] = cands[0]
            if not ok:
                continue
            may_addr = {}
            for p, img in may_imgs:
                if p not in child_may:
                    continue
                cands = [b for b in may_hits[p]
                         if (int.from_bytes(img[b + xref_off : b + xref_off + 4], "little")
                             + b + xref_off + 4) & 0xFFFFFFFF == child_may[p]]
                if len(cands) > 1:
                    ok = False
                    break
                if cands:
                    may_addr[p] = cands[0]
            if not ok:
                continue

            snip = (f"// {sym} ({Path(args.lib).name}, {len(fn.data)} bytes; "
                    f"call@0x{xref_off:02X} {callee_sym} -> {enum_name}_T)\n"
                    f"// XRef chain level: saved to {enum_name}, discriminated by callee.\n"
                    + render(signame, pairs, save_index=enum_name,
                             xref_pairs=[(xref_off, enum_name + "_T")]))
            return (child_snips + [snip], must_addr, may_addr,
                    child_enums + [enum_name])

        return None

    all_chain_enums: list[str] = []
    for spec in args.xref_func:
        sym, _, rest = spec.partition("=")
        parts = rest.split(":")
        if len(parts) not in (3, 4):
            sys.exit(f"--xref-func needs SYMBOL=WRAPSIG:INTSIG:XREF_ENUM[:TARGETMATCH], got: {spec}")
        wrapsig, intsig, xref_enum = parts[:3]
        target_match = parts[3] if len(parts) == 4 else None

        wrapper = extract_function(lib, symbols, sym)
        calls = wrapper.rel32_calls
        if target_match is not None:
            calls = [c for c in calls if target_match in c[1]]
        if len(calls) != 1:
            sys.exit(f"{sym}: need exactly 1 discriminating REL32 call"
                     f"{f' matching {target_match!r}' if target_match else ''}, found "
                     f"{len(calls)}: {calls or wrapper.rel32_calls}")
        xref_off, internal_sym = calls[0]

        chain = resolve_chain(internal_sym, intsig, xref_enum, None, 1)
        if chain is None:
            failed = True
            continue
        chain_snips, int_must, int_may, chain_enums = chain

        wrap_pairs = pick_pairs(wrapper, args.pairs, args.max_offset, allow_fewer=True)
        ok = True
        for p, img in must_imgs:
            n = count_xref_matches(img, wrap_pairs, xref_off, int_must[p])
            if n != 1:
                print(f"FAIL {wrapsig}: {n} xref-qualified matches in {p.name} (need exactly 1)")
                ok = False
                failed = True
        for p, img in may_imgs:
            if p in int_may:
                n = count_xref_matches(img, wrap_pairs, xref_off, int_may[p])
                if n > 1:
                    print(f"FAIL {wrapsig}: {n} xref-qualified matches in {p.name} (collision)")
                    ok = False
                    failed = True
        if ok:
            depth_note = f" (chain depth {len(chain_enums)})" if len(chain_enums) > 1 else ""
            print(f"OK   {wrapsig}: {sym} ({len(wrapper.data)} bytes) "
                  f"-> call@0x{xref_off:02X} {internal_sym} [{xref_enum}]{depth_note}")
            all_chain_enums.extend(chain_enums)
            snippets.extend(chain_snips)
            snippets.append(
                f"// {sym} ({Path(args.lib).name}, {len(wrapper.data)} bytes; "
                f"call@0x{xref_off:02X} -> {xref_enum})\n"
                + render(wrapsig, wrap_pairs, xref_pairs=[(xref_off, xref_enum)]))

    if all_chain_enums:
        print("\nREQUIRED XREF ENUM ENTRIES (add to HLEDataBase.h + .cpp array):")
        for e in all_chain_enums:
            print(f"    {e}")

    text = "\n".join(snippets)
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8")
    else:
        print("\n" + text)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
