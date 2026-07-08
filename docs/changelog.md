# Changelog

This changelog records the original caustik CXBX release history and is
preserved as archival project context.

Original website: <http://www.caustik.com/xbox/>

## 0.7.8c (2003-09-02)

- Spontaneous `CreateDevice` failures fixed.
- EXE generation can now use the temporary directory, allowing games to run from
  read-only devices without path issues.
- `Cxbx.dll` and `Cxbx.exe` now enforce version synchronization.
- Very minor splash image tweaks.

## 0.7.8b (2003-08-30)

- Fixed debug messages accidentally left in the previous release.

## 0.7.8 (2003-08-29)

- Halo executes, with no graphics yet.
- Overlays simulated on PCs that do not support them in hardware.
- YUY2 overlay capabilities detection improved significantly.
- Mesh rendering fixes, credited to kingofc. The XRay XDK demo, Gamepad demo,
  and Rumble demo improved.
- `Z:` drive simulation repaired.

## 0.7.7b (2003-07-16)

- Fixed lost compatibility with X-Marbles and similar titles.

## 0.7.7 (2003-07-15)

- Turok Evolution displays startup graphics and intro sequence.
- Stella and a few other homebrew games are playable.
- Lower-level heap emulation was added, fixing glitches and bugs.
- Timing fixes increased FPS.

## 0.7.6 (2003-07-07)

- More homebrew apps show graphics.
- DirectInput bugs fixed.
- PointSprites works without source hacks.
- Code cleanup.

## 0.7.5 (2003-06-30)

- X-Marbles homebrew demo is playable.
- New GUI bitmap, credited to bot.
- DirectSound emulation began.
- Corrected converted EXE stack commit.
- Fixed a long-standing debugger attach problem.
- Advanced 4627 coverage.
- PointSprites, Gamepad demos, and other XDK samples run better.
- Added more Direct3D/XAPI emulation. Hunter: The Reckoning gets farther, but
  still without graphics.

## 0.7.4 (2003-06-23)

- First retail game graphics.
- Quad rendering.
- New XD3D emulation work.
- Additional demos play, including PointSprites, with known mipmap-filter issues.
- Fixed a user input bug that ignored digital buttons.

## 0.7.3 (2003-06-18)

- Meshes.
- Indexed primitive and vertex rendering.
- Part of the invisible texture problem was fixed.

## 0.7.2 (2003-06-13)

- Texture support for BMP, JPG, PNG, partial XPR, and related paths.
- `rtinit` and `cinit` run at a lower level.
- `stdio` appears to work well.
- Added advanced texture work, including TCI.

## 0.7.1 (2003-05-30)

- Video configuration.
- Fixed XBE change detection logic.
- Open XBE and Import EXE can be used when a file is already open. The current
  file is closed automatically after checking for unsaved changes.
- Direct3D lighting works.

## 0.7.0 (2003-05-27)

- `cxbx.dll` and `cxbx.exe` file sizes shrank substantially.
- Added controller input and configuration.
- Added recent XBE/EXE file menus.
- Added support for `__declspec(thread)` style TLS.
- Fixed GUI color issues.
- Massive code reorganization.
- Random optimizations.
- Moved certain emulation components lower level.
- XBE parsing and debug output fixes, including better handling of unusual Linux
  XBE files.
- Better emulation exception handling.

## 0.6.0-pre12 (2003-02-23)

- HLE advanced to intercepting Direct3D and Xapilib calls.
- A simple Xbox app built with XDK 4361 or 4627 was shown to work.

## 0.6.0-pre11 (2003-02-09)

- HLE began. Cxbx can emulate a blank XDK project.
- New icon pending author approval.

## 0.6.0-pre10 (2003-02-07)

- Added extensive debug console output when opening, converting, or saving XBE
  and EXE files.

## 0.6.0-pre9 (2003-02-06)

- Released source code under the GNU license.
- Debugging interface changed and became cleaner.

## 0.5.2 (2002-12-14)

- Fixed a section-name generation bug.

## 0.5.1 (unknown date)

- Added more XBE information to core and XBE dump output.

## 0.5.0 (2002-11-16)

- Fixed display of section digests.
- Added conversion from EXE to XBE.
- Code cleanup and small UI improvements.

## 0.4.4 (2002-11-01)

- Updated XBE structure for better accuracy.
- Added and fixed XBE info dump details, especially TLS information.
- Added many accurate kernel function prototypes, structs, and enums.

## 0.4.3 (2002-10-09)

- Added edit menu options for patching more than 64 MB of RAM and toggling
  between debug and release mode.
- Fixed minor GUI behavior, such as suggesting an appropriate filename when
  saving an XBE instead of always defaulting to `default.xbe`.

## 0.4.2 (2002-10-07)

- Added logo bitmap import, allowing software boot logos to be changed.

## 0.4.1 (2002-10-04)

- Internal cleanup and organization.
- Software running through the emulator typically terminates safely.

## 0.4.0 beta (2002-09-16)

- Total code rewrite while preserving most functionality.
- Cleaner UI and code design.
- Logo bitmap is decoded and displayed in the main window when opening an XBE.
- Debug output window traces kernel calls.
- Logo bitmap export to BMP.
- `xbe_info.txt` displays the correctly decoded kernel thunk table address.

## 0.3.1 (2002-09-02)

- Decreased file sizes for `cxbx.exe` and `cxbx_krnl.dll`.
- Debug output became cleaner.

## 0.3.0 (2002-08-19)

- GUI changes, new website, and significant emulation-theory changes.
- Kernel exports are now hijacked and interpreted.

## 0.2.2 (2002-07-24)

- Fixed minor GUI problems.
- Added kernel thunk address description in the GUI.

## 0.2.1 (2002-07-24)

- Added Convert To EXE menu option.
- Fixed entry-point detection when converting to EXE.

## 0.2.0 (2002-07-24)

- Drastically changed UI.
- Temporarily removed convert-to-EXE feature.

## 0.1.3b (2002-07-18)

- Fixed an incorrect debug XOR value.

## 0.1.3 (2002-07-16)

- XBE information dump shows retail/debug translated addresses.

## 0.1.2 (2002-07-16)

- Cxbx dumps XBE file information to a text file.
- New icon.
- Small improvements.
