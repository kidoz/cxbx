#!/usr/bin/env python3
# Generate the committed authoritative ordinal->name reference from nxdk's
# xboxkrnl.exe.def (the real Xbox kernel ABI). Handles the leading '@' fastcall
# decoration. Output is committed so the check is self-contained.
import re, os
DEF = r"D:\projects\nxdk\lib\xboxkrnl\xboxkrnl.exe.def"
OUT = r"D:\projects\cxbx\tools\kernelaudit\xboxkrnl_ordinals.csv"
os.makedirs(os.path.dirname(OUT), exist_ok=True)
rows = {}
for line in open(DEF, encoding="utf-8", errors="replace"):
    m = re.match(r"\s*@?([A-Za-z_]\w*)(@\d+)?\s+@\s+(\d+)\s+NONAME(\s+DATA)?", line)
    if not m:
        continue
    o = int(m.group(3))
    if 1 <= o <= 366:
        rows[o] = (m.group(1), "data" if m.group(4) else "func")
with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("# Authoritative Xbox kernel export ordinals (from nxdk xboxkrnl.exe.def).\n")
    f.write("# Regenerate with tools/kernelaudit/make_ref.py. Do not hand-edit.\n")
    f.write("ordinal,name,kind\n")
    for o in sorted(rows):
        f.write(f"{o},{rows[o][0]},{rows[o][1]}\n")
print(f"wrote {OUT} with {len(rows)} ordinals")
