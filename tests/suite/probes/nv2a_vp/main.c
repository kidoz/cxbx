// SPDX-License-Identifier: MIT
//
// nv2a_vp - NV2A software-rasterizer Phase 2. Phase 0/1 fed the raster with
// vertices already in clip/screen space. This probe closes the front of the
// pipeline: it uploads a real NV2A vertex program (microcode) plus a transform
// matrix in constant memory, switches the transform unit to PROGRAM mode, and
// submits *object-space* vertices. A conforming target must run the microcode
// per vertex on the CPU -- dp4 the object position against the matrix rows into
// clip-space oPos and pass the diffuse through oD0 -- then hand the result to
// the Phase 1 back-end (perspective divide + viewport + Gouraud).
//
// The program (assembled offline, see tools note) is:
//     dp4 oPos.x, v0, c0        ; clip.x = obj . row0
//     dp4 oPos.y, v0, c1
//     dp4 oPos.z, v0, c2
//     dp4 oPos.w, v0, c3
//     mov oD0,    v3            ; diffuse passthrough (FINAL)
// with the matrix c0..c3 = scale(0.5,0.5,1) so object (x,y) maps to ndc (x/2,
// y/2). Without VP execution the object coords would land somewhere else, so
// the near-vertex/centroid samples prove the microcode actually ran.
//
// SCOPE: vertex-program execution (dp4/mov, constants, R12/oPos alias). No
// z-buffer / texturing. Requires the target's raster path (CXBX_NV2A_RASTER=1).

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

// Assembled by tools/vpasm (fields per EmuVshDecoder g_VshFields).
static const uint32_t VP_PROGRAM[20] = {
    0x00000000, 0x00E0001B, 0x0836186C, 0x00008800,   // dp4 oPos.x, v0, c0
    0x00000000, 0x00E0201B, 0x0836186C, 0x00004800,   // dp4 oPos.y, v0, c1
    0x00000000, 0x00E0401B, 0x0836186C, 0x00002800,   // dp4 oPos.z, v0, c2
    0x00000000, 0x00E0601B, 0x0836186C, 0x00001800,   // dp4 oPos.w, v0, c3
    0x00000000, 0x0020061B, 0x0836006C, 0x0000F819,   // mov oD0, v3 (FINAL)
};

int main(void)
{
    xt_begin("v1", "nv2a_vp");
    xt_note("NV2A rasterizer Phase 2 (CPU vertex-program execution)");
    xt_note("requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_check_bool("nv2a_vp.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t  pitch_px = pb_back_buffer_pitch() / 4;
    uint32_t  bbh = pb_back_buffer_height();
    xt_check_bool("nv2a_vp.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) {
        pb_kill();
        return xt_end();
    }
    for (uint32_t i = 0; i < pitch_px * bbh; i++)
        bb[i] = 0xFF000000u;

    // Object-space triangle (NOT pre-transformed) + per-vertex diffuse.
    const uint32_t RED = 0xFFFF0000u, GREEN = 0xFF00FF00u, BLUE = 0xFF0000FFu;
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        3 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_vp.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) {
        pb_kill();
        return xt_end();
    }
    vb[0] = (Vertex){  0.0f,  1.0f, 0.0f, 1.0f, RED   };
    vb[1] = (Vertex){  1.0f, -1.0f, 0.0f, 1.0f, GREEN };
    vb[2] = (Vertex){ -1.0f, -1.0f, 0.0f, 1.0f, BLUE  };
    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    // Transform matrix rows (DP4 row vectors): scale x,y by 0.5, keep z, w=1.
    const float MATRIX[16] = {
        0.5f, 0.0f, 0.0f, 0.0f,   // c0 -> clip.x = 0.5*obj.x
        0.0f, 0.5f, 0.0f, 0.0f,   // c1 -> clip.y = 0.5*obj.y
        0.0f, 0.0f, 1.0f, 0.0f,   // c2 -> clip.z = obj.z
        0.0f, 0.0f, 0.0f, 1.0f,   // c3 -> clip.w = obj.w (=1)
    };

    // Upload the vertex program.
    uint32_t *p = pb_begin();
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    for (int i = 0; i < 20; i++)
        p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM, VP_PROGRAM[i]);
    pb_end(p);

    // Upload the matrix constants and switch to PROGRAM mode.
    p = pb_begin();
    p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT_LOAD, 0);
    for (int i = 0; i < 16; i++)
        p = pb_push1(p, NV097_SET_TRANSFORM_CONSTANT, f2u(MATRIX[i]));
    p = pb_push1(p, NV097_SET_TRANSFORM_PROGRAM_START, 0);
    p = pb_push1(p, NV097_SET_TRANSFORM_EXECUTION_MODE,
                 NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM);
    pb_end(p);

    // Same viewport as nv2a_transform: ndc [-1,1] -> 640x480, y flipped.
    const float vp_sx = 320.0f, vp_sy = -240.0f, vp_ox = 320.0f, vp_oy = 240.0f;

    for (int rep = 0; rep < 4; rep++) {
        p = pb_begin();
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);

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

    // The VP maps object (x,y) -> ndc (x/2,y/2) -> screen: A(320,120)
    // B(480,360) C(160,360), centroid (320,280).
#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t centroid = PX(320, 280);
    uint32_t nearA    = PX(320, 150);
    uint32_t nearB    = PX(440, 350);
    uint32_t nearC    = PX(200, 350);
    uint32_t above    = PX(320,  60);

    xt_ev("nv2a_vp.centroid=0x%08lX nearA=0x%08lX nearB=0x%08lX nearC=0x%08lX",
          (unsigned long)centroid, (unsigned long)nearA,
          (unsigned long)nearB, (unsigned long)nearC);

    xt_check_bool("nv2a_vp.centroid_filled", 1, (centroid & 0x00FFFFFF) != 0);
    xt_check_bool("nv2a_vp.above_clear", 1, (above & 0x00FFFFFF) == 0);

    int ra = (nearA >> 16) & 0xFF, ba = nearA & 0xFF;
    int gb = (nearB >> 8) & 0xFF,  rb = (nearB >> 16) & 0xFF, bb2 = nearB & 0xFF;
    int bc = nearC & 0xFF,         rc = (nearC >> 16) & 0xFF, gc = (nearC >> 8) & 0xFF;
    xt_check("nv2a_vp.nearA_red",  ra > ba + 40,
             "R=%d must exceed B=%d near the transformed red vertex", ra, ba);
    xt_check("nv2a_vp.nearB_green", gb > rb + 40 && gb > bb2 + 40,
             "G=%d must dominate R=%d B=%d near the transformed green vertex", gb, rb, bb2);
    xt_check("nv2a_vp.nearC_blue",  bc > rc + 40 && bc > gc + 40,
             "B=%d must dominate R=%d G=%d near the transformed blue vertex", bc, rc, gc);

    int cr = (centroid >> 16) & 0xFF, cg = (centroid >> 8) & 0xFF, cbb = centroid & 0xFF;
    int hi = cr > cg ? (cr > cbb ? cr : cbb) : (cg > cbb ? cg : cbb);
    int lo = cr < cg ? (cr < cbb ? cr : cbb) : (cg < cbb ? cg : cbb);
    xt_check("nv2a_vp.centroid_balanced", (hi - lo) <= 40 && lo >= 40,
             "centroid R=%d G=%d B=%d should be a balanced mid blend", cr, cg, cbb);

    pb_kill();
    return xt_end();
}
