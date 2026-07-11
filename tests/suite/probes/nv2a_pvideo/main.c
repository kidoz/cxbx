// SPDX-License-Identifier: MIT
//
// nv2a_pvideo - NV2A PVIDEO (video-port overlay/capture) registers, exercised
// through the 0xFD000000 MMIO aperture. On CXBX these accesses fault and are
// trap-and-emulated into the NV2A model in Emu.cpp; on an LLE target they hit
// the real GPU model. Self-checking against deterministic values.
//
// This is a GAP probe, not a behavior probe. Cxbx's NV2A model special-cases
// PMC/PFIFO/PGRAPH/PCRTC/PFB/PRAMDAC but does NOT model the PVIDEO engine, so
// PVIDEO register offsets fall through to the MMIO decoder's default path:
// reads return the cache (0 on first access), writes are cached, and there is
// no overlay/buffer/scanout behavior. Two groups of checks encode CURRENT
// reality so the golden locks it:
//
//   nv2a.pvideo_*.read_zero   expect=0  -- fresh read returns 0 (no model)
//   nv2a.pvideo_*.roundtrip   expect=P  -- a write is cached and reads back
//
// When a real PVIDEO model lands, the read-zero group flips (the engine now
// reports real state) and the round-trip group may change semantics; update
// the expectations and the golden then, exactly like d3d_tex_swizzle.
//
// Tagged "nv2a" (probe.toml) so it is skipped on targets that do not advertise
// a GPU model.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE  0xFD000000u
#define REG32(off) (*(volatile uint32_t*)(NV2A_BASE + (uint32_t)(off)))

// PMC anchor -- a modeled register, so this confirms the aperture is live and
// the trap-and-emulate path works before we probe the unmodeled PVIDEO block.
#define PMC_BOOT_0 0x000000u

// PVIDEO register offsets (0x008000..0x00809C). These names follow the
// canonical NV2A register map (xemu/open-rate); they are not in nxdk's
// nv_regs.h, so the offsets are defined here directly, like nv2a_pmc.
#define PVIDEO_SIZE_IN   0x008000u
#define PVIDEO_POINT_IN  0x008004u
#define PVIDEO_SIZE_OUT  0x008008u
#define PVIDEO_POINT_OUT 0x00800Cu
#define PVIDEO_FORMAT    0x008018u
#define PVIDEO_BASE      0x008090u
#define PVIDEO_LIMIT     0x008094u

int main(void)
{
    xt_begin("v1", "nv2a_pvideo");
    xt_note("NV2A PVIDEO via 0xFD000000 MMIO (unmodeled on Cxbx: cache-backed)");

    // --- PMC_BOOT_0: fixed chip ID (aperture-live canary) --------------------
    // Same anchor as nv2a_pmc: if this passes, the guest -> access-violation
    // -> instruction-decode -> NV2A model path works for this codegen.
    uint32_t boot0 = REG32(PMC_BOOT_0);
    xt_ev("PMC_BOOT_0 = 0x%08lX", (unsigned long)boot0);
    xt_check_u32("nv2a.pmc_boot0", 0x02A000A1u, boot0);

    // --- PVIDEO: fresh reads return 0 (no engine model) ---------------------
    // Each register is read once before any write. On Cxbx the MMIO default
    // returns the cached value, which is 0 on first access. A target that
    // models PVIDEO would report real register reset state here.
    uint32_t r_size = REG32(PVIDEO_SIZE_IN);
    uint32_t r_point = REG32(PVIDEO_POINT_IN);
    uint32_t r_fmt = REG32(PVIDEO_FORMAT);
    uint32_t r_base = REG32(PVIDEO_BASE);
    uint32_t r_limit = REG32(PVIDEO_LIMIT);
    xt_ev("PVIDEO_SIZE_IN=0x%08lX POINT_IN=0x%08lX FORMAT=0x%08lX",
          (unsigned long)r_size, (unsigned long)r_point, (unsigned long)r_fmt);
    xt_ev("PVIDEO_BASE=0x%08lX LIMIT=0x%08lX",
          (unsigned long)r_base, (unsigned long)r_limit);
    xt_check_u32("nv2a.pvideo_size_in.read_zero", 0u, r_size);
    xt_check_u32("nv2a.pvideo_format.read_zero", 0u, r_fmt);
    xt_check_u32("nv2a.pvideo_base.read_zero", 0u, r_base);
    xt_check_u32("nv2a.pvideo_limit.read_zero", 0u, r_limit);

    // --- PVIDEO: write/readback round-trip (cache-backed, not modeled) ------
    // Volatile source array so the stores compile to register stores
    // (mov [mem],reg) rather than immediate stores, keeping the faulting
    // instruction in the set the MMIO decoder handles (same discipline as
    // nv2a_pmc's RAMIN writes).
    volatile uint32_t pattern[4] = {
        0x01234567u,
        0x89ABCDEFu,
        0x00010001u, // a plausible SIZE/POINT-style packed field
        0x00FF00FFu,
    };

    REG32(PVIDEO_SIZE_IN) = pattern[0];
    REG32(PVIDEO_POINT_IN) = pattern[1];
    REG32(PVIDEO_BASE) = pattern[2];
    REG32(PVIDEO_LIMIT) = pattern[3];

    xt_check_u32("nv2a.pvideo_size_in.roundtrip", pattern[0], REG32(PVIDEO_SIZE_IN));
    xt_check_u32("nv2a.pvideo_point_in.roundtrip", pattern[1], REG32(PVIDEO_POINT_IN));
    xt_check_u32("nv2a.pvideo_base.roundtrip", pattern[2], REG32(PVIDEO_BASE));
    xt_check_u32("nv2a.pvideo_limit.roundtrip", pattern[3], REG32(PVIDEO_LIMIT));

    return xt_end();
}
