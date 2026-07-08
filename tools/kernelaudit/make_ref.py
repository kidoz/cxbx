#!/usr/bin/env python3
# Generate the committed authoritative ordinal->name reference from nxdk's
# xboxkrnl.exe.def (the real Xbox kernel ABI). Handles the leading '@' fastcall
# decoration. Output is committed so the check is self-contained.
import re
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[1]
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from tool_config import config_path_value, load_config


def main() -> int:
    try:
        cfg = load_config(required=True)
        nxdk_dir = config_path_value(cfg, "paths", "nxdk_dir", required=True)
        source_def = config_path_value(
            cfg,
            "kernelaudit",
            "xboxkrnl_def",
            default=nxdk_dir / "lib" / "xboxkrnl" / "xboxkrnl.exe.def",
        )
        out_path = config_path_value(
            cfg,
            "kernelaudit",
            "ordinals_csv",
            default=Path(__file__).with_name("xboxkrnl_ordinals.csv"),
        )
    except (FileNotFoundError, KeyError, OSError) as e:
        print(e, file=sys.stderr)
        return 1

    if source_def is None or out_path is None:
        print("missing kernel-audit source or output path", file=sys.stderr)
        return 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    rows = {}
    for line in source_def.read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"\s*@?([A-Za-z_]\w*)(@\d+)?\s+@\s+(\d+)\s+NONAME(\s+DATA)?", line)
        if not m:
            continue
        ordinal = int(m.group(3))
        if 1 <= ordinal <= 366:
            rows[ordinal] = (m.group(1), "data" if m.group(4) else "func")
    with out_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# Authoritative Xbox kernel export ordinals (from nxdk xboxkrnl.exe.def).\n")
        f.write("# Regenerate with tools/kernelaudit/make_ref.py. Do not hand-edit.\n")
        f.write("ordinal,name,kind\n")
        for ordinal in sorted(rows):
            f.write(f"{ordinal},{rows[ordinal][0]},{rows[ordinal][1]}\n")
    print(f"wrote {out_path} with {len(rows)} ordinals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
