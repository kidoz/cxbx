# LTCG D3D8 titles (D3D8LTCG) — why API-level HLE cannot work

Findings from investigating why **Samurai Shodown V** does not run. They apply
equally to **King of Fighters 2002** (same SNK engine, same XDK) and, in principle,
to any title that links an LTCG library.

The conclusion is negative and worth recording so nobody re-derives it: **a
D3D8LTCG title cannot be emulated by patching D3D API entry points.** The work that
established this was reverted; only the diagnostic tools were kept.

## What an LTCG title is

An XBE declares its libraries in the header. The SNK titles declare `D3D8LTCG`
rather than `D3D8`:

```
    XAPILIB   1.0.5849
    D3DX8     1.0.5849
    D3D8LTCG  1.0.5849      <-- link-time code generation
    ...
```

`D3D8LTCG` is D3D8 compiled with MSVC `/GL` (whole-program optimisation). The
library ships as **intermediate language, not x86**: every member of
`d3d8ltcg.lib` is an `ANON_OBJECT_HEADER` (`Sig1=0x0000`, `Sig2=0xFFFF`), which is
why the archive is 22 MB against 2 MB for the stock `d3d8.lib`. The machine code
does not exist until the *title's own linker* runs.

Three consequences follow, in increasing order of severity.

## 1. Stock signatures do not resolve

LTCG re-optimises every function body at link time, so signatures cut from the
stock library miss. Only **13 of the 88** entries in `D3D8_1_0_5849` resolve
against Samurai Shodown V, and every rendering-critical one (`CreateDevice`,
`Swap`, `Clear`, `DrawVertices`, `SetTexture`, the whole `SetRenderState_*` family)
is among the misses:

```
python tools/oovpa/scan_oovpa.py --table D3D8_1_0_5849 \
    --inl "src/cxbx/src/win32/CxbxKrnl/*.inl" \
    --image "other/games/Samurai Showdown V/default.xbe"
```

Note the title still declares `D3DX8`, and `EmuInit`'s `D3DX8 -> D3D8` alias
therefore installs the *stock* table on it anyway. That is what leaves D3D
**half hooked** today.

Signatures can be recovered from the shipped images (the code is there, just
re-optimised), and were: 58 of them, verified unique across both titles. That was
not the blocker.

## 2. LTCG rewrites calling conventions, per function

With `/GL` the linker sees every caller of a library function and may pick whatever
convention it likes. This is per-function — some keep stdcall, some do not — and
only the `ret N` reveals which. Worked examples, all verified by disassembly:

| Function | Stock | LTCG |
|---|---|---|
| `SetRenderState_CullMode@4` | `mov ecx,[esp+8]` … `ret 4` | unchanged — stdcall preserved |
| `Swap@4` | arg on stack, `ret 4` | **arg in EAX**, bare `ret` (`mov ebx, eax` at entry) |
| `SetTransform@8` | `mov eax,[esp+4]` / `mov edx,[esp+8]` | those two loads **deleted**; args arrive in EAX/EDX, bare `ret` |
| `Direct3D_CreateDevice@24` | 6 stack args, `ret 24` | `BehaviorFlags` in **EAX**, `ppReturnedDeviceInterface` in **ECX**, `pPresentationParameters` the lone stack arg, `ret 4` — the three arguments the stock body never used (`Adapter`, `DeviceType`, `hFocusWindow`) were **eliminated outright** |

Patching such a function by jumping straight at a `__stdcall` Emu wrapper pops
argument bytes the guest caller never pushed and unbalances its stack. A
marshalling thunk fixes this, and one was written and shown to work
(`CreateDevice` reached the HLE, and the title stopped doing native GPU init
entirely — `MmClaimGpuInstanceMemory` went to zero). So this was not the blocker
either.

## 3. The blocker: LTCG inlines the API into the game

`/GL` inlines across module boundaries. The D3D API wrappers are small, so the
linker inlined them **into the game's own code**, leaving behind only direct calls
to D3D's *internal* helpers. The game does not call `D3DDevice_SetRenderState_*`;
it calls the pushbuffer primitives underneath them.

```
python tools/oovpa/xbe_api_usage.py "other/games/Samurai Showdown V/default.xbe" --section D3D
```

Of the 47 D3D entry points the game actually calls, most are internal helpers.
The hottest, called **19 times directly from `.text`**, is a 39-byte function at
`0x002FF7C0`:

```asm
mov eax, [0x30f040]      ; pushbuffer write pointer
add eax, 8
cmp eax, [0x30f044]      ; pushbuffer limit
jae  slow_path
mov [0x30f040], eax
mov [eax-8], ecx         ; NV2A method header
mov [eax-4], edx         ; value
ret                      ; args in ECX/EDX, pops nothing
```

That is the NV2A method-emit primitive. `MakeRequestedSpace` (the pushbuffer
reservation helper) is likewise called 15 times straight from game code. Others in
the same set: `UpdateProjectionViewportTransform`, `CommonSetViewport`,
`CommonSetControl0`, `DestroyResource`, `BlockOnTime`, `MapRegisters`.

**There is no seam.** For those call sites the API call does not exist any more, so
there is nothing to patch. And the moment `CreateDevice` *is* hooked, the emulator
owns the device and the guest's own D3D globals (its `CDevice` struct at
`0x30f040`, its pushbuffer pointer at `0x3114e8`) are never initialised — so every
inlined path dereferences null. That is exactly the observed fault: `0xC0000005` at
`0x003050A8`, `test byte ptr [esi+8], 4` with `esi = [0x3114e8] = NULL`.

Full API coverage does not help. It is not a coverage problem.

## Where that leaves these titles

The guest's D3D is self-contained and already drives the hardware directly — the
un-hooked title happily performs its own GPU bring-up (`MmClaimGpuInstanceMemory`,
`KeConnectInterrupt` on the GPU vector, `AvSendTVEncoderOption`, pushbuffer DMA,
NV2A semaphores). The tractable path is therefore **LLE**: let the guest's D3D run
and emulate the NV2A, which is the direction the NV2A work in this tree is already
taking.

Two things to know when picking that up:

- Un-hooked, the title stalls in `BlockUntilVerticalBlank`. That stall is
  deliberate and documented (`EmuKrnl.cpp`, `EmuStartVblankThread`); vblank
  synthesis is opt-in via `CXBX_ENABLE_VBLANK=1`. Enabling it lets the ISR fire but
  did not by itself reach a draw.
- With D3D fully un-hooked (`CXBX_HLE_SKIP=D3D8`) the title emits hundreds of NV2A
  state methods but **no draw methods** (no `SET_BEGIN_END`, `DRAW_ARRAYS` or
  `INLINE_ARRAY`). It never reaches its render loop, so something in the device-init
  handshake is still unsatisfied. That is the thread to pull.

## Tools kept

- `tools/oovpa/xbe_api_usage.py` — which library APIs does a title actually call?
  Walks `.text -> library` call edges. **Run this first on any new title**; it is
  the check that would have short-circuited this whole investigation.
- `tools/oovpa/scan_oovpa.py` — which signatures of an existing OOVPA table resolve
  against an image (OK / MISS / MULTI). Useful for any title, not just LTCG.
