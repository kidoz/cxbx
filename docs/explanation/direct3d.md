# Direct3D Notes

[Explanation](README.md)

This describes the Direct3D HLE resource strategy captured in the original CXBX
notes. Treat it as implementation context, not as a complete modern graphics
design.

## Resource Registration Problem

Direct3D resources are awkward for CXBX because precompiled Xbox resources
(`.xpr`) can be loaded into memory manually by an Xbox title and then registered
with:

```cpp
pResource->Register(addr);
```

At that point the emulator does not naturally get a clean interception point for
the resource object's `this` pointer. The resource layout involved is:

```cpp
DWORD Common;
DWORD Data;
DWORD Lock;
```

## Resource Pointer Strategies

The first idea was to store the host `IDirect3D*` resource pointer in
`pResource->Data`. That is unsafe because some Xbox titles directly access and
modify `Data`.

Another option was to hide the host pointer inside the buffer allocated by
`Data`. That can work only while titles do not access the resource data after
registration.

The recorded method stores the host pointer in `Lock` and hijacks functions that
access the `Lock` member.

## Related graphics paths

Direct3D HLE and raw NV2A emulation are separate graphics paths. See
[LTCG D3D8](ltcg-d3d8.md) for why some titles need the latter, and the
[NV2A replay reference](../reference/nv2a-capture.md) for its offline replay boundary.
