// SPDX-License-Identifier: MIT
//
// nv2a_transform - NV2A software-rasterizer Phase 1. Phase 0 (nv2a_raster)
// proved the pushbuffer -> pixels path with a flat, pre-transformed triangle.
// This probe exercises the transform back-end that sits between the vertex
// program and the raster: it programs a viewport (SET_VIEWPORT_OFFSET/SCALE)
// and submits *homogeneous clip-space* vertices (w != 1, so the perspective
// divide matters), each carrying a distinct diffuse color. A conforming target
// must (a) map each vertex through clip -> NDC -> viewport to the screen pixel
// we expect and (b) Gouraud-interpolate the three vertex colors across the
// triangle. The near-vertex samples pin down the mapping; the centroid pins
// down the interpolation.
//
// SCOPE: perspective divide + viewport + barycentric color interpolation. No
// vertex-program execution yet (positions are supplied already in clip space),
// no z-buffer / texturing. Requires the target's raster path (Cxbx:
// CXBX_NV2A_RASTER=1).

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

// x,y,z,w homogeneous clip position + a packed D3DCOLOR (0xAARRGGBB) diffuse.
typedef struct { float x, y, z, w; uint32_t color; } Vertex;

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    xt_begin("v1", "nv2a_transform");
    xt_note("NV2A rasterizer Phase 1 (viewport + perspective divide + Gouraud)");
    xt_note("requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_check_bool("nv2a_transform.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t  pitch_px = pb_back_buffer_pitch() / 4;
    uint32_t  bbh = pb_back_buffer_height();
    xt_check_bool("nv2a_transform.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) {
        pb_kill();
        return xt_end();
    }

    for (uint32_t i = 0; i < pitch_px * bbh; i++)
        bb[i] = 0xFF000000u;

    // Viewport maps NDC [-1,1] -> the 640x480 surface with y flipped:
    //   screen.x = ndc.x*320 + 320,  screen.y = ndc.y*(-240) + 240.
    const float vp_sx = 320.0f, vp_sy = -240.0f;
    const float vp_ox = 320.0f, vp_oy = 240.0f;

    // Target screen triangle: A top-centre, B bottom-right, C bottom-left.
    //   A=(320,120) B=(500,360) C=(140,360), centroid (320,280).
    // Back-solve NDC via the inverse viewport, then scale by w to get clip.
    const float W = 2.0f;
    // ndc = ((sx-ox)/vp_sx, (sy-oy)/vp_sy); clip = ndc*W.
    // A: ndc(0, 0.5)      B: ndc(0.5625,-0.5)      C: ndc(-0.5625,-0.5)
    const uint32_t RED = 0xFFFF0000u, GREEN = 0xFF00FF00u, BLUE = 0xFF0000FFu;
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        3 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_transform.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) {
        pb_kill();
        return xt_end();
    }
    vb[0] = (Vertex){  0.0f * W,  0.5f    * W, 0.0f, W, RED   };
    vb[1] = (Vertex){  0.5625f*W, -0.5f   * W, 0.0f, W, GREEN };
    vb[2] = (Vertex){ -0.5625f*W, -0.5f   * W, 0.0f, W, BLUE  };

    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    for (int rep = 0; rep < 4; rep++) {
        uint32_t *p = pb_begin();

        // Bind the color surface (raw back-buffer pointer, base-0 DMA).
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);

        // Program the viewport (4 floats each; z/w unused by Phase 1).
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(vp_ox));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(vp_oy));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(vp_sx));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(vp_sy));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));

        // Position (float4 clip) + diffuse (D3DCOLOR) arrays.
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

    // --- Readback ---------------------------------------------------------- //
#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t centroid = PX(320, 280);   // balanced blend
    uint32_t nearA    = PX(320, 160);   // near red vertex
    uint32_t nearB    = PX(460, 350);   // near green vertex
    uint32_t nearC    = PX(180, 350);   // near blue vertex
    uint32_t above    = PX(320,  60);   // above the apex -> outside
    uint32_t corner   = PX(8, 8);       // outside

    xt_ev("nv2a_transform.centroid=0x%08lX nearA=0x%08lX nearB=0x%08lX nearC=0x%08lX",
          (unsigned long)centroid, (unsigned long)nearA,
          (unsigned long)nearB, (unsigned long)nearC);
    xt_ev("nv2a_transform.above=0x%08lX corner=0x%08lX",
          (unsigned long)above, (unsigned long)corner);

    // Position mapping: the triangle lands where the viewport maps it.
    xt_check_bool("nv2a_transform.centroid_filled", 1, (centroid & 0x00FFFFFF) != 0);
    xt_check_bool("nv2a_transform.above_clear", 1, (above & 0x00FFFFFF) == 0);
    xt_check_bool("nv2a_transform.corner_clear", 1, (corner & 0x00FFFFFF) == 0);

    // Interpolation: each near-vertex sample is dominated by that vertex's colour.
    int ra = (nearA >> 16) & 0xFF, ba = nearA & 0xFF;
    int gb = (nearB >> 8) & 0xFF,  rb = (nearB >> 16) & 0xFF, bb2 = nearB & 0xFF;
    int bc = nearC & 0xFF,         rc = (nearC >> 16) & 0xFF, gc = (nearC >> 8) & 0xFF;
    xt_check("nv2a_transform.nearA_red",  ra > ba + 40,
             "R=%d must exceed B=%d near the red vertex", ra, ba);
    xt_check("nv2a_transform.nearB_green", gb > rb + 40 && gb > bb2 + 40,
             "G=%d must dominate R=%d B=%d near the green vertex", gb, rb, bb2);
    xt_check("nv2a_transform.nearC_blue",  bc > rc + 40 && bc > gc + 40,
             "B=%d must dominate R=%d G=%d near the blue vertex", bc, rc, gc);

    // Centroid is a balanced blend of the three primaries (each ~1/3 -> ~0x55).
    int cr = (centroid >> 16) & 0xFF, cg = (centroid >> 8) & 0xFF, cbb = centroid & 0xFF;
    int hi = cr > cg ? (cr > cbb ? cr : cbb) : (cg > cbb ? cg : cbb);
    int lo = cr < cg ? (cr < cbb ? cr : cbb) : (cg < cbb ? cg : cbb);
    xt_check("nv2a_transform.centroid_balanced", (hi - lo) <= 40 && lo >= 40,
             "centroid R=%d G=%d B=%d should be a balanced mid blend", cr, cg, cbb);

    pb_kill();
    return xt_end();
}
