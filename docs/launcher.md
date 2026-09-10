# CXBX launcher

The Windows x86 launcher uses the pinned NodalKit 0.2.0 library. Build it with
Meson 1.11 or later and a C++23-capable Clang/Windows SDK toolchain. NodalKit
is a Meson wrap (`subprojects/nodalkit.wrap`, git-pinned to a commit); the
first configure clones it into `subprojects/nodalkit`, and the wrap's diff file
under `subprojects/packagefiles/` drops its examples, tools, and tests so a
production build never fetches their dependencies. NodalKit is MIT licensed;
CXBX remains GPL-2.0-or-later.

Run `build-min/src/cxbx/cxbx.exe`, open an XBE, and press **F5** or **Start**.
The guest runs in its own process and render window. Closing that window stops
the guest; the launcher reports its exit code. Closing the launcher while a
guest is running asks whether to leave the guest running.

## Files and settings

- File: open/close/save XBE, import/export EXE, and the ten most recent files
  of each type. Unsaved documents prompt to save, discard, or cancel.
- Edit: import/export a 100 × 17 uncompressed 24-bit BMP logo, toggle the 64 MB
  limit or debug/retail flags, and dump XBE information.
- Settings: video adapter, device, resolution, fullscreen and VSync; controller
  mapping with cancellable five-second input capture; generated EXE location
  and launcher/kernel debug output. Cancel discards a settings working copy.
- View: clear or export the displayed log. File-based guest output is streamed
  into a bounded view retaining 4,000 lines. Console output remains available.

Existing `HKCU\Software\cxbx` preferences and recent-file lists are retained.
Video/controller settings continue through the shared-runtime configuration
facades. Paths must fit the existing emulator's 260-byte Windows ANSI path
contract; the UI rejects paths that would lose characters during conversion.

## Automation

```powershell
build-min/src/cxbx/cxbx.exe --run "path with spaces/default.xbe" --log run.log
```

`--run` constructs no NodalKit application, waits for the guest, and returns its
exit code. A missing XBE argument returns 2; an open/conversion/launch error
returns 1. Without `--log`, batch output goes to `cxbx-run.log` beside the
launcher. A positional XBE starts the UI and launches using temporary EXE
generation, preserving the existing command-line behavior.

Both entry paths use the same conversion, DLL staging, guest working directory,
suspended process creation, Xbox RAM reservation, and MMIO aperture fences.
`CXBX_HOST_D3D8_DIR` and `CXBX_NO_HOST_D3D8` retain their existing meanings.

## Rendering and verification

NodalKit owns only the launcher window. Its C++23 target does not receive the
legacy DirectX SDK include paths. The emulator and launcher service remain
C++20, and the runtime DLL has no NodalKit dependency. The launcher uses D3D11
by default; `NK_RENDERER_BACKEND=software` selects the toolkit's software path.
Neither choice changes guest rendering or resolves guest driver crashes.
Video settings enumerate through the Windows system D3D8 module explicitly,
so an old guest graphics override beside the launcher is not initialized for
display enumeration.

```powershell
meson compile -C build-min
meson test -C build-min --print-errorlogs
```

`host-launcher-session` creates synthetic XBE/PE/BMP fixtures and checks document
error preservation, EXE import, patch and save/reopen PE equivalence, logo I/O,
and information dumps. `launcher-ui-smoke` and `launcher-ui-document` exercise
real windows, settings dialogs, Escape cancellation, and dirty-document
prompts. They suppress configuration persistence. A desktop session is required
for the `ui` suite; use `--no-suite ui` for host-only checks.

For a locally built self-exiting guest probe, `CXBX_UI_SMOKE_LAUNCH=1` together
with `--ui-smoke-test <probe.xbe> --log <log>` also checks the GUI launch and
exit flow through F5. `CXBX_UI_SMOKE_SCREENSHOT=<path.ppm>` captures the launcher
frame directly. This mode is intended for verification and discards in-memory edits.
Physical controller mapping and multi-monitor DPI transitions still require
interactive hardware validation. NodalKit's Windows APIs remain experimental.
