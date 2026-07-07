# kernelaudit — kernel-ordinal regression guard

Static source check that the emulator's `KernelThunkTable[]`
(`src/cxbx/src/win32/CxbxKrnl/KernelThunk.cpp`) matches the **authoritative Xbox
kernel export ordinals** — the ABI every title imports against.

## Why

Each guest kernel call resolves an ordinal through `KernelThunkTable[ordinal]`.
If a table edit shifts a block of ordinals so they point at the wrong `Emu`
function, *every* nxdk/XDK title's imports resolve wrong and the emulator
crashes at guest startup — with no obvious cause (all conformance probes fail
identically). This check turns that whole failure class into a precise,
source-level error naming the exact ordinal, before anything is built or run.

## Usage

```
python tools/kernelaudit/check_kernel_thunks.py     # exit 0 = OK, 1 = mismatch
```

It runs automatically as the first (fail-fast) step of `python tools/xtest/xtest.py gate`
(and therefore in CI, `.github/workflows/conformance.yml`).

## Files

- `check_kernel_thunks.py` — the check. Parses the thunk table + the reference,
  fuzzy-matches each ordinal's wired `Emu` symbol against the ABI name, reports
  mismatches. A curated `ALLOWLIST` covers any intentional Cxbx aliases.
- `xboxkrnl_ordinals.csv` — the committed authoritative `ordinal,name,kind`
  reference (self-contained, so the check needs no external SDK/nxdk install).
- `make_ref.py` — regenerates the CSV from nxdk's `xboxkrnl.exe.def`
  (run only when updating the reference; requires a local nxdk).
