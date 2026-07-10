# CXBX

[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue.svg)](LICENSE)
[![Build system: Meson](https://img.shields.io/badge/build-Meson-00ADD8.svg)](https://mesonbuild.com/)
[![C](https://img.shields.io/badge/C-C11-555555.svg)](meson.build)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](meson.build)
[![Target](https://img.shields.io/badge/target-Win32%20x86-lightgrey.svg)](cross/i686-windows-clang.ini)

CXBX is a classic original Xbox emulator. This tree contains the emulator,
the Win32 launcher/runtime code, bundled open-xdk support libraries, and a
modern Meson-based build/porting workspace.

This is the classic caustik CXBX codebase, not Cxbx-Reloaded. It is currently
being brought forward as a 32-bit Windows/x86 HLE emulator with reproducible
build files, structured debug traces, and an emulator conformance suite.

![CXBX emulating Dolphin demos](docs/screenshots/dolphin-demo.png)

![CXBX emulating NestopiaX 1.3](docs/screenshots/nestopiax-v1.3.png)

## Current Status

This repository is for development and emulator bring-up. It is not a polished
end-user release.

- Build system: Meson-only.
- Primary target: 32-bit Windows (`i686-pc-windows-msvc`) with clang/lld tooling.
- Emulation model: HLE loader/API bridge; guest x86 code runs directly on the host CPU.
- Graphics: Direct3D HLE plus a partial register-level NV2A model for MMIO/RAMIN/PFIFO/PGRAPH-method paths. This is not full NV2A rasterization.
- Debugging: structured `Emu`, `KTRACE|`, `NV2A|`, and conformance trace paths.
- Conformance: the in-tree probe suite currently tracks CPU, memory, file I/O, kernel, graphics, and NV2A behavior.

Known active work includes source portability, x86 inline assembly/toolchain
compatibility, kernel export coverage, contiguous-memory behavior, and graphics
bring-up.

## Quick Start

Install Meson, Ninja, clang/LLVM, and a Windows SDK-capable toolchain. The
emulator code is 32-bit x86 and should be configured with the provided cross
file:

```powershell
meson setup build-min --cross-file cross/i686-windows-clang.ini
meson compile -C build-min
```

If you use `just`:

```powershell
just build
```

`just build` expects `build-min` to already be configured.

Optional legacy USB stack:

```powershell
meson setup build-min --cross-file cross/i686-windows-clang.ini -Dbuild_xusb=true
meson compile -C build-min
```

## Development Commands

```powershell
just --list
just format
just lint
just build
```

Python tooling is managed with `uv`:

```powershell
uv sync --dev
```

## Conformance Suite

The conformance suite builds small self-checking Xbox executables and runs them
against an emulator. Each probe emits deterministic trace output and PASS/FAIL
checks.

```powershell
cd tools/xtest
python xtest.py list
python xtest.py run --emulator cxbx
python xtest.py run --emulator cxbx --probe cpu_flags
python xtest.py run --emulator cxbx --update-golden
```

The suite is emulator-agnostic; custom targets can be launched with:

```powershell
python xtest.py run --emulator custom --cmd "myemu --run {xbe} --dvd {rundir}"
```

Current probe areas include:

- CPU flags and native x86 behavior
- memory allocation and read/write checks
- FATX-style file I/O through `D:`
- kernel HLE coverage and unimplemented-export trapping
- framebuffer availability checks
- NV2A PMC, interrupt, RAMIN, PFIFO, and PGRAPH method paths

## Debugging

Prefer file-based logging for reproducible runs:

```powershell
$env:CXBX_LOG_FILE = "run.log"
```

Important trace channels:

- `Emu (0x<tid>): ...` for general emulator/runtime logs.
- `EmuFS (0x<tid>): ...` for FS-segment swaps.
- `KTRACE| ...` for kernel-thunk diagnostics.
- `NV2A| ...` for GPU MMIO/PFIFO/PGRAPH/RAMIN traces.
- `XT| ...` and `D:\<probe>.trace` for conformance probes.

The debug workflow is built around identifying whether a failure is an OOVPA
signature miss, a wrong wrapper patch, an unimplemented kernel export, an FS
swap imbalance, or an NV2A register/model divergence.

## Repository Layout

```text
3rdparty/dxsdk/              Bundled DirectX SDK headers
Bin/                         Legacy binary/output placeholders
docs/                        Project documentation, reference notes, screenshots
Lib/                         Bundled import libraries
PostBuild/                   Legacy post-build utilities
Resource/                    Win32 resources, icons, bitmaps, menus, dialogs
cross/                       Meson cross files
include/cxbx/include/        CXBX public/internal headers
include/open-xdk/include/    Bundled open-xdk headers
src/cxbx/                    Emulator launcher, core, and HLE runtime
src/open-xdk/src/            Bundled open-xdk support libraries
tests/                       Test assets and XBE probes
tools/xtest/                 Conformance-suite runner
```

## Architecture Notes

CXBX is an HLE emulator:

1. It loads an Xbox executable (`.xbe`).
2. It maps the guest image and prepares a kernel thunk table.
3. It identifies XDK library functions with OOVPA signatures.
4. It patches located guest functions to jump to host implementations.
5. Guest x86 code runs natively on the host CPU.

This makes most failures API-boundary failures rather than CPU interpreter
failures. Typical debugging targets are missing XDK signatures, wrong HLE
implementations, unimplemented kernel ordinals, FS-segment state, and GPU MMIO
state.

## Documentation

Useful starting points:

- [docs/README.md](docs/README.md) - documentation index.
- [docs/todo.md](docs/todo.md) - task list and open engineering topics.
- [docs/changelog.md](docs/changelog.md) - historical project changes.
- [docs/direct3d.md](docs/direct3d.md) - Direct3D notes.
- [docs/input.md](docs/input.md) - input notes.
- [tests/suite/README.md](tests/suite/README.md) - conformance suite guide.
- [tools/xtest/README.md](tools/xtest/README.md) - probe runner usage.

## Legal

CXBX is licensed under the GNU General Public License version 2 or later. See
[LICENSE](LICENSE).

This project does not include Xbox BIOS images, retail game data, or console
firmware. It does include legacy compatibility material such as DirectX headers
and import libraries; review the tree before redistributing binaries or bundles.
Use only software and game dumps that you are legally allowed to use. CXBX is
not affiliated with Microsoft.

## Contributing

Good contributions are narrow, testable, and preserve the current Meson build.
For emulator behavior changes, prefer adding or updating a conformance probe so
the expected behavior is captured in a repeatable way.

Before submitting changes:

```powershell
just format
just lint
meson compile -C build-min
```

If a build or test cannot be run on your host, call that out with the reason and
the target/toolchain you used.
