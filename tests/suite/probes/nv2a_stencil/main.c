// SPDX-License-Identifier: MIT
//
// nv2a_stencil - NV2A software-rasterizer stencil buffer. The classic mask test:
//   pass 1 draws a centre quad with stencil ALWAYS + ZPASS=REPLACE(ref=1),
//           stamping stencil=1 into the centre of the Z24S8 zeta buffer;
//   pass 2 draws a full-screen blue quad with stencil EQUAL(ref=1), which only
//           survives where the stencil was set.
// A conforming target shows blue only inside the centre region; everywhere else
// the full-screen quad is masked away and stays black. A target that ignores
// stencil paints the whole screen blue.
//
// SCOPE: stencil test + REPLACE op in the Z24S8 buffer. Requires the raster path
// (Cxbx: CXBX_NV2A_RASTER=1).

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

static uint32_t *push_common(uint32_t *p, uint32_t *bb, uint32_t *zbuf)
{
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, ((uint32_t)FBW << 16));
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL,   ((uint32_t)FBH << 16));
    p = pb_push1(p, NV097_SET_SURFACE_FORMAT, (uint32_t)NV097_SET_SURFACE_FORMAT_ZETA_Z24S8 << 4);
    p = pb_push1(p, NV097_SET_SURFACE_PITCH,
                 ((uint32_t)(FBW * 4) << 16) | (pb_back_buffer_pitch() & 0xFFFF));
    p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, (uint32_t)(uintptr_t)bb);
    p = pb_push1(p, NV097_SET_SURFACE_ZETA_OFFSET,  (uint32_t)(uintptr_t)zbuf);
    p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 0,  f2u(0.0f));
    p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 4,  f2u(0.0f));
    p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 8,  f2u(0.0f));
    p = pb_push1(p, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
    p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 0,   f2u(1.0f));
    p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 4,   f2u(1.0f));
    p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 8,   f2u(1.0f));
    p = pb_push1(p, NV097_SET_VIEWPORT_SCALE + 12,  f2u(1.0f));
    p = pb_push1(p, NV097_SET_STENCIL_TEST_ENABLE, 1);
    p = pb_push1(p, NV097_SET_STENCIL_FUNC_MASK, 0xFF);
    p = pb_push1(p, NV097_SET_STENCIL_MASK, 0xFF);
    return p;
}

static uint32_t *push_quad(uint32_t *p, uint32_t addr)
{
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4, addr);
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                 VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3, sizeof(Vertex)));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_DIFFUSE * 4,
                 addr + (uint32_t)offsetof(Vertex, color));
    p = pb_push1(p, NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_DIFFUSE * 4,
                 VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D, 4, sizeof(Vertex)));
    p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_QUADS);
    p = pb_push1(p, NV097_DRAW_ARRAYS, ((4u - 1u) << 24) | 0u);
    p = pb_push1(p, NV097_SET_BEGIN_END, NV097_SET_BEGIN_END_OP_END);
    return p;
}

int main(void)
{
    xt_begin("v1", "nv2a_stencil");
    xt_note("NV2A rasterizer stencil buffer (mask test, Z24S8)");
    xt_note("requires the raster path (Cxbx: CXBX_NV2A_RASTER=1)");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);
    int status = pb_init();
    xt_check_bool("nv2a_stencil.pb_init", 1, status == 0);
    if (status != 0) return xt_end();

    uint32_t *bb = (uint32_t *)pb_back_buffer();
    uint32_t pitch_px = pb_back_buffer_pitch() / 4;
    xt_check_bool("nv2a_stencil.back_buffer", 1, bb != NULL && pitch_px != 0);
    if (bb == NULL || pitch_px == 0) { pb_kill(); return xt_end(); }

    uint32_t *zbuf = (uint32_t *)MmAllocateContiguousMemoryEx(
        FBW * FBH * 4, 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_stencil.zbuf_alloc", 1, zbuf != NULL);
    if (zbuf == NULL) { pb_kill(); return xt_end(); }

    // Centre quad (pass 1, stamps stencil) and full-screen quad (pass 2, masked).
    Vertex *center = (Vertex *)MmAllocateContiguousMemoryEx(
        4 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    Vertex *full = (Vertex *)MmAllocateContiguousMemoryEx(
        4 * sizeof(Vertex), 0, 0x3ffb000, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_stencil.vbuf_alloc", 1, center != NULL && full != NULL);
    if (center == NULL || full == NULL) { pb_kill(); return xt_end(); }
    const uint32_t RED = 0xFFFF0000u, BLUE = 0xFF0000FFu;
    center[0] = (Vertex){ 240.0f, 180.0f, 0.0f, RED };
    center[1] = (Vertex){ 400.0f, 180.0f, 0.0f, RED };
    center[2] = (Vertex){ 400.0f, 300.0f, 0.0f, RED };
    center[3] = (Vertex){ 240.0f, 300.0f, 0.0f, RED };
    full[0] = (Vertex){   0.0f,   0.0f, 0.0f, BLUE };
    full[1] = (Vertex){ 640.0f,   0.0f, 0.0f, BLUE };
    full[2] = (Vertex){ 640.0f, 480.0f, 0.0f, BLUE };
    full[3] = (Vertex){   0.0f, 480.0f, 0.0f, BLUE };

    for (int rep = 0; rep < 4; rep++) {
        for (uint32_t i = 0; i < pitch_px * FBH; i++) bb[i] = 0xFF000000u; // black
        for (int i = 0; i < FBW * FBH; i++) zbuf[i] = 0;                    // stencil 0

        // Pass 1: stencil ALWAYS, ZPASS=REPLACE(ref=1) -> stamp 1 in the centre.
        uint32_t *p = pb_begin();
        p = push_common(p, bb, zbuf);
        p = pb_push1(p, NV097_SET_STENCIL_FUNC, NV097_SET_DEPTH_FUNC_V_ALWAYS);
        p = pb_push1(p, NV097_SET_STENCIL_FUNC_REF, 1);
        p = pb_push1(p, NV097_SET_STENCIL_OP_FAIL,  NV097_SET_STENCIL_OP_V_KEEP);
        p = pb_push1(p, NV097_SET_STENCIL_OP_ZFAIL, NV097_SET_STENCIL_OP_V_KEEP);
        p = pb_push1(p, NV097_SET_STENCIL_OP_ZPASS, NV097_SET_STENCIL_OP_V_REPLACE);
        p = push_quad(p, (uint32_t)(uintptr_t)center);
        pb_end(p);
        while (pb_busy()) ;

        // Pass 2: stencil EQUAL(ref=1), keep -> full-screen blue only where set.
        p = pb_begin();
        p = push_common(p, bb, zbuf);
        p = pb_push1(p, NV097_SET_STENCIL_FUNC, NV097_SET_DEPTH_FUNC_V_EQUAL);
        p = pb_push1(p, NV097_SET_STENCIL_FUNC_REF, 1);
        p = pb_push1(p, NV097_SET_STENCIL_OP_FAIL,  NV097_SET_STENCIL_OP_V_KEEP);
        p = pb_push1(p, NV097_SET_STENCIL_OP_ZFAIL, NV097_SET_STENCIL_OP_V_KEEP);
        p = pb_push1(p, NV097_SET_STENCIL_OP_ZPASS, NV097_SET_STENCIL_OP_V_KEEP);
        p = push_quad(p, (uint32_t)(uintptr_t)full);
        pb_end(p);
        while (pb_busy()) ;
    }

#define PX(x, y) (bb[(uint32_t)(y) * pitch_px + (uint32_t)(x)] | 0xFF000000u)
    uint32_t inside  = PX(320, 240);   // stencil==1 -> pass 2 blue survives
    uint32_t outside = PX(560, 240);   // stencil==0 -> pass 2 masked, stays black
    xt_ev("nv2a_stencil.inside=0x%08lX outside=0x%08lX",
          (unsigned long)inside, (unsigned long)outside);

    xt_check("nv2a_stencil.inside_blue",
             (inside & 0xFF) > 200 && ((inside >> 16) & 0xFF) < 40,
             "centre 0x%08lX should be blue where stencil was set", (unsigned long)inside);
    xt_check_u32("nv2a_stencil.outside_masked", 0xFF000000u, outside);

    pb_kill();
    return xt_end();
}
