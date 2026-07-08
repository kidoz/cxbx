// SPDX-License-Identifier: MIT
//
// nv2a_raster - NV2A software-rasterizer Phase 0. Where gfx_draw proves the
// pusher tolerates a full draw batch (submission only), this probe closes the
// loop to *pixels*: it points the position + diffuse vertex arrays at a
// screen-space triangle and submits BEGIN(TRIANGLES) + DRAW_ARRAYS + END, then
// reads the bound color surface (pbkit's back buffer) back and asserts the
// triangle's interior holds the flat diffuse color.
//
// The vertices are already in pixel coordinates (0..640, 0..480), so no vertex
// program is needed -- this is the pre-transformed slice a passthrough pipeline
// would emit. A target with the Phase 0 raster path fills the triangle; a
// submission-only target leaves the back buffer at the cleared color, so the
// centroid check distinguishes the two.
//
// SCOPE: one flat-shaded triangle, no z-buffer / texturing / vertex program.
// Requires the target's raster path (Cxbx: run with CXBX_NV2A_RASTER=1).

#include "xtest.h"
#include <stddef.h>
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>

// KELVIN vertex-attribute indices (array offset/format are indexed by attr*4).
#define ATTR_POSITION 0
#define ATTR_DIFFUSE  3

// SET_VERTEX_DATA_ARRAY_FORMAT: (stride << 8) | (size << 4) | type.
#define VTX_FMT(type, size, stride) \
    (((uint32_t)(stride) << 8) | ((uint32_t)(size) << 4) | (uint32_t)(type))

#define FBW 640
#define FBH 480

// x,y,z position + a packed D3DCOLOR (0xAARRGGBB) diffuse per vertex.
typedef struct { float x, y, z; uint32_t color; } Vertex;

int main(void)
{
    xt_begin("v1", "nv2a_raster");
    xt_note("NV2A software rasterizer Phase 0 (flat triangle -> surface readback)");
    xt_note("requires the target's raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_ev("nv2a_raster.pb_init_status=%d", status);
    xt_check_bool("nv2a_raster.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    // The color surface pbkit renders into -- and the emulator resolves from
    // SET_SURFACE_COLOR_OFFSET. Both write/read the same guest memory.
    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t  pitch_px = pb_back_buffer_pitch() / 4;
    uint32_t  bbw = pb_back_buffer_width();
    uint32_t  bbh = pb_back_buffer_height();
    xt_ev("nv2a_raster.back_buffer=0x%08lX pitch_px=%lu %lux%lu",
          (unsigned long)(uintptr_t)bb, (unsigned long)pitch_px,
          (unsigned long)bbw, (unsigned long)bbh);
    xt_check_bool("nv2a_raster.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) {
        pb_kill();
        return xt_end();
    }

    // Baseline: clear the surface to opaque black so a non-black centroid can
    // only come from the rasterizer filling the triangle.
    for (uint32_t i = 0; i < pitch_px * bbh; i++)
        bb[i] = 0xFF000000u;

    // A green triangle whose centroid (~320,287) sits well inside the surface.
    const uint32_t GREEN = 0xFF00FF00u;
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        3 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_raster.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) {
        pb_kill();
        return xt_end();
    }
    vb[0] = (Vertex){ 320.0f, 100.0f, 0.0f, GREEN };
    vb[1] = (Vertex){ 520.0f, 380.0f, 0.0f, GREEN };
    vb[2] = (Vertex){ 120.0f, 380.0f, 0.0f, GREEN };

    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    for (int rep = 0; rep < 4; rep++) {
        uint32_t *p = pb_begin();

        // Bind the color surface explicitly to the back buffer as a raw pointer
        // (base-0 DMA, like the raw vertex/texture pointers gfx_draw uses), plus
        // its pitch and clip rect, so the rasterizer targets the buffer we read.
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);

        // Point the position (float3) + diffuse (D3DCOLOR) arrays at the buffer.
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, vbAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_DIFFUSE * 4,
                     vbAddr + (uint32_t)offsetof(Vertex, color));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, sizeof(Vertex)));

        // Draw the triangle: 3 vertices starting at index 0.
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((3u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);

        pb_end(p);
        while (pb_busy())
            ;
    }

    // Read the surface back at the triangle centroid and a corner. The centroid
    // must be green (filled); the corner must stay black (outside the triangle).
    uint32_t centroid = bb[287u * pitch_px + 320u] | 0xFF000000u;
    uint32_t corner   = bb[8u * pitch_px + 8u] | 0xFF000000u;
    xt_ev("nv2a_raster.centroid=0x%08lX corner=0x%08lX",
          (unsigned long)centroid, (unsigned long)corner);
    xt_check_u32("nv2a_raster.centroid_filled", GREEN, centroid);
    xt_check_u32("nv2a_raster.corner_clear", 0xFF000000u, corner);

    pb_kill();
    return xt_end();
}
