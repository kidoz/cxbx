// SPDX-License-Identifier: MIT
//
// nv2a_depth - NV2A software-rasterizer Phase 3. Phases 0-2 always painted the
// last triangle over whatever was there. This probe adds the depth buffer: it
// binds a Z16 zeta surface, enables the LESS depth test, and draws two regions
// that each stack two triangles in a fixed order but at different depths.
//
//   left  region: RED at z=0.5 first, then GREEN at z=0.9 (farther)  -> RED wins
//   right region: RED at z=0.5 first, then BLUE at z=0.2 (nearer)     -> BLUE wins
//
// Both second triangles are drawn *after* the red one, so a target that ignores
// depth would show GREEN on the left and BLUE on the right. A conforming target
// rejects the farther green (LESS fails) but accepts the nearer blue -- proving
// the depth test works and is independent of draw order.
//
// SCOPE: depth test + write (Z16), screen-space z interpolation. No stencil,
// texturing. Requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1).

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
#define ZMAX 65535.0f

typedef struct { float x, y, z; uint32_t color; } Vertex;

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    xt_begin("v1", "nv2a_depth");
    xt_note("NV2A rasterizer Phase 3 (Z16 depth test, draw-order independent)");
    xt_note("requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_check_bool("nv2a_depth.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t  pitch_px = pb_back_buffer_pitch() / 4;
    uint32_t  bbh = pb_back_buffer_height();
    xt_check_bool("nv2a_depth.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) { pb_kill(); return xt_end(); }
    for (uint32_t i = 0; i < pitch_px * bbh; i++)
        bb[i] = 0xFF000000u;

    // Z16 depth buffer, cleared to the far value so the first triangle passes.
    uint16_t *zbuf = (uint16_t *)MmAllocateContiguousMemoryEx(
        FBW * FBH * 2, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_depth.zbuf_alloc", 1, zbuf != NULL);
    if (zbuf == NULL) { pb_kill(); return xt_end(); }
    for (int i = 0; i < FBW * FBH; i++)
        zbuf[i] = 0xFFFF;

    const uint32_t RED = 0xFFFF0000u, GREEN = 0xFF00FF00u, BLUE = 0xFF0000FFu;
    // 12 vertices = 4 flat triangles (3 each). z in ndc [0,1]; the viewport
    // z-scale maps it to the Z16 range.
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        12 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_depth.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) { pb_kill(); return xt_end(); }

    // Left region triangle (centroid ~190,287), right region (~450,287).
    #define LTRI(zz, c) \
        (Vertex){190.0f,100.0f,(zz),(c)}, (Vertex){300.0f,380.0f,(zz),(c)}, (Vertex){80.0f,380.0f,(zz),(c)}
    #define RTRI(zz, c) \
        (Vertex){450.0f,100.0f,(zz),(c)}, (Vertex){560.0f,380.0f,(zz),(c)}, (Vertex){340.0f,380.0f,(zz),(c)}
    Vertex verts[12] = {
        LTRI(0.5f, RED),    // 0-2 : left red, mid depth (drawn first)
        LTRI(0.9f, GREEN),  // 3-5 : left green, far (drawn after -> rejected)
        RTRI(0.5f, RED),    // 6-8 : right red, mid depth (drawn first)
        RTRI(0.2f, BLUE),   // 9-11: right blue, near (drawn after -> accepted)
    };
    for (int i = 0; i < 12; i++) vb[i] = verts[i];
    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    for (int rep = 0; rep < 4; rep++) {
        uint32_t *p = pb_begin();

        // Color + zeta surfaces (both raw base-0 pointers).
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_FORMAT, (uint32_t)NV097_SET_SURFACE_FORMAT_ZETA_Z16 << 4);
        p = pb_push1(p, NV097_SET_SURFACE_PITCH,
                     ((uint32_t)(FBW * 2) << 16) | (pb_back_buffer_pitch() & 0xFFFF));
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);
        p = pb_push1(p, NV097_SET_SURFACE_ZETA_OFFSET,  (uint32_t)(uintptr_t)zbuf);

        // Depth test: LESS, writes enabled.
        p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, 1);
        p = pb_push1(p, NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LESS);
        p = pb_push1(p, NV097_SET_DEPTH_MASK, 1);

        // Viewport: xy identity (positions are screen space); z -> [0,ZMAX].
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(ZMAX));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));

        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, vbAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_DIFFUSE * 4,
                     vbAddr + (uint32_t)offsetof(Vertex, color));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, sizeof(Vertex)));

        // Four draws in order: L-red, L-green, R-red, R-blue.
        for (uint32_t t = 0; t < 4; t++) {
            p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
            p = pb_push1(p, NV097_DRAW_ARRAYS, ((3u - 1u) << 24) | (t * 3u));
            p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
        }
        pb_end(p);
        while (pb_busy())
            ;
    }

#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t left  = PX(190, 287);
    uint32_t right = PX(450, 287);
    xt_ev("nv2a_depth.left=0x%08lX right=0x%08lX",
          (unsigned long)left, (unsigned long)right);

    int lr = (left >> 16) & 0xFF, lg = (left >> 8) & 0xFF, lb2 = left & 0xFF;
    int rr = (right >> 16) & 0xFF, rg = (right >> 8) & 0xFF, rb = right & 0xFF;

    // Left: the later, farther green was rejected -> still red.
    xt_check("nv2a_depth.left_red_kept", lr > 200 && lg < 80 && lb2 < 80,
             "left R=%d G=%d B=%d must stay red (far green rejected)", lr, lg, lb2);
    // Right: the later, nearer blue passed -> blue.
    xt_check("nv2a_depth.right_blue_wins", rb > 200 && rr < 80 && rg < 80,
             "right R=%d G=%d B=%d must be blue (near blue accepted)", rr, rg, rb);

    pb_kill();
    return xt_end();
}
