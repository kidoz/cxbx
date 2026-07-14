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

## Gamepad Hot Plug

The host input layer registers the emulation window for Raw Input device-change
notifications for joystick, gamepad, and multi-axis HID devices. A notification
forces an immediate four-port XInput connection scan. XInput remains the source
of truth because a Raw Input device notification does not identify an XInput
user index.

The XInput backend is independent of the legacy configurable DirectInput path.
Failure to initialize DirectInput does not disable XInput polling or prevent the
emulation window from receiving device-change notifications.

Connected ports are checked whenever the guest queries device state.
Disconnected ports are checked at most once per second when notifications are
missing or unavailable. This avoids polling every empty XInput slot on every
guest query while still providing a bounded fallback.

Connection changes are latched until `XGetDevices` or `XGetDeviceChanges`
consumes them. Each port also has a connection generation. Handles returned by
`XInputOpen` capture that generation, so unplugging and reconnecting a controller
invalidates handles opened for the previous connection even when both events
occur between guest queries.

The legacy DirectInput controller remains available as a port 0 fallback. When
it is active, physical XInput transitions on that same effective port are hidden
from the guest. Device-change notifications re-enumerate configured DirectInput
devices, and failed state reads are reported as disconnected instead of
returning a neutral controller state.

## Synchronization

All access to shared memory should be protected by a mutual exclusion mechanism.
Win32 provides the required primitives, and code that updates this path should
preserve that synchronization boundary.
