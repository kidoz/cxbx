// d3d_depth -- pixel-exact verification of the depth-test path: a bound
// depth-stencil surface, depth clear, and ZEnable/ZWriteEnable/ZFUNC
// resolution between three overlapping quads at different depths.
//
// Layout drawn onto a blue clear (all pretransformed XYZRHW, depth tested):
//   red   quad x 0..256   at z = 0.50  (drawn first)
//   green quad x 128..384 at z = 0.25  (closer -- wins the 128..256 overlap)
//   white quad x 0..384   at z = 0.75  (farther -- rejected everywhere)
//
// Expected final state:
//   x 0..128     red    (white rejected behind red)
//   x 128..384   green  (green beat red, white rejected behind green)
//   x 384..      blue   (untouched clear)
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF; // blue
static const D3DCOLOR COL_RED = 0xFFFF0000;
static const D3DCOLOR COL_GREEN = 0xFF00FF00;
static const D3DCOLOR COL_WHITE = 0xFFFFFFFF;

struct VERTEX
{
    float x, y, z, rhw;
    D3DCOLOR color;
};
#define FVF_VERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static DWORD read_pixel(void* pBits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static void draw_quad(float x0, float x1, float z, D3DCOLOR color)
{
    VERTEX quad[6] = {
        { x0, 0.0f, z, 1.0f, color },
        { x1, 0.0f, z, 1.0f, color },
        { x0, 480.0f, z, 1.0f, color },
        { x1, 0.0f, z, 1.0f, color },
        { x1, 480.0f, z, 1.0f, color },
        { x0, 480.0f, z, 1.0f, color },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, quad, sizeof(VERTEX));
}

void __cdecl main()
{
    xt_begin("d3d_depth");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.BackBufferWidth = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice* pDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &d3dpp, &pDevice);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && pDevice != NULL);
    if(FAILED(hr) || pDevice == NULL)
        xt_end_and_exit();

    // Depth surface exactly as a title builds one (CreateTexture2 with
    // D3DUSAGE_DEPTHSTENCIL, then take its level-0 surface).
    D3DTexture* pDSTex =
        D3DDevice_CreateTexture2(640, 480, 1, 1, D3DUSAGE_DEPTHSTENCIL,
                                 D3DFMT_LIN_D24S8, D3DRTYPE_TEXTURE);
    xt_chk("d3d.ds_tex_ok", 1, pDSTex != NULL);
    D3DSurface* pDSSurf = pDSTex ? D3DTexture_GetSurfaceLevel2(pDSTex, 0) : NULL;
    xt_chk("d3d.ds_surf_ok", 1, pDSSurf != NULL);
    if(pDSTex == NULL || pDSSurf == NULL)
        xt_end_and_exit();

    D3DDevice_SetRenderTarget(NULL, pDSSurf);
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, COL_CLEAR,
                    1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetRenderState_ZEnable(TRUE);
    D3DDevice_SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    D3DDevice_SetVertexShader(FVF_VERTEX);

    draw_quad(0.0f, 256.0f, 0.50f, COL_RED);
    draw_quad(128.0f, 384.0f, 0.25f, COL_GREEN);
    draw_quad(0.0f, 384.0f, 0.75f, COL_WHITE);

    // Single readback at the end.
    D3DSurface* pBB = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, pBB != NULL);
    if(pBB != NULL)
    {
        D3DLOCKED_RECT lr;
        lr.pBits = NULL;
        D3DSurface_LockRect(pBB, &lr, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
        if(lr.pBits != NULL)
        {
            xt_chk_u32("d3d.depth_red_kept", COL_RED & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 64, 64));
            xt_chk_u32("d3d.depth_overlap_green", COL_GREEN & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 192, 64));
            xt_chk_u32("d3d.depth_green_only", COL_GREEN & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 320, 64));
            xt_chk_u32("d3d.depth_white_rejected", COL_GREEN & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 340, 420));
            xt_chk_u32("d3d.depth_clear", COL_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 512, 400));
        }
    }

    xt_end_and_exit();
}
