# XMV architecture spike

The existing `xmv_decode` probe is the architecture baseline: the 5849 native
XMV library opens a file, reports exact video and audio descriptors, creates and
synchronizes a DirectSound stream, decodes a timestamped frame into a YUY2
Direct3D surface, resets to the beginning, and decodes again under Cxbx. That
26-check lifecycle is green, so replacing the decoder with speculative HLE
would add risk without a demonstrated compatibility gain.

## Recommended boundary

Keep XMV as a native-library pass-through while its probe and target titles are
stable. Treat the emulator-owned boundaries around it as the supported
architecture:

- filesystem callbacks and packet reads provide deterministic guest data;
- Direct3D owns YUY2 surfaces, conversion, overlay presentation, and lifetime;
- DirectSound owns enabled audio streams and the playback clock;
- vblank and performance-counter emulation provide pacing;
- trace events record create, descriptor, frame result, timestamp, reset, and
  close transitions without recording media payloads.

The 5849 library keeps a decoder-owned DirectSound stream allocated after
`DisableAudioStream`; disabling clears its enabled and synchronization state,
while `GetAudioStream` can still return an additional reference to the same
object. Callers must release every interface reference returned by enable or
query operations. The probe locks this lifetime behavior because treating
disable as immediate object destruction would break native decoder state.

For the sample movie, `GetVideoDescriptor` returns a zero `FramesPerSecond`
field even though the encoded frame timestamps have an approximately 30 fps
cadence. Consumers must use decoder timestamps and synchronization APIs rather
than assuming this descriptor field is nonzero.

If a probe or title demonstrates that native decode is insufficient, introduce
an `XmvDecoderBackend` behind the exported decoder lifecycle. Its contract
should cover file and callback packet sources, descriptor discovery, frame
decode into a locked YUY2 surface, audio stream selection, reset, termination,
and close. Decoder handles must be emulator-owned and validated before use.

## Evidence gates

HLE starts only from a reproducing probe or title trace. The first failing
operation determines the smallest implementation slice: container/open,
descriptor, video decode, audio, or clocking. A replacement backend is accepted
only when it preserves the current `xmv_decode` pass and adds a regression for
the motivating failure. LTCG signatures or secondary media services follow the
same rule; they are not added from SDK inventory alone.

The remaining unprobed XMV boundaries are packet-callback input, blocking
`Play` and cross-thread termination, looping/end-of-file, and multi-stream or
ADPCM audio. Add each only when a focused probe or title supplies the expected
ordering and lifetime contract.
