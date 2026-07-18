// d3d_pixel_shader_fallbacks -- bounded XDK 4627 pixel checks for three
// register-combiner forms captured from Turok's fallback path:
// 1. stage-2 BUMPENVMAP with a t1 bump source;
// 2. a sum that requires r1 scratch after final-combiner simplification; and
// 3. eight overwritten combiners whose final r0 is the only observable result.
// Each quad has a stable interior pixel and the clear pixel remains a control.
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF;
static const D3DCOLOR COL_BUMP_SOURCE = 0xFFFF0000;
static const D3DCOLOR COL_ENV_LEFT = 0xFFFF0000;
static const D3DCOLOR COL_ENV_RIGHT = 0xFF00FF00;
static const D3DCOLOR COL_SUM_BASE = 0xFF800000;
static const D3DCOLOR COL_SUM_ADD = 0xFF004000;
static const D3DCOLOR COL_BUDGET = 0xFF204080;

static const DWORD EXPECT_BUMP = 0x00FF00;
static const DWORD EXPECT_SUM = 0x404000;
static const DWORD EXPECT_BUDGET = 0x00DFBF7F;

struct VERTEX {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u0, v0;
    float u1, v1;
    float u2, v2;
};

#define FVF_VERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX3)

static DWORD read_pixel(void *pBits, INT pitch, int x, int y)
{
    return (*(DWORD *)((BYTE *)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static DWORD float_bits(float value)
{
    union {
        float f;
        DWORD d;
    } bits;
    bits.f = value;
    return bits.d;
}

static D3DTexture *create_solid_texture(D3DCOLOR color)
{
    D3DTexture *texture = D3DDevice_CreateTexture2(4, 4, 1, 1, 0,
                                                    D3DFMT_LIN_A8R8G8B8,
                                                    D3DRTYPE_TEXTURE);
    if(texture == NULL) {
        return NULL;
    }

    D3DLOCKED_RECT lock;
    lock.pBits = NULL;
    D3DTexture_LockRect(texture, 0, &lock, NULL, 0);
    if(lock.pBits != NULL) {
        for(int y = 0; y < 4; ++y) {
            DWORD *row = (DWORD *)((BYTE *)lock.pBits + y * lock.Pitch);
            for(int x = 0; x < 4; ++x) {
                row[x] = color;
            }
        }
    }
    return texture;
}

static D3DTexture *create_environment_texture(void)
{
    D3DTexture *texture = D3DDevice_CreateTexture2(4, 4, 1, 1, 0,
                                                    D3DFMT_LIN_A8R8G8B8,
                                                    D3DRTYPE_TEXTURE);
    if(texture == NULL) {
        return NULL;
    }

    D3DLOCKED_RECT lock;
    lock.pBits = NULL;
    D3DTexture_LockRect(texture, 0, &lock, NULL, 0);
    if(lock.pBits != NULL) {
        for(int y = 0; y < 4; ++y) {
            DWORD *row = (DWORD *)((BYTE *)lock.pBits + y * lock.Pitch);
            for(int x = 0; x < 4; ++x) {
                row[x] = x < 2 ? COL_ENV_LEFT : COL_ENV_RIGHT;
            }
        }
    }
    return texture;
}

static void draw_quad(float x0, float y0)
{
    const VERTEX tris[6] = {
        { x0,          y0,          0.0f, 1.0f, 0xFFFFFFFF,
          0.25f, 0.50f, 0.25f, 0.50f, 0.25f, 0.50f },
        { x0 + 128.0f, y0,          0.0f, 1.0f, 0xFFFFFFFF,
          0.25f, 0.50f, 0.25f, 0.50f, 0.25f, 0.50f },
        { x0,          y0 + 128.0f, 0.0f, 1.0f, 0xFFFFFFFF,
          0.25f, 0.50f, 0.25f, 0.50f, 0.25f, 0.50f },
        { x0 + 128.0f, y0,          0.0f, 1.0f, 0xFFFFFFFF,
          0.25f, 0.50f, 0.25f, 0.50f, 0.25f, 0.50f },
        { x0 + 128.0f, y0 + 128.0f, 0.0f, 1.0f, 0xFFFFFFFF,
          0.25f, 0.50f, 0.25f, 0.50f, 0.25f, 0.50f },
        { x0,          y0 + 128.0f, 0.0f, 1.0f, 0xFFFFFFFF,
          0.25f, 0.50f, 0.25f, 0.50f, 0.25f, 0.50f },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, tris, sizeof(VERTEX));
}

static void build_bump_shader(D3DPIXELSHADERDEF *shader)
{
    ZeroMemory(shader, sizeof(*shader));
    shader->PSCombinerCount = PS_COMBINERCOUNT(1, 0);
    shader->PSTextureModes = PS_TEXTUREMODES(PS_TEXTUREMODES_PROJECT2D,
        PS_TEXTUREMODES_PROJECT2D, PS_TEXTUREMODES_BUMPENVMAP,
        PS_TEXTUREMODES_NONE);
    shader->PSInputTexture = PS_INPUTTEXTURE(0, 0, 1, 0);
    shader->PSRGBInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T2, PS_REGISTER_ONE, PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    shader->PSRGBOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    shader->PSAlphaInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T2 | PS_CHANNEL_ALPHA, PS_REGISTER_ONE | PS_CHANNEL_ALPHA,
        PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    shader->PSAlphaOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
}

static void build_sum_shader(D3DPIXELSHADERDEF *shader)
{
    ZeroMemory(shader, sizeof(*shader));
    shader->PSCombinerCount = PS_COMBINERCOUNT(2, 0);
    shader->PSTextureModes = PS_TEXTUREMODES(PS_TEXTUREMODES_PROJECT2D,
        PS_TEXTUREMODES_PROJECT2D, PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE);
    shader->PSRGBInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T0, PS_REGISTER_ONE, PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    shader->PSRGBOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    shader->PSAlphaInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T0 | PS_CHANNEL_ALPHA, PS_REGISTER_ONE | PS_CHANNEL_ALPHA,
        PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    shader->PSAlphaOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    shader->PSRGBInputs[1] = PS_COMBINERINPUTS(
        PS_REGISTER_R0, PS_REGISTER_T0, PS_REGISTER_T1, PS_REGISTER_ONE);
    shader->PSRGBOutputs[1] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    shader->PSAlphaInputs[1] = PS_COMBINERINPUTS(
        PS_REGISTER_T0 | PS_CHANNEL_ALPHA, PS_REGISTER_ONE | PS_CHANNEL_ALPHA,
        PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    shader->PSAlphaOutputs[1] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    shader->PSFinalCombinerInputsABCD = PS_COMBINERINPUTS(
        PS_REGISTER_ONE, PS_REGISTER_R0, PS_REGISTER_R1, PS_REGISTER_ZERO);
    shader->PSFinalCombinerInputsEFG = PS_COMBINERINPUTS(
        PS_REGISTER_ZERO, PS_REGISTER_ZERO, PS_REGISTER_R0 | PS_CHANNEL_ALPHA,
        PS_REGISTER_ZERO);
}

static void build_instruction_budget_shader(D3DPIXELSHADERDEF *shader)
{
    ZeroMemory(shader, sizeof(*shader));
    shader->PSCombinerCount = PS_COMBINERCOUNT(8, 0);
    shader->PSTextureModes = PS_TEXTUREMODES(PS_TEXTUREMODES_PROJECT2D,
        PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE, PS_TEXTUREMODES_NONE);
    for(int stage = 0; stage < 8; ++stage) {
        shader->PSRGBInputs[stage] = PS_COMBINERINPUTS(
            PS_REGISTER_ZERO, PS_REGISTER_ZERO, PS_REGISTER_ONE,
            PS_REGISTER_T0 | PS_INPUTMAPPING_UNSIGNED_INVERT);
        shader->PSRGBOutputs[stage] = PS_COMBINEROUTPUTS(
            PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
        shader->PSAlphaInputs[stage] = PS_COMBINERINPUTS(
            PS_REGISTER_ZERO, PS_REGISTER_ZERO, PS_REGISTER_ONE | PS_CHANNEL_ALPHA,
            PS_REGISTER_T0 | PS_CHANNEL_ALPHA | PS_INPUTMAPPING_UNSIGNED_INVERT);
        shader->PSAlphaOutputs[stage] = PS_COMBINEROUTPUTS(
            PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    }
}

void __cdecl main()
{
    xt_begin("d3d_pixel_shader_fallbacks");

    xt_chk("d3d.create_pixel_shader_hle", 1,
           xt_is_hle_patched((const void *)D3DDevice_CreatePixelShader));
    xt_chk("d3d.set_pixel_shader_hle", 1,
           xt_is_hle_patched((const void *)D3DDevice_SetPixelShader));
    xt_chk("d3d.bump_env_hle", 1,
           xt_is_hle_patched((const void *)D3DDevice_SetTextureState_BumpEnv));
    if(!xt_is_hle_patched((const void *)D3DDevice_CreatePixelShader) ||
       !xt_is_hle_patched((const void *)D3DDevice_SetPixelShader) ||
       !xt_is_hle_patched((const void *)D3DDevice_SetTextureState_BumpEnv)) {
        xt_end_and_exit();
    }

    LPDIRECT3D8 d3d = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, d3d != NULL);

    D3DPRESENT_PARAMETERS present;
    ZeroMemory(&present, sizeof(present));
    present.BackBufferWidth = 640;
    present.BackBufferHeight = 480;
    present.BackBufferFormat = D3DFMT_X8R8G8B8;
    present.BackBufferCount = 1;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice *device = NULL;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &present, &device);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && device != NULL);
    if(FAILED(hr) || device == NULL) {
        xt_end_and_exit();
    }

    D3DTexture *bumpSource = create_solid_texture(COL_BUMP_SOURCE);
    D3DTexture *environment = create_environment_texture();
    D3DTexture *sumBase = create_solid_texture(COL_SUM_BASE);
    D3DTexture *sumAdd = create_solid_texture(COL_SUM_ADD);
    D3DTexture *budget = create_solid_texture(COL_BUDGET);
    xt_chk("d3d.textures_ok", 1, bumpSource != NULL && environment != NULL &&
           sumBase != NULL && sumAdd != NULL && budget != NULL);
    if(bumpSource == NULL || environment == NULL || sumBase == NULL ||
       sumAdd == NULL || budget == NULL) {
        xt_end_and_exit();
    }

    D3DPIXELSHADERDEF bumpShader, sumShader, budgetShader;
    build_bump_shader(&bumpShader);
    build_sum_shader(&sumShader);
    build_instruction_budget_shader(&budgetShader);

    DWORD bumpHandle = 0, sumHandle = 0, budgetHandle = 0;
    D3DDevice_CreatePixelShader(&bumpShader, &bumpHandle);
    D3DDevice_CreatePixelShader(&sumShader, &sumHandle);
    D3DDevice_CreatePixelShader(&budgetShader, &budgetHandle);
    xt_chk("ps.handles_ok", 1, bumpHandle != 0 && sumHandle != 0 &&
           budgetHandle != 0);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetVertexShader(FVF_VERTEX);

    D3DDevice_SetTexture(1, bumpSource);
    D3DDevice_SetTexture(2, environment);
    D3DDevice_SetTextureState_BumpEnv(2, D3DTSS_BUMPENVMAT00, float_bits(0.5f));
    D3DDevice_SetTextureState_BumpEnv(2, D3DTSS_BUMPENVMAT01, float_bits(0.0f));
    D3DDevice_SetTextureState_BumpEnv(2, D3DTSS_BUMPENVMAT10, float_bits(0.0f));
    D3DDevice_SetTextureState_BumpEnv(2, D3DTSS_BUMPENVMAT11, float_bits(0.0f));
    D3DDevice_SetPixelShader(bumpHandle);
    draw_quad(64.0f, 64.0f);

    D3DDevice_SetTexture(0, sumBase);
    D3DDevice_SetTexture(1, sumAdd);
    D3DDevice_SetPixelShader(sumHandle);
    draw_quad(256.0f, 64.0f);

    D3DDevice_SetTexture(0, budget);
    D3DDevice_SetPixelShader(budgetHandle);
    draw_quad(448.0f, 64.0f);
    D3DDevice_SetPixelShader(0);

    D3DSurface *backBuffer = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, backBuffer != NULL);
    if(backBuffer != NULL) {
        D3DLOCKED_RECT lock;
        lock.pBits = NULL;
        D3DSurface_LockRect(backBuffer, &lock, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lock.pBits != NULL);
        if(lock.pBits != NULL) {
            xt_chk_u32("ps.bump_stage2", EXPECT_BUMP,
                       read_pixel(lock.pBits, lock.Pitch, 128, 128));
            xt_chk_u32("ps.sum_shape", EXPECT_SUM,
                       read_pixel(lock.pBits, lock.Pitch, 320, 128));
            xt_chk_u32("ps.instruction_budget", EXPECT_BUDGET,
                       read_pixel(lock.pBits, lock.Pitch, 512, 128));
            xt_chk_u32("ps.clear", COL_CLEAR & 0x00FFFFFF,
                       read_pixel(lock.pBits, lock.Pitch, 320, 360));
        }
    }

    xt_end_and_exit();
}
