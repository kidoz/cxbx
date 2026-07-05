// SPDX-License-Identifier: MIT
//
// gfx_tex - NV2A source-texture upload. Fills a known 64x64 ARGB texture in
// contiguous video memory and submits the KELVIN SET_TEXTURE_* descriptor plus
// a BEGIN_END primitive batch through pbkit. A GPU-emulating (or texture-
// intercepting) target can then capture the source image the title uploaded.
//
// On this Cxbx build it drives Emu.cpp's KELVIN texture interception, which
// decodes the bound stage-0 texture straight from guest memory and writes it to
// %TEMP%\cxbx_texN.bmp -- proving the source-texture path end to end with a
// deterministic, verifiable pattern (matching the gfx probe's pattern_px).
//
// SCOPE: submission only. No rasterizer is required; the interception triggers
// on SET_BEGIN_END while a texture is bound, before any pixels would be drawn.

#include "xtest.h"
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>

#define TW 64
#define TH 64

static uint32_t pattern_px(int x, int y)
{
    return 0xFF000000u
         | ((uint32_t)((x * 4) & 0xFF) << 16)
         | ((uint32_t)((y * 4) & 0xFF) << 8)
         | (uint32_t)((x ^ y) & 0xFF);
}

int main(void)
{
    xt_begin("v1", "gfx_tex");
    xt_note("NV2A KELVIN texture upload (source-texture interception)");
    xt_note("on a texture-intercepting target this dumps the uploaded image");

    // pbkit's pb_init asserts the current display mode is 32bpp (XVideoGetMode).
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_ev("gfx_tex.pb_init_status=%d", status);
    xt_check_bool("gfx_tex.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    // Known texture in contiguous, write-combined video memory.
    uint32_t *tex = (uint32_t *)MmAllocateContiguousMemoryEx(
        TW * TH * 4, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("gfx_tex.tex_alloc", 1, tex != NULL);
    if (tex == NULL) {
        pb_kill();
        return xt_end();
    }

    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++)
            tex[y * TW + x] = pattern_px(x, y);

    // Cxbx programs the NV2A with raw host pointers into contiguous blocks; the
    // interception resolves either that or a physical address back to host mem.
    uint32_t texAddr = (uint32_t)tex;
    uint32_t fmt     = (0x12u << 8) | (0x02u << 4);   // LU_A8R8G8B8, 2D
    uint32_t rect    = (TW << 16) | TH;

    xt_ev("gfx_tex.tex_addr=0x%08lX", (unsigned long)texAddr);
    xt_ev("gfx_tex.tex_fmt=0x%08lX rect=0x%08lX", (unsigned long)fmt, (unsigned long)rect);

    // Submit the descriptor + a primitive batch a handful of times; the
    // interception fires on SET_BEGIN_END with a bound stage-0 texture.
    for (int i = 0; i < 8; i++) {
        uint32_t *p = pb_begin();
        p = pb_push1(p, NV097_SET_TEXTURE_OFFSET,     texAddr);
        p = pb_push1(p, NV097_SET_TEXTURE_FORMAT,     fmt);
        p = pb_push1(p, NV097_SET_TEXTURE_IMAGE_RECT, rect);
        p = pb_push1(p, NV097_SET_BEGIN_END, 4);       // TRIANGLES (non-zero)
        p = pb_push1(p, NV097_SET_BEGIN_END, 0);       // END
        pb_end(p);
        while (pb_busy())
            ;
    }

    xt_check_bool("gfx_tex.submitted", 1, 1);

    pb_kill();
    return xt_end();
}
