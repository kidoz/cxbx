// d3d_draw -- pixel-exact verification of the two HLE draw paths titles use
// for UI: DrawVerticesUP (triangle list) and the immediate-mode
// Begin / SetVertexData / End bracket (quad list -- NestopiaX's menu/text
// path, never before verified against actual pixels).
//
// Layout drawn onto a blue clear (all pretransformed XYZRHW):
//   left half  (0..320 x 0..480)   green   via DrawVerticesUP triangles
//   top-right  (320..640 x 0..240) red     via Begin/SetVertexData/End quad
//   bottom-right                   stays blue (untouched clear)
//
// Readback discipline: the HLE LockRect leaves the host surface locked, so
// all drawing happens before the single readback at the end (see
// d3d_clear_present).
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF; // blue
static const D3DCOLOR COL_TRI   = 0xFF00FF00; // green
static const D3DCOLOR COL_QUAD  = 0xFFFF0000; // red

struct VERTEX {
    float x, y, z, rhw;
    D3DCOLOR color;
};
#define FVF_VERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static DWORD read_pixel(void *pBits, INT pitch, int x, int y)
{
    return (*(DWORD *)((BYTE *)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static void im_vertex(float x, float y)
{
    // Register -1 = D3DVSDE_VERTEX: the position write that emits the vertex.
    D3DDevice_SetVertexData4f(D3DVSDE_VERTEX, x, y, 0.0f, 1.0f);
}

void __cdecl main()
{
    xt_begin("d3d_draw");

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

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetVertexShader(FVF_VERTEX);

    // Path 1: DrawVerticesUP, triangle list covering the left half.
    static const VERTEX tris[6] = {
        {   0.0f,   0.0f, 0.0f, 1.0f, COL_TRI },
        { 320.0f,   0.0f, 0.0f, 1.0f, COL_TRI },
        {   0.0f, 480.0f, 0.0f, 1.0f, COL_TRI },
        { 320.0f,   0.0f, 0.0f, 1.0f, COL_TRI },
        { 320.0f, 480.0f, 0.0f, 1.0f, COL_TRI },
        {   0.0f, 480.0f, 0.0f, 1.0f, COL_TRI },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, tris, sizeof(VERTEX));

    // Path 2: immediate-mode quad covering the top-right quarter.
    D3DDevice_Begin(D3DPT_QUADLIST);
    D3DDevice_SetVertexDataColor(D3DVSDE_DIFFUSE, COL_QUAD);
    im_vertex(320.0f,   0.0f);
    im_vertex(640.0f,   0.0f);
    im_vertex(640.0f, 240.0f);
    im_vertex(320.0f, 240.0f);
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
            xt_chk_u32("d3d.px_tri", COL_TRI & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 160, 240));
            xt_chk_u32("d3d.px_quad", COL_QUAD & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 480, 120));
            xt_chk_u32("d3d.px_clear", COL_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 480, 360));
        }
    }

    xt_end_and_exit();
}
