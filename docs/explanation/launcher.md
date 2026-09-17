# Launcher architecture

[Explanation](README.md)

The Windows x86 launcher uses NodalKit 0.2.0, pinned by the
[Meson wrap](../../subprojects/nodalkit.wrap). The wrap patch excludes
upstream examples, tools, and tests from the production dependency build.
NodalKit is MIT licensed; CXBX remains GPL-2.0-or-later.

The GUI and batch entry paths use the same conversion, DLL staging, guest
working directory, suspended process creation, Xbox RAM reservation, and
MMIO aperture fences.

## Rendering ownership

NodalKit owns only the launcher window. Its C++23 target does not receive the
legacy DirectX SDK include paths. The emulator and launcher service remain
C++20, and the runtime DLL has no NodalKit dependency. The launcher uses D3D11
by default; `NK_RENDERER_BACKEND=software` selects the toolkit's software path.
Neither choice changes guest rendering or resolves guest driver crashes.
Video settings enumerate through the Windows system D3D8 module explicitly,
so an old guest graphics override beside the launcher is not initialized for
display enumeration.

See [launcher verification](../how-to/test-launcher.md) for tests and
[launcher reference](../reference/launcher.md) for settings and automation.
