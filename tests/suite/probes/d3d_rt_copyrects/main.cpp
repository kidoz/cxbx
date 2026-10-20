// d3d_rt_copyrects -- pixel-exact verification of CopyRects whose SOURCE is
// a render target. Under the native Vulkan backend the rendered pixels live
// in the backend's target registry; the host surface for the render target
// holds stale bytes, so the host CopyRects forward copies garbage. The HLE
// must read the registry target back and write the destination itself.
//
// The probe renders the same 256x256 target as d3d_render_to_texture (green
// clear, red left half), then copies the target's level surface into a
// system-memory image surface with one whole-surface CopyRects and locks the
// image back. The discriminating check is the locked image content: it only
// matches when the copy served the render's actual pixels.
//
// Readback discipline (see d3d_clear_present / d3d_texture): all rendering
// first, one LockRect readback at the very end, no Present afterward.
#include "xdk_xtrace.h"

static const D3DCOLOR RT_CLEAR = 0xFF00FF00; // green -- RT right half
static const D3DCOLOR RT_LEFT = 0xFFFF0000;  // red   -- RT left half

#define RT_SIZE 256

struct VTX
{
    float x, y, z, rhw;
    D3DCOLOR color;
};
#define FVF_COL (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static DWORD read_pixel(void* pBits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

void __cdecl main()
{
    xt_begin("d3d_rt_copyrects");

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

    D3DSurface* pOriginalRT = D3DDevice_GetRenderTarget2();
    xt_chk("d3d.orig_rt_ok", 1, pOriginalRT != NULL);

    D3DTexture* pRTTex = D3DDevice_CreateTexture2(RT_SIZE, RT_SIZE, 1, 1,
                                                  D3DUSAGE_RENDERTARGET,
                                                  D3DFMT_LIN_A8R8G8B8,
                                                  D3DRTYPE_TEXTURE);
    xt_chk("d3d.rt_tex_create_ok", 1, pRTTex != NULL);
    if(pRTTex == NULL)
        xt_end_and_exit();

    D3DSurface* pRTSurf = D3DTexture_GetSurfaceLevel2(pRTTex, 0);
    xt_chk("d3d.rt_surf_ok", 1, pRTSurf != NULL);
    if(pRTSurf == NULL)
        xt_end_and_exit();

    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetRenderState_ZEnable(FALSE);

    // -- Render into the target: green clear, red left half ----------------
    D3DDevice_SetRenderTarget(pRTSurf, NULL);

    D3DVIEWPORT8 vpRT = { 0, 0, RT_SIZE, RT_SIZE, 0.0f, 1.0f };
    D3DDevice_SetViewport(&vpRT);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, RT_CLEAR, 1.0f, 0);

    D3DDevice_SetVertexShader(FVF_COL);
    {
        const float mid = (float)(RT_SIZE / 2);
        const float max = (float)RT_SIZE;
        VTX left[6] = {
            { 0.0f, 0.0f, 0.0f, 1.0f, RT_LEFT },
            { mid, 0.0f, 0.0f, 1.0f, RT_LEFT },
            { 0.0f, max, 0.0f, 1.0f, RT_LEFT },
            { mid, 0.0f, 0.0f, 1.0f, RT_LEFT },
            { mid, max, 0.0f, 1.0f, RT_LEFT },
            { 0.0f, max, 0.0f, 1.0f, RT_LEFT },
        };
        D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, left, sizeof(VTX));
    }

    // -- Restore the back buffer -------------------------------------------
    D3DDevice_SetRenderTarget(pOriginalRT, NULL);

    D3DVIEWPORT8 vpBB = { 0, 0, 640, 480, 0.0f, 1.0f };
    D3DDevice_SetViewport(&vpBB);

    // -- Copy the target into a system-memory image surface ----------------
    D3DSurface* pImage = NULL;
    D3DDevice_CreateImageSurface(RT_SIZE, RT_SIZE, D3DFMT_LIN_A8R8G8B8,
                                 &pImage);
    xt_chk("d3d.image_create_ok", 1, pImage != NULL);
    if(pImage == NULL)
        xt_end_and_exit();

    D3DDevice_CopyRects(pRTSurf, NULL, 0, pImage, NULL);
    xt_chk("d3d.copyrects_called", 1, 1);

    // -- Single readback at the end ----------------------------------------
    D3DLOCKED_RECT lr;
    lr.pBits = NULL;
    D3DSurface_LockRect(pImage, &lr, NULL, D3DLOCK_READONLY);
    xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
    if(lr.pBits != NULL)
    {
        // Left half red, right half green -- sampled at region centres so a
        // boundary off-by-one cannot flip the verdict.
        xt_chk_u32("d3d.rt_left_red", RT_LEFT & 0xFFFFFF,
                   read_pixel(lr.pBits, lr.Pitch, 64, 128));
        xt_chk_u32("d3d.rt_right_green", RT_CLEAR & 0xFFFFFF,
                   read_pixel(lr.pBits, lr.Pitch, 192, 128));
    }

    xt_end_and_exit();
}
