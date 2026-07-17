// SPDX-License-Identifier: MIT
//
// nv2a_texture - NV2A software-rasterizer Phase 4. The final raster-stage
// feature: texture sampling. This probe uploads a 2x2 linear A8R8G8B8 texture
// with four distinct corner colors (TL red, TR green, BL blue, BR white), binds
// it to stage 0, and draws a full-screen quad whose texcoords span (0,0)-(1,1).
// A conforming target perspective-correctly interpolates the texcoords, point-
// samples the texture, and modulates it with the (white) diffuse -- so each
// screen quadrant must show its corresponding texel color.
//
// gfx_draw already proved the texture *interception* fires on a real draw; this
// proves the texels actually reach the framebuffer at the right coordinates.
//
// SCOPE: nearest texture sampling + MODULATE, perspective-correct texcoords. No
// filtering / mips. Requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1).

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
    xt_begin("v1", "nv2a_texture");
    xt_note("NV2A rasterizer Phase 4 (2x2 texture sampling + MODULATE)");
    xt_note("requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_check_bool("nv2a_texture.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t  pitch_px = pb_back_buffer_pitch() / 4;
    uint32_t  bbh = pb_back_buffer_height();
    xt_check_bool("nv2a_texture.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) { pb_kill(); return xt_end(); }
    for (uint32_t i = 0; i < pitch_px * bbh; i++)
        bb[i] = 0xFF000000u;

    // 2x2 linear A8R8G8B8 texture: row-major TL,TR / BL,BR.
    uint32_t *tex = (uint32_t *)MmAllocateContiguousMemoryEx(
        TW * TH * 4, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_texture.tex_alloc", 1, tex != NULL);
    if (tex == NULL) { pb_kill(); return xt_end(); }
    tex[0] = 0xFFFF0000u; // (0,0) TL red
    tex[1] = 0xFF00FF00u; // (1,0) TR green
    tex[2] = 0xFF0000FFu; // (0,1) BL blue
    tex[3] = 0xFFFFFFFFu; // (1,1) BR white

    // Full-screen quad in screen space, texcoords (0,0)-(1,1).
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        4 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_texture.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) { pb_kill(); return xt_end(); }
    vb[0] = (Vertex){   0.0f,   0.0f, 0.0f, 0.0f, 0.0f };
    vb[1] = (Vertex){ 640.0f,   0.0f, 0.0f, 1.0f, 0.0f };
    vb[2] = (Vertex){ 640.0f, 480.0f, 0.0f, 1.0f, 1.0f };
    vb[3] = (Vertex){   0.0f, 480.0f, 0.0f, 0.0f, 1.0f };

    uint32_t vbAddr  = (uint32_t)(uintptr_t)vb;
    uint32_t texAddr = (uint32_t)(uintptr_t)tex;
    uint32_t fmt     = (0x12u << 8) | (0x02u << 4);   // LU_A8R8G8B8, 2D
    uint32_t rect    = (TW << 16) | TH;

    for (int rep = 0; rep < 4; rep++) {
        uint32_t *p = pb_begin();

        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);

        // Identity viewport (positions are screen space).
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));

        // Bind the stage-0 texture (descriptor + control/filter/address).
        p = pb_push1(p, NV097_SET_TEXTURE_OFFSET,     texAddr);
        p = pb_push1(p, NV097_SET_TEXTURE_FORMAT,     fmt);
        p = pb_push1(p, NV097_SET_TEXTURE_IMAGE_RECT, rect);
        p = pb_push1(p, NV097_SET_TEXTURE_ADDRESS,    0x00030303);
        p = pb_push1(p, NV097_SET_TEXTURE_CONTROL0,   0x4003ffc0);
        p = pb_push1(p, NV097_SET_TEXTURE_FILTER,     0x02022000);
        p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM,
                     NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_2D_PROJECTIVE);

        // Position + texcoord0 arrays (no diffuse -> defaults to white).
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
        while (pb_busy())
            ;
    }

    // Each screen quadrant samples one texel.
#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t tl = PX(160, 120), tr = PX(480, 120);
    uint32_t bl = PX(160, 360), br = PX(480, 360);
    xt_ev("nv2a_texture.tl=0x%08lX tr=0x%08lX bl=0x%08lX br=0x%08lX",
          (unsigned long)tl, (unsigned long)tr, (unsigned long)bl, (unsigned long)br);

    #define R_(c) (((c) >> 16) & 0xFF)
    #define G_(c) (((c) >>  8) & 0xFF)
    #define B_(c) ( (c)        & 0xFF)
    xt_check("nv2a_texture.tl_red",   R_(tl) > 200 && G_(tl) < 64 && B_(tl) < 64,
             "TL R=%d G=%d B=%d must be red", R_(tl), G_(tl), B_(tl));
    xt_check("nv2a_texture.tr_green", G_(tr) > 200 && R_(tr) < 64 && B_(tr) < 64,
             "TR R=%d G=%d B=%d must be green", R_(tr), G_(tr), B_(tr));
    xt_check("nv2a_texture.bl_blue",  B_(bl) > 200 && R_(bl) < 64 && G_(bl) < 64,
             "BL R=%d G=%d B=%d must be blue", R_(bl), G_(bl), B_(bl));
    xt_check("nv2a_texture.br_white", R_(br) > 200 && G_(br) > 200 && B_(br) > 200,
             "BR R=%d G=%d B=%d must be white", R_(br), G_(br), B_(br));

    pb_kill();
    return xt_end();
}
