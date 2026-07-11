// SPDX-License-Identifier: MIT
// PGRAPH method-zero object binding and subchannel isolation probe.

#include "nv2a_test.h"
#include "xtest.h"
#include <xboxkrnl/xboxkrnl.h>

#define KELVIN_HANDLE   0x00000111u
#define SURF2D_HANDLE   0x00000112u
#define DMA_HANDLE      0x00000113u
#define KELVIN_INSTANCE 0x00004000u
#define SURF2D_INSTANCE 0x00005000u
#define DMA_INSTANCE    0x00006000u

static void setup_objects(volatile uint32_t* target)
{
    uint32_t physical = (uint32_t)MmGetPhysicalAddress((void*)target);
    volatile uint32_t* dma =
        (volatile uint32_t*)(NV2A_BASE + NV2A_PRAMIN + DMA_INSTANCE);

    nv2a_setup_ramht(0u);
    nv2a_write_object(KELVIN_INSTANCE, 0x97u);
    nv2a_write_object(SURF2D_INSTANCE, 0x62u);
    dma[0] = ((physical & 0xFFFu) << 20) | 0x00000003u;
    dma[1] = 15u;
    dma[2] = physical & 0xFFFFF000u;
    nv2a_write_ramht(KELVIN_HANDLE, KELVIN_INSTANCE,
                     NV2A_RAMHT_ENGINE_GRAPHICS, 1);
    nv2a_write_ramht(SURF2D_HANDLE, SURF2D_INSTANCE,
                     NV2A_RAMHT_ENGINE_GRAPHICS, 1);
    nv2a_write_ramht(DMA_HANDLE, DMA_INSTANCE,
                     NV2A_RAMHT_ENGINE_GRAPHICS, 1);
}

static uint32_t append_method(volatile uint32_t* pb, uint32_t index,
                              uint32_t subchannel, uint32_t method,
                              uint32_t data)
{
    pb[index++] = nv2a_packet(method, subchannel, 1u, 0);
    pb[index++] = data;
    return index;
}

int main(void)
{
    const uint32_t pb_physical = 0x00020000u;
    volatile uint32_t* pb =
        (volatile uint32_t*)(NV2A_PHYS_BASE + pb_physical);
    volatile uint32_t* target =
        (volatile uint32_t*)MmAllocateContiguousMemory(16u);
    uint32_t count = 0u;

    xt_begin("v1", "nv2a_pgraph_bind");
    xt_note("PGRAPH SET_OBJECT class selection and subchannel isolation");
    xt_check_bool("nv2a.bind.alloc", 1, target != NULL);
    if(target == NULL)
    {
        return xt_end();
    }

    target[0] = 0xAAAAAAAAu;
    setup_objects(target);
    nv2a_setup_dma(pb_physical, 0x0000FFFFu);

    count = append_method(pb, count, 0u, 0x000u, KELVIN_HANDLE);
    count = append_method(pb, count, 0u, 0x1A4u, DMA_HANDLE);
    count = append_method(pb, count, 0u, 0x1D6Cu, 0u);
    count = append_method(pb, count, 0u, 0x1D70u, 0x11111111u);
    nv2a_reset_pusher();
    nv2a_kick(count * 4u);
    xt_check_u32("nv2a.bind.kelvin_executes", 0x11111111u, target[0]);

    count = 0u;
    count = append_method(pb, count, 1u, 0x000u, SURF2D_HANDLE);
    count = append_method(pb, count, 1u, 0x1D70u, 0x22222222u);
    nv2a_reset_pusher();
    nv2a_kick(count * 4u);
    xt_check_u32("nv2a.bind.non_kelvin_ignored", 0x11111111u, target[0]);

    count = 0u;
    count = append_method(pb, count, 0u, 0x1D70u, 0x33333333u);
    nv2a_reset_pusher();
    nv2a_kick(count * 4u);
    xt_check_u32("nv2a.bind.subchannel0_persists", 0x33333333u, target[0]);

    count = 0u;
    count = append_method(pb, count, 1u, 0x000u, 0x0000DEADu);
    count = append_method(pb, count, 1u, 0x1D70u, 0x44444444u);
    nv2a_reset_pusher();
    nv2a_kick(count * 4u);
    xt_check_u32("nv2a.bind.invalid_does_not_rebind", 0x33333333u, target[0]);
    xt_check_u32("nv2a.bind.get_eq_put", count * 4u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET));

    MmFreeContiguousMemory((void*)target);
    return xt_end();
}
