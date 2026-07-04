// SPDX-License-Identifier: MIT
//
// nv2a_pfifo - drives the full PFIFO pipeline: a DMA object in RAMIN points at a
// pushbuffer in guest physical memory; writing CACHE1_DMA_PUT kicks the pusher,
// which fetches an increasing-method packet and dispatches it to PGRAPH.
// Verifies GET catches up to PUT and the method's data lands in PGRAPH state.
//
// This exercises: MMIO (0xFD...) + physical memory (0x80...) trap-and-emulate,
// RAMIN DMA-object load, the DMA pusher, and PGRAPH method dispatch.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE   0xFD000000u
#define REG32(off)  (*(volatile uint32_t *)(NV2A_BASE + (uint32_t)(off)))
#define PHYS_BASE   0x80000000u

#define PFIFO_CACHE1_PUSH0         0x003200u
#define PFIFO_CACHE1_DMA_PUSH      0x003220u
#define PFIFO_CACHE1_DMA_INSTANCE  0x00322Cu
#define PFIFO_CACHE1_DMA_PUT       0x003240u
#define PFIFO_CACHE1_DMA_GET       0x003244u
#define PRAMIN                     0x700000u
#define PGRAPH                     0x400000u

int main(void)
{
    xt_begin("v1", "nv2a_pfifo");
    xt_note("PFIFO: DMA object + pushbuffer -> pusher -> PGRAPH method dispatch");

    // Pushbuffer at physical 0x00010000 (guest window address 0x80010000).
    // One increasing method: method=0x200, subch=0, count=1, data=0xABCD1234.
    // (0x200 avoids the 0x180..0x1FF RAMHT-relocate range and PGRAPH specials.)
    const uint32_t pbPhys = 0x00010000u;
    volatile uint32_t *pb = (volatile uint32_t *)(PHYS_BASE + pbPhys);
    volatile uint32_t w0 = 0x00040200u;
    volatile uint32_t w1 = 0xABCD1234u;
    pb[0] = w0;
    pb[1] = w1;

    // DMA object in RAMIN at instance handle 0x1000 -> RAMIN offset 0x10000.
    // Layout [Flags, Limit, Frame]; Address = (Frame & 0x07FFFFFF)|(Flags&0xFFF).
    volatile uint32_t *dma = (volatile uint32_t *)(NV2A_BASE + PRAMIN + 0x10000u);
    volatile uint32_t f0 = 0x00000000u;   // Flags
    volatile uint32_t f1 = 0x00FFFFFFu;   // Limit
    volatile uint32_t f2 = pbPhys;        // Frame -> base 0x00010000
    dma[0] = f0;
    dma[1] = f1;
    dma[2] = f2;

    // Point CACHE1 at the DMA object and enable the pusher.
    volatile uint32_t one = 1u;
    REG32(PFIFO_CACHE1_DMA_INSTANCE) = 0x1000u;
    REG32(PFIFO_CACHE1_PUSH0) = one;
    REG32(PFIFO_CACHE1_DMA_PUSH) = one;

    // GET=0, then writing PUT=8 kicks EmuNv2aRunPusher over the 2-dword packet.
    volatile uint32_t zero = 0u, put = 8u;
    REG32(PFIFO_CACHE1_DMA_GET) = zero;
    REG32(PFIFO_CACHE1_DMA_PUT) = put;

    // The pusher should have advanced GET to PUT...
    uint32_t get = REG32(PFIFO_CACHE1_DMA_GET);
    xt_ev("CACHE1_DMA_GET after kick = 0x%08lX", (unsigned long)get);
    xt_check_u32("nv2a.pusher_get_eq_put", 8u, get);

    // ...and dispatched method 0x200 into PGRAPH state.
    uint32_t pg = REG32(PGRAPH + 0x200u);
    xt_ev("PGRAPH+0x200 = 0x%08lX", (unsigned long)pg);
    xt_check_u32("nv2a.pgraph_method_data", 0xABCD1234u, pg);

    return xt_end();
}
