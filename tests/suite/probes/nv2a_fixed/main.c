// SPDX-License-Identifier: MIT
//
// nv2a_fixed - NV2A software-rasterizer fixed-function transform. Where nv2a_vp
// transforms object-space vertices with a vertex program, this exercises the
// other front-end: the fixed-function pipeline. It stays in FIXED execution
// mode (no program), uploads a composite matrix (object -> screen homogeneous),
// and submits object-space vertices. The fixed-function composite matrix
// already includes viewport scale; hardware divides by w and then adds the
// viewport offset.
//
// The matrix folds scale(0.5,0.5,1) into the 640x480 viewport, so the object
// triangle maps to A(320,120) B(480,360) C(160,360), centroid (320,280) --
// identical placement to nv2a_vp, but reached through the fixed-function matrix
// rather than a program.
//
// SCOPE: composite-matrix transform + Gouraud. Requires the target's raster
// path (Cxbx: CXBX_NV2A_RASTER=1).

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

typedef struct { float x, y, z, w; uint32_t color; } Vertex;

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    xt_begin("v1", "nv2a_fixed");
    xt_note("NV2A rasterizer fixed-function transform (composite matrix)");
    xt_note("requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_check_bool("nv2a_fixed.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t  pitch_px = pb_back_buffer_pitch() / 4;
    uint32_t  bbh = pb_back_buffer_height();
    xt_check_bool("nv2a_fixed.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) { pb_kill(); return xt_end(); }
    for (uint32_t i = 0; i < pitch_px * bbh; i++)
        bb[i] = 0xFF000000u;

    const uint32_t RED = 0xFFFF0000u, GREEN = 0xFF00FF00u, BLUE = 0xFF0000FFu;
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        3 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_fixed.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) { pb_kill(); return xt_end(); }
    vb[0] = (Vertex){  0.0f,  1.0f, 0.0f, 1.0f, RED   };
    vb[1] = (Vertex){  1.0f, -1.0f, 0.0f, 1.0f, GREEN };
    vb[2] = (Vertex){ -1.0f, -1.0f, 0.0f, 1.0f, BLUE  };
    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    // Composite matrix, row-major: object -> screen homogeneous. The XDK
    // fixed-function path folds viewport scale into these matrix columns.
    const float M[16] = {
        160.0f,   0.0f, 0.0f, 0.0f,
          0.0f, -120.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    const float vp_sx = 320.0f, vp_sy = -240.0f, vp_ox = 320.0f, vp_oy = 240.0f;

    for (int rep = 0; rep < 4; rep++) {
        uint32_t *p = pb_begin();

        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);

        // Fixed-function execution mode (no vertex program).
        p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
                     NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_FIXED);

        // Composite matrix (16 floats).
        for (int m = 0; m < 16; m++)
            p = pb_push1(p, NV097_SET_COMPOSITE_MATRIX + m * 4, f2u(M[m]));

        // Fixed-function hardware adds the viewport offset after divide. The
        // scale registers remain programmed because that is normal XDK state,
        // but fixed-function position output already contains their effect.
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(vp_ox));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(vp_oy));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(vp_sx));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(vp_sy));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));

        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, vbAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 4, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_DIFFUSE * 4,
                     vbAddr + (uint32_t)offsetof(Vertex, color));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, sizeof(Vertex)));

        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((3u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
        pb_end(p);
        while (pb_busy())
            ;
    }

#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t centroid = PX(320, 280);
    uint32_t nearA    = PX(320, 150);
    uint32_t nearB    = PX(440, 350);
    uint32_t nearC    = PX(200, 350);
    uint32_t above    = PX(320,  60);
    xt_ev("nv2a_fixed.centroid=0x%08lX nearA=0x%08lX nearB=0x%08lX nearC=0x%08lX",
          (unsigned long)centroid, (unsigned long)nearA,
          (unsigned long)nearB, (unsigned long)nearC);

    xt_check_bool("nv2a_fixed.centroid_filled", 1, (centroid & 0x00FFFFFF) != 0);
    xt_check_bool("nv2a_fixed.above_clear", 1, (above & 0x00FFFFFF) == 0);

    int ra = (nearA >> 16) & 0xFF, ba = nearA & 0xFF;
    int gb = (nearB >> 8) & 0xFF,  rb = (nearB >> 16) & 0xFF, bb2 = nearB & 0xFF;
    int bc = nearC & 0xFF,         rc = (nearC >> 16) & 0xFF, gc = (nearC >> 8) & 0xFF;
    xt_check("nv2a_fixed.nearA_red",  ra > ba + 40,
             "R=%d must exceed B=%d near the transformed red vertex", ra, ba);
    xt_check("nv2a_fixed.nearB_green", gb > rb + 40 && gb > bb2 + 40,
             "G=%d must dominate R=%d B=%d near the transformed green vertex", gb, rb, bb2);
    xt_check("nv2a_fixed.nearC_blue",  bc > rc + 40 && bc > gc + 40,
             "B=%d must dominate R=%d G=%d near the transformed blue vertex", bc, rc, gc);

    int cr = (centroid >> 16) & 0xFF, cg = (centroid >> 8) & 0xFF, cbb = centroid & 0xFF;
    int hi = cr > cg ? (cr > cbb ? cr : cbb) : (cg > cbb ? cg : cbb);
    int lo = cr < cg ? (cr < cbb ? cr : cbb) : (cg < cbb ? cg : cbb);
    xt_check("nv2a_fixed.centroid_balanced", (hi - lo) <= 40 && lo >= 40,
             "centroid R=%d G=%d B=%d should be a balanced mid blend", cr, cg, cbb);

    pb_kill();
    return xt_end();
}
