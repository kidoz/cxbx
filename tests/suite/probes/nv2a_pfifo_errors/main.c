// SPDX-License-Identifier: MIT
// PFIFO reserved-command, protection, suspension, and recovery probe.

#include "nv2a_test.h"
#include "xtest.h"

#define DMA_ERROR_MASK        0xE0000000u
#define DMA_ERROR_RESERVED    0x80000000u
#define DMA_ERROR_PROTECTION  0xC0000000u
#define DMA_PUSH_SUSPENDED    0x00001000u
#define PFIFO_INTR_DMA_PUSHER 0x00001000u

int main(void)
{
    const uint32_t pb_physical = 0x0001A000u;
    volatile uint32_t* pb =
        (volatile uint32_t*)(NV2A_PHYS_BASE + pb_physical);

    xt_begin("v1", "nv2a_pfifo_errors");
    xt_note("PFIFO error classification, suspension, W1C, and recovery");

    pb[0] = 0xE0000000u;
    pb[1] = 0u;
    nv2a_setup_dma(pb_physical, 0x0000FFFFu);
    nv2a_reset_pusher();
    nv2a_kick(8u);

    xt_check_u32("nv2a.errors.reserved_state", DMA_ERROR_RESERVED,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) & DMA_ERROR_MASK);
    xt_check_u32("nv2a.errors.reserved_get", 4u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.errors.reserved_suspended", DMA_PUSH_SUSPENDED,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_PUSH) & DMA_PUSH_SUSPENDED);
    xt_check_u32("nv2a.errors.reserved_intr", PFIFO_INTR_DMA_PUSHER,
                 NV2A_REG32(NV2A_PFIFO_INTR_0) & PFIFO_INTR_DMA_PUSHER);

    pb[0] = nv2a_packet(0x240u, 0u, 1u, 0);
    pb[1] = 0x12345678u;
    nv2a_setup_dma(pb_physical, 3u);
    nv2a_reset_pusher();
    nv2a_kick(8u);

    xt_check_u32("nv2a.errors.protection_state", DMA_ERROR_PROTECTION,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) & DMA_ERROR_MASK);
    xt_check_u32("nv2a.errors.protection_get", 4u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.errors.protection_count", 1u,
                 (NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) >> 18) & 0x7FFu);
    xt_check_u32("nv2a.errors.protection_intr", PFIFO_INTR_DMA_PUSHER,
                 NV2A_REG32(NV2A_PFIFO_INTR_0) & PFIFO_INTR_DMA_PUSHER);

    nv2a_setup_dma(pb_physical, 0x0000FFFFu);
    nv2a_reset_pusher();
    nv2a_kick(8u);
    xt_check_u32("nv2a.errors.recovery_get", 8u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.errors.recovery_state", 0u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) & DMA_ERROR_MASK);
    xt_check_u32("nv2a.errors.recovery_data", 0x12345678u,
                 NV2A_REG32(NV2A_PGRAPH + 0x240u));
    xt_check_u32("nv2a.errors.recovery_intr_clear", 0u,
                 NV2A_REG32(NV2A_PFIFO_INTR_0) & PFIFO_INTR_DMA_PUSHER);

    return xt_end();
}
