# Input Notes

## Controller Configuration

Controller input configuration is handled through the dialog implemented around
`EmuDInput.cpp` and `EmuDInput.h`.

Configuration is saved in two places:

- the Windows registry
- shared memory inside `Cxbx.dll`

That arrangement lets active emulated titles refresh input configuration while
they are running, so changes made in the configuration UI can take effect
without restarting the title.

## Synchronization

All access to shared memory should be protected by a mutual exclusion mechanism.
Win32 provides the required primitives, and code that updates this path should
preserve that synchronization boundary.
