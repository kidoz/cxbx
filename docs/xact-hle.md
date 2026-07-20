# XACT 5849 HLE bring-up

The first integration target is the engine lifecycle, not cue playback. The
`xact_engine` conformance probe links the 5849 `xacteng.lib` and verifies engine
creation, reference counting, `XACTEngineDoWork`, deterministic teardown, and
recreation. Its four public entry points are HLE-patched together so a title
cannot receive an emulator-owned engine and then fall through to a native XACT
method for lifecycle management.

## Boundary

XACT objects must be emulator-owned objects with stable guest-visible handles.
Do not return a dummy engine from `XACTEngineCreate`: titles immediately invoke
the procedural `IXACTEngine_*` entry points, so partial lifecycle support would
turn a clear missing hook into an opaque guest crash. The initial object model
needs reference counting, parameter validation, deterministic teardown, and an
explicit relationship to the existing DirectSound device.

The engine lifecycle, in-memory wave-bank, sound-bank metadata, and initial cue
lifecycle slices are implemented. Registered wave banks retain the guest buffer
as borrowed data until explicit unregistration. Registration validates the
version 3 bank header, segment bounds, metadata dimensions, entry formats, and
wave-data regions before publishing an emulator-owned handle.

Sound-bank creation validates the version 11 header, declared size, cue table,
and every available friendly-name string before publishing an emulator-owned
handle. Friendly-name lookup is exact and case-sensitive, returns the cue-table
index, and leaves the output at `0xFFFFFFFF` when no cue matches.

`PrepareEx`, `PlayEx`, and `Stop` resolve a one-track direct-play cue to a
registered in-memory wave bank and copy its PCM play region into an
emulator-owned DirectSound buffer. Prepared and playing cue instances are owned
by their sound bank. `Stop` can release one instance, every instance for a cue
index, or every instance in the bank; final sound-bank release also destroys
all remaining voices. Autorelease calls with no output handle are accepted, but
the voice remains bank-owned until an explicit stop or bank release.

This first playback boundary intentionally rejects compact wave banks,
streaming banks, ADPCM/WMA entries, multi-track and variation sounds, explicit
sound sources, and parameter controls. Each requires a focused probe or title
trace before the parser and lifetime model expand.

Wave and sound banks hold an engine reference, matching the XACT object model.
Releasing a caller's engine reference therefore leaves the engine alive while a
bank exists; unregistering or finally releasing the last bank can complete
engine teardown.

The remaining order is:

1. Streaming wave banks, notifications, parameter controls, and WMA playlists,
   each added only with its own probe or title trace.

Every slice must add exact 5849 signatures, verify one match in its probe and
at most one across the XBE corpus, and retain a fail-before/pass-after golden.
No XACT bank data or SDK binaries belong in the repository.
