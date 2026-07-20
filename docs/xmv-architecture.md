# XMV architecture spike

The existing `xmv_decode` probe is the architecture baseline: the 5849 native
XMV library currently opens a file, reports a plausible video descriptor, and
decodes a frame into a YUY2 Direct3D surface under Cxbx. That six-check probe is
green, so replacing the decoder with speculative HLE would add risk without a
demonstrated compatibility gain.

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
