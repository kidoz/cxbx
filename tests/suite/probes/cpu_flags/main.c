// SPDX-License-Identifier: MIT
//
// cpu_flags - x86 EFLAGS conformance. Executes arithmetic/logic instructions
// and checks the resulting value AND the condition flags against hand-verified,
// hardcoded expectations (the "ground truth" for a real Pentium III).
//
// No kernel dependencies beyond the trace harness, so this is the ideal first
// target for a CPU-accuracy emulator: a divergence here points straight at a
// flag-computation bug. (On an HLE emulator like Cxbx that runs guest code
// natively, every check passes, which also validates the expectations.)

#include "xtest.h"
#include <stdint.h>

// --- ALU helpers: run one instruction, capture result + EFLAGS ---------------

static uint8_t alu_add8(uint8_t a, uint8_t b, uint32_t *flags)
{
    uint8_t r = a; uint32_t f;
    __asm__ volatile("addb %2, %0\n\t pushfl\n\t popl %1"
                     : "+q"(r), "=r"(f) : "q"(b) : "cc");
    *flags = f; return r;
}

static uint8_t alu_sub8(uint8_t a, uint8_t b, uint32_t *flags)
{
    uint8_t r = a; uint32_t f;
    __asm__ volatile("subb %2, %0\n\t pushfl\n\t popl %1"
                     : "+q"(r), "=r"(f) : "q"(b) : "cc");
    *flags = f; return r;
}

static uint32_t alu_and32(uint32_t a, uint32_t b, uint32_t *flags)
{
    uint32_t r = a, f;
    __asm__ volatile("andl %2, %0\n\t pushfl\n\t popl %1"
                     : "+r"(r), "=r"(f) : "r"(b) : "cc");
    *flags = f; return r;
}

static uint32_t alu_or32(uint32_t a, uint32_t b, uint32_t *flags)
{
    uint32_t r = a, f;
    __asm__ volatile("orl %2, %0\n\t pushfl\n\t popl %1"
                     : "+r"(r), "=r"(f) : "r"(b) : "cc");
    *flags = f; return r;
}

static uint32_t alu_xor32(uint32_t a, uint32_t b, uint32_t *flags)
{
    uint32_t r = a, f;
    __asm__ volatile("xorl %2, %0\n\t pushfl\n\t popl %1"
                     : "+r"(r), "=r"(f) : "r"(b) : "cc");
    *flags = f; return r;
}

// --- Test vectors ------------------------------------------------------------
// exp_flags is the OR of the flags expected SET; flag_mask selects the flags
// that are architecturally defined for the op (AF is excluded for logic ops).

typedef struct { const char *name; uint8_t a, b, res; uint32_t exp_flags; } vec8;
typedef struct { const char *name; uint32_t a, b, res, mask, exp_flags; } vec32;

static const vec8 add8_vecs[] = {
    // 0x7F+0x01=0x80: signed overflow, sign set, aux carry; not zero/carry; odd parity
    { "add8.7F+01", 0x7F, 0x01, 0x80, XT_SF | XT_OF | XT_AF },
    // 0xFF+0x01=0x00: carry out, zero, aux carry, even parity
    { "add8.FF+01", 0xFF, 0x01, 0x00, XT_CF | XT_ZF | XT_AF | XT_PF },
    // 0x40+0x40=0x80: signed overflow + sign, no aux carry, odd parity
    { "add8.40+40", 0x40, 0x40, 0x80, XT_SF | XT_OF },
    // 0x00+0x00=0x00: zero, even parity
    { "add8.00+00", 0x00, 0x00, 0x00, XT_ZF | XT_PF },
};

static const vec8 sub8_vecs[] = {
    // 0x00-0x01=0xFF: borrow(CF), sign, aux borrow, even parity (0xFF has 8 bits)
    { "sub8.00-01", 0x00, 0x01, 0xFF, XT_CF | XT_SF | XT_AF | XT_PF },
    // 0x80-0x01=0x7F: signed overflow, aux borrow; not carry/sign/zero; odd parity
    { "sub8.80-01", 0x80, 0x01, 0x7F, XT_OF | XT_AF },
    // 0x05-0x05=0x00: zero, even parity
    { "sub8.05-05", 0x05, 0x05, 0x00, XT_ZF | XT_PF },
};

#define LOGIC_MASK (XT_CF | XT_OF | XT_ZF | XT_SF | XT_PF)  // AF undefined for logic

static const vec32 and_vecs[] = {
    // clears CF/OF; result 0 => ZF, even parity
    { "and32.F0F0&0F0F", 0xF0F0F0F0u, 0x0F0F0F0Fu, 0x00000000u, LOGIC_MASK, XT_ZF | XT_PF },
    // result low byte 0x0F (4 bits, even) => PF; nonzero, positive
    { "and32.FFFF&0F0F", 0xFFFFFFFFu, 0x00000F0Fu, 0x00000F0Fu, LOGIC_MASK, XT_PF },
};

static const vec32 or_vecs[] = {
    // 1|2=3: low byte 0x03 (2 bits, even) => PF
    { "or32.1|2", 0x00000001u, 0x00000002u, 0x00000003u, LOGIC_MASK, XT_PF },
    // high bit set => SF; low byte 0x00 => PF
    { "or32.8000_0000|0", 0x80000000u, 0x00000000u, 0x80000000u, LOGIC_MASK, XT_SF | XT_PF },
};

static const vec32 xor_vecs[] = {
    // FF^FF=0: ZF, even parity
    { "xor32.FF^FF", 0x000000FFu, 0x000000FFu, 0x00000000u, LOGIC_MASK, XT_ZF | XT_PF },
    // AAAA_AAAA^5555_5555=FFFF_FFFF: SF; low byte 0xFF (8 bits, even) => PF
    { "xor32.AA^55", 0xAAAAAAAAu, 0x55555555u, 0xFFFFFFFFu, LOGIC_MASK, XT_SF | XT_PF },
};

int main(void)
{
    unsigned i;
    uint32_t f;

    xt_begin("v1", "cpu_flags");

    for (i = 0; i < sizeof(add8_vecs) / sizeof(add8_vecs[0]); i++) {
        const vec8 *v = &add8_vecs[i];
        uint8_t r = alu_add8(v->a, v->b, &f);
        xt_ev("%s res=0x%02X eflags=0x%08lX", v->name, r, (unsigned long)f);
        xt_check_u32(v->name, v->res, r);
        xt_check_flags(v->name, XT_ARITH_FLAGS, v->exp_flags, f);
    }

    for (i = 0; i < sizeof(sub8_vecs) / sizeof(sub8_vecs[0]); i++) {
        const vec8 *v = &sub8_vecs[i];
        uint8_t r = alu_sub8(v->a, v->b, &f);
        xt_ev("%s res=0x%02X eflags=0x%08lX", v->name, r, (unsigned long)f);
        xt_check_u32(v->name, v->res, r);
        xt_check_flags(v->name, XT_ARITH_FLAGS, v->exp_flags, f);
    }

    for (i = 0; i < sizeof(and_vecs) / sizeof(and_vecs[0]); i++) {
        const vec32 *v = &and_vecs[i];
        uint32_t r = alu_and32(v->a, v->b, &f);
        xt_ev("%s res=0x%08lX eflags=0x%08lX", v->name, (unsigned long)r, (unsigned long)f);
        xt_check_u32(v->name, v->res, r);
        xt_check_flags(v->name, v->mask, v->exp_flags, f);
    }
    for (i = 0; i < sizeof(or_vecs) / sizeof(or_vecs[0]); i++) {
        const vec32 *v = &or_vecs[i];
        uint32_t r = alu_or32(v->a, v->b, &f);
        xt_ev("%s res=0x%08lX eflags=0x%08lX", v->name, (unsigned long)r, (unsigned long)f);
        xt_check_u32(v->name, v->res, r);
        xt_check_flags(v->name, v->mask, v->exp_flags, f);
    }
    for (i = 0; i < sizeof(xor_vecs) / sizeof(xor_vecs[0]); i++) {
        const vec32 *v = &xor_vecs[i];
        uint32_t r = alu_xor32(v->a, v->b, &f);
        xt_ev("%s res=0x%08lX eflags=0x%08lX", v->name, (unsigned long)r, (unsigned long)f);
        xt_check_u32(v->name, v->res, r);
        xt_check_flags(v->name, v->mask, v->exp_flags, f);
    }

    return xt_end();
}
