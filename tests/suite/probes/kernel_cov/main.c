// SPDX-License-Identifier: MIT
//
// kernel_cov - kernel HLE conformance. v1 self-checks a curated set of
// known-implemented kernel exports (time sources, RTL string/memory ops).
//
// A FULL ordinal-coverage sweep (call every export, record implemented vs
// unimplemented) requires the emulator to survive calls into unimplemented
// kernel functions. On Cxbx that means the "warned trap" build (see the suite
// README); on stock Cxbx an unimplemented ordinal jumps to a bogus address and
// crashes the title, so v1 deliberately does not sweep.

#include "xtrace.h"
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>
#include <string.h>

int main(void)
{
    xt_begin("v1", "kernel_cov");
    xt_note("v1: self-checks of known-implemented kernel exports");
    xt_note("full ordinal sweep requires the warned-trap Cxbx build");

    // --- KeQuerySystemTime: nonzero and monotonic non-decreasing -------------
    LARGE_INTEGER t1, t2;
    volatile int spin;
    KeQuerySystemTime(&t1);
    for (spin = 0; spin < 200000; spin++) { }
    KeQuerySystemTime(&t2);
    xt_ev("systime t1=0x%016llX t2=0x%016llX",
          (unsigned long long)t1.QuadPart, (unsigned long long)t2.QuadPart);
    xt_check_bool("ke.systime_nonzero",   1, t1.QuadPart != 0);
    xt_check_bool("ke.systime_monotonic", 1, t2.QuadPart >= t1.QuadPart);

    // --- KeTickCount: readable variable, non-decreasing ----------------------
    DWORD k1 = KeTickCount;
    for (spin = 0; spin < 200000; spin++) { }
    DWORD k2 = KeTickCount;
    xt_ev("tickcount k1=%lu k2=%lu", (unsigned long)k1, (unsigned long)k2);
    xt_check_bool("ke.tickcount_monotonic", 1, k2 >= k1);

    // --- RtlEqualString ------------------------------------------------------
    // Build ANSI_STRINGs by hand to avoid depending on RtlInitAnsiString.
    ANSI_STRING a, b, c;
    a.Length = 5; a.MaximumLength = 6; a.Buffer = (PCHAR)"hello";
    b.Length = 5; b.MaximumLength = 6; b.Buffer = (PCHAR)"hello";
    c.Length = 5; c.MaximumLength = 6; c.Buffer = (PCHAR)"world";
    xt_check_bool("rtl.equalstring_eq", 1, RtlEqualString(&a, &b, FALSE));
    xt_check_bool("rtl.equalstring_ne", 0, RtlEqualString(&a, &c, FALSE));

    // --- RtlCompareMemory: returns count of equal leading bytes --------------
    char m1[16], m2[16];
    memcpy(m1, "ABCDEFGHIJKLMNOP", 16);
    memcpy(m2, "ABCDEFGHIJKLMNOP", 16);
    xt_check_u32("rtl.comparemem_equal", 16u, (uint32_t)RtlCompareMemory(m1, m2, 16));
    m2[8] = 'x';
    xt_check_u32("rtl.comparemem_first8", 8u, (uint32_t)RtlCompareMemory(m1, m2, 16));

    return xt_end();
}
