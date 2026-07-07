// SPDX-License-Identifier: MIT
//
// usb0 - USB host controller (OHCI) register semantics via the 0xFED00000
// aperture: the HcRevision identity register (reports OHCI 1.0 and ignores
// writes) and an operational-register latch round-trip. Deterministic
// self-check.

#include "xtest.h"
#include <stdint.h>

#define USB0_BASE    0xFED00000u
#define REG32(off)   (*(volatile uint32_t *)(USB0_BASE + (uint32_t)(off)))

#define HC_REVISION     0x00u   // BCD OHCI spec revision (1.0 -> 0x10)
#define HC_FM_INTERVAL  0x34u   // frame interval (a plain operational register)

#define OHCI_REV_1_0    0x00000010u

int main(void)
{
    xt_begin("v1", "usb0");
    xt_note("USB0/OHCI semantics: HcRevision identity + read-only, operational latch");

    // HcRevision identifies the controller as OHCI 1.0 (BCD 0x10 in the low byte).
    uint32_t rev = REG32(HC_REVISION);
    xt_ev("HcRevision = 0x%08lX", (unsigned long)rev);
    xt_check_u32("usb0.hcrevision", OHCI_REV_1_0, rev & 0xFFu);

    // HcRevision is read-only: a write must not change what is read back.
    REG32(HC_REVISION) = 0xDEADBEEFu;
    uint32_t rev2 = REG32(HC_REVISION);
    xt_ev("HcRevision after write = 0x%08lX", (unsigned long)rev2);
    xt_check_u32("usb0.hcrevision_readonly", OHCI_REV_1_0, rev2 & 0xFFu);

    // An operational register latches a written value (write / read-back
    // round-trip on HcFmInterval).
    REG32(HC_FM_INTERVAL) = 0x2EDF2EDFu;
    uint32_t fi = REG32(HC_FM_INTERVAL);
    xt_ev("HcFmInterval readback = 0x%08lX", (unsigned long)fi);
    xt_check_u32("usb0.operational_latch", 0x2EDF2EDFu, fi);

    return xt_end();
}
