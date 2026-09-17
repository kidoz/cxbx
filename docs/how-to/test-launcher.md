# Verify launcher changes

[How-to guides](README.md)

Start with a [configured build](build.md). Replace the illustrative build
directory with your own. The UI suite requires a Windows desktop session.

```powershell
$buildDir = 'C:/cxbx-work/build'
meson compile -C $buildDir
meson test -C $buildDir --print-errorlogs
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
