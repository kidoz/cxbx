// d3d_draw_indexed -- pixel-exact verification of the indexed draw path:
// SetStreamSource + SetIndices + DrawIndexedVertices (triangle list), the
// fixed-function geometry path titles use for world geometry.
//
// Layout drawn onto a blue clear (all pretransformed XYZRHW):
//   left  128x128 quad (0..128 x 0..128)    red   via indices [0..5]
//   right 128x128 quad (128..256 x 0..128)  green via indices [6..11],
//   drawn with an index-buffer byte offset of 12 (pIndexData)
//   everything else                          stays blue (untouched clear)
//
// Readback discipline: the HLE LockRect leaves the host surface locked, so
// all drawing happens before the single readback at the end (see
// d3d_clear_present).
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF; // blue
static const D3DCOLOR COL_LEFT = 0xFFFF0000;  // red
static const D3DCOLOR COL_RIGHT = 0xFF00FF00; // green

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

void __cdecl main()
{
    xt_begin("d3d_draw_indexed");

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

    // Two quads: vertices 0..3 red (left), vertices 4..7 green (right).
    static const VERTEX verts[8] = {
        { 0.0f, 0.0f, 0.0f, 1.0f, COL_LEFT },
        { 128.0f, 0.0f, 0.0f, 1.0f, COL_LEFT },
        { 128.0f, 128.0f, 0.0f, 1.0f, COL_LEFT },
        { 0.0f, 128.0f, 0.0f, 1.0f, COL_LEFT },
        { 128.0f, 0.0f, 0.0f, 1.0f, COL_RIGHT },
        { 256.0f, 0.0f, 0.0f, 1.0f, COL_RIGHT },
        { 256.0f, 128.0f, 0.0f, 1.0f, COL_RIGHT },
        { 128.0f, 128.0f, 0.0f, 1.0f, COL_RIGHT },
    };
    static const WORD indices[12] = {
        0,
        1,
        2,
        0,
        2,
        3, // left quad
        4,
        5,
        6,
        4,
        6,
        7, // right quad
    };

    D3DVertexBuffer* pVB = D3DDevice_CreateVertexBuffer2(sizeof(verts));
    xt_chk("d3d.vb_ok", 1, pVB != NULL);
    D3DIndexBuffer* pIB = D3DDevice_CreateIndexBuffer2(sizeof(indices));
    xt_chk("d3d.ib_ok", 1, pIB != NULL);
    if(pVB == NULL || pIB == NULL)
        xt_end_and_exit();

    BYTE* pVBData = NULL;
    hr = pVB->Lock(0, 0, &pVBData, 0);
    xt_chk("d3d.vb_lock_ok", 1, SUCCEEDED(hr) && pVBData != NULL);
    if(SUCCEEDED(hr) && pVBData != NULL)
    {
        memcpy(pVBData, verts, sizeof(verts));
        pVB->Unlock();
    }
    BYTE* pIBData = NULL;
    hr = pIB->Lock(0, 0, &pIBData, 0);
    xt_chk("d3d.ib_lock_ok", 1, SUCCEEDED(hr) && pIBData != NULL);
    if(SUCCEEDED(hr) && pIBData != NULL)
    {
        memcpy(pIBData, indices, sizeof(indices));
        pIB->Unlock();
    }

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetVertexShader(FVF_VERTEX);
    D3DDevice_SetStreamSource(0, pVB, sizeof(VERTEX));
    D3DDevice_SetIndices(pIB, 0);

    // Draw 1: indices [0..5] from the buffer start (left quad).
    D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, 6, NULL);
    // Draw 2: indices [6..11] via a 12-byte index offset (right quad).
    D3DDevice_DrawIndexedVertices(D3DPT_TRIANGLELIST, 6, (CONST WORD*)12);

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
            xt_chk_u32("d3d.idx_left_red", COL_LEFT & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 64, 64));
            xt_chk_u32("d3d.idx_right_green", COL_RIGHT & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 192, 64));
            xt_chk_u32("d3d.idx_clear", COL_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 480, 400));
        }
    }

    xt_end_and_exit();
}
