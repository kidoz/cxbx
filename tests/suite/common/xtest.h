// SPDX-License-Identifier: MIT
//
// xtest - small helpers layered on xtrace for writing conformance probes.

#ifndef XTEST_H
#define XTEST_H

#include "xtrace.h"

#ifdef __cplusplus
extern "C" {
#endif

// x86 EFLAGS bit masks (used by the cpu_flags probe).
#define XT_CF 0x0001u
#define XT_PF 0x0004u
#define XT_AF 0x0010u
#define XT_ZF 0x0040u
#define XT_SF 0x0080u
#define XT_OF 0x0800u

// The condition/status flags an arithmetic instruction is defined to affect.
#define XT_ARITH_FLAGS (XT_CF | XT_PF | XT_AF | XT_ZF | XT_SF | XT_OF)

// Check a masked subset of EFLAGS, emitting one CHK line per selected flag
// (named "<name>.CF", "<name>.OF", ...). Returns 1 if all selected flags match.
int xt_check_flags(const char *name, uint32_t mask, uint32_t expect, uint32_t got);

#ifdef __cplusplus
}
#endif

#endif // XTEST_H
