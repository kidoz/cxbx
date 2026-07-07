// SPDX-License-Identifier: MIT
//
// apu - Xbox APU (MCPX audio processor) register semantics via the 0xFE800000
// aperture: the free-running XGSCNT global sample counter (monotonic), the
// voice-processor PIO-free status, and a plain register latch. Deterministic
// self-check.

#include "xtest.h"
#include <stdint.h>

#define APU_BASE     0xFE800000u
#define REG32(off)   (*(volatile uint32_t *)(APU_BASE + (uint32_t)(off)))

#define XGSCNT              0x0000200Cu   // global sample counter (free-running)
#define VP_PIO_FREE         0x00020010u   // voice-processor PIO-free status
#define VP_PIO_QUEUE_EMPTY  0x00000080u
#define SCRATCH             0x00000100u   // a plain (non-special) register

int main(void)
{
    xt_begin("v1", "apu");
    xt_note("APU semantics: XGSCNT monotonic sample counter, VP PIO-free, register latch");

    // XGSCNT is a free-running sample counter: successive reads never go
    // backwards (they are equal or advancing, never decreasing).
    uint32_t t0 = REG32(XGSCNT);
    uint32_t t1 = REG32(XGSCNT);
    uint32_t t2 = REG32(XGSCNT);
    xt_ev("XGSCNT reads = 0x%08lX 0x%08lX 0x%08lX",
          (unsigned long)t0, (unsigned long)t1, (unsigned long)t2);
    xt_check_bool("apu.xgscnt_monotonic", 1, (t1 >= t0) && (t2 >= t1));

    // The voice processor reports its PIO command queue free (ready to accept a
    // command); a title waiting on this bit before pushing voice PIO proceeds.
    uint32_t vp = REG32(VP_PIO_FREE);
    xt_ev("VP PIO-free = 0x%08lX", (unsigned long)vp);
    xt_check_bool("apu.vp_pio_free", 1, (vp & VP_PIO_QUEUE_EMPTY) != 0);

    // A plain APU register latches a written value (write / read-back round-trip).
    REG32(SCRATCH) = 0xABCD1234u;
    uint32_t rb = REG32(SCRATCH);
    xt_ev("scratch reg readback = 0x%08lX", (unsigned long)rb);
    xt_check_u32("apu.register_latch", 0xABCD1234u, rb);

    return xt_end();
}
