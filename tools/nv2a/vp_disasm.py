#!/usr/bin/env python3
"""Disassemble NV2A vertex-program microcode (KELVIN transform programs).

Instructions are 4 x 32-bit words. The field layout mirrors the emulator's
interpreter (src/cxbx/src/win32/CxbxKrnl/EmuVshDecoder.cpp, itself from nxdk's
vp20compiler), so what this prints is exactly what the software rasterizer
executes.

Input forms:
  vp_disasm.py f00001_d0003.txt      state sidecar written by CXBX_NV2A_DUMP_DRAWS
  vp_disasm.py program.hex           4 hex dwords per line (whitespace separated)
  vp_disasm.py --self-test
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# Field -> (subtoken, start bit, bit length). Order matches EmuVshDecoder.cpp.
FIELDS: dict[str, tuple[int, int, int]] = {
    "ILU": (1, 25, 3),
    "MAC": (1, 21, 4),
    "CONST": (1, 13, 8),
    "V": (1, 9, 4),
    "A_NEG": (1, 8, 1),
    "A_SWZ_X": (1, 6, 2),
    "A_SWZ_Y": (1, 4, 2),
    "A_SWZ_Z": (1, 2, 2),
    "A_SWZ_W": (1, 0, 2),
    "A_R": (2, 28, 4),
    "A_MUX": (2, 26, 2),
    "B_NEG": (2, 25, 1),
    "B_SWZ_X": (2, 23, 2),
    "B_SWZ_Y": (2, 21, 2),
    "B_SWZ_Z": (2, 19, 2),
    "B_SWZ_W": (2, 17, 2),
    "B_R": (2, 13, 4),
    "B_MUX": (2, 11, 2),
    "C_NEG": (2, 10, 1),
    "C_SWZ_X": (2, 8, 2),
    "C_SWZ_Y": (2, 6, 2),
    "C_SWZ_Z": (2, 4, 2),
    "C_SWZ_W": (2, 2, 2),
    "C_R_HIGH": (2, 0, 2),
    "C_R_LOW": (3, 30, 2),
    "C_MUX": (3, 28, 2),
    "OUT_MAC_MASK": (3, 24, 4),
    "OUT_R": (3, 20, 4),
    "OUT_ILU_MASK": (3, 16, 4),
    "OUT_O_MASK": (3, 12, 4),
    "OUT_ORB": (3, 11, 1),
    "OUT_ADDRESS": (3, 3, 8),
    "OUT_MUX": (3, 2, 1),
    "A0X": (3, 1, 1),
    "FINAL": (3, 0, 1),
}

MAC_NAMES = [
    "nop",
    "mov",
    "mul",
    "add",
    "mad",
    "dp3",
    "dph",
    "dp4",
    "dst",
    "min",
    "max",
    "slt",
    "sge",
    "arl",
]
ILU_NAMES = ["nop", "mov", "rcp", "rcc", "rsq", "exp", "log", "lit"]

# Which sources each MAC op consumes (ADD reads A and C on the NV2A; the ILU
# always reads source C).
MAC_SOURCES: dict[str, str] = {
    "nop": "",
    "mov": "a",
    "mul": "ab",
    "add": "ac",
    "mad": "abc",
    "dp3": "ab",
    "dph": "ab",
    "dp4": "ab",
    "dst": "ab",
    "min": "ab",
    "max": "ab",
    "slt": "ab",
    "sge": "ab",
    "arl": "a",
}

PARAM_R, PARAM_V, PARAM_C = 1, 2, 3
OUTPUT_NAMES: dict[int, str] = {
    0: "oPos",
    3: "oD0",
    4: "oD1",
    5: "oFog",
    6: "oPts",
    7: "oB0",
    8: "oB1",
    9: "oT0",
    10: "oT1",
    11: "oT2",
    12: "oT3",
}
COMPONENTS = "xyzw"


def field(words: tuple[int, int, int, int], name: str) -> int:
    subtoken, start, length = FIELDS[name]
    return (words[subtoken] >> start) & ((1 << length) - 1)


def _swizzle(values: tuple[int, int, int, int]) -> str:
    text = "".join(COMPONENTS[v & 3] for v in values)
    if text == "xyzw":
        return ""
    if text == text[0] * 4:
        return f".{text[0]}"
    return f".{text}"


def _mask(bits: int) -> str:
    # NV2A write mask: bit3=x .. bit0=w.
    if bits == 0xF:
        return ""
    return "." + "".join(COMPONENTS[i] for i in range(4) if bits & (8 >> i))


def _source(words: tuple[int, int, int, int], which: str, relative: bool) -> str:
    mux = field(words, f"{which}_MUX")
    neg = "-" if field(words, f"{which}_NEG") else ""
    swz = _swizzle(
        (
            field(words, f"{which}_SWZ_X"),
            field(words, f"{which}_SWZ_Y"),
            field(words, f"{which}_SWZ_Z"),
            field(words, f"{which}_SWZ_W"),
        )
    )
    if which == "C":
        reg = (field(words, "C_R_HIGH") << 2) | field(words, "C_R_LOW")
    else:
        reg = field(words, f"{which}_R")
    if mux == PARAM_R:
        base = f"r{reg}"
    elif mux == PARAM_V:
        base = f"v{field(words, 'V')}"
    elif mux == PARAM_C:
        const = field(words, "CONST")
        base = f"c[A0.x+{const}]" if relative else f"c{const}"
    else:
        base = f"invalid{mux}"
    return f"{neg}{base}{swz}"


def disassemble_instruction(words: tuple[int, int, int, int]) -> list[str]:
    """One microcode instruction -> zero or more assembly lines."""
    lines: list[str] = []
    mac = field(words, "MAC")
    ilu = field(words, "ILU")
    relative = field(words, "A0X") != 0
    out_mux_is_ilu = field(words, "OUT_MUX") != 0

    mac_name = MAC_NAMES[mac] if mac < len(MAC_NAMES) else f"mac{mac}"
    ilu_name = ILU_NAMES[ilu] if ilu < len(ILU_NAMES) else f"ilu{ilu}"

    def sources(spec: str) -> str:
        return ", ".join(_source(words, s.upper(), relative) for s in spec)

    out_o_mask = field(words, "OUT_O_MASK")
    out_dest = ""
    if out_o_mask != 0:
        address = field(words, "OUT_ADDRESS")
        if field(words, "OUT_ORB") != 0:
            out_dest = OUTPUT_NAMES.get(address, f"o[{address}]")
        else:
            out_dest = f"c{address}"
        out_dest += _mask(out_o_mask)

    if mac != 0:
        spec = MAC_SOURCES.get(mac_name, "abc")
        dests: list[str] = []
        mac_mask = field(words, "OUT_MAC_MASK")
        if mac_name == "arl":
            dests.append("a0.x")
        elif mac_mask != 0:
            dests.append(f"r{field(words, 'OUT_R')}{_mask(mac_mask)}")
        if out_dest and not out_mux_is_ilu:
            dests.append(out_dest)
        if dests:
            lines.append(f"{mac_name} {' / '.join(dests)}, {sources(spec)}")

    if ilu != 0:
        dests = []
        ilu_mask = field(words, "OUT_ILU_MASK")
        if ilu_mask != 0:
            lines_dest = f"r1{_mask(ilu_mask)}"
            dests.append(lines_dest)
        if out_dest and out_mux_is_ilu:
            dests.append(out_dest)
        if dests:
            lines.append(f"{ilu_name} {' / '.join(dests)}, {sources('c')}")

    if not lines and (mac != 0 or ilu != 0):
        lines.append(f"; {mac_name}/{ilu_name} with no destination")
    return lines


def disassemble(program: list[tuple[int, int, int, int]]) -> list[str]:
    lines: list[str] = []
    for index, words in enumerate(program):
        text = disassemble_instruction(words)
        raw = " ".join(f"{w:08X}" for w in words)
        final = " (final)" if field(words, "FINAL") else ""
        if not text:
            lines.append(f"{index:3d}  {raw}  nop{final}")
        else:
            lines.append(f"{index:3d}  {raw}  {text[0]}{final}")
            for extra in text[1:]:
                lines.append(f"{'':3}  {'':35}  + {extra}")
    return lines


def parse_program(path: Path) -> list[tuple[int, int, int, int]]:
    """Read instructions from a draw-state sidecar (vp[i] lines) or raw hex."""
    program: list[tuple[int, int, int, int]] = []
    sidecar = re.compile(
        r"^vp\[\d+\]\s+([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s+"
        r"([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})\s*$"
    )
    words: list[int] = []
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    for line in lines:
        match = sidecar.match(line.strip())
        if match:
            program.append(tuple(int(g, 16) for g in match.groups()))  # type: ignore[arg-type]
    if program:
        # A state sidecar: the vp[] lines are authoritative, ignore other numbers.
        return program
    for line in lines:
        if "=" in line or line.lstrip().startswith("#"):
            continue
        for token in line.replace(",", " ").split():
            token = token.removeprefix("0x").removeprefix("0X")
            if re.fullmatch(r"[0-9A-Fa-f]{1,8}", token):
                words.append(int(token, 16))
    while len(words) >= 4:
        program.append((words[0], words[1], words[2], words[3]))
        words = words[4:]
    return program


def _pack(fields: dict[str, int]) -> tuple[int, int, int, int]:
    words = [0, 0, 0, 0]
    for name, value in fields.items():
        subtoken, start, length = FIELDS[name]
        words[subtoken] |= (value & ((1 << length) - 1)) << start
    return (words[0], words[1], words[2], words[3])


def self_test() -> int:
    # mov oPos.xyzw, v0.xyzw -- the canonical passthrough instruction.
    mov_opos = _pack(
        {
            "MAC": 1,  # MOV
            "A_MUX": PARAM_V,
            "V": 0,
            "A_SWZ_X": 0,
            "A_SWZ_Y": 1,
            "A_SWZ_Z": 2,
            "A_SWZ_W": 3,
            "OUT_O_MASK": 0xF,
            "OUT_ORB": 1,
            "OUT_ADDRESS": 0,
            "FINAL": 1,
        }
    )
    got = disassemble_instruction(mov_opos)
    expected = ["mov oPos, v0"]
    if got != expected:
        print(f"self-test FAIL: {got!r} != {expected!r}", file=sys.stderr)
        return 1

    # dp4 r2.x, v0, c96 -- a matrix-row transform.
    dp4 = _pack(
        {
            "MAC": 7,  # DP4
            "A_MUX": PARAM_V,
            "V": 0,
            "A_SWZ_X": 0,
            "A_SWZ_Y": 1,
            "A_SWZ_Z": 2,
            "A_SWZ_W": 3,
            "B_MUX": PARAM_C,
            "CONST": 96,
            "B_SWZ_X": 0,
            "B_SWZ_Y": 1,
            "B_SWZ_Z": 2,
            "B_SWZ_W": 3,
            "OUT_MAC_MASK": 0x8,  # x
            "OUT_R": 2,
        }
    )
    got = disassemble_instruction(dp4)
    expected = ["dp4 r2.x, v0, c96"]
    if got != expected:
        print(f"self-test FAIL: {got!r} != {expected!r}", file=sys.stderr)
        return 1

    # rsq r1.x, r2.x with paired output write via ILU mux.
    rsq = _pack(
        {
            "ILU": 4,  # RSQ
            "C_MUX": PARAM_R,
            "C_R_HIGH": 0,
            "C_R_LOW": 2,
            "C_SWZ_X": 0,
            "C_SWZ_Y": 0,
            "C_SWZ_Z": 0,
            "C_SWZ_W": 0,
            "OUT_ILU_MASK": 0x8,
        }
    )
    got = disassemble_instruction(rsq)
    expected = ["rsq r1.x, r2.x"]
    if got != expected:
        print(f"self-test FAIL: {got!r} != {expected!r}", file=sys.stderr)
        return 1

    print("self-test OK")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "input", nargs="?", type=Path, help="draw-state sidecar (.txt) or hex dword file"
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.input is None:
        parser.error("an input file (or --self-test) is required")

    program = parse_program(args.input)
    if not program:
        print("no vertex-program instructions found", file=sys.stderr)
        return 1
    for line in disassemble(program):
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
