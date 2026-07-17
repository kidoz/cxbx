// SPDX-License-Identifier: MIT
//
// nv2a_multitexture - verifies that the raw NV2A raster path preserves
// independent vertex-program coordinates for texture stages 0 and 1, samples
// both textures, and exposes t0/t1 to the register combiner.

#include "xtest.h"
#include <stddef.h>
#include <windows.h>
#include <hal/video.h>
#include <xboxkrnl/xboxkrnl.h>
#include <pbkit/pbkit.h>
#include <pbkit/nv_regs.h>

#define ATTR_POSITION 0
#define ATTR_TEXCOORD0 9
#define ATTR_TEXCOORD1 10
#define VTX_FMT(type, size, stride) \
    (((uint32_t)(stride) << 8) | ((uint32_t)(size) << 4) | (uint32_t)(type))

#define FBW 640
#define FBH 480
#define TW 2
#define TH 2
#define TEXTURE_STAGE_STRIDE 0x40u

typedef struct
{
    float x, y, z;
    float u0, v0;
    float u1, v1;
} Vertex;

// mov oPos, v0
// mov oT0, v9
// mov oT1, v10 (FINAL)
// Encoded with the NV2A instruction fields used by EmuVshDecoder.
static const uint32_t VP_PROGRAM[12] = {
    0x00000000u, 0x0020001Bu, 0x0836006Cu, 0x0000F800u,
    0x00000000u, 0x0020121Bu, 0x0836006Cu, 0x0000F848u,
    0x00000000u, 0x0020141Bu, 0x0836006Cu, 0x0000F851u,
};

static uint32_t f2u(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint32_t* allocate_texture(uint32_t top_left, uint32_t top_right,
                                  uint32_t bottom_left, uint32_t bottom_right)
{
    uint32_t* texture = (uint32_t*)MmAllocateContiguousMemoryEx(
        TW * TH * sizeof(uint32_t), 0, 0x3ffb000, 0,
        PAGE_READWRITE | PAGE_WRITECOMBINE);
    if(texture != NULL)
    {
        texture[0] = top_left;
        texture[1] = top_right;
        texture[2] = bottom_left;
        texture[3] = bottom_right;
    }
    return texture;
}

static uint32_t* bind_texture(uint32_t* push, uint32_t stage,
                              uint32_t texture_address)
{
    const uint32_t method_offset = stage * TEXTURE_STAGE_STRIDE;
    push = pb_push1(push, NV097_SET_TEXTURE_OFFSET + method_offset,
                    texture_address);
    push = pb_push1(push, NV097_SET_TEXTURE_FORMAT + method_offset,
                    (0x12u << 8) | (0x02u << 4));
    push = pb_push1(push, NV097_SET_TEXTURE_IMAGE_RECT + method_offset,
                    (TW << 16) | TH);
    push = pb_push1(push, NV097_SET_TEXTURE_ADDRESS + method_offset,
                    0x00030303u);
    push = pb_push1(push, NV097_SET_TEXTURE_CONTROL0 + method_offset,
                    0x4003ffc0u);
    push = pb_push1(push, NV097_SET_TEXTURE_FILTER + method_offset,
                    0x02022000u);
    return push;
}

int main(void)
{
    xt_begin("v1", "nv2a_multitexture");
    xt_note("NV2A VP oT0/oT1, stage-0/stage-1 textures, and t0*t1 combiner");

    XVideoSetMode(FBW, FBH, 32, REFRESH_DEFAULT);

    const int status = pb_init();
    xt_check_bool("nv2a_multitexture.pb_init", 1, status == 0);
    if(status != 0)
    {
        return xt_end();
    }

    uint32_t* back_buffer = (uint32_t*)pb_back_buffer();
    const uint32_t pitch_pixels = pb_back_buffer_pitch() / sizeof(uint32_t);
    const uint32_t back_buffer_height = pb_back_buffer_height();
    xt_check_bool("nv2a_multitexture.back_buffer", 1,
                  back_buffer != NULL && pitch_pixels != 0);
    if(back_buffer == NULL || pitch_pixels == 0)
    {
        pb_kill();
        return xt_end();
    }
    for(uint32_t index = 0; index < pitch_pixels * back_buffer_height; ++index)
    {
        back_buffer[index] = 0xFF000000u;
    }

    // Stage 0 samples top-left cyan. Stage 1 samples top-right yellow.
    // cyan * yellow = green. Reusing stage-0 coordinates for stage 1 instead
    // selects magenta, producing blue and making the coordinate bug visible.
    uint32_t* texture0 = allocate_texture(
        0xFF00FFFFu, 0xFFFF0000u, 0xFFFFFFFFu, 0xFF000000u);
    uint32_t* texture1 = allocate_texture(
        0xFFFF00FFu, 0xFFFFFF00u, 0xFFFFFFFFu, 0xFF000000u);
    xt_check_bool("nv2a_multitexture.texture0_alloc", 1, texture0 != NULL);
    xt_check_bool("nv2a_multitexture.texture1_alloc", 1, texture1 != NULL);
    if(texture0 == NULL || texture1 == NULL)
    {
        pb_kill();
        return xt_end();
    }

    Vertex* vertices = (Vertex*)MmAllocateContiguousMemoryEx(
        4 * sizeof(Vertex), 0, 0x3ffb000, 0,
        PAGE_READWRITE | PAGE_WRITECOMBINE);
    xt_check_bool("nv2a_multitexture.vbuf_alloc", 1, vertices != NULL);
    if(vertices == NULL)
    {
        pb_kill();
        return xt_end();
    }

    const Vertex quad[4] = {
        {64.0f, 64.0f, 0.0f, 0.25f, 0.25f, 0.75f, 0.25f},
        {320.0f, 64.0f, 0.0f, 0.25f, 0.25f, 0.75f, 0.25f},
        {320.0f, 320.0f, 0.0f, 0.25f, 0.25f, 0.75f, 0.25f},
        {64.0f, 320.0f, 0.0f, 0.25f, 0.25f, 0.75f, 0.25f},
    };
    memcpy(vertices, quad, sizeof(quad));

    uint32_t* push = pb_begin();
    push = pb_push1(push, NV097_SET_TRANSFORM_PROGRAM_LOAD, 0);
    for(uint32_t index = 0; index < sizeof(VP_PROGRAM) / sizeof(VP_PROGRAM[0]);
        ++index)
    {
        push = pb_push1(push, NV097_SET_TRANSFORM_PROGRAM, VP_PROGRAM[index]);
    }
    push = pb_push1(push, NV097_SET_TRANSFORM_PROGRAM_START, 0);
    push = pb_push1(push, NV097_SET_TRANSFORM_EXECUTION_MODE,
                    NV097_SET_TRANSFORM_EXECUTION_MODE_MODE_PROGRAM);
    pb_end(push);

    for(int repetition = 0; repetition < 4; ++repetition)
    {
        push = pb_begin();
        push = pb_push1(push, NV097_SET_SURFACE_CLIP_HORIZONTAL,
                        ((uint32_t)FBW << 16));
        push = pb_push1(push, NV097_SET_SURFACE_CLIP_VERTICAL,
                        ((uint32_t)FBH << 16));
        push = pb_push1(push, NV097_SET_SURFACE_PITCH,
                        pb_back_buffer_pitch() & 0xFFFFu);
        push = pb_push1(push, NV097_SET_SURFACE_COLOR_OFFSET,
                        (uint32_t)(uintptr_t)back_buffer);

        push = pb_push1(push, NV097_SET_VIEWPORT_OFFSET + 0, f2u(0.0f));
        push = pb_push1(push, NV097_SET_VIEWPORT_OFFSET + 4, f2u(0.0f));
        push = pb_push1(push, NV097_SET_VIEWPORT_OFFSET + 8, f2u(0.0f));
        push = pb_push1(push, NV097_SET_VIEWPORT_OFFSET + 12, f2u(0.0f));
        push = pb_push1(push, NV097_SET_VIEWPORT_SCALE + 0, f2u(1.0f));
        push = pb_push1(push, NV097_SET_VIEWPORT_SCALE + 4, f2u(1.0f));
        push = pb_push1(push, NV097_SET_VIEWPORT_SCALE + 8, f2u(1.0f));
        push = pb_push1(push, NV097_SET_VIEWPORT_SCALE + 12, f2u(1.0f));

        push = bind_texture(push, 0, (uint32_t)(uintptr_t)texture0);
        push = bind_texture(push, 1, (uint32_t)(uintptr_t)texture1);
        push = pb_push1(push, NV097_SET_SHADER_STAGE_PROGRAM,
                        NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_2D_PROJECTIVE |
                        (NV097_SET_SHADER_STAGE_PROGRAM_STAGE1_2D_PROJECTIVE << 5));

        // One general combiner: r0.rgb/a = t0 * t1, followed by r0 passthrough.
        push = pb_push1(push, NV097_SET_COMBINER_CONTROL, 1u);
        push = pb_push1(push, NV097_SET_COMBINER_COLOR_ICW, 0x08090000u);
        push = pb_push1(push, NV097_SET_COMBINER_ALPHA_ICW, 0x18190000u);
        push = pb_push1(push, NV097_SET_COMBINER_COLOR_OCW, 0x00000C00u);
        push = pb_push1(push, NV097_SET_COMBINER_ALPHA_OCW, 0x00000C00u);
        push = pb_push1(push, NV097_SET_COMBINER_SPECULAR_FOG_CW0, 0x0000000Cu);
        push = pb_push1(push, NV097_SET_COMBINER_SPECULAR_FOG_CW1, 0x00001C80u);

        const uint32_t vertex_address = (uint32_t)(uintptr_t)vertices;
        push = pb_push1(push,
                        NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_POSITION * 4,
                        vertex_address);
        push = pb_push1(push,
                        NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_POSITION * 4,
                        VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 3,
                                sizeof(Vertex)));
        push = pb_push1(push,
                        NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_TEXCOORD0 * 4,
                        vertex_address + (uint32_t)offsetof(Vertex, u0));
        push = pb_push1(push,
                        NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_TEXCOORD0 * 4,
                        VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 2,
                                sizeof(Vertex)));
        push = pb_push1(push,
                        NV097_SET_VERTEX_DATA_ARRAY_OFFSET + ATTR_TEXCOORD1 * 4,
                        vertex_address + (uint32_t)offsetof(Vertex, u1));
        push = pb_push1(push,
                        NV097_SET_VERTEX_DATA_ARRAY_FORMAT + ATTR_TEXCOORD1 * 4,
                        VTX_FMT(NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F, 2,
                                sizeof(Vertex)));

        push = pb_push1(push, NV097_SET_BEGIN_END,
                        NV097_SET_BEGIN_END_OP_QUADS);
        push = pb_push1(push, NV097_DRAW_ARRAYS, ((4u - 1u) << 24));
        push = pb_push1(push, NV097_SET_BEGIN_END,
                        NV097_SET_BEGIN_END_OP_END);
        pb_end(push);
        while(pb_busy())
        {
        }
    }

    const uint32_t inside =
        back_buffer[160u * pitch_pixels + 160u] | 0xFF000000u;
    const uint32_t outside =
        back_buffer[400u * pitch_pixels + 480u] | 0xFF000000u;
    xt_ev("nv2a_multitexture.inside=0x%08lX outside=0x%08lX",
          (unsigned long)inside, (unsigned long)outside);
    xt_check_u32("nv2a_multitexture.stage0_times_stage1",
                 0xFF00FF00u, inside);
    xt_check_u32("nv2a_multitexture.outside_clear",
                 0xFF000000u, outside);

    pb_kill();
    return xt_end();
}
