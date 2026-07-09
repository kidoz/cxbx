// SPDX-License-Identifier: MIT
//
// nv2a_filter - NV2A software-rasterizer bilinear texture filtering. Binds the
// same 2x2 texture as nv2a_texture (TL red, TR green, BL blue, BR white) but
// with the magnification filter set to LINEAR, and draws a full-screen quad.
// A conforming target blends the four texels: at the screen centre (u=v=0.5)
// the sample is the average of red+green+blue+white -> mid grey, where nearest
// sampling would pick a single primary. Off-centre samples lean toward their
// nearest texel.
//
// SCOPE: bilinear (2x2) magnification. Requires the raster path
// (Cxbx: CXBX_NV2A_RASTER=1).

#include "xtest.h"
#include <stddef.h>
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>

#define ATTR_POSITION 0
#define ATTR_TEXCOORD0 9
#define VTX_FMT(type, size, stride) \
    (((uint32_t)(stride) << 8) | ((uint32_t)(size) << 4) | (uint32_t)(type))

#define FBW 640
#define FBH 480
#define TW 2
#define TH 2

typedef struct { float x, y, z; float u, v; } Vertex;

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    xt_begin("v1", "nv2a_filter");
    xt_note("NV2A rasterizer bilinear (2x2) texture magnification");
    xt_note("requires the raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);
    int status = pb_init();
    xt_check_bool("nv2a_filter.pb_init", 1, status == 0);
    if (status != 0) return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t pitch_px = pb_back_buffer_pitch() / 4;
    uint32_t bbh = pb_back_buffer_height();
    xt_check_bool("nv2a_filter.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) { pb_kill(); return xt_end(); }
    for (uint32_t i = 0; i < pitch_px * bbh; i++) bb[i] = 0xFF000000u;

    uint32_t *tex = (uint32_t *)MmAllocateContiguousMemoryEx(
        TW * TH * 4, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_filter.tex_alloc", 1, tex != NULL);
    if (tex == NULL) { pb_kill(); return xt_end(); }
    tex[0] = 0xFFFF0000u; // TL red
    tex[1] = 0xFF00FF00u; // TR green
    tex[2] = 0xFF0000FFu; // BL blue
    tex[3] = 0xFFFFFFFFu; // BR white

    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        4 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_filter.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) { pb_kill(); return xt_end(); }
    vb[0] = (Vertex){   0.0f,   0.0f, 0.0f, 0.0f, 0.0f };
    vb[1] = (Vertex){ 640.0f,   0.0f, 0.0f, 1.0f, 0.0f };
    vb[2] = (Vertex){ 640.0f, 480.0f, 0.0f, 1.0f, 1.0f };
    vb[3] = (Vertex){   0.0f, 480.0f, 0.0f, 0.0f, 1.0f };
    uint32_t vbAddr  = (uint32_t)(uintptr_t)vb;
    uint32_t texAddr = (uint32_t)(uintptr_t)tex;

    for (int rep = 0; rep < 4; rep++) {
        uint32_t *p = pb_begin();
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));
        // Bind the texture with a LINEAR magnification filter (MAG = 2 in bits 24-27).
        p = pb_push1(p, NV097_SET_TEXTURE_OFFSET,     texAddr);
        p = pb_push1(p, NV097_SET_TEXTURE_FORMAT,     (0x12u << 8) | (0x02u << 4));
        p = pb_push1(p, NV097_SET_TEXTURE_IMAGE_RECT, (TW << 16) | TH);
        p = pb_push1(p, NV097_SET_TEXTURE_FILTER,     (0x2u << 24) | (0x2u << 16));

        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, vbAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_TEXCOORD0 * 4,
                     vbAddr + (uint32_t)offsetof(Vertex, u));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_TEXCOORD0 * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 2, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((4u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
        pb_end(p);
        while (pb_busy()) ;
    }

#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t center = PX(320, 240);   // u=v=0.5 -> average of all four texels
    xt_ev("nv2a_filter.center=0x%08lX", (unsigned long)center);
    int cr = (center >> 16) & 0xFF, cg = (center >> 8) & 0xFF, cbb = center & 0xFF;
    // Bilinear: R,G,B all near 127 (red+green+blue+white averaged), balanced.
    int hi = cr > cg ? (cr > cbb ? cr : cbb) : (cg > cbb ? cg : cbb);
    int lo = cr < cg ? (cr < cbb ? cr : cbb) : (cg < cbb ? cg : cbb);
    xt_check("nv2a_filter.center_blended", lo > 90 && hi < 165 && (hi - lo) <= 40,
             "centre R=%d G=%d B=%d should be a balanced 4-texel blend", cr, cg, cbb);

    pb_kill();
    return xt_end();
}
