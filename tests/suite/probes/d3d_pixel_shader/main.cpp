// d3d_pixel_shader -- pixel-exact verification of Xbox register-combiner
// pixel shaders through the HLE (the ps.1.1 translator added 2026-07-14).
// Two one-combiner shaders over a solid texture and known diffuse:
//
//   modulate: r0 = t0 * v0          -- the classic textured-modulate; the
//             fixed-function fallback also computes this, so it pins the
//             baseline semantics whichever path serves it.
//   invert:   r0 = (1 - t0) * v0    -- the fixed-function approximation
//             CANNOT express this (it modulates the un-inverted texture);
//             only a real translation produces the correct pixels. This is
//             the discriminating check.
//
// Colors are chosen so every product/complement is exact in 8 bits.
// Readback discipline: all rendering first, one LockRect readback at the
// end (the HLE Surface LockRect stays locked until the next lock).
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF; // blue
static const D3DCOLOR COL_TEX   = 0xFFFF4000; // texture: r=255 g=64 b=0
static const D3DCOLOR COL_WHITE = 0xFFFFFFFF;

// (1 - t) with t = COL_TEX: r=0x00 g=0xBF b=0xFF
static const DWORD EXPECT_MODULATE = 0xFF4000;
static const DWORD EXPECT_INVERT   = 0x00BFFF;

struct VERTEX {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};
#define FVF_VERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static DWORD read_pixel(void *pBits, INT pitch, int x, int y)
{
    return (*(DWORD *)((BYTE *)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static void draw_quad(float x0, float y0)
{
    const VERTEX tris[6] = {
        { x0,          y0,          0.0f, 1.0f, COL_WHITE, 0.0f, 0.0f },
        { x0 + 128.0f, y0,          0.0f, 1.0f, COL_WHITE, 1.0f, 0.0f },
        { x0,          y0 + 128.0f, 0.0f, 1.0f, COL_WHITE, 0.0f, 1.0f },
        { x0 + 128.0f, y0,          0.0f, 1.0f, COL_WHITE, 1.0f, 0.0f },
        { x0 + 128.0f, y0 + 128.0f, 0.0f, 1.0f, COL_WHITE, 1.0f, 1.0f },
        { x0,          y0 + 128.0f, 0.0f, 1.0f, COL_WHITE, 0.0f, 1.0f },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, tris, sizeof(VERTEX));
}

// One-combiner shader: r0.rgb = map(t0) * v0, r0.a = t0.a * v0.a.
static void build_shader(D3DPIXELSHADERDEF *psDef, DWORD textureMapping)
{
    memset(psDef, 0, sizeof(*psDef));
    psDef->PSCombinerCount = PS_COMBINERCOUNT(1, 0);
    psDef->PSTextureModes = PS_TEXTUREMODES_PROJECT2D; // stage 0
    psDef->PSRGBInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T0 | textureMapping | PS_CHANNEL_RGB,
        PS_REGISTER_V0 | PS_INPUTMAPPING_UNSIGNED_IDENTITY | PS_CHANNEL_RGB,
        PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    psDef->PSRGBOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    psDef->PSAlphaInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T0 | PS_INPUTMAPPING_UNSIGNED_IDENTITY | PS_CHANNEL_ALPHA,
        PS_REGISTER_V0 | PS_INPUTMAPPING_UNSIGNED_IDENTITY | PS_CHANNEL_ALPHA,
        PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    psDef->PSAlphaOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
}

void __cdecl main()
{
    xt_begin("d3d_pixel_shader");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.BackBufferWidth  = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount  = 1;
    d3dpp.SwapEffect       = D3DSWAPEFFECT_DISCARD;

    D3DDevice *pDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &d3dpp, &pDevice);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && pDevice != NULL);
    if (FAILED(hr) || pDevice == NULL)
        xt_end_and_exit();

    // Solid-color linear texture (converts 1:1 to host A8R8G8B8).
    D3DTexture *pTex = D3DDevice_CreateTexture2(8, 8, 1, 1, 0,
                                                D3DFMT_LIN_A8R8G8B8,
                                                D3DRTYPE_TEXTURE);
    xt_chk("d3d.tex_create_ok", 1, pTex != NULL);
    if (pTex == NULL)
        xt_end_and_exit();

    D3DLOCKED_RECT tlr;
    tlr.pBits = NULL;
    D3DTexture_LockRect(pTex, 0, &tlr, NULL, 0);
    xt_chk("d3d.tex_lock_ok", 1, tlr.pBits != NULL);
    if (tlr.pBits != NULL) {
        for (int y = 0; y < 8; y++) {
            DWORD *row = (DWORD *)((BYTE *)tlr.pBits + y * tlr.Pitch);
            for (int x = 0; x < 8; x++)
                row[x] = COL_TEX;
        }
    }

    D3DPIXELSHADERDEF psModulate, psInvert;
    build_shader(&psModulate, PS_INPUTMAPPING_UNSIGNED_IDENTITY);
    build_shader(&psInvert, PS_INPUTMAPPING_UNSIGNED_INVERT);

    // 5849 C API: CreatePixelShader returns void; the handle is the signal.
    DWORD hModulate = 0, hInvert = 0;
    D3DDevice_CreatePixelShader(&psModulate, &hModulate);
    D3DDevice_CreatePixelShader(&psInvert, &hInvert);
    xt_chk("ps.create_modulate_ok", 1, hModulate != 0);
    xt_chk("ps.create_invert_ok", 1, hInvert != 0);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetTexture(0, pTex);
    D3DDevice_SetVertexShader(FVF_VERTEX);

    // Quad 1 at (64,64): modulate shader -> texture color (diffuse white).
    D3DDevice_SetPixelShader(hModulate);
    draw_quad(64.0f, 64.0f);

    // Quad 2 at (256,64): invert shader -> complemented texture color.
    D3DDevice_SetPixelShader(hInvert);
    draw_quad(256.0f, 64.0f);

    D3DDevice_SetPixelShader(0);

    // Single readback at the end.
    D3DSurface *pBB = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, pBB != NULL);
    if (pBB != NULL) {
        D3DLOCKED_RECT lr;
        lr.pBits = NULL;
        D3DSurface_LockRect(pBB, &lr, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
        if (lr.pBits != NULL) {
            xt_chk_u32("ps.px_modulate", EXPECT_MODULATE,
                       read_pixel(lr.pBits, lr.Pitch, 128, 128));
            xt_chk_u32("ps.px_invert", EXPECT_INVERT,
                       read_pixel(lr.pBits, lr.Pitch, 320, 128));
            xt_chk_u32("ps.px_clear", COL_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 480, 360));
        }
    }

    xt_end_and_exit();
}
