# Build CXBX

[How-to guides](README.md)

Use a Windows development environment with Clang/LLVM (including LLD,
llvm-lib, and llvm-rc), the Windows SDK, Meson 1.11 or later, and Ninja.
The NodalKit launcher requires a C++23-capable compiler; emulator and launcher
service code use C++20. Python tooling requires Python 3.14 or later and uv.

Run commands from the repository root. Choose an absolute output directory
outside the checkout; replace the illustrative path below with your own.

```powershell
$buildDir = 'C:/cxbx-work/build'
uv sync --dev
meson setup $buildDir --cross-file cross/i686-windows-clang.ini --buildtype debugoptimized
meson compile -C $buildDir
```

The cross file selects 32-bit Windows/x86. The first configure fetches the
pinned NodalKit dependency, so it needs network access. For an existing build,
use `meson configure $buildDir -Dbuildtype=debugoptimized` to change its build
type, then compile again.

Check the build with host tests that do not need a desktop session:

```powershell
meson test -C $buildDir --no-suite ui --print-errorlogs
```

Inspect any failures in the test output before proceeding. UI tests need an
interactive desktop; see [Verify launcher changes](test-launcher.md).
Once built, [launch a title](launch-title.md).

For optional targets and the `just` workflow, see the
[root build notes](../../README.md#build). For source-layout background, see
[Development and OpenXDK](../explanation/development.md).
