// SPDX-License-Identifier: MIT
// NV2A PFIFO increasing/non-increasing packet and subchannel probe.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE  0xFD000000u
#define PHYS_BASE  0x80000000u
#define REG32(off) (*(volatile uint32_t*)(NV2A_BASE + (uint32_t)(off)))

#define PFIFO_CACHE1_PUSH0        0x003200u
#define PFIFO_CACHE1_DMA_PUSH     0x003220u
#define PFIFO_CACHE1_DMA_STATE    0x003228u
#define PFIFO_CACHE1_DMA_INSTANCE 0x00322Cu
#define PFIFO_CACHE1_DMA_PUT      0x003240u
#define PFIFO_CACHE1_DMA_GET      0x003244u
#define PRAMIN                    0x700000u
#define PGRAPH                    0x400000u

static void setup_dma(uint32_t physical_address)
{
    volatile uint32_t* dma = (volatile uint32_t*)(NV2A_BASE + PRAMIN + 0x10000u);
    volatile uint32_t zero = 0u;
    volatile uint32_t one = 1u;

    dma[0] = zero;
    dma[1] = 0x0000FFFFu;
    dma[2] = physical_address;
    REG32(PFIFO_CACHE1_DMA_INSTANCE) = 0x1000u;
    REG32(PFIFO_CACHE1_DMA_STATE) = zero;
    REG32(PFIFO_CACHE1_PUSH0) = one;
    REG32(PFIFO_CACHE1_DMA_PUSH) = one;
}

int main(void)
{
    const uint32_t pb_physical = 0x00016000u;
    volatile uint32_t* pb = (volatile uint32_t*)(PHYS_BASE + pb_physical);
    volatile uint32_t zero = 0u;
    volatile uint32_t put = 40u;

    xt_begin("v1", "nv2a_pfifo_packets");
    xt_note("PFIFO increasing, non-increasing, count, and subchannel decoding");

    pb[0] = 0x000C0240u;
    pb[1] = 0x11111111u;
    pb[2] = 0x22222222u;
    pb[3] = 0x33333333u;
    pb[4] = 0x400C0260u;
    pb[5] = 0x44444444u;
    pb[6] = 0x55555555u;
    pb[7] = 0x66666666u;
    pb[8] = 0x00046280u;
    pb[9] = 0x77777777u;

    REG32(PGRAPH + 0x264u) = 0xA5A5A5A5u;
    setup_dma(pb_physical);
    REG32(PFIFO_CACHE1_DMA_GET) = zero;
    REG32(PFIFO_CACHE1_DMA_PUT) = put;

    xt_check_u32("nv2a.packets.get_eq_put", 40u, REG32(PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.packets.inc_0", 0x11111111u, REG32(PGRAPH + 0x240u));
    xt_check_u32("nv2a.packets.inc_1", 0x22222222u, REG32(PGRAPH + 0x244u));
    xt_check_u32("nv2a.packets.inc_2", 0x33333333u, REG32(PGRAPH + 0x248u));
    xt_check_u32("nv2a.packets.noninc_last", 0x66666666u, REG32(PGRAPH + 0x260u));
    xt_check_u32("nv2a.packets.noninc_no_advance", 0xA5A5A5A5u, REG32(PGRAPH + 0x264u));
    xt_check_u32("nv2a.packets.subchannel_3", 0x77777777u, REG32(PGRAPH + 0x280u));

    return xt_end();
}
