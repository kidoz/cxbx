// SPDX-License-Identifier: MIT
//
// nv2a_blend - NV2A software-rasterizer alpha blending. Clears the surface to
// opaque red, then draws a green quad whose diffuse alpha is 0x80 with blending
// enabled (SRC_ALPHA / ONE_MINUS_SRC_ALPHA, FUNC_ADD). A conforming target
// blends the green over the red inside the quad -- ~50/50, so the overlap reads
// as a red+green mix, not pure green (source-overwrite) and not pure red. The
// background outside the quad stays red.
//
// SCOPE: SRC_ALPHA over-blend. Requires the raster path (CXBX_NV2A_RASTER=1).

#include "xtest.h"
#include <stddef.h>
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>

#define ATTR_POSITION 0
#define ATTR_DIFFUSE  3
#define VTX_FMT(type, size, stride) \
    (((uint32_t)(stride) << 8) | ((uint32_t)(size) << 4) | (uint32_t)(type))

#define FBW 640
#define FBH 480

typedef struct { float x, y, z; uint32_t color; } Vertex;

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    xt_begin("v1", "nv2a_blend");
    xt_note("NV2A rasterizer alpha blending (SRC_ALPHA over red)");
    xt_note("requires the raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);
    int status = pb_init();
    xt_check_bool("nv2a_blend.pb_init", 1, status == 0);
    if (status != 0) return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t pitch_px = pb_back_buffer_pitch() / 4;
    xt_check_bool("nv2a_blend.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) { pb_kill(); return xt_end(); }

    // A green quad with alpha 0x80 (~0.5) over the centre.
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        4 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_blend.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) { pb_kill(); return xt_end(); }
    const uint32_t HALF_GREEN = 0x8000FF00u;   // A=0x80, G=0xFF
    vb[0] = (Vertex){ 160.0f, 120.0f, 0.0f, HALF_GREEN };
    vb[1] = (Vertex){ 480.0f, 120.0f, 0.0f, HALF_GREEN };
    vb[2] = (Vertex){ 480.0f, 360.0f, 0.0f, HALF_GREEN };
    vb[3] = (Vertex){ 160.0f, 360.0f, 0.0f, HALF_GREEN };
    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    for (int rep = 0; rep < 4; rep++) {
        // Fresh opaque red background each frame (blending accumulates otherwise).
        for (uint32_t y = 0; y < FBH; y++)
            for (uint32_t x = 0; x < FBW; x++)
                bb[y * pitch_px + x] = 0xFFFF0000u;

        uint32_t *p = pb_begin();
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);
        // Identity viewport (screen-space positions).
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));
        // Enable SRC_ALPHA / ONE_MINUS_SRC_ALPHA blending.
        p = pb_push1(p, NV097_SET_BLEND_ENABLE, 1);
        p = pb_push1(p, NV097_SET_BLEND_FUNC_SFACTOR, NV097_SET_BLEND_FUNC_SFACTOR_V_SRC_ALPHA);
        p = pb_push1(p, NV097_SET_BLEND_FUNC_DFACTOR, NV097_SET_BLEND_FUNC_DFACTOR_V_ONE_MINUS_SRC_ALPHA);
        p = pb_push1(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);

        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, vbAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_DIFFUSE * 4,
                     vbAddr + (uint32_t)offsetof(Vertex, color));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((4u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
        pb_end(p);
        while (pb_busy()) ;
    }

#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t mixed  = PX(320, 240);   // inside the quad -> blended
    uint32_t bgpx   = PX(40, 40);     // outside -> red background
    xt_ev("nv2a_blend.mixed=0x%08lX bg=0x%08lX", (unsigned long)mixed, (unsigned long)bgpx);

    int mr = (mixed >> 16) & 0xFF, mg = (mixed >> 8) & 0xFF, mb = mixed & 0xFF;
    // ~50/50 red+green: both channels mid, neither dominant, blue absent.
    xt_check("nv2a_blend.mixed_is_blend",
             mr > 90 && mr < 165 && mg > 90 && mg < 165 && mb < 32,
             "overlap R=%d G=%d B=%d should be a red+green mix", mr, mg, mb);

    int br = (bgpx >> 16) & 0xFF, bg2 = (bgpx >> 8) & 0xFF;
    xt_check("nv2a_blend.bg_red", br > 220 && bg2 < 40,
             "background R=%d G=%d should stay red", br, bg2);

    pb_kill();
    return xt_end();
}
