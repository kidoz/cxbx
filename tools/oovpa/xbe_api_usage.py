#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
xbe_api_usage - which library APIs does a title actually call?

An XDK title statically links its libraries, so there is no import table to read:
the D3D/DSOUND/XAPI entry points are just addresses inside the XBE, reached by
direct CALLs from the game's own code. This walks that edge.

For each library code section (D3D, DSOUND, XGRPH, ...), it finds every site OUTSIDE
that section which CALLs or JMPs into it. Those targets are the library's real entry
points -- the functions the game actually uses. Everything else in the section is
internal: helpers only ever reached from within the library, which the HLE never has
to patch because guest code never calls them directly.

That distinction is what makes the LTCG coverage problem tractable. tools/oovpa
can locate ~111 D3D functions, but a title only calls a fraction of them, and only
the called ones must be patched: once CreateDevice is HLE'd the guest's own D3D
globals are never initialised, so a called-but-unpatched function faults -- while an
uncalled one is harmless dead code.

Cross-reference with --located (addresses that gen_oovpa_ltcg.py resolved) to get
the exact gap: entry points the game calls that are NOT yet covered.

    python xbe_api_usage.py "other/games/Samurai Showdown V/default.xbe" \\
        [--section D3D] [--located addrs.txt] [--kernel]
"""

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

MD = Cs(CS_ARCH_X86, CS_MODE_32)
INT3 = 0xCC
BRANCH = ("call", "jmp")


class Xbe:
    def __init__(self, path: Path) -> None:
        d = path.read_bytes()
        if d[:4] != b"XBEH":
            sys.exit(f"{path.name}: not an XBE")
        self.path = path
        self.base, = struct.unpack_from("<I", d, 0x104)
        size, = struct.unpack_from("<I", d, 0x10C)
        nsec, = struct.unpack_from("<I", d, 0x11C)
        psec, = struct.unpack_from("<I", d, 0x120)
        self.kernel_thunk, = struct.unpack_from("<I", d, 0x158)

        img = bytearray(size)
        self.sections: dict[str, tuple[int, int]] = {}
        off = psec - self.base
        for i in range(nsec):
            h = d[off + 0x38 * i: off + 0x38 * (i + 1)]
            vaddr, vsize, raw, rawsize = struct.unpack_from("<IIII", h, 4)
            pname, = struct.unpack_from("<I", h, 0x14)
            end = d.find(b"\0", pname - self.base)
            name = d[pname - self.base:end].decode("ascii", "replace")
            n = min(rawsize, len(d) - raw)
            img[vaddr - self.base: vaddr - self.base + n] = d[raw: raw + n]
            self.sections[name] = (vaddr, vaddr + vsize)
        self.data = bytes(img)

    def va(self, off: int) -> int:
        return self.base + off

    def off(self, va: int) -> int:
        return va - self.base


def scan_calls(xbe: Xbe, target: tuple[int, int], exclude: tuple[int, int]):
    """Every (caller_va, target_va) where code outside `exclude` branches into
    `target`. Linear disassembly of an entire image mis-decodes some data, so a hit
    is only trusted when the target lands on a plausible function start -- i.e. the
    byte before it is int3 padding, or the target is 16-byte aligned (MSVC aligns
    every function entry). That filters the noise a raw rel32 scan would produce."""
    lo, hi = target
    ex_lo, ex_hi = exclude
    hits = []

    for name, (s_lo, s_hi) in xbe.sections.items():
        if s_lo >= ex_lo and s_hi <= ex_hi:
            continue                       # inside the library itself: not a caller
        blob = xbe.data[xbe.off(s_lo):xbe.off(s_hi)]
        for ins in MD.disasm(blob, s_lo):
            if ins.mnemonic not in BRANCH:
                continue
            op = ins.op_str.strip()
            if not op.startswith("0x"):
                continue
            try:
                tgt = int(op, 16)
            except ValueError:
                continue
            if not (lo <= tgt < hi):
                continue
            o = xbe.off(tgt)
            plausible = (tgt % 16 == 0) or (o > 0 and xbe.data[o - 1] == INT3)
            if plausible:
                hits.append((ins.address, tgt, name))
    return hits


def main() -> int:
    ap = argparse.ArgumentParser(description="Which library APIs does a title call?")
    ap.add_argument("xbe")
    ap.add_argument("--section", action="append", default=[],
                    help="library code section (default: every RWX/R-X library section)")
    ap.add_argument("--located", help="file of 0x-addresses already covered by an "
                                      "OOVPA table; reports the uncovered gap")
    ap.add_argument("--kernel", action="store_true",
                    help="also dump the kernel import ordinals the title links")
    args = ap.parse_args()

    xbe = Xbe(Path(args.xbe))
    print(f"{Path(args.xbe).parent.name}  base=0x{xbe.base:08X}")

    # Default to the XDK library sections (the game's own code lives in .text).
    LIBS = ["D3D", "DSOUND", "XGRPH", "XONLINE", "XNET", "XMV", "XACTENG", "XPP", "D3DX"]
    sections = args.section or [s for s in LIBS if s in xbe.sections]

    located = set()
    if args.located:
        for tok in Path(args.located).read_text().split():
            try:
                located.add(int(tok, 16))
            except ValueError:
                pass

    for sec in sections:
        if sec not in xbe.sections:
            continue
        lo, hi = xbe.sections[sec]
        hits = scan_calls(xbe, (lo, hi), (lo, hi))
        targets = Counter(t for _c, t, _s in hits)
        callers_by_sec = Counter(s for _c, _t, s in hits)

        print(f"\n=== section {sec}  0x{lo:08X}..0x{hi:08X}")
        print(f"    {len(targets)} distinct entry points called from outside, "
              f"{len(hits)} call sites")
        print(f"    callers: {dict(callers_by_sec)}")

        if located:
            covered = [t for t in targets if t in located]
            missing = [t for t in targets if t not in located]
            print(f"    COVERED by the OOVPA table : {len(covered)}")
            print(f"    NOT COVERED (must patch)   : {len(missing)}")
            if missing:
                print("    uncovered entry points, by call count:")
                for t in sorted(missing, key=lambda t: -targets[t]):
                    print(f"       0x{t:08X}  called {targets[t]:4d}x")
        else:
            print("    entry points by call count:")
            for t, n in targets.most_common(40):
                print(f"       0x{t:08X}  called {n:4d}x")

    if args.kernel:
        print(f"\n=== kernel thunk table @ 0x{xbe.kernel_thunk:08X} (XOR-encoded)")
        print("    (ordinals resolve at load; see cxbxdbg thunks)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
