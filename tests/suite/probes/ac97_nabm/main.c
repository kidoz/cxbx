// SPDX-License-Identifier: MIT
//
// ac97_nabm - AC97 Native Audio Bus Master per-channel register semantics via
// the 0xFEC00000 aperture: the byte-wide channel Control register (5-bit mask,
// run/reset bits) and the 16-bit channel Status register's DMA-halted state.
// Exercises the PCM-Out channel (NABM offset 0x10). Deterministic self-check.
//
// The control semantics only trigger on a *byte* write to the control register
// (a 32-bit write to the channel dword bypasses the run/reset/halt logic), so
// this probe deliberately uses sub-dword MMIO accesses.

#include "xtest.h"
#include <stdint.h>

#define ACI_BASE     0xFEC00000u
#define REG8(off)    (*(volatile uint8_t  *)(ACI_BASE + (uint32_t)(off)))

// NABM block at +0x100; PCM-Out channel at +0x10 within it. Per-channel layout:
// Status (SR) at channel+0x06 (16-bit), Control (CR) at channel+0x0B (8-bit).
// The DMA-halt bit sits in the status register's low byte, so it is read here
// byte-wide (portable to targets that only decode 8/32-bit MMIO accesses).
#define PO_SR        (0x110u + 0x06u)   // 0x116
#define PO_CR        (0x110u + 0x0Bu)   // 0x11B

#define CR_RUN       0x01u
#define CR_RESET     0x02u
#define SR_DMA_HALT  0x01u

int main(void)
{
    xt_begin("v1", "ac97_nabm");
    xt_note("AC97 NABM per-channel: control 5-bit mask + run/reset, status DMA-halt");

    // Control register latches only its low 5 bits. Write 0xFD (Run set, reset
    // bit clear, plus high bits that must be masked off) -> reads back 0x1D.
    REG8(PO_CR) = 0xFDu;
    uint8_t cr = REG8(PO_CR);
    xt_ev("PO_CR after write 0xFD = 0x%02X", (unsigned)cr);
    xt_check_u32("ac97.nabm_ctrl_mask", 0x1Du, cr);

    // Clearing Run (write 0x00) halts the channel: the status register latches
    // DMA-halted.
    REG8(PO_CR) = 0x00u;
    uint8_t sr = REG8(PO_SR);
    xt_ev("PO_SR after Run=0 = 0x%02X", (unsigned)sr);
    xt_check_bool("ac97.nabm_halt_sets_dmahalt", 1, (sr & SR_DMA_HALT) != 0);

    // The reset bit is a control action, not stored: write Run|Reset (0x03) and
    // the control register reads back 0 (reset wins, clears it)...
    REG8(PO_CR) = CR_RUN | CR_RESET;
    uint8_t cr2 = REG8(PO_CR);
    xt_ev("PO_CR after Run|Reset = 0x%02X", (unsigned)cr2);
    xt_check_u32("ac97.nabm_reset_clears_ctrl", 0x00u, cr2);

    // ...and because the stored control then has Run clear, the channel is halted.
    uint8_t sr2 = REG8(PO_SR);
    xt_ev("PO_SR after reset = 0x%02X", (unsigned)sr2);
    xt_check_bool("ac97.nabm_reset_halts", 1, (sr2 & SR_DMA_HALT) != 0);

    return xt_end();
}
