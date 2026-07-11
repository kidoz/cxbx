// SPDX-License-Identifier: MIT
// PFIFO packet state persistence across partial PUT updates.

#include "nv2a_test.h"
#include "xtest.h"

int main(void)
{
    const uint32_t pb_physical = 0x00018000u;
    volatile uint32_t* pb =
        (volatile uint32_t*)(NV2A_PHYS_BASE + pb_physical);

    xt_begin("v1", "nv2a_pfifo_dma_state");
    xt_note("PFIFO method/count/DCOUNT persistence across split submissions");

    pb[0] = nv2a_packet(0x240u, 2u, 3u, 0);
    pb[1] = 0x11111111u;
    pb[2] = 0x22222222u;
    pb[3] = 0x33333333u;
    nv2a_setup_dma(pb_physical, 0x0000FFFFu);
    nv2a_reset_pusher();

    NV2A_REG32(NV2A_PGRAPH + 0x244u) = 0xA5A5A5A5u;
    nv2a_kick(8u);
    xt_check_u32("nv2a.dma_state.first_get", 8u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.dma_state.first_count", 2u,
                 (NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) >> 18) & 0x7FFu);
    xt_check_u32("nv2a.dma_state.first_method", 0x244u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) & 0x1FFCu);
    xt_check_u32("nv2a.dma_state.first_dcount", 1u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_DCOUNT));
    xt_check_u32("nv2a.dma_state.first_data", 0x11111111u,
                 NV2A_REG32(NV2A_PGRAPH + 0x240u));
    xt_check_u32("nv2a.dma_state.next_untouched", 0xA5A5A5A5u,
                 NV2A_REG32(NV2A_PGRAPH + 0x244u));

    nv2a_kick(12u);
    xt_check_u32("nv2a.dma_state.second_count", 1u,
                 (NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) >> 18) & 0x7FFu);
    xt_check_u32("nv2a.dma_state.second_method", 0x248u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) & 0x1FFCu);
    xt_check_u32("nv2a.dma_state.second_dcount", 2u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_DCOUNT));
    xt_check_u32("nv2a.dma_state.second_data", 0x22222222u,
                 NV2A_REG32(NV2A_PGRAPH + 0x244u));

    nv2a_kick(16u);
    xt_check_u32("nv2a.dma_state.final_get", 16u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.dma_state.final_count", 0u,
                 (NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) >> 18) & 0x7FFu);
    xt_check_u32("nv2a.dma_state.final_dcount", 3u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_DCOUNT));
    xt_check_u32("nv2a.dma_state.final_data", 0x33333333u,
                 NV2A_REG32(NV2A_PGRAPH + 0x248u));

    pb[0] = nv2a_packet(0x260u, 4u, 2u, 1);
    pb[1] = 0x44444444u;
    pb[2] = 0x55555555u;
    nv2a_reset_pusher();
    nv2a_kick(8u);
    nv2a_kick(12u);
    xt_check_u32("nv2a.dma_state.noninc_method", 0x260u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) & 0x1FFCu);
    xt_check_u32("nv2a.dma_state.noninc_last", 0x55555555u,
                 NV2A_REG32(NV2A_PGRAPH + 0x260u));

    return xt_end();
}
