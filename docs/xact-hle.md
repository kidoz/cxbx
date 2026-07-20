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

The engine lifecycle and in-memory wave-bank slices are implemented. Registered
wave banks retain the guest buffer as borrowed data until explicit
unregistration or engine teardown. Registration validates the version 3 bank
header, segment bounds, metadata dimensions, entry formats, and wave-data
regions before publishing an emulator-owned handle.

The remaining order is:

1. Sound-bank creation plus cue-name lookup.
2. Cue prepare/play/stop with DirectSound-backed voices.
3. Streaming wave banks, notifications, parameter controls, and WMA playlists,
   each added only with its own probe or title trace.

Every slice must add exact 5849 signatures, verify one match in its probe and
at most one across the XBE corpus, and retain a fail-before/pass-after golden.
No XACT bank data or SDK binaries belong in the repository.
