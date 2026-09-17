# Launcher reference

[Reference](README.md)

For launch steps, see [Launch an Xbox title](../how-to/launch-title.md).

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

`--run` constructs no NodalKit application, waits for the guest, and returns its
exit code. A missing XBE argument returns 2; an open/conversion/launch error
returns 1. Without `--log`, batch output goes to `cxbx-run.log` beside the
launcher. A positional XBE starts the UI and launches using temporary EXE
generation, preserving the existing command-line behavior.

`CXBX_HOST_D3D8_DIR` and `CXBX_NO_HOST_D3D8` control host D3D8 overrides.

For renderer ownership, see [Launcher architecture](../explanation/launcher.md).
