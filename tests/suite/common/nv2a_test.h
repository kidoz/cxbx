// SPDX-License-Identifier: MIT
// Shared direct-MMIO helpers for focused NV2A conformance probes.

#ifndef XTEST_NV2A_TEST_H
#define XTEST_NV2A_TEST_H

#include <stdint.h>

#define NV2A_BASE       0xFD000000u
#define NV2A_PHYS_BASE  0x80000000u
#define NV2A_REG32(off) (*(volatile uint32_t*)(NV2A_BASE + (uint32_t)(off)))

#define NV2A_PMC_INTR_0                  0x000100u
#define NV2A_PFIFO_INTR_0                0x002100u
#define NV2A_PFIFO_INTR_EN_0             0x002140u
#define NV2A_PFIFO_RAMHT                 0x002210u
#define NV2A_PFIFO_CACHE1_PUSH0          0x003200u
#define NV2A_PFIFO_CACHE1_PUSH1          0x003204u
#define NV2A_PFIFO_CACHE1_DMA_PUSH       0x003220u
#define NV2A_PFIFO_CACHE1_DMA_STATE      0x003228u
#define NV2A_PFIFO_CACHE1_DMA_INSTANCE   0x00322Cu
#define NV2A_PFIFO_CACHE1_DMA_PUT        0x003240u
#define NV2A_PFIFO_CACHE1_DMA_GET        0x003244u
#define NV2A_PFIFO_CACHE1_DMA_SUBROUTINE 0x00324Cu
#define NV2A_PFIFO_CACHE1_DMA_DCOUNT     0x0032A0u
#define NV2A_PGRAPH_INTR                 0x400100u
#define NV2A_PGRAPH_INTR_EN              0x400140u
#define NV2A_PRAMIN                      0x700000u
#define NV2A_PGRAPH                      0x400000u

#define NV2A_RAMHT_BASE            0x00002000u
#define NV2A_RAMHT_SIZE            0x00001000u
#define NV2A_RAMHT_REGISTER        0x00000020u
#define NV2A_RAMHT_VALID           0x80000000u
#define NV2A_RAMHT_ENGINE_GRAPHICS 0x00010000u

static inline uint32_t nv2a_packet(uint32_t method, uint32_t subchannel,
                                   uint32_t count, int non_incrementing)
{
    return (non_incrementing ? 0x40000000u : 0u) |
           ((count & 0x7FFu) << 18) | ((subchannel & 7u) << 13) |
           (method & 0x1FFCu);
}

static inline void nv2a_setup_dma(uint32_t physical_address, uint32_t limit)
{
    volatile uint32_t* dma =
        (volatile uint32_t*)(NV2A_BASE + NV2A_PRAMIN + 0x10000u);
    volatile uint32_t zero = 0u;

    dma[0] = zero;
    dma[1] = limit;
    dma[2] = physical_address;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_INSTANCE) = 0x1000u;
}

static inline void nv2a_reset_pusher(void)
{
    volatile uint32_t zero = 0u;
    volatile uint32_t one = 1u;
    volatile uint32_t all = 0xFFFFFFFFu;

    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_PUSH) = zero;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_STATE) = zero;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_SUBROUTINE) = zero;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_DCOUNT) = zero;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET) = zero;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_PUT) = zero;
    NV2A_REG32(NV2A_PFIFO_INTR_0) = all;
    NV2A_REG32(NV2A_PFIFO_CACHE1_PUSH0) = one;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_PUSH) = one;
}

static inline void nv2a_kick(uint32_t put)
{
    volatile uint32_t put_value = put;
    NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_PUT) = put_value;
}

static inline uint32_t nv2a_ramht_hash(uint32_t handle, uint32_t channel)
{
    const uint32_t bits = 11u;
    uint32_t hash = 0u;

    while(handle != 0u)
    {
        hash ^= handle & ((1u << bits) - 1u);
        handle >>= bits;
    }

    hash ^= channel << (bits - 4u);
    return hash & ((NV2A_RAMHT_SIZE / 8u) - 1u);
}

static inline void nv2a_setup_ramht(uint32_t channel)
{
    NV2A_REG32(NV2A_PFIFO_RAMHT) = NV2A_RAMHT_REGISTER;
    NV2A_REG32(NV2A_PFIFO_CACHE1_PUSH1) = channel & 0x1Fu;
}

static inline void nv2a_write_ramht(uint32_t handle, uint32_t instance,
                                    uint32_t engine, int valid)
{
    uint32_t slot = nv2a_ramht_hash(handle, 0u);
    volatile uint32_t* entry = (volatile uint32_t*)(NV2A_BASE + NV2A_PRAMIN +
                                                    NV2A_RAMHT_BASE + slot * 8u);
    entry[0] = handle;
    entry[1] = (valid ? NV2A_RAMHT_VALID : 0u) | engine | (instance >> 4u);
}

static inline void nv2a_write_object(uint32_t instance, uint32_t object_class)
{
    volatile uint32_t* object =
        (volatile uint32_t*)(NV2A_BASE + NV2A_PRAMIN + instance);
    object[0] = object_class;
}

#endif
