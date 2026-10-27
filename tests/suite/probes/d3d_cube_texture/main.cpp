// d3d_cube_texture -- pixel-exact verification of cube-map texture
// mirroring under the native Vulkan backend. Historically cubes sampled
// white: the bind pull mirrored only D3DRTYPE_TEXTURE. The probe creates a
// 64x64 cube the way a title does, fills all six faces with distinct
// colors through D3DCubeTexture_LockRect, then samples three faces by
// direction (+Z, +X, -Z) with a 3-float FVF texcoord set and verifies the
// backbuffer pixels.
//
// Readback discipline (see d3d_clear_present / d3d_texture): all rendering
// first, one LockRect readback at the very end, no Present afterward.
#include "xdk_xtrace.h"

static const D3DCOLOR FACE_COLORS[6] = {
    0xFFFF0000, // +X red
    0xFF802000, // -X
    0xFF0080FF, // +Y
    0xFF004080, // -Y
    0xFF00FF00, // +Z green
    0xFFFFFF00, // -Z yellow
};
static const D3DCOLOR BB_CLEAR = 0xFF0000FF;

#define CUBE_SIZE 64

struct VTX
{
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v, w;
};
#define FVF_CUBE (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | \
                  D3DFVF_TEXCOORDSIZE3(0))

static DWORD read_pixel(void* pBits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

void __cdecl main()
{
    xt_begin("d3d_cube_texture");

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

    D3DCubeTexture* pCube = NULL;
    hr = D3DDevice_CreateCubeTexture(CUBE_SIZE, 1, 0,
                                     D3DFMT_LIN_A8R8G8B8, D3DPOOL_MANAGED,
                                     &pCube);
    xt_chk("d3d.cube_create_ok", 1, SUCCEEDED(hr) && pCube != NULL);
    if(FAILED(hr) || pCube == NULL)
        xt_end_and_exit();

    // Fill every face with its solid color.
    BOOL facesOk = TRUE;
    for(int face = 0; face < 6; ++face)
    {
        D3DLOCKED_RECT lr;
        ZeroMemory(&lr, sizeof(lr));
        D3DCubeTexture_LockRect(pCube, (D3DCUBEMAP_FACES)face, 0, &lr, NULL,
                                0);
        if(lr.pBits == NULL)
        {
            facesOk = FALSE;
            break;
        }
        for(int y = 0; y < CUBE_SIZE; ++y)
        {
            DWORD* row = (DWORD*)((BYTE*)lr.pBits + y * lr.Pitch);
            for(int x = 0; x < CUBE_SIZE; ++x)
                row[x] = FACE_COLORS[face];
        }
        D3DCubeTexture_UnlockRect(pCube, (D3DCUBEMAP_FACES)face, 0);
    }
    xt_chk("d3d.faces_locked", 1, facesOk);
    if(!facesOk)
        xt_end_and_exit();

    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetRenderState_ZEnable(FALSE);
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, BB_CLEAR, 1.0f, 0);

    // Draw three direction quads: +Z (green), +X (red), -Z (yellow).
    D3DDevice_SetTexture(0, pCube);
    D3DDevice_SetVertexShader(FVF_CUBE);

    const float mid = 128.0f;
    const struct { float x, y; float dx, dy, dz; D3DCOLOR expect; } cases[3] = {
        { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, FACE_COLORS[4] },
        { 0.0f, 160.0f, 1.0f, 0.0f, 0.0f, FACE_COLORS[0] },
        { 160.0f, 160.0f, 0.0f, 0.0f, -1.0f, FACE_COLORS[5] },
    };
    for(int c = 0; c < 3; ++c)
    {
        const float x0 = cases[c].x, y0 = cases[c].y;
        VTX quad[6] = {
            { x0, y0, 0.0f, 1.0f, 0xFFFFFFFF, cases[c].dx, cases[c].dy, cases[c].dz },
            { x0 + mid, y0, 0.0f, 1.0f, 0xFFFFFFFF, cases[c].dx, cases[c].dy, cases[c].dz },
            { x0, y0 + mid, 0.0f, 1.0f, 0xFFFFFFFF, cases[c].dx, cases[c].dy, cases[c].dz },
            { x0 + mid, y0, 0.0f, 1.0f, 0xFFFFFFFF, cases[c].dx, cases[c].dy, cases[c].dz },
            { x0 + mid, y0 + mid, 0.0f, 1.0f, 0xFFFFFFFF, cases[c].dx, cases[c].dy, cases[c].dz },
            { x0, y0 + mid, 0.0f, 1.0f, 0xFFFFFFFF, cases[c].dx, cases[c].dy, cases[c].dz },
        };
        // Xbox DrawVerticesUP takes the VERTEX count (6 verts = 2 tris).
        D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, quad, sizeof(VTX));
    }

    // -- Single readback at the end ----------------------------------------
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
            xt_chk_u32("d3d.cube_posz_green", FACE_COLORS[4] & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 64, 64));
            xt_chk_u32("d3d.cube_posx_red", FACE_COLORS[0] & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 64, 224));
            xt_chk_u32("d3d.cube_negz_yellow", FACE_COLORS[5] & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 224, 224));
            // Untouched background beyond the quads.
            xt_chk_u32("d3d.bb_clear", BB_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 500, 400));
        }
    }

    xt_end_and_exit();
}
