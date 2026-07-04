// SPDX-License-Identifier: MIT
//
// kernel_trap - verifies the emulator's unimplemented-export handling.
//
// It deliberately calls KeQueryPerformanceCounter, which is an unimplemented
// (PANIC) ordinal on stock Cxbx. With the CXBX_TRAP_UNIMPLEMENTED build the
// call is trapped: the emulator logs "KTRACE| UNIMPLEMENTED ordinal=..." and
// returns 0, so the title survives and writes its #result. On stock Cxbx the
// call jumps to a bogus low address and crashes the title, so no #result line
// is produced and the runner reports PARTIAL/TIMEOUT instead of PASS.
//
// KeQueryPerformanceCounter takes zero arguments, so the trap's __stdcall stub
// keeps the caller stack balanced and survival is clean.

#include "xtrace.h"
#include <xboxkrnl/xboxkrnl.h>

int main(void)
{
    xt_begin("v1", "kernel_trap");
    xt_note("calls UNIMPLEMENTED export KeQueryPerformanceCounter");
    xt_note("PASS here means the emulator trapped the call instead of crashing");

    ULONGLONG pc = KeQueryPerformanceCounter();
    xt_ev("KeQueryPerformanceCounter returned 0x%016llX", (unsigned long long)pc);

    // Reaching this line at all means the unimplemented call did not crash us.
    xt_check_bool("trap.survived_unimplemented_call", 1, 1);
    return xt_end();
}
