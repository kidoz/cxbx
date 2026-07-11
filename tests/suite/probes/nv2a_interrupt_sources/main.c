// SPDX-License-Identifier: MIT
// PFIFO/PGRAPH pending interrupt, mask, aggregation, and W1C probe.

#include "nv2a_test.h"
#include "xtest.h"

#define PMC_INTR_PFIFO        0x00000100u
#define PMC_INTR_PGRAPH       0x00001000u
#define PFIFO_INTR_DMA_PUSHER 0x00001000u
#define PGRAPH_INTR_ERROR     0x00100000u

int main(void)
{
    const uint32_t pb_physical = 0x0001C000u;
    volatile uint32_t* pb =
        (volatile uint32_t*)(NV2A_PHYS_BASE + pb_physical);
    volatile uint32_t zero = 0u;
    volatile uint32_t all = 0xFFFFFFFFu;

    xt_begin("v1", "nv2a_interrupt_sources");
    xt_note("PFIFO/PGRAPH pending sources, enable masks, PMC aggregation, and W1C");

    NV2A_REG32(NV2A_PFIFO_INTR_0) = all;
    NV2A_REG32(NV2A_PGRAPH_INTR) = all;
    NV2A_REG32(NV2A_PFIFO_INTR_EN_0) = zero;
    NV2A_REG32(NV2A_PGRAPH_INTR_EN) = zero;

    pb[0] = 0xE0000000u;
    pb[1] = 0u;
    nv2a_setup_dma(pb_physical, 0x0000FFFFu);
    nv2a_reset_pusher();
    nv2a_kick(8u);
    xt_check_u32("nv2a.intr_sources.pfifo_pending", PFIFO_INTR_DMA_PUSHER,
                 NV2A_REG32(NV2A_PFIFO_INTR_0) & PFIFO_INTR_DMA_PUSHER);
    xt_check_u32("nv2a.intr_sources.pfifo_masked", 0u,
                 NV2A_REG32(NV2A_PMC_INTR_0) & PMC_INTR_PFIFO);

    NV2A_REG32(NV2A_PFIFO_INTR_EN_0) = PFIFO_INTR_DMA_PUSHER;
    xt_check_u32("nv2a.intr_sources.pfifo_aggregated", PMC_INTR_PFIFO,
                 NV2A_REG32(NV2A_PMC_INTR_0) & PMC_INTR_PFIFO);
    NV2A_REG32(NV2A_PFIFO_INTR_0) = PFIFO_INTR_DMA_PUSHER;
    xt_check_u32("nv2a.intr_sources.pfifo_w1c", 0u,
                 NV2A_REG32(NV2A_PFIFO_INTR_0) & PFIFO_INTR_DMA_PUSHER);
    xt_check_u32("nv2a.intr_sources.pfifo_aggregate_clear", 0u,
                 NV2A_REG32(NV2A_PMC_INTR_0) & PMC_INTR_PFIFO);

    pb[0] = nv2a_packet(0x100u, 0u, 1u, 0);
    pb[1] = 1u;
    nv2a_reset_pusher();
    nv2a_kick(8u);
    xt_check_u32("nv2a.intr_sources.pgraph_pending", PGRAPH_INTR_ERROR,
                 NV2A_REG32(NV2A_PGRAPH_INTR) & PGRAPH_INTR_ERROR);
    xt_check_u32("nv2a.intr_sources.pgraph_masked", 0u,
                 NV2A_REG32(NV2A_PMC_INTR_0) & PMC_INTR_PGRAPH);

    NV2A_REG32(NV2A_PGRAPH_INTR_EN) = PGRAPH_INTR_ERROR;
    xt_check_u32("nv2a.intr_sources.pgraph_aggregated", PMC_INTR_PGRAPH,
                 NV2A_REG32(NV2A_PMC_INTR_0) & PMC_INTR_PGRAPH);
    NV2A_REG32(NV2A_PGRAPH_INTR_EN) = zero;
    xt_check_u32("nv2a.intr_sources.pgraph_mask_after_pending", 0u,
                 NV2A_REG32(NV2A_PMC_INTR_0) & PMC_INTR_PGRAPH);
    NV2A_REG32(NV2A_PGRAPH_INTR) = PGRAPH_INTR_ERROR;
    xt_check_u32("nv2a.intr_sources.pgraph_w1c", 0u,
                 NV2A_REG32(NV2A_PGRAPH_INTR) & PGRAPH_INTR_ERROR);

    return xt_end();
}
