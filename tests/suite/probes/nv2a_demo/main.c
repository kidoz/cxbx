// SPDX-License-Identifier: MIT
//
// nv2a_demo - end-to-end visible render. The other nv2a probes verify the
// rasterizer by reading pixels back out of guest memory; this one draws THROUGH
// the rasterizer into the *displayed* framebuffer (XVideoGetFB) and flips the
// CRTC scanout to it, so a scanout-capturing target writes the result to
// %TEMP%\cxbx_fbN.bmp -- an actual on-screen image, not just a memory check.
//
// It clears to dark blue and draws a large Gouraud triangle (red/green/blue
// corners) plus a smaller opaque quad, then presents. Requires the target's
// raster path (Cxbx: CXBX_NV2A_RASTER=1).

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
    xt_begin("v1", "nv2a_demo");
    xt_note("end-to-end visible render: rasterize into the displayed framebuffer");
    xt_note("open %TEMP%\\cxbx_fb0.bmp to see the image (needs CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    int status = pb_init();
    xt_check_bool("nv2a_demo.pb_init", 1, status == 0);
    if (status != 0)
        return xt_end();

    // Render straight into the displayed surface so the flip shows our pixels.
    uint32_t *fb = (uint32_t *)XVideoGetFB();
    uint32_t  pitch_px = pb_back_buffer_pitch() / 4;
    xt_check_bool("nv2a_demo.fb", 1, fb != NULL && pitch_px != 0);
    if (fb == NULL || pitch_px == 0) { pb_kill(); return xt_end(); }

    // Dark-blue background so the frame is clearly a rendered scene, not black.
    for (uint32_t y = 0; y < FBH; y++)
        for (uint32_t x = 0; x < FBW; x++)
            fb[y * pitch_px + x] = 0xFF000030u;

    // Big Gouraud triangle (screen space) + a solid orange quad.
    const uint32_t RED = 0xFFFF0000u, GREEN = 0xFF00FF00u, BLUE = 0xFF0000FFu, ORANGE = 0xFFFF8000u;
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        7 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_demo.vbuf_alloc", 1, vb != NULL);
    if (vb == NULL) { pb_kill(); return xt_end(); }
    vb[0] = (Vertex){ 320.0f,  60.0f, 0.0f, RED   };
    vb[1] = (Vertex){ 560.0f, 420.0f, 0.0f, GREEN };
    vb[2] = (Vertex){  80.0f, 420.0f, 0.0f, BLUE  };
    // small orange quad, top-left
    vb[3] = (Vertex){  40.0f,  40.0f, 0.0f, ORANGE };
    vb[4] = (Vertex){ 180.0f,  40.0f, 0.0f, ORANGE };
    vb[5] = (Vertex){ 180.0f, 120.0f, 0.0f, ORANGE };
    vb[6] = (Vertex){  40.0f, 120.0f, 0.0f, ORANGE };
    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    for (int rep = 0; rep < 4; rep++) {
        uint32_t *p = pb_begin();

        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)fb);

        // Identity viewport: these are already screen-space positions.
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(1.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));

        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, vbAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(Vertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_DIFFUSE * 4,
                     vbAddr + (uint32_t)offsetof(Vertex, color));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, sizeof(Vertex)));

        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((3u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);

        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((4u - 1u) << 24) | 3u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);

        pb_end(p);
        while (pb_busy())
            ;
    }

    // Flip the CRTC scanout to the surface we drew into -> the capture path
    // snapshots it to cxbx_fbN.bmp.
    for (int i = 0; i < 4; i++) {
        pb_show_debug_screen();
        while (pb_busy())
            ;
    }

#define PX(x, y) (fb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t centroid = PX(320, 300);   // inside the Gouraud triangle
    uint32_t quad     = PX(110, 80);    // inside the orange quad
    uint32_t bg       = PX(600, 20);    // background corner
    xt_ev("nv2a_demo.centroid=0x%08lX quad=0x%08lX bg=0x%08lX",
          (unsigned long)centroid, (unsigned long)quad, (unsigned long)bg);
    xt_check_bool("nv2a_demo.triangle_drawn", 1, (centroid & 0x00FFFFFF) != 0x000030);
    xt_check_bool("nv2a_demo.quad_orange", 1,
                  ((quad >> 16) & 0xFF) > 200 && ((quad >> 8) & 0xFF) > 80);
    xt_check_u32("nv2a_demo.bg_blue", 0xFF000030u, bg);

    pb_kill();
    return xt_end();
}
