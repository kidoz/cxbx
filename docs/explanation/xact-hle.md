# XACT HLE design

[Explanation](README.md)

XACT 5849 support begins with a complete engine lifecycle because titles use
the object returned by `XACTEngineCreate` immediately. They invoke procedural
`IXACTEngine_*` entry points to manage it. Returning a dummy engine, or hooking
creation without the related lifecycle methods, would let native library code
operate on an emulator-owned object and turn a missing hook into a guest crash.

The initial integration patched the four public engine entry points together.
The `xact_engine` conformance probe links the 5849 `xacteng.lib` and checks
creation, reference counting, `XACTEngineDoWork`, teardown, and recreation.
That lifecycle provides the foundation for wave banks, sound banks, and cue
playback. The [support reference](../reference/xact-hle.md) defines their
implemented contracts.

## Ownership across the audio boundary

Guest-visible handles identify emulator-owned objects, so the host can validate
parameters and control teardown. Banks keep their engine alive, and sound banks
own their cue instances. Playback connects this object model to DirectSound,
where PCM play regions become emulator-owned buffers.

The ownership model makes registration and release order part of the API
contract. Supporting a bank format therefore requires evidence about both its
layout and its lifetime; accepting a header alone is insufficient.

## Evidence for extending support

Support grows from a reproducing probe or title trace. Compact or streaming
banks, additional encodings, parameter controls, and more notification types
each introduce parser or lifetime behavior that needs a focused case.

An implementation slice requires exact 5849 signatures, one signature match
in its probe and at most one per title image in the validation corpus, and a
golden trace showing failure before the change and success afterward. The
[conformance suite guide](../../tests/suite/README.md) describes probe authoring.
Probe results can be retained without committing XACT bank data or SDK binaries.
