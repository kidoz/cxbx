#!/usr/bin/env python3
"""Decode NV2A register-combiner words into readable equations.

The operand/flag layout mirrors the in-tree translator
(include/cxbx/include/core/d3d_pixel_shader_translate.h), so the decode agrees
with what the emulator's pixel paths implement:

  ICW  (input):  byte3=A byte2=B byte1=C byte0=D; each byte is
                 reg[3:0], alpha-select bit4, mapping[7:5]
  OCW  (output): cd_dst[3:0] ab_dst[7:4] sum_dst[11:8], flags from bit 12:
                 cd_dot, ab_dot, mux, bias, shift[5:4], blue_to_alpha[7:6]
  Final (SPECULAR_FOG_CW0/CW1): CW0 holds A,B,C,D; CW1 holds E,F,G + flags.
                 out.rgb = A*B + (1-A)*C + D, out.a = G

Input forms:
  combiner.py f00001_d0003.txt              decode a draw-state sidecar
  combiner.py --icw 0x04200000 --ocw 0xC00  decode one stage pair
  combiner.py --final 0x1C80... 0x0000...   decode a final-combiner pair
  combiner.py --self-test
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REGISTERS: dict[int, str] = {
    0x0: "zero",
    0x1: "c0",
    0x2: "c1",
    0x3: "fog",
    0x4: "v0",  # primary (diffuse)
    0x5: "v1",  # secondary (specular)
    0x8: "t0",
    0x9: "t1",
    0xA: "t2",
    0xB: "t3",
    0xC: "r0",  # spare0
    0xD: "r1",  # spare1
    0xE: "v1r0_sum",  # final combiner only
    0xF: "ef_prod",  # final combiner only
}

MAPPINGS: dict[int, str] = {
    0: "",  # unsigned identity
    1: "1-{}",  # unsigned invert
    2: "2*{}-1",  # expand normal
    3: "-(2*{}-1)",  # expand negate
    4: "{}-0.5",  # halfbias normal
    5: "-({}-0.5)",  # halfbias negate
    6: "{}",  # signed identity
    7: "-{}",  # signed negate
}

MAPPING_NAMES: dict[int, str] = {
    0: "unsigned_identity",
    1: "unsigned_invert",
    2: "expand_normal",
    3: "expand_negate",
    4: "halfbias_normal",
    5: "halfbias_negate",
    6: "signed_identity",
    7: "signed_negate",
}

SHIFT_NAMES = {0x00: "", 0x10: " x2", 0x20: " x4", 0x30: " /2"}


def operand(word: int, slot: int) -> str:
    """Render one 8-bit operand (slot 0=A .. 3=D)."""
    byte = (word >> ((3 - slot) * 8)) & 0xFF
    reg = REGISTERS.get(byte & 0xF, f"reg{byte & 0xF}")
    if byte & 0x10:
        reg += ".a"
    mapping = (byte >> 5) & 0x7
    template = MAPPINGS[mapping]
    return template.format(reg) if template else reg


def operand_is_zero(word: int, slot: int) -> bool:
    byte = (word >> ((3 - slot) * 8)) & 0xFF
    return (byte & 0xF) == 0 and ((byte >> 5) & 0x7) in (0, 6)


def decode_stage(icw: int, ocw: int, portion: str) -> list[str]:
    """Decode one general-combiner stage portion (rgb or alpha)."""
    a, b, c, d = (operand(icw, slot) for slot in range(4))
    cd_dst = REGISTERS.get(ocw & 0xF, f"reg{ocw & 0xF}")
    ab_dst = REGISTERS.get((ocw >> 4) & 0xF, f"reg{(ocw >> 4) & 0xF}")
    sum_dst = REGISTERS.get((ocw >> 8) & 0xF, f"reg{(ocw >> 8) & 0xF}")
    flags = ocw >> 12
    ab = f"({a})dot({b})" if flags & 0x02 else f"({a})*({b})"
    cd = f"({c})dot({d})" if flags & 0x01 else f"({c})*({d})"
    shift = SHIFT_NAMES.get(flags & 0x30, " shift?")
    lines: list[str] = []
    if ab_dst != "zero":
        lines.append(f"{portion}: {ab_dst} = {ab}{shift}")
    if cd_dst != "zero":
        lines.append(f"{portion}: {cd_dst} = {cd}{shift}")
    if sum_dst != "zero":
        op = "mux(r0.a<0.5 ? CD : AB)" if flags & 0x04 else f"{ab} + {cd}"
        lines.append(f"{portion}: {sum_dst} = {op}{shift}")
    if flags & 0x08:
        lines.append(f"{portion}: (bias output)")
    if flags & 0xC0:
        lines.append(f"{portion}: (blue-to-alpha 0x{flags & 0xC0:02X})")
    if not lines:
        lines.append(f"{portion}: (no destination)")
    return lines


def decode_final(cw0: int, cw1: int) -> list[str]:
    a, b, c, d = (operand(cw0, slot) for slot in range(4))
    e, f, g = (operand(cw1, slot) for slot in range(3))
    flags = cw1 & 0xFF
    lines = [
        f"final: out.rgb = ({a})*({b}) + (1-({a}))*({c}) + ({d})",
        f"final: out.a   = {g}" + ("" if "." in g or g == "zero" else ".a"),
    ]
    if not (operand_is_zero(cw1, 0) and operand_is_zero(cw1, 1)):
        lines.append(f"final: ef_prod = ({e})*({f})")
    if flags:
        lines.append(f"final: flags=0x{flags:02X}")
    return lines


STAGE_RE = re.compile(
    r"^combiner\[(\d)\]\s+rgb_icw=0x([0-9A-Fa-f]+)\s+rgb_ocw=0x([0-9A-Fa-f]+)\s+"
    r"alpha_icw=0x([0-9A-Fa-f]+)\s+alpha_ocw=0x([0-9A-Fa-f]+)\s+"
    r"factor0=0x([0-9A-Fa-f]+)\s+factor1=0x([0-9A-Fa-f]+)"
)
CONTROL_RE = re.compile(
    r"^combiner\s+control=0x([0-9A-Fa-f]+)\s+shader_stage_program=0x([0-9A-Fa-f]+)\s+"
    r"final_cw0=0x([0-9A-Fa-f]+)\s+final_cw1=0x([0-9A-Fa-f]+)\s+final_mask=0x([0-9A-Fa-f]+)"
)


def decode_state_file(path: Path) -> list[str]:
    lines: list[str] = []
    stage_count = 1
    final_cw0 = final_cw1 = 0
    have_final = False
    stages: list[tuple[int, int, int, int, int, int, int]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        control = CONTROL_RE.match(line.strip())
        if control:
            control_word = int(control.group(1), 16)
            stage_count = max(control_word & 0xF, 1)
            final_cw0 = int(control.group(3), 16)
            final_cw1 = int(control.group(4), 16)
            have_final = int(control.group(5), 16) == 3
            continue
        stage = STAGE_RE.match(line.strip())
        if stage:
            stages.append(tuple(int(g, 16) for g in stage.groups()))  # type: ignore[arg-type]

    lines.append(f"stage count = {stage_count}")
    for index, rgb_icw, rgb_ocw, alpha_icw, alpha_ocw, factor0, factor1 in stages:
        if index >= stage_count:
            continue
        if not any((rgb_icw, rgb_ocw, alpha_icw, alpha_ocw)):
            lines.append(f"stage {index}: (all zero)")
            continue
        lines.append(f"stage {index}: factor0=0x{factor0:08X} factor1=0x{factor1:08X}")
        lines.extend("  " + text for text in decode_stage(rgb_icw, rgb_ocw, "rgb"))
        lines.extend("  " + text for text in decode_stage(alpha_icw, alpha_ocw, "alpha"))
    if have_final:
        lines.extend(decode_final(final_cw0, final_cw1))
    else:
        lines.append("final: not programmed (passthrough r0)")
    return lines


def self_test() -> int:
    # A = t0 (slot A byte at bits 24-31: reg=8), B = v0 (reg 4), C = D = zero;
    # sum -> r0. This is the classic "modulate texture by diffuse" stage.
    icw = (0x08 << 24) | (0x04 << 16)
    ocw = 0xC << 8
    got = decode_stage(icw, ocw, "rgb")
    expected = ["rgb: r0 = (t0)*(v0) + (zero)*(zero)"]
    if got != expected:
        print(f"self-test FAIL: {got!r} != {expected!r}", file=sys.stderr)
        return 1

    # Final combiner: A=r0 so out.rgb = r0 (B=1-zero=1, C=D=0), out.a = r0.a.
    cw0 = (0x0C << 24) | (0x20 << 16)  # A=r0, B=unsigned_invert(zero)=1
    cw1 = 0x1C << 8  # G byte (bits 8-15): reg=r0, alpha select
    got = decode_final(cw0, cw1)
    if "out.rgb = (r0)*(1-zero)" not in got[0] or "out.a   = r0.a" not in got[1]:
        print(f"self-test FAIL: {got!r}", file=sys.stderr)
        return 1

    print("self-test OK")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "input", nargs="?", type=Path, help="draw-state sidecar written by CXBX_NV2A_DUMP_DRAWS"
    )
    parser.add_argument("--icw", type=lambda v: int(v, 0), help="input combiner word")
    parser.add_argument("--ocw", type=lambda v: int(v, 0), help="output combiner word")
    parser.add_argument(
        "--alpha", action="store_true", help="label --icw/--ocw as the alpha portion"
    )
    parser.add_argument(
        "--final",
        nargs=2,
        type=lambda v: int(v, 0),
        metavar=("CW0", "CW1"),
        help="final combiner words",
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    printed = False
    if args.input is not None:
        for line in decode_state_file(args.input):
            print(line)
        printed = True
    if args.icw is not None or args.ocw is not None:
        portion = "alpha" if args.alpha else "rgb"
        for line in decode_stage(args.icw or 0, args.ocw or 0, portion):
            print(line)
        printed = True
    if args.final is not None:
        for line in decode_final(args.final[0], args.final[1]):
            print(line)
        printed = True
    if not printed:
        parser.error("give a state file, --icw/--ocw, --final, or --self-test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
