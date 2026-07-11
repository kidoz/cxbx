// SPDX-License-Identifier: MIT
// NV2A PFIFO jump, call/return, and subroutine-error probe.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE  0xFD000000u
#define PHYS_BASE  0x80000000u
#define REG32(off) (*(volatile uint32_t*)(NV2A_BASE + (uint32_t)(off)))

#define PFIFO_CACHE1_PUSH0          0x003200u
#define PFIFO_CACHE1_DMA_PUSH       0x003220u
#define PFIFO_CACHE1_DMA_STATE      0x003228u
#define PFIFO_CACHE1_DMA_INSTANCE   0x00322Cu
#define PFIFO_CACHE1_DMA_PUT        0x003240u
#define PFIFO_CACHE1_DMA_GET        0x003244u
#define PFIFO_CACHE1_DMA_SUBROUTINE 0x00324Cu
#define PRAMIN                      0x700000u
#define PGRAPH                      0x400000u

#define DMA_STATE_ERROR_MASK   0xE0000000u
#define DMA_STATE_ERROR_CALL   0x20000000u
#define DMA_STATE_ERROR_RETURN 0x60000000u
#define DMA_PUSH_SUSPENDED     0x00001000u

static void setup_dma(uint32_t physical_address)
{
    volatile uint32_t* dma = (volatile uint32_t*)(NV2A_BASE + PRAMIN + 0x10000u);
    volatile uint32_t zero = 0u;
    volatile uint32_t one = 1u;

    dma[0] = zero;
    dma[1] = 0x0000FFFFu;
    dma[2] = physical_address;
    REG32(PFIFO_CACHE1_DMA_INSTANCE) = 0x1000u;
    REG32(PFIFO_CACHE1_PUSH0) = one;
}

static void reset_pusher(void)
{
    volatile uint32_t zero = 0u;
    volatile uint32_t one = 1u;

    REG32(PFIFO_CACHE1_DMA_PUSH) = zero;
    REG32(PFIFO_CACHE1_DMA_STATE) = zero;
    REG32(PFIFO_CACHE1_DMA_SUBROUTINE) = zero;
    REG32(PFIFO_CACHE1_DMA_GET) = zero;
    REG32(PFIFO_CACHE1_DMA_PUT) = zero;
    REG32(PFIFO_CACHE1_DMA_PUSH) = one;
}

static void kick(uint32_t put)
{
    volatile uint32_t put_value = put;
    REG32(PFIFO_CACHE1_DMA_PUT) = put_value;
}

int main(void)
{
    const uint32_t pb_physical = 0x00014000u;
    volatile uint32_t* pb = (volatile uint32_t*)(PHYS_BASE + pb_physical);

    xt_begin("v1", "nv2a_pfifo_control_flow");
    xt_note("PFIFO old/new jumps, one-level CALL/RETURN, and invalid subroutine state");
    setup_dma(pb_physical);

    pb[0] = 0x00000012u;
    pb[1] = 0x00040210u;
    pb[2] = 0x11112222u;
    pb[3] = 0x0000001Du;
    pb[4] = 0x00040220u;
    pb[5] = 0x33334444u;
    pb[6] = 0x00020000u;
    reset_pusher();
    kick(28u);

    xt_check_u32("nv2a.control.call_body", 0x33334444u, REG32(PGRAPH + 0x220u));
    xt_check_u32("nv2a.control.return_body", 0x11112222u, REG32(PGRAPH + 0x210u));
    xt_check_u32("nv2a.control.call_get_eq_put", 28u, REG32(PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.control.subroutine_cleared", 0u, REG32(PFIFO_CACHE1_DMA_SUBROUTINE));

    REG32(PGRAPH + 0x230u) = 0xA5A5A5A5u;
    pb[0] = 0x2000000Cu;
    pb[1] = 0x00040230u;
    pb[2] = 0xDEADDEADu;
    pb[3] = 0x00040234u;
    pb[4] = 0x55556666u;
    reset_pusher();
    kick(20u);

    xt_check_u32("nv2a.control.old_jump_skips", 0xA5A5A5A5u, REG32(PGRAPH + 0x230u));
    xt_check_u32("nv2a.control.old_jump_target", 0x55556666u, REG32(PGRAPH + 0x234u));
    xt_check_u32("nv2a.control.old_jump_get_eq_put", 20u, REG32(PFIFO_CACHE1_DMA_GET));

    pb[0] = 0x00020000u;
    pb[1] = 0u;
    reset_pusher();
    kick(8u);

    xt_check_u32("nv2a.control.invalid_return_error", DMA_STATE_ERROR_RETURN,
                 REG32(PFIFO_CACHE1_DMA_STATE) & DMA_STATE_ERROR_MASK);
    xt_check_u32("nv2a.control.invalid_return_get", 4u, REG32(PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.control.invalid_return_suspended", DMA_PUSH_SUSPENDED,
                 REG32(PFIFO_CACHE1_DMA_PUSH) & DMA_PUSH_SUSPENDED);

    pb[0] = 0x0000000Au;
    pb[1] = 0x00000011u;
    pb[2] = 0x0000000Eu;
    pb[3] = 0x00020000u;
    reset_pusher();
    kick(16u);

    xt_check_u32("nv2a.control.nested_call_error", DMA_STATE_ERROR_CALL,
                 REG32(PFIFO_CACHE1_DMA_STATE) & DMA_STATE_ERROR_MASK);
    xt_check_u32("nv2a.control.nested_call_get", 12u, REG32(PFIFO_CACHE1_DMA_GET));
    xt_check_u32("nv2a.control.nested_call_suspended", DMA_PUSH_SUSPENDED,
                 REG32(PFIFO_CACHE1_DMA_PUSH) & DMA_PUSH_SUSPENDED);

    reset_pusher();
    return xt_end();
}
