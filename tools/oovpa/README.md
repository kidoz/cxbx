# tools/oovpa — HLE signature tooling

Tools for authoring and verifying the OOVPA signatures that let the HLE layer
patch XDK library functions inside title images. The full authoring workflow
(generation, `.inl` registration, XbSymbolDatabase cross-check, gotchas) is
documented in the project's OOVPA workflow context; this README is the tool
reference.

## gen_oovpa.py — signature generator + verifier

Two modes:

- **Lib mode** (`--lib <d3d8.lib> --func SYMBOL=SIGNAME`): extract the body +
  relocations from an XDK COFF archive, pick relocation-free `(offset, byte)`
  pairs, verify uniqueness against `--verify-one` / `--verify` images, emit the
  `SOOVPA`/`LOOVPA` initializer. `--xref-func` builds XRef chains for
  byte-identical thin wrappers.
- **Image mode** (`--from-image <xbe> --va 0x... --name SIG`): when the shipped
  body differs from every archived lib (LTCG titles, relinked builds), derive
  the signature from the title's own bytes. A disassembler skips
  address-dependent operand bytes — **install capstone** (`python -m pip
  install capstone`); the fallback heuristic over-bans and usually cannot find
  enough usable offsets. `--save-index XREF_ENUM` emits the signature with an
  XRef save slot so XRef consumers can reference the matched address.

## xdklib.py — XDK corpus explorer + lib↔image matcher

Answers, in bulk, the questions the authoring workflow otherwise answers by
hand: which XDK versions ship a symbol, whether its body changed between
versions, where it lives in a title image, and where a shipped body diverges
from the archived one. Discovers every `XDK/xbox/lib` tree under
`other/xbox-sdks/extracted` and `other/sdk` automatically (cached in
`%LOCALAPPDATA%`; `--rescan` refreshes).

```powershell
python tools/oovpa/xdklib.py versions
python tools/oovpa/xdklib.py libs --version 5558.2
python tools/oovpa/xdklib.py find MakeSpace --lib d3d8 --bodies
python tools/oovpa/xdklib.py body  --version 5558 --lib d3d8 --sym "_D3DDevice_MakeSpace@0" --disasm
python tools/oovpa/xdklib.py diff  --lib d3d8 --sym "_D3DDevice_Swap@4" --versions 5455 5558
python tools/oovpa/xdklib.py match --version 5558 --lib d3d8 --sym "?SetFence@D3D@@YGKK@Z" `
    --image other/games/<title>/default.xbe --fuzzy
python tools/oovpa/xdklib.py map   --version 5558 --lib d3d8 `
    --image other/games/<title>/default.xbe --out title_d3d8.map
```

- `find --bodies` prints each body's size + masked CRC per version, so
  byte-identity groups across builds ("reuse vs generate") are visible at a
  glance.
- `match --fuzzy` anchors on several relocation-free runs and reports each
  candidate's first-divergence offset — the "shipped body diverges from the
  lib at +0xNN" fact that drives image-derived signatures, automated.
- `map` matches **every** code symbol of a lib against an image (FLIRT-style)
  and cross-checks the result against itself: every rel32 call edge between two
  mapped functions must encode the callee's mapped VA. Inconsistent edges are
  flagged `!!`. Typical run: a 5558 `d3d8.lib` vs a 3 MB title ≈ 0.4 s.
  The map feeds `gen_oovpa --from-image --va`, `cxbxdbg` investigations, and
  disassembly sessions with real names.

All matching masks relocated dwords, so link-time addresses never anchor or
break a comparison. XBE images are laid out by section vaddr (reported
addresses are guest VAs); PE by section RVA; anything else is scanned raw.

Known limit: `*ltcg.lib` archives (d3d8ltcg, dmusicltcg, xactengltcg, …)
contain MSVC LTCG **intermediate language** (anonymous objects, `00 00 FF FF`
signature), not machine code — there is nothing to byte-match. LTCG-compiled
title code is reachable only through image-derived signatures.

Self-test: `python tools/oovpa/xdklib.py --self-test`.

## scan_oovpa.py / xbe_api_usage.py

- `scan_oovpa.py` — run the in-tree OOVPA tables against an XBE and report
  OK/MISS/ambiguous per signature (coverage report; feeds
  `cxbxdbg symbols --missing`).
- `xbe_api_usage.py` — find every call/jmp edge from game code into each
  statically-linked library section: the entry points the title actually uses
  (and therefore the only ones HLE must patch).
