// SPDX-License-Identifier: MIT
//
// gfx_draw - NV2A textured-quad draw. Where gfx_tex proves the source-texture
// interception fires on a bare SET_BEGIN_END, this probe drives it from a
// *realistic* draw: it binds a full stage-0 texture (offset/format/rect plus
// control/filter/address), points the position and texcoord0 vertex-attribute
// arrays at a contiguous vertex buffer, and submits BEGIN(QUADS) + DRAW_ARRAYS
// + END. The whole vertex + draw batch streams through the PFIFO pusher and
// PGRAPH method dispatch, exercising the path a real title uses -- and the
// interception still snapshots the bound texture to %TEMP%\cxbx_texN.bmp.
//
// SCOPE: submission only. No rasterizer is required; the interception triggers
// on SET_BEGIN_END while a texture is bound, before any pixels would be drawn.
// The vertex/draw methods verify the pusher tolerates a full draw batch.

#include "xtest.h"
#include <stddef.h>
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>

#define TW 64
#define TH 64

// KELVIN vertex-attribute indices (array offset/format are indexed by attr*4).
#define ATTR_POSITION 0
#define ATTR_TEXCOORD0 9

// SET_VERTEX_DATA_ARRAY_FORMAT: (stride << 8) | (size << 4) | type.
#define VTX_FMT(type, size, stride) \
    (((uint32_t)(stride) << 8) | ((uint32_t)(size) << 4) | (uint32_t)(type))

typedef struct { float x, y, z; float u, v; } Vertex;

static uint32_t pattern_px(int x, int y)
{
    return 0xFF000000u
         | ((uint32_t)((x * 4) & 0xFF) << 16)
         | ((uint32_t)((y * 4) & 0xFF) << 8)
         | (uint32_t)((x ^ y) & 0xFF);
}

int main(void)
{
    xt_begin("v1", "gfx_draw");
    xt_note("NV2A textured-quad draw (source-texture interception via a real draw)");
    xt_note("on a texture-intercepting target this dumps the uploaded image");

    // pbkit's pb_init asserts the current display mode is 32bpp (XVideoGetMode).
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_ev("gfx_draw.pb_init_status=%d", status);
    xt_check_bool("gfx_draw.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    // Known texture in contiguous, write-combined video memory.
    uint32_t *tex = (uint32_t *)MmAllocateContiguousMemoryEx(
        TW * TH * 4, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("gfx_draw.tex_alloc", 1, tex != NULL);
    if (tex == NULL) {
        pb_kill();
        return xt_end();
    }
    for (int y = 0; y < TH; y++)
        for (int x = 0; x < TW; x++)
            tex[y * TW + x] = pattern_px(x, y);

    // A textured quad's four vertices in contiguous memory. The NV2A reads
    // attribute arrays straight from these guest addresses.
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        4 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("gfx_draw.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) {
        pb_kill();
        return xt_end();
    }
    vb[0] = (Vertex){   0.0f,   0.0f, 0.0f, 0.0f, 0.0f };
    vb[1] = (Vertex){ 640.0f,   0.0f, 0.0f, 1.0f, 0.0f };
    vb[2] = (Vertex){ 640.0f, 480.0f, 0.0f, 1.0f, 1.0f };
    vb[3] = (Vertex){   0.0f, 480.0f, 0.0f, 0.0f, 1.0f };

    uint32_t texAddr = (uint32_t)tex;
    uint32_t fmt     = (0x12u << 8) | (0x02u << 4);   // LU_A8R8G8B8, 2D
    uint32_t rect    = (TW << 16) | TH;
    uint32_t vbAddr  = (uint32_t)vb;

    xt_ev("gfx_draw.tex_addr=0x%08lX vb_addr=0x%08lX", (unsigned long)texAddr,
          (unsigned long)vbAddr);
    xt_ev("gfx_draw.tex_fmt=0x%08lX rect=0x%08lX", (unsigned long)fmt,
          (unsigned long)rect);

    for (int i = 0; i < 8; i++) {
        uint32_t *p = pb_begin();

        // Bind stage-0 texture: descriptor the interception reads, plus the
        // control/filter/address a real bind carries.
        p = pb_push1(p, NV097_SET_TEXTURE_OFFSET,     texAddr);
        p = pb_push1(p, NV097_SET_TEXTURE_FORMAT,     fmt);
        p = pb_push1(p, NV097_SET_TEXTURE_IMAGE_RECT, rect);
        p = pb_push1(p, NV097_SET_TEXTURE_ADDRESS,    0x00030303);   // wrap S/T/R
        p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0,   0x4003ffc0);   // enable
        p = pb_push1(p, NV097_SET_TEXTURE_FILTER,     0x02022000);   // linear

        // Point the position + texcoord0 attribute arrays at the vertex buffer.
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, vbAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_TEXCOORD0 * 4,
                     vbAddr + (uint32_t)offsetof(Vertex, u));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_TEXCOORD0 * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 2, sizeof(Vertex)));

        // Draw the quad from the arrays: 4 vertices starting at index 0.
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((4u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);

        pb_end(p);
        while (pb_busy())
            ;
    }

    xt_check_bool("gfx_draw.submitted", 1, 1);

    pb_kill();
    return xt_end();
}
