# Explanation

[Documentation](../README.md)

Understand the reasoning behind the emulator's structure and subsystem boundaries.

- [Development and OpenXDK](development.md): build and source-layout background.
- [Launcher architecture](launcher.md): GUI, runtime, and rendering ownership.
- [Direct3D resources](direct3d.md): historical HLE resource-pointer strategies.
- [Input](input.md): shared configuration and controller connection lifetimes.
- [LTCG D3D8](ltcg-d3d8.md): historical findings about the limits of API interception.
- [XMV architecture](xmv-architecture.md): native decode boundaries and evidence
  needed before replacing them.
- [Title-debugging design](title-debugging.md): evidence collection, replay
  rationale, and future tooling.

For exact contracts, use [reference](../reference/README.md); to perform a task,
use the [how-to guides](../how-to/README.md).
