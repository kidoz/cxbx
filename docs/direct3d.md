# Direct3D Notes

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

## Current Context

The repository also has a partial register-level NV2A model for MMIO, RAMIN,
PFIFO, and PGRAPH method paths. That model is not full rasterization; Direct3D
HLE and NV2A register modeling remain separate graphics bring-up concerns.
