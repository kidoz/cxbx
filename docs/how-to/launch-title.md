# Launch an Xbox title

[How-to guides](README.md)

Prerequisites: a [built CXBX](build.md), a Windows desktop for the GUI, and an
XBE with its required title data. Paths below are illustrative; replace them
with your build and title locations. Run commands from the repository root.

## Launch from the GUI

```powershell
$buildDir = 'C:/cxbx-work/build'
meson devenv -C $buildDir cxbx
```

Open an XBE, then press **F5** or **Start**. The guest runs in its own process
and render window. Closing that window stops the guest; the launcher reports
its exit code. Closing the launcher while a guest is running asks whether to
leave the guest running.

Use **Settings** to configure video and controller mapping. Cancel discards
the settings working copy. See [Launcher reference](../reference/launcher.md)
for menu actions and path constraints.

## Launch from a script

```powershell
$buildDir = 'C:/cxbx-work/build'
meson devenv -C $buildDir cxbx --run 'C:/xbox-titles/example/default.xbe' --log 'C:/cxbx-work/session-output.txt'
```

Use an existing parent directory and an absolute log path: the runtime child
resolves relative log paths from its working directory. Wait for the command
to finish and inspect its exit code and log. See
[Automation](../reference/launcher.md#automation) for exit-code meanings.

If startup fails, use [Collect and narrow down a title failure](debug-title.md).
