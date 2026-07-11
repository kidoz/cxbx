// SPDX-License-Identifier: MIT
// NV2A PRAMIN boundary and subword-access probe.

#include "xtest.h"
#include <stdint.h>

#define NV2A_BASE   0xFD000000u
#define PRAMIN      0x700000u
#define PRAMIN_SIZE 0x00100000u

int main(void)
{
    volatile uint32_t* first = (volatile uint32_t*)(NV2A_BASE + PRAMIN);
    volatile uint32_t* last = (volatile uint32_t*)(NV2A_BASE + PRAMIN + PRAMIN_SIZE - 4u);
    volatile uint8_t* bytes = (volatile uint8_t*)(NV2A_BASE + PRAMIN + 0x100u);
    volatile uint16_t* halves = (volatile uint16_t*)(NV2A_BASE + PRAMIN + 0x104u);
    volatile uint32_t first_pattern = 0x01234567u;
    volatile uint32_t last_pattern = 0x89ABCDEFu;

    xt_begin("v1", "nv2a_pramin");
    xt_note("PRAMIN first/last dwords and byte/halfword read-modify-write");

    *first = first_pattern;
    *last = last_pattern;
    xt_check_u32("nv2a.pramin.first_dword", 0x01234567u, *first);
    xt_check_u32("nv2a.pramin.last_dword", 0x89ABCDEFu, *last);

    *(volatile uint32_t*)bytes = 0xA1B2C3D4u;
    bytes[1] = 0x5Au;
    xt_check_u32("nv2a.pramin.byte_rmw", 0xA1B25AD4u, *(volatile uint32_t*)bytes);
    xt_check_u32("nv2a.pramin.byte_read", 0x5Au, bytes[1]);

    *(volatile uint32_t*)halves = 0x11223344u;
    halves[1] = 0xBEEFu;
    xt_check_u32("nv2a.pramin.half_rmw", 0xBEEF3344u, *(volatile uint32_t*)halves);
    xt_check_u32("nv2a.pramin.half_read", 0xBEEFu, halves[1]);

    return xt_end();
}
