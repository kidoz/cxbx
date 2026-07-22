// SPDX-License-Identifier: MIT
//
// nv2a_pmc - NV2A PMC registers + RAMIN instance memory, exercised through the
// 0xFD000000 MMIO aperture. On CXBX these accesses fault and are trap-and-
// emulated into the NV2A model in emulation_runtime.cpp; on an LLE target they
// hit the real GPU model. Self-checking against fixed/deterministic values.
//
// This is also the round-trip canary: if `nv2a.pmc_boot0` reads back the chip
// ID, guest -> access-violation -> instruction-decode -> NV2A model works with
// the probe's clang codegen. Tagged "nv2a" (probe.toml) so it is skipped on
// targets that do not advertise a GPU model.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE   0xFD000000u
#define REG32(off)  (*(volatile uint32_t *)(NV2A_BASE + (uint32_t)(off)))

// Engine/register offsets (subset; see EmuNV2ALogging.h).
#define PMC_BOOT_0  0x000000u
#define PMC_ENABLE  0x000200u
#define PRAMIN      0x700000u

// PMC_ENABLE default bits for the FIFO/graphics engines.
#define PMC_ENABLE_PFIFO   0x00000100u
#define PMC_ENABLE_PGRAPH  0x00001000u

int main(void)
{
    xt_begin("v1", "nv2a_pmc");
    xt_note("NV2A PMC + RAMIN via 0xFD000000 MMIO (trap-and-emulate on Cxbx)");

    // --- PMC_BOOT_0: fixed chip ID (round-trip canary) -----------------------
    uint32_t boot0 = REG32(PMC_BOOT_0);
    xt_ev("PMC_BOOT_0 = 0x%08lX", (unsigned long)boot0);
    xt_check_u32("nv2a.pmc_boot0", 0x02A000A1u, boot0);

    // --- PMC_ENABLE: FIFO + graphics engines enabled by default --------------
    uint32_t enable = REG32(PMC_ENABLE);
    xt_ev("PMC_ENABLE = 0x%08lX", (unsigned long)enable);
    xt_check_bool("nv2a.pmc_enable_pfifo",  1, (enable & PMC_ENABLE_PFIFO) != 0);
    xt_check_bool("nv2a.pmc_enable_pgraph", 1, (enable & PMC_ENABLE_PGRAPH) != 0);

    // --- RAMIN: 1 MiB dword-addressable instance memory, write/readback ------
    // Volatile source array so the stores compile to register stores (mov
    // [mem],reg) rather than immediate stores, keeping the faulting instruction
    // in the set the MMIO decoder handles.
    volatile uint32_t pattern[3] = { 0xDEADBEEFu, 0xCAFEBABEu, 0x12345678u };
    volatile uint32_t *ramin = (volatile uint32_t *)(NV2A_BASE + PRAMIN);

    ramin[0] = pattern[0];
    ramin[1] = pattern[1];
    ramin[2] = pattern[2];

    xt_check_u32("nv2a.ramin_0", 0xDEADBEEFu, ramin[0]);
    xt_check_u32("nv2a.ramin_1", 0xCAFEBABEu, ramin[1]);
    xt_check_u32("nv2a.ramin_2", 0x12345678u, ramin[2]);

    return xt_end();
}
