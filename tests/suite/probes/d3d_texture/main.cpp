// d3d_texture -- pixel-exact verification of the D3D8 HLE texture path:
// CreateTexture2 -> Texture LockRect upload -> SetTexture -> textured draw ->
// backbuffer readback. This is the font/menu-texture path every title UI
// needs. Both draw routes are exercised with the texture bound:
// DrawVerticesUP (XYZRHW|DIFFUSE|TEX1) and the immediate-mode Begin/End
// bracket with SetVertexData2f(reg 9) texcoords (the NestopiaX text path).
//
// The texture is D3DFMT_LIN_A8R8G8B8 (linear): it converts 1:1 to the host
// A8R8G8B8 with no swizzle, so the upload must be pixel-exact. (Swizzled
// formats currently map to the same host format WITHOUT unswizzling -- a
// known HLE fidelity gap, deliberately not probed here.)
//
// Texture is 8x8, drawn onto 128x128 quads (16x point-filtered magnification
// -> clean color blocks). Left texture half is yellow, right half magenta;
// background clear is blue. Readback discipline: all rendering first, one
// LockRect readback at the end (see d3d_clear_present).
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF; // blue
static const D3DCOLOR COL_LEFT  = 0xFFFFFF00; // yellow (texel x 0..3)
static const D3DCOLOR COL_RIGHT = 0xFFFF00FF; // magenta (texel x 4..7)

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

void __cdecl main()
{
    xt_begin("d3d_texture");

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

    // Create + upload the 8x8 linear ARGB texture.
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
                row[x] = (x < 4) ? COL_LEFT : COL_RIGHT;
        }
    }

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetTexture(0, pTex);
    D3DDevice_SetVertexShader(FVF_VERTEX);

    // Quad 1 (DrawVerticesUP): 128x128 at (64,64), u/v 0..1, diffuse white
    // (default stage-0 MODULATE leaves the texture color unchanged).
    static const VERTEX tris[6] = {
        {  64.0f,  64.0f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
        { 192.0f,  64.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
        {  64.0f, 192.0f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
        { 192.0f,  64.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
        { 192.0f, 192.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
        {  64.0f, 192.0f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, tris, sizeof(VERTEX));

    // Quad 2 (immediate mode): 128x128 at (256,64), same texture.
    D3DDevice_Begin(D3DPT_QUADLIST);
    D3DDevice_SetVertexDataColor(D3DVSDE_DIFFUSE, 0xFFFFFFFF);
    D3DDevice_SetVertexData2f(9, 0.0f, 0.0f);
    D3DDevice_SetVertexData4f(D3DVSDE_VERTEX, 256.0f,  64.0f, 0.0f, 1.0f);
    D3DDevice_SetVertexData2f(9, 1.0f, 0.0f);
    D3DDevice_SetVertexData4f(D3DVSDE_VERTEX, 384.0f,  64.0f, 0.0f, 1.0f);
    D3DDevice_SetVertexData2f(9, 1.0f, 1.0f);
    D3DDevice_SetVertexData4f(D3DVSDE_VERTEX, 384.0f, 192.0f, 0.0f, 1.0f);
    D3DDevice_SetVertexData2f(9, 0.0f, 1.0f);
    D3DDevice_SetVertexData4f(D3DVSDE_VERTEX, 256.0f, 192.0f, 0.0f, 1.0f);
    D3DDevice_End();

    // Single readback at the end.
    D3DSurface *pBB = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, pBB != NULL);
    if (pBB != NULL) {
        D3DLOCKED_RECT lr;
        lr.pBits = NULL;
        D3DSurface_LockRect(pBB, &lr, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
        if (lr.pBits != NULL) {
            // quad 1: left texture half at x 64..128, right half at 128..192
            xt_chk_u32("d3d.up_px_left", COL_LEFT & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 96, 128));
            xt_chk_u32("d3d.up_px_right", COL_RIGHT & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 160, 128));
            // quad 2 (immediate mode): halves at 256..320 / 320..384
            xt_chk_u32("d3d.im_px_left", COL_LEFT & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 288, 128));
            xt_chk_u32("d3d.im_px_right", COL_RIGHT & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 352, 128));
            // untouched background
            xt_chk_u32("d3d.px_clear", COL_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 480, 360));
        }
    }

    xt_end_and_exit();
}
