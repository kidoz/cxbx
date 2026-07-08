// d3d_tex_swizzle -- documents the SWIZZLED-texture fidelity gap in the D3D8
// HLE. Real titles upload pre-swizzled (Morton-order) texel data into
// swizzled-format textures (X_D3DFMT_A8R8G8B8 = 0x06); real hardware reads
// them swizzled. The HLE currently maps swizzled formats to the same host
// linear format WITHOUT unswizzling (EmuXB2PC_D3DFormat), so the host samples
// the Morton-order bytes as if linear -> scrambled texels.
//
// Both behaviors are checked, with expectations encoding CURRENT reality:
//   swz.linear_read_*  expect=1  -- pixels match the linear (mis)interpretation
//   swz.unswizzled_*   expect=0  -- pixels match the CORRECT unswizzled value
// When unswizzling is implemented, both groups flip -> update expectations
// and the golden. (For the 2x2-block corner texels linear and correct agree;
// sample points below are chosen where they differ.)
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF202020;

struct VERTEX {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};
#define FVF_VERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

// Morton/swizzle offset for a square texture: x bits go to even positions,
// y bits to odd positions (the NV2A texture swizzle).
static int swz(int x, int y)
{
    int off = 0;
    for (int b = 0; b < 3; b++) { // 8x8 -> 3 bits each
        off |= ((x >> b) & 1) << (2 * b);
        off |= ((y >> b) & 1) << (2 * b + 1);
    }
    return off;
}

// Distinct, position-encoding texel color.
static D3DCOLOR texel_color(int x, int y)
{
    return 0xFF000000 | ((DWORD)(0x20 + x * 0x1C) << 16) |
           ((DWORD)(0x20 + y * 0x1C) << 8) | 0x55;
}

static DWORD read_pixel(void *pBits, INT pitch, int x, int y)
{
    return (*(DWORD *)((BYTE *)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static DWORD g_upload[64]; // what we wrote, in memory order

void __cdecl main()
{
    xt_begin("d3d_tex_swizzle");

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

    // SWIZZLED-format texture; upload the texels in Morton order, exactly as
    // a title with pre-swizzled assets does.
    D3DTexture *pTex = D3DDevice_CreateTexture2(8, 8, 1, 1, 0,
                                                D3DFMT_A8R8G8B8, // swizzled!
                                                D3DRTYPE_TEXTURE);
    xt_chk("d3d.tex_create_ok", 1, pTex != NULL);
    if (pTex == NULL)
        xt_end_and_exit();

    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            g_upload[swz(x, y)] = texel_color(x, y);

    D3DLOCKED_RECT tlr;
    tlr.pBits = NULL;
    D3DTexture_LockRect(pTex, 0, &tlr, NULL, 0);
    xt_chk("d3d.tex_lock_ok", 1, tlr.pBits != NULL);
    if (tlr.pBits != NULL) {
        // A swizzled texture is one contiguous Morton-ordered block; write it
        // through the row pointers the host lock reports.
        for (int row = 0; row < 8; row++)
            memcpy((BYTE *)tlr.pBits + row * tlr.Pitch, &g_upload[row * 8], 8 * 4);
    }

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetTexture(0, pTex);
    D3DDevice_SetVertexShader(FVF_VERTEX);

    // 64x64 quad at (64,64): 8x point-filtered magnification.
    static const VERTEX tris[6] = {
        {  64.0f,  64.0f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
        { 128.0f,  64.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
        {  64.0f, 128.0f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
        { 128.0f,  64.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
        { 128.0f, 128.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
        {  64.0f, 128.0f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, tris, sizeof(VERTEX));

    D3DSurface *pBB = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, pBB != NULL);
    if (pBB != NULL) {
        D3DLOCKED_RECT lr;
        lr.pBits = NULL;
        D3DSurface_LockRect(pBB, &lr, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
        if (lr.pBits != NULL) {
            // Texels where linear and unswizzled interpretations DIFFER.
            static const struct { int tx, ty; } samples[3] =
                { { 2, 1 }, { 5, 3 }, { 6, 6 } };
            for (int i = 0; i < 3; i++) {
                int tx = samples[i].tx, ty = samples[i].ty;
                // center of the 8x8-pixel block for texel (tx,ty)
                DWORD got = read_pixel(lr.pBits, lr.Pitch,
                                       64 + tx * 8 + 4, 64 + ty * 8 + 4);
                DWORD linear = g_upload[ty * 8 + tx] & 0xFFFFFF;
                DWORD correct = texel_color(tx, ty) & 0xFFFFFF;
                char name[64];
                _snprintf(name, sizeof(name) - 1, "swz.linear_read_%d%d", tx, ty);
                name[sizeof(name) - 1] = 0;
                xt_chk(name, 1, got == linear);
                _snprintf(name, sizeof(name) - 1, "swz.unswizzled_%d%d", tx, ty);
                name[sizeof(name) - 1] = 0;
                xt_chk(name, 0, got == correct);
            }
        }
    }

    xt_end_and_exit();
}
