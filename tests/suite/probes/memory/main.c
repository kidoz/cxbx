// SPDX-License-Identifier: MIT
//
// memory - exercises the guest RAM / contiguous-memory allocator. Uses only
// kernel exports Cxbx already implements (MmAllocateContiguousMemory / Free);
// physical-address mapping (MmGetPhysicalAddress) is covered by the kernel_cov
// ordinal sweep instead, since it is unimplemented on some targets.

#include "xtrace.h"
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

int main(void)
{
    const SIZE_T SZ = 64u * 1024u;   // 64 KiB
    SIZE_T i;

    xt_begin("v1", "memory");

    void *p = MmAllocateContiguousMemory(SZ);
    xt_ev("MmAllocateContiguousMemory(%lu) -> 0x%08lX",
          (unsigned long)SZ, (unsigned long)(uintptr_t)p);
    xt_check_bool("mm.alloc_nonnull", 1, p != NULL);

    if (p != NULL) {
        // Contiguous memory must be page-aligned (4 KiB).
        xt_check_u32("mm.alloc_page_aligned", 0u, (uint32_t)((uintptr_t)p & 0xFFFu));

        // Byte pattern write + readback across the whole block.
        unsigned char *b = (unsigned char *)p;
        for (i = 0; i < SZ; i++) {
            b[i] = (unsigned char)((i * 31u + 7u) & 0xFFu);
        }
        int ok = 1;
        for (i = 0; i < SZ; i++) {
            if (b[i] != (unsigned char)((i * 31u + 7u) & 0xFFu)) { ok = 0; break; }
        }
        xt_check_bool("mm.readback_pattern", 1, ok);

        // 32-bit word integrity at both ends of the allocation.
        volatile uint32_t *w = (volatile uint32_t *)p;
        w[0] = 0xDEADBEEFu;
        w[(SZ / 4) - 1] = 0xCAFEBABEu;
        xt_check_u32("mm.word_first", 0xDEADBEEFu, w[0]);
        xt_check_u32("mm.word_last",  0xCAFEBABEu, w[(SZ / 4) - 1]);

        MmFreeContiguousMemory(p);
        xt_note("freed contiguous block");
    }

    return xt_end();
}
