// SPDX-License-Identifier: MIT
//
// ac97 - AC97 audio-controller (ACI) register semantics via the 0xFEC00000
// aperture: the GLOB_STA codec-ready bit and its write-1-to-clear behaviour,
// the GLOB_CNT latch + reset gating, and the Codec Access Semaphore (CAS)
// free-after-access / poll-termination guarantee that a codec-access spin
// loop relies on. Deterministic self-check.

#include "xtest.h"
#include <stdint.h>

#define ACI_BASE    0xFEC00000u
#define REG32(off)  (*(volatile uint32_t *)(ACI_BASE + (uint32_t)(off)))

// Native Audio Bus Master (NABM) block starts at +0x100.
#define GLOB_CNT    0x12Cu   // global control
#define GLOB_STA    0x130u   // global status (codec-ready in bit 8)
#define CAS         0x134u   // codec access semaphore (bit 0 = access in progress)

#define CODEC0_READY 0x00000100u

int main(void)
{
    xt_begin("v1", "ac97");
    xt_note("AC97/ACI register semantics: GLOB_STA codec-ready + W1C, GLOB_CNT latch, CAS auto-clear");

    // GLOB_STA always reports the primary codec as ready: the controller model
    // forces bit 8 on so a title waiting for codec-ready proceeds.
    uint32_t globsta = REG32(GLOB_STA);
    xt_ev("GLOB_STA initial = 0x%08lX", (unsigned long)globsta);
    xt_check_bool("ac97.globsta_codec0_ready", 1, (globsta & CODEC0_READY) != 0);

    // GLOB_CNT is a latch, except that a warm/cold reset request (bits 1..2)
    // is a control action, not a stored value.
    REG32(GLOB_CNT) = 0x00000021u;                 // GIE + a control bit, no reset
    uint32_t cnt = REG32(GLOB_CNT);
    xt_ev("GLOB_CNT after write 0x21 = 0x%08lX", (unsigned long)cnt);
    xt_check_u32("ac97.globcnt_latch", 0x00000021u, cnt);

    REG32(GLOB_CNT) = 0x00000002u;                 // cold-reset request bit
    uint32_t cnt2 = REG32(GLOB_CNT);
    xt_ev("GLOB_CNT after reset-request 0x02 = 0x%08lX", (unsigned long)cnt2);
    xt_check_u32("ac97.globcnt_reset_not_latched", 0x00000021u, cnt2);

    // GLOB_STA status bits are write-1-to-clear; clearing everything must leave
    // the forced codec-ready bit set (and nothing else).
    REG32(GLOB_STA) = 0xFFFFFFFFu;
    uint32_t globsta2 = REG32(GLOB_STA);
    xt_ev("GLOB_STA after W1C-all = 0x%08lX", (unsigned long)globsta2);
    xt_check_u32("ac97.globsta_w1c_keeps_codec_ready", CODEC0_READY, globsta2);

    // Codec Access Semaphore. Reading CAS models a codec access; the access
    // completes immediately, so the semaphore reads free (bit 0 clear) again on
    // the very next read. A model that latched it busy would hang any codec-
    // access spin loop (the class of deadlock the FCEUltra DSOUND ISR hit).
    (void)REG32(CAS);                              // a codec access
    uint32_t cas = REG32(CAS);
    xt_ev("CAS after an access = 0x%08lX", (unsigned long)cas);
    xt_check_bool("ac97.cas_free_after_access", 1, (cas & 1u) == 0);

    // The concrete consequence: a bounded "wait until the codec is free" poll
    // must terminate promptly rather than spin to its retry limit.
    (void)REG32(CAS);                              // mark the semaphore
    int tries = 0;
    while ((REG32(CAS) & 1u) != 0 && tries < 16)
        tries++;
    xt_ev("CAS poll cleared after %d try(s)", tries);
    xt_check("ac97.cas_poll_terminates", tries < 16,
             "codec semaphore free after %d poll(s)", tries);

    return xt_end();
}
