// SPDX-License-Identifier: GPL-2.0-or-later
//
// EmuNV2ALogging.h - decode tables + trace macros for the NV2A (Xbox GPU)
// support code in CXBX.
//
// CXBX is still an HLE emulator, but it has a small NV2A MMIO/RAMIN/PFIFO/PGRAPH
// model for hardware-init paths. This header keeps those handlers instrumented
// with the same stable, machine-parseable trace grammar as the rest of the tree
// (compare the "XT|" guest trace prefix in tests/suite and the "KTRACE|" kernel
// prefix in kernel_logging.h). See the project NV2A tracing guidance.
//
// The macros compile to no-ops unless CXBX_NV2A_TRACE is set, so keeping the
// calls in hot MMIO paths is cheap by default.
//
// Trace grammar (one line per event, prefix "NV2A|" for the host log parser):
//   NV2A| rd  PFIFO+0x03244 CACHE1_DMA_GET = 0x00001040
//   NV2A| wr  PGRAPH+0x00100 INTR          = 0x00000001
//   NV2A| pb  INCR   subch=0 method=0x0300 count=4
//   NV2A| mthd KELVIN(0x97) method=0x0300 data=0x00000000
//   NV2A| ramht handle=0xCAFE0001 -> inst=0x00001240 class=0x97

#ifndef EMUNV2ALOGGING_H
#define EMUNV2ALOGGING_H

#include <cstdio>
#include <cstdint>

// Master switch. Off by default: NV2A register tracing is high-volume (every
// MMIO access), so enable it deliberately per build or gate it behind a runtime
// flag in the dispatcher. Define CXBX_NV2A_TRACE=1 to compile the trace bodies.
#ifndef CXBX_NV2A_TRACE
#define CXBX_NV2A_TRACE 0
#endif

// ---------------------------------------------------------------------------
// Register-space layout (offsets are relative to the NV2A MMIO aperture, which
// on the Xbox is physical base 0xFD000000, 16 MiB). These engine bases match
// the NV20 family (nouveau: nvhw / xemu: hw/xbox/nv2a). "PMC_BOOT_0" and the
// engine map are stable hardware facts; per-register offsets below cover the
// high-value registers a first tracer needs and are meant to be extended.
// ---------------------------------------------------------------------------
#define NV2A_XBOX_MMIO_BASE 0xFD000000u
#define NV2A_MMIO_SIZE      0x01000000u // 16 MiB

// Engine base offsets within the aperture.
#define NV_PMC      0x000000u // master control / interrupts / engine enable
#define NV_PBUS     0x001000u // bus control (ROM, config)
#define NV_PFIFO    0x002000u // command FIFO / DMA pusher
#define NV_PRMA     0x007000u // real-mode BAR access
#define NV_PVIDEO   0x008000u // video overlay
#define NV_PTIMER   0x009000u // programmable timer
#define NV_PCOUNTER 0x00A000u // performance counters
#define NV_PVPE     0x00B000u // MPEG / video processing
#define NV_PFB      0x100000u // framebuffer / memory controller
#define NV_PSTRAPS  0x101000u // straps
#define NV_PROM     0x300000u // BIOS ROM window
#define NV_PGRAPH   0x400000u // graphics engine (2D/3D rasterizer)
#define NV_PCRTC    0x600000u // CRTC
#define NV_PRMCIO   0x601000u // VGA CRTC I/O
#define NV_PRAMDAC  0x680000u // RAMDAC / PLLs / video
#define NV_PRMDIO   0x681000u // VGA DAC I/O
#define NV_PRAMIN   0x700000u // instance memory window (1 MiB): RAMHT, DMA objs, ctx
#define NV_USER     0x800000u // per-channel FIFO submission (PUT/GET), 32 x 0x2000

// A selection of high-value registers (absolute offsets).
#define NV_PMC_BOOT_0                0x000000u // chip ID (fixed; verify value vs ref)
#define NV_PMC_INTR_0                0x000100u
#define NV_PMC_INTR_EN_0             0x000140u
#define NV_PMC_ENABLE                0x000200u
#define NV_PFIFO_INTR_0              0x002100u
#define NV_PFIFO_INTR_EN_0           0x002140u
#define NV_PFIFO_RAMHT               0x002210u // RAMHT base/size in RAMIN
#define NV_PFIFO_RAMFC               0x002214u // RAMFC (FIFO context) base
#define NV_PFIFO_RAMRO               0x002218u // RAMRO (runout) base
#define NV_PFIFO_CACHE1_PUSH0        0x003200u
#define NV_PFIFO_CACHE1_DMA_PUSH     0x003220u
#define NV_PFIFO_CACHE1_DMA_STATE    0x003228u
#define NV_PFIFO_CACHE1_DMA_INSTANCE 0x00322Cu
#define NV_PFIFO_CACHE1_DMA_PUT      0x003240u // CPU writes; pusher target
#define NV_PFIFO_CACHE1_DMA_GET      0x003244u // pusher progress
#define NV_PFIFO_CACHE1_DMA_SUBROUTINE 0x00324Cu
#define NV_PFIFO_CACHE1_DMA_DCOUNT   0x0032A0u
#define NV_PGRAPH_INTR               0x400100u
#define NV_PGRAPH_INTR_EN            0x400140u
#define NV_PGRAPH_STATUS             0x400700u // busy/idle
#define NV_PGRAPH_FIFO               0x400720u
#define NV_PGRAPH_CTX_CONTROL        0x400144u
#define NV_PFB_CFG0                  0x100200u
#define NV_PTIMER_TIME_0             0x009400u
#define NV_PCRTC_INTR_0              0x600100u
#define NV_PCRTC_START               0x600800u

// ---------------------------------------------------------------------------
// PGRAPH object classes used by the NV2A (nouveau nv_object.h numbering). The
// Xbox 3D class is KELVIN (0x97); the rest are the 2D/copy/context helpers
// pbkit and the XDK bind.
// ---------------------------------------------------------------------------
#define NV_CLASS_BETA1       0x0012u // NV01_CONTEXT_BETA
#define NV_CLASS_CLIP        0x0019u // NV01_CONTEXT_CLIP
#define NV_CLASS_M2MF        0x0039u // NV03_MEMORY_TO_MEMORY_FORMAT
#define NV_CLASS_ROP         0x0043u // NV04_CONTEXT_ROP
#define NV_CLASS_PATTERN     0x0044u // NV04_CONTEXT_PATTERN
#define NV_CLASS_SURFACES_2D 0x0062u // NV10_CONTEXT_SURFACES_2D
#define NV_CLASS_BETA4       0x0072u // NV04_CONTEXT_BETA4
#define NV_CLASS_KELVIN      0x0097u // NV20_KELVIN_PRIMITIVE (Xbox 3D)
#define NV_CLASS_IMAGE_BLIT  0x009Fu // NV12_IMAGE_BLIT

// ---------------------------------------------------------------------------
// DMA-pusher command word types. NOTE: these bit patterns follow the standard
// NV DMA pusher; validate the exact masks against nouveau (nvhw/fifo.h) or
// xemu (hw/xbox/nv2a/nv2a.c) when the pusher is actually implemented -- the
// NV2A has jump/long-jump/call/return variants this reference decode
// approximates.
// ---------------------------------------------------------------------------
typedef enum
{
    NV2A_PB_INCREASING,     // increasing methods
    NV2A_PB_NON_INCREASING, // non-increasing methods (same method N times)
    NV2A_PB_JUMP,           // jump dma_get to address
    NV2A_PB_CALL,           // call (push return)
    NV2A_PB_RETURN,         // return
    NV2A_PB_UNKNOWN
} nv2a_pb_cmd_type;

#if CXBX_NV2A_TRACE

static inline const char* nv2a_engine_name(uint32_t off, uint32_t* rel_out)
{
    struct
    {
        uint32_t base, size;
        const char* name;
    } static const kEngines[] = {
        { NV_PMC, 0x1000u, "PMC" },
        { NV_PBUS, 0x1000u, "PBUS" },
        { NV_PFIFO, 0x2000u, "PFIFO" },
        { NV_PRMA, 0x1000u, "PRMA" },
        { NV_PVIDEO, 0x1000u, "PVIDEO" },
        { NV_PTIMER, 0x1000u, "PTIMER" },
        { NV_PCOUNTER, 0x1000u, "PCOUNTER" },
        { NV_PVPE, 0x1000u, "PVPE" },
        { NV_PFB, 0x1000u, "PFB" },
        { NV_PSTRAPS, 0x1000u, "PSTRAPS" },
        { NV_PROM, 0x20000u, "PROM" },
        { NV_PGRAPH, 0x2000u, "PGRAPH" },
        { NV_PCRTC, 0x1000u, "PCRTC" },
        { NV_PRMCIO, 0x1000u, "PRMCIO" },
        { NV_PRAMDAC, 0x1000u, "PRAMDAC" },
        { NV_PRMDIO, 0x1000u, "PRMDIO" },
        { NV_PRAMIN, 0x100000u, "PRAMIN" },
        { NV_USER, 0x800000u, "USER" },
    };
    for(unsigned i = 0; i < sizeof(kEngines) / sizeof(kEngines[0]); i++)
    {
        if(off >= kEngines[i].base && off < kEngines[i].base + kEngines[i].size)
        {
            if(rel_out) *rel_out = off - kEngines[i].base;
            return kEngines[i].name;
        }
    }
    if(rel_out) *rel_out = off;
    return "?";
}

static inline const char* nv2a_reg_name(uint32_t off)
{
    struct
    {
        uint32_t off;
        const char* name;
    } static const kRegs[] = {
        { NV_PMC_BOOT_0, "PMC_BOOT_0" },
        { NV_PMC_INTR_0, "PMC_INTR_0" },
        { NV_PMC_INTR_EN_0, "PMC_INTR_EN_0" },
        { NV_PMC_ENABLE, "PMC_ENABLE" },
        { NV_PFIFO_INTR_0, "PFIFO_INTR_0" },
        { NV_PFIFO_INTR_EN_0, "PFIFO_INTR_EN_0" },
        { NV_PFIFO_RAMHT, "PFIFO_RAMHT" },
        { NV_PFIFO_RAMFC, "PFIFO_RAMFC" },
        { NV_PFIFO_RAMRO, "PFIFO_RAMRO" },
        { NV_PFIFO_CACHE1_PUSH0, "CACHE1_PUSH0" },
        { NV_PFIFO_CACHE1_DMA_PUSH, "CACHE1_DMA_PUSH" },
        { NV_PFIFO_CACHE1_DMA_STATE, "CACHE1_DMA_STATE" },
        { NV_PFIFO_CACHE1_DMA_INSTANCE, "CACHE1_DMA_INSTANCE" },
        { NV_PFIFO_CACHE1_DMA_PUT, "CACHE1_DMA_PUT" },
        { NV_PFIFO_CACHE1_DMA_GET, "CACHE1_DMA_GET" },
        { NV_PGRAPH_INTR, "PGRAPH_INTR" },
        { NV_PGRAPH_INTR_EN, "PGRAPH_INTR_EN" },
        { NV_PGRAPH_STATUS, "PGRAPH_STATUS" },
        { NV_PGRAPH_FIFO, "PGRAPH_FIFO" },
        { NV_PGRAPH_CTX_CONTROL, "PGRAPH_CTX_CONTROL" },
        { NV_PFB_CFG0, "PFB_CFG0" },
        { NV_PTIMER_TIME_0, "PTIMER_TIME_0" },
        { NV_PCRTC_INTR_0, "PCRTC_INTR_0" },
        { NV_PCRTC_START, "PCRTC_START" },
    };
    for(unsigned i = 0; i < sizeof(kRegs) / sizeof(kRegs[0]); i++)
        if(kRegs[i].off == off) return kRegs[i].name;
    return nullptr;
}

static inline const char* nv2a_class_name(uint32_t cls)
{
    switch(cls & 0xFFu)
    {
        case NV_CLASS_BETA1: return "BETA1";
        case NV_CLASS_CLIP: return "CLIP";
        case NV_CLASS_M2MF: return "M2MF";
        case NV_CLASS_ROP: return "ROP";
        case NV_CLASS_PATTERN: return "PATTERN";
        case NV_CLASS_SURFACES_2D: return "SURFACES_2D";
        case NV_CLASS_BETA4: return "BETA4";
        case NV_CLASS_KELVIN: return "KELVIN";
        case NV_CLASS_IMAGE_BLIT: return "IMAGE_BLIT";
        default: return "?";
    }
}

// Reference DMA-pusher decode -- validate masks against nouveau/xemu (see note
// above) before relying on it in the real pusher.
static inline nv2a_pb_cmd_type nv2a_pb_decode(uint32_t word, uint32_t* subch,
                                              uint32_t* method, uint32_t* count)
{
    if((word & 0xE0000003u) == 0x20000000u) return NV2A_PB_JUMP;
    if((word & 0x00000003u) == 0x00000002u) return NV2A_PB_CALL;
    if(word == 0x00020000u) return NV2A_PB_RETURN;
    if(subch) *subch = (word >> 13) & 0x7u;
    if(method) *method = word & 0x1FFFu;
    if(count) *count = (word >> 18) & 0x7FFu;
    if((word & 0xE0030003u) == 0x40000000u) return NV2A_PB_NON_INCREASING;
    if((word & 0xE0030003u) == 0x00000000u) return NV2A_PB_INCREASING;
    return NV2A_PB_UNKNOWN;
}

static inline void EmuNV2ALogReg(const char* rw, uint32_t off, uint32_t val)
{
    uint32_t rel = 0;
    const char* eng = nv2a_engine_name(off, &rel);
    const char* reg = nv2a_reg_name(off);
    printf("NV2A| %-2s %s+0x%05X %-20s = 0x%08X\n",
           rw, eng, rel, reg ? reg : "", val);
    fflush(stdout);
}

static inline void EmuNV2ALogPushCommand(uint32_t word)
{
    uint32_t subch = 0, method = 0, count = 0;
    nv2a_pb_cmd_type t = nv2a_pb_decode(word, &subch, &method, &count);
    static const char* kName[] = { "INCR", "NONINCR", "JUMP", "CALL", "RET", "UNK" };
    if(t == NV2A_PB_INCREASING || t == NV2A_PB_NON_INCREASING)
        printf("NV2A| pb %-7s subch=%u method=0x%04X count=%u\n",
               kName[t], subch, method, count);
    else
        printf("NV2A| pb %-7s word=0x%08X\n", kName[t], word);
    fflush(stdout);
}

static inline void EmuNV2ALogMethod(uint32_t cls, uint32_t method, uint32_t data)
{
    printf("NV2A| mthd %s(0x%02X) method=0x%04X data=0x%08X\n",
           nv2a_class_name(cls), cls & 0xFFu, method, data);
    fflush(stdout);
}

static inline void EmuNV2ALogRamht(uint32_t handle, uint32_t instance, uint32_t cls)
{
    printf("NV2A| ramht handle=0x%08X -> inst=0x%05X class=0x%02X (%s)\n",
           handle, instance, cls & 0xFFu, nv2a_class_name(cls));
    fflush(stdout);
}

#define NV2A_TRACE_REG(rw, off, val)         EmuNV2ALogReg((rw), (off), (val))
#define NV2A_TRACE_PB(word)                  EmuNV2ALogPushCommand((word))
#define NV2A_TRACE_METHOD(cls, method, data) EmuNV2ALogMethod((cls), (method), (data))
#define NV2A_TRACE_RAMHT(handle, inst, cls)  EmuNV2ALogRamht((handle), (inst), (cls))

#else // !CXBX_NV2A_TRACE

#define NV2A_TRACE_REG(rw, off, val)         ((void)0)
#define NV2A_TRACE_PB(word)                  ((void)0)
#define NV2A_TRACE_METHOD(cls, method, data) ((void)0)
#define NV2A_TRACE_RAMHT(handle, inst, cls)  ((void)0)

#endif // CXBX_NV2A_TRACE

#endif // EMUNV2ALOGGING_H
