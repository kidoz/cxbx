#!/usr/bin/env python3
# Kernel-ordinal regression guard.
#
# Cross-references src/.../kernel_thunk.cpp's KernelThunkTable[] against the
# authoritative Xbox kernel export ordinals (tools/kernelaudit/xboxkrnl_ordinals.csv,
# derived from nxdk's xboxkrnl.exe.def -- the ABI every title imports against).
#
# Catches the ordinal-misalignment bug class: if a thunk-table edit shifts a block
# of ordinals so they point at the wrong Emu function, every nxdk title's imports
# resolve wrong and the emulator crashes at guest startup with no obvious cause.
# This turns that into a precise, source-level failure naming the exact ordinal.
#
# Exit code 0 = table matches the ABI (modulo the curated ALLOWLIST); 1 = mismatch.
import csv, re, os, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
THUNK = os.path.join(ROOT, "src", "cxbx", "src", "win32", "CxbxKrnl", "kernel_thunk.cpp")
REF   = os.path.join(os.path.dirname(__file__), "xboxkrnl_ordinals.csv")

# Ordinals whose wired Emu symbol legitimately does not name-match the export
# (intentional Cxbx aliases / shared implementations). Each needs a reason.
ALLOWLIST = {
    # ordinal: "reason"
}

def derive_name(expr):
    """Emu symbol expression -> export name, or None if unimplemented (PANIC/stub)."""
    if "PANIC" in expr or "UnimplementedStub" in expr:
        return None
    s = expr.strip().lstrip("&").split("::")[-1]
    if s.startswith("g_"): s = s[2:]
    if s.startswith("Emu"): s = s[3:]
    if s.endswith("Export"): s = s[:-6]
    return s

def load_thunks():
    text = open(THUNK, encoding="utf-8", errors="replace").read()
    tbl = text[text.index("KernelThunkTable[367]"):]
    out = {}
    for m in re.finditer(r"\(uint32\)\s*(.+?),\s*//\s*0x[0-9A-Fa-f]+\s*\((\d+)\)", tbl):
        out[int(m.group(2))] = m.group(1).strip()
    return out

def matches(wired, expected):
    a, b = wired.lower(), expected.lower()
    return a == b or b in a or a in b

def main():
    thunks = load_thunks()
    ref = {}
    with open(REF, encoding="utf-8") as f:
        for row in csv.DictReader(l for l in f if not l.startswith("#")):
            ref[int(row["ordinal"])] = (row["name"], row["kind"])

    mismatches, unimplemented = [], 0
    for o in sorted(ref):
        name, kind = ref[o]
        expr = thunks.get(o, "<MISSING>")
        wired = derive_name(expr)
        if wired is None:
            unimplemented += 1
            continue
        if not matches(wired, name) and o not in ALLOWLIST:
            mismatches.append((o, name, expr))

    total = len(ref)
    print(f"kernel-thunk audit: {total} exports, {total-unimplemented-len(mismatches)} matched, "
          f"{unimplemented} unimplemented (PANIC), {len(mismatches)} MISMATCH")
    if mismatches:
        print("\nORDINAL MISMATCHES (thunk table disagrees with the Xbox ABI):")
        for o, name, expr in mismatches:
            print(f"  [{o:3d}] ABI expects '{name}' but ordinal is wired to: {expr}")
        print("\nA block of these usually means the thunk table's ordinals were shifted.")
        print("Fix the wiring, or (if intentional) add the ordinal to ALLOWLIST with a reason.")
        return 1
    print("OK: every ABI export ordinal is wired to the matching Emu function (or a stub).")
    return 0

if __name__ == "__main__":
    sys.exit(main())
