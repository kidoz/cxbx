// SPDX-License-Identifier: MIT
//
// nv2a_scene - a standalone raw-NV2A viewer (NOT a conformance probe; it lives
// outside probes/ so the suite never runs it). It draws a scene through the
// software rasterizer into the displayed framebuffer and then presents forever,
// so the live window (Cxbx: CXBX_NV2A_RASTER=1 CXBX_NV2A_WINDOW=1) stays up and
// shows it. Same content as nv2a_demo: dark-blue background, a Gouraud triangle
// (red/green/blue corners) and a solid orange quad.

#include <stddef.h>
#include <string.h>
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
    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);
    if (pb_init() != 0)
        return 1;

    uint32_t *fb = (uint32_t *)XVideoGetFB();
    uint32_t pitch_px = pb_back_buffer_pitch() / 4;
    if (fb == NULL || pitch_px == 0) { pb_kill(); return 1; }

    for (uint32_t y = 0; y < FBH; y++)
        for (uint32_t x = 0; x < FBW; x++)
            fb[y * pitch_px + x] = 0xFF000030u;   // dark blue

    const uint32_t RED = 0xFFFF0000u, GREEN = 0xFF00FF00u, BLUE = 0xFF0000FFu, ORANGE = 0xFFFF8000u;
    Vertex *vb = (Vertex *)MmAllocateContiguousMemoryEx(
        7 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (vb == NULL) { pb_kill(); return 1; }
    vb[0] = (Vertex){ 320.0f,  60.0f, 0.0f, RED   };
    vb[1] = (Vertex){ 560.0f, 420.0f, 0.0f, GREEN };
    vb[2] = (Vertex){  80.0f, 420.0f, 0.0f, BLUE  };
    vb[3] = (Vertex){  40.0f,  40.0f, 0.0f, ORANGE };
    vb[4] = (Vertex){ 180.0f,  40.0f, 0.0f, ORANGE };
    vb[5] = (Vertex){ 180.0f, 120.0f, 0.0f, ORANGE };
    vb[6] = (Vertex){  40.0f, 120.0f, 0.0f, ORANGE };
    uint32_t vbAddr = (uint32_t)(uintptr_t)vb;

    uint32_t *p = pb_begin();
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
    p = pb_push1(p, NV097_SET_SURFACE_PITCH, pb_back_buffer_pitch() & 0xFFFF);
    p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)fb);
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

    // Present forever so the live window stays up showing the frame.
    for (;;) {
        pb_show_debug_screen();
        while (pb_busy())
            ;
        Sleep(16);
    }
    return 0;
}
