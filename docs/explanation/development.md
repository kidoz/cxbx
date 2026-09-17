# Development and OpenXDK

[Explanation](README.md)

## Current Build Context

CXBX is built with Meson as a 32-bit Windows/x86 target. See
[Build CXBX](../how-to/build.md) for setup and verification.
The [root README](../../README.md) lists development commands.

The tree still bundles open-xdk support headers and libraries under
`include/open-xdk/include` and `src/open-xdk/src`. Keep those include paths
available when working on kernel prototypes and open-xdk-backed code.

## OpenXDK Context

Older MSVC project files expected an OpenXDK source tree in the compiler
`/lib` and `/include` search paths because Cxbx implements kernel prototypes
inside `xboxkrnl.h`. That is useful context for understanding the source layout,
but current development should use the repository's Meson files rather than
manually configuring MSVC project search paths.
