# XACT 5849 HLE bring-up

The first integration target is the engine lifecycle, not cue playback. The
`xact_engine` conformance probe links the 5849 `xacteng.lib` and verifies that
`XACTEngineCreate` was replaced by HLE. It deliberately stops before invoking
the native body. It remains a red probe until create and release can be
implemented together; semantic lifecycle checks are added in that same change.

## Boundary

XACT objects must be emulator-owned objects with stable guest-visible handles.
Do not return a dummy engine from `XACTEngineCreate`: titles immediately invoke
the procedural `IXACTEngine_*` entry points, so partial lifecycle support would
turn a clear missing hook into an opaque guest crash. The initial object model
needs reference counting, parameter validation, deterministic teardown, and an
explicit relationship to the existing DirectSound device.

The implementation order is:

1. Engine create, add-ref, release, and `XACTEngineDoWork`.
2. In-memory wave-bank registration and unregistration.
3. Sound-bank creation plus cue-name lookup.
4. Cue prepare/play/stop with DirectSound-backed voices.
5. Streaming wave banks, notifications, parameter controls, and WMA playlists,
   each added only with its own probe or title trace.

Every slice must add exact 5849 signatures, verify one match in its probe and
at most one across the XBE corpus, and retain a fail-before/pass-after golden.
No XACT bank data or SDK binaries belong in the repository.
