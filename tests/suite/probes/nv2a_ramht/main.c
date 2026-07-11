// SPDX-License-Identifier: MIT
// NV2A RAMHT hash, valid-bit, collision-slot, and object-translation probe.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE  0xFD000000u
#define PHYS_BASE  0x80000000u
#define REG32(off) (*(volatile uint32_t*)(NV2A_BASE + (uint32_t)(off)))

#define PFIFO_RAMHT               0x002210u
#define PFIFO_CACHE1_PUSH0        0x003200u
#define PFIFO_CACHE1_PUSH1        0x003204u
#define PFIFO_CACHE1_DMA_PUSH     0x003220u
#define PFIFO_CACHE1_DMA_STATE    0x003228u
#define PFIFO_CACHE1_DMA_INSTANCE 0x00322Cu
#define PFIFO_CACHE1_DMA_PUT      0x003240u
#define PFIFO_CACHE1_DMA_GET      0x003244u
#define PRAMIN                    0x700000u
#define PGRAPH                    0x400000u

#define RAMHT_BASE            0x00002000u
#define RAMHT_SIZE            0x00001000u
#define RAMHT_REGISTER        0x00000020u
#define RAMHT_VALID           0x80000000u
#define RAMHT_ENGINE_GRAPHICS 0x00010000u

static uint32_t ramht_hash(uint32_t handle, uint32_t channel)
{
    const uint32_t bits = 11u;
    uint32_t hash = 0u;

    while(handle != 0u)
    {
        hash ^= handle & ((1u << bits) - 1u);
        handle >>= bits;
    }

    hash ^= channel << (bits - 4u);
    return hash & ((RAMHT_SIZE / 8u) - 1u);
}

static void write_ramht_entry(uint32_t slot, uint32_t handle, uint32_t context)
{
    volatile uint32_t* entry = (volatile uint32_t*)(NV2A_BASE + PRAMIN + RAMHT_BASE + slot * 8u);
    entry[0] = handle;
    entry[1] = context;
}

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
    REG32(PFIFO_CACHE1_DMA_PUSH) = one;
}

static void submit_object_method(volatile uint32_t* pb, uint32_t handle)
{
    volatile uint32_t zero = 0u;
    volatile uint32_t put = 8u;

    pb[0] = 0x00040180u;
    pb[1] = handle;
    REG32(PFIFO_CACHE1_DMA_STATE) = zero;
    REG32(PFIFO_CACHE1_DMA_GET) = zero;
    REG32(PFIFO_CACHE1_DMA_PUT) = put;
}

int main(void)
{
    const uint32_t pb_physical = 0x00012000u;
    const uint32_t direct_handle = 0x00000001u;
    const uint32_t collision_handle = 0x00001802u;
    const uint32_t invalid_handle = 0x00000055u;
    const uint32_t direct_instance = 0x00004000u;
    const uint32_t collision_instance = 0x00005000u;
    const uint32_t channel = 0u;
    const uint32_t direct_slot = ramht_hash(direct_handle, channel);
    const uint32_t collision_slot = ramht_hash(collision_handle, channel);
    const uint32_t invalid_slot = ramht_hash(invalid_handle, channel);
    volatile uint32_t* pb = (volatile uint32_t*)(PHYS_BASE + pb_physical);
    volatile uint32_t* direct_object = (volatile uint32_t*)(NV2A_BASE + PRAMIN + direct_instance);
    volatile uint32_t* collision_object = (volatile uint32_t*)(NV2A_BASE + PRAMIN + collision_instance);

    xt_begin("v1", "nv2a_ramht");
    xt_note("RAMHT hashing and object-handle translation through PFIFO/PGRAPH");

    REG32(PFIFO_RAMHT) = RAMHT_REGISTER;
    REG32(PFIFO_CACHE1_PUSH1) = channel;
    direct_object[0] = 0x00000097u;
    collision_object[0] = 0x00000097u;

    xt_check_u32("nv2a.ramht.collision_hash", direct_slot, collision_slot);
    write_ramht_entry(direct_slot, direct_handle,
                      RAMHT_VALID | RAMHT_ENGINE_GRAPHICS | (direct_instance >> 4u));
    write_ramht_entry((direct_slot + 1u) & ((RAMHT_SIZE / 8u) - 1u), collision_handle,
                      RAMHT_VALID | RAMHT_ENGINE_GRAPHICS | (collision_instance >> 4u));
    write_ramht_entry(invalid_slot, invalid_handle, 0u);
    setup_dma(pb_physical);

    submit_object_method(pb, direct_handle);
    xt_check_u32("nv2a.ramht.direct_instance", direct_instance, REG32(PGRAPH + 0x180u));

    submit_object_method(pb, collision_handle);
    xt_check_u32("nv2a.ramht.collision_instance", collision_instance, REG32(PGRAPH + 0x180u));

    REG32(PGRAPH + 0x180u) = 0xC0DEC0DEu;
    submit_object_method(pb, invalid_handle);
    xt_check_u32("nv2a.ramht.invalid_not_translated", invalid_handle, REG32(PGRAPH + 0x180u));
    xt_check_u32("nv2a.ramht.get_eq_put", 8u, REG32(PFIFO_CACHE1_DMA_GET));

    return xt_end();
}
