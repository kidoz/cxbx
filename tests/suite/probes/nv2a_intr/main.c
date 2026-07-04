// SPDX-License-Identifier: MIT
//
// nv2a_intr - NV2A interrupt register semantics via the 0xFD000000 aperture:
// the per-engine INTR_EN latches, PMC_INTR_0 aggregation, and the write-1-to-
// clear behaviour of the INTR registers. Deterministic self-check.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE   0xFD000000u
#define REG32(off)  (*(volatile uint32_t *)(NV2A_BASE + (uint32_t)(off)))

#define PMC_INTR_0        0x000100u
#define PFIFO_INTR_0      0x002100u
#define PFIFO_INTR_EN_0   0x002140u
#define PGRAPH_INTR_EN    0x400140u

int main(void)
{
    xt_begin("v1", "nv2a_intr");
    xt_note("NV2A interrupt semantics: INTR_EN latch, PMC aggregation, W1C");

    // No interrupt is pending at start -> PMC_INTR_0 aggregates to 0.
    uint32_t pmc0 = REG32(PMC_INTR_0);
    xt_ev("PMC_INTR_0 initial = 0x%08lX", (unsigned long)pmc0);
    xt_check_u32("nv2a.pmc_intr_initial", 0u, pmc0);

    // INTR_EN registers are plain latches: write then read back.
    volatile uint32_t all = 0xFFFFFFFFu;
    REG32(PFIFO_INTR_EN_0) = all;
    uint32_t en = REG32(PFIFO_INTR_EN_0);
    xt_ev("PFIFO_INTR_EN_0 = 0x%08lX", (unsigned long)en);
    xt_check_u32("nv2a.pfifo_inten_latch", 0xFFFFFFFFu, en);

    volatile uint32_t one = 0x00000001u;
    REG32(PGRAPH_INTR_EN) = one;
    xt_check_u32("nv2a.pgraph_inten_latch", 0x00000001u, REG32(PGRAPH_INTR_EN));

    // Even with INTR_EN=all, PMC_INTR_0 stays 0: it aggregates only *pending*
    // INTR bits, and INTR is write-1-to-clear (the guest cannot set it).
    uint32_t pmc1 = REG32(PMC_INTR_0);
    xt_ev("PMC_INTR_0 with EN set = 0x%08lX", (unsigned long)pmc1);
    xt_check_u32("nv2a.pmc_intr_no_pending", 0u, pmc1);

    // Write-1-to-clear: writing all-ones to an already-clear INTR leaves it 0.
    REG32(PFIFO_INTR_0) = all;
    uint32_t intr = REG32(PFIFO_INTR_0);
    xt_ev("PFIFO_INTR_0 after W1C = 0x%08lX", (unsigned long)intr);
    xt_check_u32("nv2a.pfifo_intr_w1c", 0u, intr);

    return xt_end();
}
