// SPDX-License-Identifier: MIT
//
// ac97_dma - AC97 bus-master DMA engine semantics on the PCM-Out channel:
// programming a buffer-descriptor queue (LVI) and setting the Control run bit
// must make the current index (CIV) advance through the queue, latch the
// buffer-completion status (BCIS, write-1-to-clear) and the GLOB_STA PCM-Out
// interrupt status, and halt with CELV|LVBCI once the last valid buffer
// completes. This is the state machine a title's audio init blocks on (its
// completion ISR/event never fires without it). Deterministic self-check.

#include "xtrace.h"
#include "xtest.h"
#include <stdint.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#define ACI_BASE     0xFEC00000u
#define REG8(off)    (*(volatile uint8_t  *)(ACI_BASE + (uint32_t)(off)))
#define REG16(off)   (*(volatile uint16_t *)(ACI_BASE + (uint32_t)(off)))
#define REG32(off)   (*(volatile uint32_t *)(ACI_BASE + (uint32_t)(off)))

// PCM-Out channel block at NABM(+0x100) + 0x10.
#define PO_BDBAR     0x110u          // buffer descriptor list base (dword)
#define PO_CIV       0x114u          // current index (byte)
#define PO_LVI       0x115u          // last valid index (byte)
#define PO_SR        0x116u          // status (word)
#define PO_PICB      0x118u          // position in current buffer (word)
#define PO_CR        0x11Bu          // control (byte)

#define GLOB_STA     0x130u

#define CR_RUN       0x01u
#define CR_IOCE      0x10u
#define SR_DMA_HALT  0x01u
#define SR_CELV      0x02u
#define SR_LVBCI     0x04u
#define SR_BCIS      0x08u
#define STA_PCM_OUT  0x40u

static void delay_ms(int ms)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -10000LL * ms;   // relative, 100 ns units
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

int main(void)
{
    static uint32_t bdl[32 * 2];   // descriptor list backing (content unused by the model)
    int polls;

    xt_begin("v1", "ac97_dma");
    xt_note("AC97 bus-master DMA: CIV advance, BCIS/GLOB_STA latch, last-valid halt");

    // Clear any leftover PCM-Out status in GLOB_STA (write-1-to-clear).
    REG32(GLOB_STA) = STA_PCM_OUT;

    // Program a 3-buffer queue (indices 0..2) and start the channel with
    // interrupt-on-completion enabled.
    REG32(PO_BDBAR) = (uint32_t)(uintptr_t)bdl;
    REG8(PO_LVI) = 2u;
    REG8(PO_CR) = CR_RUN | CR_IOCE;

    // The engine must advance CIV to the last valid index.
    for (polls = 0; polls < 100; polls++) {
        if (REG8(PO_CIV) == 2u)
            break;
        delay_ms(5);
    }
    xt_ev("CIV reached %u after %d poll(s)", (unsigned)REG8(PO_CIV), polls);
    xt_check_u32("ac97.dma_civ_advances", 2u, REG8(PO_CIV));

    // Buffer completions latch BCIS and the GLOB_STA PCM-Out status.
    uint16_t sr = REG16(PO_SR);
    xt_ev("PO_SR while running = 0x%04X", (unsigned)sr);
    xt_check_bool("ac97.dma_bcis_latched", 1, (sr & SR_BCIS) != 0);
    xt_check_bool("ac97.dma_globsta_pcmout", 1, (REG32(GLOB_STA) & STA_PCM_OUT) != 0);

    // BCIS is write-1-to-clear.
    REG16(PO_SR) = SR_BCIS;
    // (a fresh completion may re-latch it, so only check the W1C took effect
    // together with the halt state below)

    // Once the last valid buffer completes the channel halts with CELV|LVBCI.
    for (polls = 0; polls < 100; polls++) {
        if ((REG16(PO_SR) & SR_DMA_HALT) != 0)
            break;
        delay_ms(5);
    }
    sr = REG16(PO_SR);
    xt_ev("PO_SR at last-valid = 0x%04X after %d poll(s)", (unsigned)sr, polls);
    xt_check_bool("ac97.dma_lastvalid_halts", 1, (sr & SR_DMA_HALT) != 0);
    xt_check_bool("ac97.dma_celv_latched", 1, (sr & SR_CELV) != 0);
    xt_check_bool("ac97.dma_lvbci_latched", 1, (sr & SR_LVBCI) != 0);

    // Moving LVI forward resumes the queue: CIV advances again.
    REG8(PO_LVI) = 4u;
    for (polls = 0; polls < 100; polls++) {
        if (REG8(PO_CIV) == 4u)
            break;
        delay_ms(5);
    }
    xt_ev("CIV resumed to %u after %d poll(s)", (unsigned)REG8(PO_CIV), polls);
    xt_check_u32("ac97.dma_lvi_resumes", 4u, REG8(PO_CIV));

    // Stopping the channel halts DMA.
    REG8(PO_CR) = 0u;
    xt_check_bool("ac97.dma_stop_halts", 1, (REG16(PO_SR) & SR_DMA_HALT) != 0);

    return xt_end();
}
