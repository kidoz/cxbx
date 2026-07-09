// SPDX-License-Identifier: MIT
//
// nv2a_scene - a standalone animated raw-NV2A demo (NOT a conformance probe; it
// lives outside probes/ so the suite never runs it). It drives the pushbuffer
// every frame to exercise the whole software-rasterizer pipeline live -- fixed-
// function matrix transform, depth testing, Gouraud shading and texturing -- so
// the live window (Cxbx: CXBX_NV2A_RASTER=1 CXBX_NV2A_WINDOW=1) shows an actual
// moving scene: a spinning RGB Gouraud triangle behind a spinning textured quad,
// depth-sorted so the quad stays in front.

#include <stddef.h>
#include <string.h>
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>

#define ATTR_POSITION 0
#define ATTR_DIFFUSE  3
#define ATTR_TEXCOORD 9
#define VTX_FMT(type, size, stride) \
    (((uint32_t)(stride) << 8) | ((uint32_t)(size) << 4) | (uint32_t)(type))

#define FBW 640
#define FBH 480
#define ZMAX 65535.0f

typedef struct { float x, y, z; uint32_t color; } CVertex;   // Gouraud triangle
typedef struct { float x, y, z; float u, v;    } TVertex;   // textured quad

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

// Freestanding sin/cos (no libm dependency) -- plenty accurate for animation.
static float mysin(float x)
{
    while (x >  3.14159265f) x -= 6.28318531f;
    while (x < -3.14159265f) x += 6.28318531f;
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}
static float mycos(float x) { return mysin(x + 1.5707963f); }

// Row-major rotation-about-Z composite matrix (object -> clip).
static void rotZ(float M[16], float a)
{
    float c = mycos(a), s = mysin(a);
    memset(M, 0, 16 * sizeof(float));
    M[0] = c;  M[1] = s;
    M[4] = -s; M[5] = c;
    M[10] = 1.0f; M[15] = 1.0f;
}

int main(void)
{
    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);
    if (pb_init() != 0)
        return 1;

    uint32_t *fb = (uint32_t *)XVideoGetFB();
    uint32_t pitch_px = pb_back_buffer_pitch() / 4;
    if (fb == NULL || pitch_px == 0) { pb_kill(); return 1; }

    uint16_t *zbuf = (uint16_t *)MmAllocateContiguousMemoryEx(
        FBW * FBH * 2, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (zbuf == NULL) { pb_kill(); return 1; }

    // A small bright 4x4 texture (linear A8R8G8B8) for the quad.
    uint32_t *tex = (uint32_t *)MmAllocateContiguousMemoryEx(
        4 * 4 * 4, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (tex == NULL) { pb_kill(); return 1; }
    static const uint32_t pal[4] = { 0xFFFF3030u, 0xFF30FF30u, 0xFF3030FFu, 0xFFFFFF30u };
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            tex[y * 4 + x] = pal[((x + y) & 1) ? ((x * 3 + y) & 3) : ((x + y * 2) & 3)];

    // Object-space geometry. Triangle sits behind (z far), quad in front (z near).
    CVertex *tri = (CVertex *)MmAllocateContiguousMemoryEx(
        3 * sizeof(CVertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    TVertex *quad = (TVertex *)MmAllocateContiguousMemoryEx(
        4 * sizeof(TVertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    if (tri == NULL || quad == NULL) { pb_kill(); return 1; }
    tri[0] = (CVertex){  0.00f,  0.80f, 0.7f, 0xFFFF0000u };
    tri[1] = (CVertex){  0.72f, -0.55f, 0.7f, 0xFF00FF00u };
    tri[2] = (CVertex){ -0.72f, -0.55f, 0.7f, 0xFF0000FFu };
    quad[0] = (TVertex){ -0.45f, -0.45f, 0.3f, 0.0f, 0.0f };
    quad[1] = (TVertex){  0.45f, -0.45f, 0.3f, 1.0f, 0.0f };
    quad[2] = (TVertex){  0.45f,  0.45f, 0.3f, 1.0f, 1.0f };
    quad[3] = (TVertex){ -0.45f,  0.45f, 0.3f, 0.0f, 1.0f };

    uint32_t triAddr = (uint32_t)(uintptr_t)tri;
    uint32_t quadAddr = (uint32_t)(uintptr_t)quad;

    float angle = 0.0f;
    for (;;) {
        angle += 0.02f;

        // Clear the frame (dark blue) and the depth buffer (far).
        for (uint32_t i = 0; i < pitch_px * FBH; i++) fb[i] = 0xFF101828u;
        for (int i = 0; i < FBW * FBH; i++) zbuf[i] = 0xFFFF;

        float M[16];

        // --- shared render-target + viewport + depth state, then the triangle ---
        uint32_t *p = pb_begin();
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
        p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
        p = pb_push1(p, NV097_SET_SURFACE_FORMAT, (uint32_t)NV097_SET_SURFACE_FORMAT_ZETA_Z16 << 4);
        p = pb_push1(p, NV097_SET_SURFACE_PITCH,
                     ((uint32_t)(FBW * 2) << 16) | (pb_back_buffer_pitch() & 0xFFFF));
        p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)fb);
        p = pb_push1(p, NV097_SET_SURFACE_ZETA_OFFSET,  (uint32_t)(uintptr_t)zbuf);
        p = pb_push1(p, NV097_SET_DEPTH_TEST_ENABLE, 1);
        p = pb_push1(p, NV097_SET_DEPTH_FUNC, NV097_SET_DEPTH_FUNC_V_LESS);
        p = pb_push1(p, NV097_SET_DEPTH_MASK, 1);
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(320.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(240.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(320.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(-240.0f));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(ZMAX));
        p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));
        rotZ(M, angle);
        for (int m = 0; m < 16; m++)
            p = pb_push1(p, NV097_SET_COMPOSITE_MATRIX + m * 4, f2u(M[m]));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, triAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(CVertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_DIFFUSE * 4,
                     triAddr + (uint32_t)offsetof(CVertex, color));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, sizeof(CVertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_TEXCOORD * 4, 0); // no texcoords
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_TRIANGLES);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((3u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
        pb_end(p);
        while (pb_busy()) ;

        // --- the textured quad, spinning the other way, in front ---
        p = pb_begin();
        rotZ(M, -angle * 1.4f);
        for (int m = 0; m < 16; m++)
            p = pb_push1(p, NV097_SET_COMPOSITE_MATRIX + m * 4, f2u(M[m]));
        p = pb_push1(p, NV097_SET_TEXTURE_OFFSET,     (uint32_t)(uintptr_t)tex);
        p = pb_push1(p, NV097_SET_TEXTURE_FORMAT,     (0x12u << 8) | (0x02u << 4)); // LU_A8R8G8B8
        p = pb_push1(p, NV097_SET_TEXTURE_IMAGE_RECT, (4 << 16) | 4);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, quadAddr);
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(TVertex)));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4, 0); // white diffuse
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_TEXCOORD * 4,
                     quadAddr + (uint32_t)offsetof(TVertex, u));
        p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_TEXCOORD * 4,
                     VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 2, sizeof(TVertex)));
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);
        p = pb_push1(p, NV097_DRAW_ARRAYS, ((4u - 1u) << 24) | 0u);
        p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
        pb_end(p);
        while (pb_busy()) ;

        pb_show_debug_screen();     // present -> the live window blits this frame
        while (pb_busy()) ;
        Sleep(16);
    }
    return 0;
}
