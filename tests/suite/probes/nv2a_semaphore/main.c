// SPDX-License-Identifier: MIT
// Kelvin back-end semaphore release and DMA-boundary probe.

#include "nv2a_test.h"
#include "xtest.h"
#include <xboxkrnl/xboxkrnl.h>

#define KELVIN_HANDLE   0x00000101u
#define DMA_HANDLE      0x00000102u
#define KELVIN_INSTANCE 0x00004000u
#define DMA_INSTANCE    0x00005000u

static void setup_objects(volatile uint32_t* target)
{
    uint32_t physical = (uint32_t)MmGetPhysicalAddress((void*)target);
    volatile uint32_t* dma =
        (volatile uint32_t*)(NV2A_BASE + NV2A_PRAMIN + DMA_INSTANCE);

    nv2a_setup_ramht(0u);
    nv2a_write_object(KELVIN_INSTANCE, 0x97u);
    dma[0] = ((physical & 0xFFFu) << 20) | 0x00000003u;
    dma[1] = 15u;
    dma[2] = physical & 0xFFFFF000u;
    nv2a_write_ramht(KELVIN_HANDLE, KELVIN_INSTANCE,
                     NV2A_RAMHT_ENGINE_GRAPHICS, 1);
    nv2a_write_ramht(DMA_HANDLE, DMA_INSTANCE,
                     NV2A_RAMHT_ENGINE_GRAPHICS, 1);
}

static void submit_release(volatile uint32_t* pb, uint32_t dma_handle,
                           uint32_t offset, uint32_t value)
{
    pb[0] = nv2a_packet(0x000u, 0u, 1u, 0);
    pb[1] = KELVIN_HANDLE;
    pb[2] = nv2a_packet(0x1A4u, 0u, 1u, 0);
    pb[3] = dma_handle;
    pb[4] = nv2a_packet(0x1D6Cu, 0u, 1u, 0);
    pb[5] = offset;
    pb[6] = nv2a_packet(0x1D70u, 0u, 1u, 0);
    pb[7] = value;
    nv2a_reset_pusher();
    nv2a_kick(32u);
}

int main(void)
{
    const uint32_t pb_physical = 0x0001E000u;
    volatile uint32_t* pb =
        (volatile uint32_t*)(NV2A_PHYS_BASE + pb_physical);
    volatile uint32_t* target =
        (volatile uint32_t*)MmAllocateContiguousMemory(32u);

    xt_begin("v1", "nv2a_semaphore");
    xt_note("Kelvin BACK_END_WRITE_SEMAPHORE_RELEASE ordering and bounds");
    xt_check_bool("nv2a.semaphore.alloc", 1, target != NULL);
    if(target == NULL)
    {
        return xt_end();
    }

    target[0] = 0xAAAAAAAAu;
    target[1] = 0xBBBBBBBBu;
    target[4] = 0xCCCCCCCCu;
    setup_objects(target);
    nv2a_setup_dma(pb_physical, 0x0000FFFFu);

    submit_release(pb, DMA_HANDLE, 4u, 0x12345678u);
    xt_check_u32("nv2a.semaphore.before_untouched", 0xAAAAAAAAu, target[0]);
    xt_check_u32("nv2a.semaphore.release", 0x12345678u, target[1]);
    xt_check_u32("nv2a.semaphore.get_eq_put", 32u,
                 NV2A_REG32(NV2A_PFIFO_CACHE1_DMA_GET));

    submit_release(pb, DMA_HANDLE, 4u, 0x87654321u);
    xt_check_u32("nv2a.semaphore.repeat_release", 0x87654321u, target[1]);

    submit_release(pb, 0x0000DEADu, 4u, 0xDEADBEEFu);
    xt_check_u32("nv2a.semaphore.invalid_dma_safe", 0x87654321u, target[1]);

    submit_release(pb, DMA_HANDLE, 16u, 0xDEADBEEFu);
    xt_check_u32("nv2a.semaphore.limit_safe", 0xCCCCCCCCu, target[4]);

    MmFreeContiguousMemory((void*)target);
    return xt_end();
}
