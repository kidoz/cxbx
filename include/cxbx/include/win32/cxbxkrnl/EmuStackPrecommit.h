// EmuStackPrecommit.h - pre-commit a thread's entire stack reservation.
//
// Guest threads run Xbox code with the TEB's NtTib stack fields holding
// KPCR/TLS content (the LDT-less FS convention: fs:[4] is the Xbox TLS
// pointer while in Xbox role, and legacy mode clobbers it permanently). The
// kernel decides whether a guard-page fault is stack growth by looking at
// those TEB fields, so with them repurposed a guest-role stack touch below
// the committed floor is NOT grown: the fault surfaces as
// STATUS_GUARD_PAGE_VIOLATION, the guard is consumed without extending the
// stack, and a deeper touch dies 0xC0000005 just below the committed floor
// with most of the reservation still unused. Observed as Turok Evolution's
// intermittent early crash during vertex-shader fallback setup (main XBE
// thread: fault at stack-floor-4 with 104 KiB of reservation left) and as
// the conformance suite's sporadic guard-violation/teardown-crash runs.
//
// Committing the whole reservation up front removes the need for growth
// entirely. The lowest reserved page is left uncommitted as a hard overflow
// fence so a genuine runaway still faults instead of writing under the stack.

#ifndef EMUSTACKPRECOMMIT_H
#define EMUSTACKPRECOMMIT_H

#ifndef POINTER_64
#define POINTER_64 __ptr64
#endif
#include <windows.h>

// Commit every reserved page of the current thread's stack except the lowest
// (kept as a fence), clear any PAGE_GUARD protection inside the range, and
// lower the TEB stack limit to match. Must run while the host TIB is still
// authoritative (before any FS role switch). Idempotent: a second call
// returns 0. Returns the number of bytes newly made usable (committed or
// un-guarded), or 0 when nothing needed doing or the stack could not be
// resolved.
static inline SIZE_T EmuPrecommitThreadStack(void)
{
    NT_TIB *Tib = (NT_TIB *)NtCurrentTeb();
    BYTE *StackLimit = (BYTE *)Tib->StackLimit;
    // TEB.DeallocationStack (32-bit TEB offset 0xE0C): the reservation base.
    BYTE *DeallocationBase = *(BYTE **)((BYTE *)Tib + 0xE0C);

    if(DeallocationBase == NULL || StackLimit == NULL ||
       StackLimit <= DeallocationBase)
    {
        return 0;
    }

    SYSTEM_INFO SystemInfo;
    GetSystemInfo(&SystemInfo);
    const SIZE_T PageSize = SystemInfo.dwPageSize;

    BYTE *Fence = DeallocationBase + PageSize;
    SIZE_T Changed = 0;

    BYTE *Cursor = Fence;
    while(Cursor < StackLimit)
    {
        MEMORY_BASIC_INFORMATION Info;
        if(VirtualQuery(Cursor, &Info, sizeof(Info)) != sizeof(Info))
        {
            return Changed;
        }

        BYTE *RegionEnd = (BYTE *)Info.BaseAddress + Info.RegionSize;
        if(RegionEnd > StackLimit)
        {
            RegionEnd = StackLimit;
        }
        const SIZE_T Span = (SIZE_T)(RegionEnd - Cursor);

        if(Info.State == MEM_RESERVE)
        {
            if(VirtualAlloc(Cursor, Span, MEM_COMMIT, PAGE_READWRITE) == NULL)
            {
                return Changed;
            }
            Changed += Span;
        }
        else if(Info.State == MEM_COMMIT && (Info.Protect & PAGE_GUARD) != 0)
        {
            DWORD OldProtect = 0;
            if(!VirtualProtect(Cursor, Span, PAGE_READWRITE, &OldProtect))
            {
                return Changed;
            }
            Changed += Span;
        }

        Cursor = RegionEnd;
    }

    if(Changed != 0)
    {
        Tib->StackLimit = Fence;
    }

    return Changed;
}

#endif // EMUSTACKPRECOMMIT_H
