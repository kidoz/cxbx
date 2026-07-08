# Todo

This list captures historical project tasks and open engineering topics. Current
bring-up work should be tracked through the Meson build, conformance probes, and
issue/PR workflow.

Priority markers from the original file:

- `high` was marked with `*`
- `medium` was marked with `+`

## High Priority

- Go lower level with mutants, possibly using `\??\` and setting the root
  directory to null.
- Investigate `DirectSoundUseFullHRTF -> 0x192D8` in Halo; the target may be too
  small to detect.
- Actually use the palette rather than only allowing palette paths to execute.
- Check whether 4361 `Resource8_Release` matches 4627, 3925, and 3911.
- When focus is lost, enumerate and pause all threads except the input thread and
  any other critical threads. Another option is to keep thread handles in a list
  and suspend them when necessary.
- Add a configurable dead zone for touchy controllers.
- Investigate the push-buffer size global; it may be important.
- Improve global-variable detection with function/offset probe pairs so a title
  that does not link one function still has other chances to locate the variable.
  If none are found, set the global pointer to null and make references handle
  null safely.
- Track temporary `X_D3DResource` handles and periodically garbage collect them.
  Garbage-collection frequency can be a core configuration option.
- Avoid re-unswizzling on every `Register()` call if possible. Cache the address
  or maintain a global table, then update the data pointer to the matching
  `IDirect3DTexture8` instance.
- Support cards that do not have 32-bit color.
- Debug the heap allocation crash at a lower level instead of intercepting the
  entire `Rtl` heap function set.
- Stabilize TLS.

## Medium Priority

- Closing a console should not terminate the entire process.
- Perfect timing for `KeTickCount`. This can be updated with the
  Xbox-never-sleeps behavior for higher precision than the current separate
  thread method.
- Add delete-after-emulation functionality.
- Use `SetDataFormat` instead of parsing device input by hand.
- Add batch configuration for all buttons.
- Reconsider whether configuration screens need to be modal windows.
- Add XBE file associations through user configuration, with options to run an
  XBE automatically or open it in the Cxbx main window. For this, `Cxbx.dll`
  should also be registered so a converted EXE can run from anywhere.

## Unprioritized Items

- Converted EXE files should use the Cxbx icon.
- When loading a file, menus and `WM_CLOSE` should be disabled and progress
  should be sent through a callback from core.
- Encapsulate recent files into a small class.
- Allow a logo bitmap to be added if one does not exist. This may require
  increasing `sizeof_headers`.
- Allocate `Xbe::m_Header` dynamically to make room for huge headers.
