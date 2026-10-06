# XACT 5849 HLE support

[Reference](README.md)

This page describes implemented XACT 5849 objects, validation, ownership, and
playback limits. For the reasoning behind the implementation boundary, see
[XACT HLE design](../explanation/xact-hle.md).

## Engine and object ownership

XACT objects are emulator-owned objects with stable guest-visible handles.
The engine lifecycle includes reference counting, parameter validation,
deterministic teardown, and integration with the existing DirectSound device.

Wave and sound banks hold an engine reference. Releasing a caller's engine
reference leaves the engine alive while a bank exists; unregistering or
finally releasing the last bank can complete engine teardown.

## In-memory wave banks

Registered wave banks retain the guest buffer as borrowed data until explicit
unregistration. Registration validates the version 3 bank header, segment
bounds, metadata dimensions, entry formats, and wave-data regions before
publishing an emulator-owned handle.

## Sound-bank metadata

Sound-bank creation validates the version 11 header, declared size, cue table,
and every available friendly-name string before publishing an emulator-owned
handle. Friendly-name lookup is exact and case-sensitive, returns the cue-table
index, and leaves the output at `0xFFFFFFFF` when no cue matches.

## Cue playback and lifetime

`PrepareEx`, `PlayEx`, and `Stop` resolve a one-track direct-play cue to a
registered in-memory wave bank and copy its PCM play region into an
emulator-owned DirectSound buffer. Prepared and playing cue instances are owned
by their sound bank. `Stop` can release one instance, every instance for a cue
index, or every instance in the bank; final sound-bank release also destroys
all remaining voices. `XACTEngineDoWork` detects naturally completed voices and
reaps autorelease cue instances.

## Stop notifications

Stop notifications support persistent and one-shot registrations filtered by
sound bank/cue index or cue instance. Natural completion and explicit stop both
queue notifications; autoreleased notifications carry the cue-destroyed flag.
The engine honors its notification queue limit and supports polling, flushing,
and unregistering.

## Unsupported behavior

- Compact and streaming wave banks.
- ADPCM/WMA entries and WMA playlists.
- Multi-track and variation sounds.
- Explicit sound sources and parameter controls.
- Notification types other than stop, wave-bank notification filters, and
  event-backed notification delivery.
