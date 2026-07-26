// d3d_default_state -- pin Xbox render-state defaults that differ from the
// host Direct3D 8 device. Titles may render diffuse-colored vertices without
// issuing an explicit D3DRS_LIGHTING write, so inheriting the host's TRUE
// default turns them black.
#include "xdk_xtrace.h"

struct Vertex
{
    float x, y, z;
    float nx, ny, nz;
    D3DCOLOR color;
};

#define FVF_VERTEX (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE)

static DWORD read_pixel(void* bits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)bits + y * pitch + x * 4)) & 0x00FFFFFF;
}

void __cdecl main()
{
    xt_begin("d3d_default_state");

    LPDIRECT3D8 d3d = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, d3d != NULL);

    D3DPRESENT_PARAMETERS parameters;
    ZeroMemory(&parameters, sizeof(parameters));
    parameters.BackBufferWidth = 640;
    parameters.BackBufferHeight = 480;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice* device = NULL;
    const HRESULT result = d3d->CreateDevice(
        0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &parameters, &device);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(result) && device != NULL);
    if(FAILED(result) || device == NULL)
    {
        xt_end_and_exit();
    }

    DWORD lighting = 0xFFFFFFFF;
    D3DDevice_GetRenderState(D3DRS_LIGHTING, &lighting);
    xt_chk_u32("state.lighting", FALSE, lighting);

    const D3DCOLOR clearColor = 0xFF0000FF;
    const D3DCOLOR vertexColor = 0xFFFF4000;
    const Vertex quad[4] = {
        { -0.5f, 0.5f, 0.5f, 0.0f, 0.0f, -1.0f, vertexColor },
        { 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, -1.0f, vertexColor },
        { -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, -1.0f, vertexColor },
        { 0.5f, -0.5f, 0.5f, 0.0f, 0.0f, -1.0f, vertexColor },
    };

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, clearColor, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetRenderState_ZEnable(FALSE);
    D3DDevice_SetTextureStageState(
        0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    D3DDevice_SetTextureStageState(
        0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    D3DDevice_SetTextureStageState(
        1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    D3DDevice_SetVertexShader(FVF_VERTEX);
    D3DDevice_DrawVerticesUP(
        D3DPT_TRIANGLESTRIP, 4, quad, sizeof(Vertex));

    D3DSurface* backBuffer = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, backBuffer != NULL);
    if(backBuffer != NULL)
    {
        D3DLOCKED_RECT locked;
        locked.pBits = NULL;
        D3DSurface_LockRect(
            backBuffer, &locked, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, locked.pBits != NULL);
        if(locked.pBits != NULL)
        {
            xt_chk_u32(
                "state.default_lighting_pixel", vertexColor & 0x00FFFFFF,
                read_pixel(locked.pBits, locked.Pitch, 320, 240));
            xt_chk_u32(
                "state.clear_pixel", clearColor & 0x00FFFFFF,
                read_pixel(locked.pBits, locked.Pitch, 32, 32));
        }
    }

    xt_end_and_exit();
}
