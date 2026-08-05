// d3d_render_to_texture -- pixel-exact verification of the D3D8 HLE
// render-to-texture path. This is the path Turok - Evolution's world passes
// use and that CXBX historically broke: CreateTexture discarded the
// RENDERTARGET/DEPTHSTENCIL usage, forced D3DPOOL_MANAGED, and rewrote the
// depth format, so the surface handed to SetRenderTarget failed the host
// runtime's usage check. The host target was then silently never switched and
// the title's render-to-texture pass painted the back buffer instead, leaving
// the texture blank.
//
// The probe creates a 256x256 RENDERTARGET colour texture and a matching
// DEPTHSTENCIL depth texture (both via CreateTexture2, exactly as a title
// does), binds their surfaces as the render target / depth buffer, clears the
// colour target green and fills its left half red, then restores the back
// buffer and samples the render-target texture back onto it. The back-buffer
// readback is the discriminating check: GetRenderTarget2 returns the cached
// guest pointer whether or not the host bind succeeded, but the sampled colour
// only appears when the render actually landed in the texture.
//
// Readback discipline (see d3d_clear_present / d3d_texture): all rendering
// first, one LockRect readback at the very end, no Present afterward.
#include "xdk_xtrace.h"

static const D3DCOLOR RT_CLEAR = 0xFF00FF00; // green -- RT right half
static const D3DCOLOR RT_LEFT = 0xFFFF0000;  // red   -- RT left half
static const D3DCOLOR BB_CLEAR = 0xFF0000FF; // blue  -- untouched back buffer

#define RT_SIZE 256

struct VTX
{
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};
#define FVF_COL (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
#define FVF_TEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

static DWORD read_pixel(void* pBits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

void __cdecl main()
{
    xt_begin("d3d_render_to_texture");

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

    // Snapshot the implicit back-buffer render target so we can restore it.
    D3DSurface* pOriginalRT = D3DDevice_GetRenderTarget2();
    xt_chk("d3d.orig_rt_ok", 1, pOriginalRT != NULL);

    // Create the render-to-texture pair the way a title does: a colour texture
    // with RENDERTARGET usage and a depth texture with DEPTHSTENCIL usage.
    // Before the fix these came back as plain managed textures whose surfaces
    // SetRenderTarget rejected.
    D3DTexture* pRTTex = D3DDevice_CreateTexture2(RT_SIZE, RT_SIZE, 1, 1,
                                                  D3DUSAGE_RENDERTARGET,
                                                  D3DFMT_LIN_A8R8G8B8,
                                                  D3DRTYPE_TEXTURE);
    xt_chk("d3d.rt_tex_create_ok", 1, pRTTex != NULL);

    D3DTexture* pDSTex = D3DDevice_CreateTexture2(RT_SIZE, RT_SIZE, 1, 1,
                                                  D3DUSAGE_DEPTHSTENCIL,
                                                  D3DFMT_LIN_D24S8,
                                                  D3DRTYPE_TEXTURE);
    xt_chk("d3d.ds_tex_create_ok", 1, pDSTex != NULL);
    if(pRTTex == NULL || pDSTex == NULL)
        xt_end_and_exit();

    D3DSurface* pRTSurf = D3DTexture_GetSurfaceLevel2(pRTTex, 0);
    xt_chk("d3d.rt_surf_ok", 1, pRTSurf != NULL);

    D3DSurface* pDSSurf = D3DTexture_GetSurfaceLevel2(pDSTex, 0);
    xt_chk("d3d.ds_surf_ok", 1, pDSSurf != NULL);
    if(pRTSurf == NULL || pDSSurf == NULL)
        xt_end_and_exit();

    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetRenderState_ZEnable(FALSE);

    // -- Pass 1: render into the texture ----------------------------------
    D3DDevice_SetRenderTarget(pRTSurf, pDSSurf);

    D3DVIEWPORT8 vpRT = { 0, 0, RT_SIZE, RT_SIZE, 0.0f, 1.0f };
    D3DDevice_SetViewport(&vpRT);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, RT_CLEAR, 1.0f, 0);

    // Fill the left half (x 0..128) red; the clear leaves the right half green.
    D3DDevice_SetVertexShader(FVF_COL);
    {
        const float mid = (float)(RT_SIZE / 2);
        const float max = (float)RT_SIZE;
        VTX left[6] = {
            { 0.0f, 0.0f, 0.0f, 1.0f, RT_LEFT, 0.0f, 0.0f },
            { mid, 0.0f, 0.0f, 1.0f, RT_LEFT, 0.0f, 0.0f },
            { 0.0f, max, 0.0f, 1.0f, RT_LEFT, 0.0f, 0.0f },
            { mid, 0.0f, 0.0f, 1.0f, RT_LEFT, 0.0f, 0.0f },
            { mid, max, 0.0f, 1.0f, RT_LEFT, 0.0f, 0.0f },
            { 0.0f, max, 0.0f, 1.0f, RT_LEFT, 0.0f, 0.0f },
        };
        D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, left, sizeof(VTX));
    }

    // -- Restore the back buffer and sample the texture back --------------
    D3DDevice_SetRenderTarget(pOriginalRT, NULL);

    D3DVIEWPORT8 vpBB = { 0, 0, 640, 480, 0.0f, 1.0f };
    D3DDevice_SetViewport(&vpBB);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, BB_CLEAR, 1.0f, 0);

    // Draw the 256x256 render-target texture 1:1 at the top-left corner.
    D3DDevice_SetTexture(0, pRTTex);
    D3DDevice_SetVertexShader(FVF_TEX);
    {
        const float s = (float)RT_SIZE;
        VTX quad[6] = {
            { 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f },
            { s, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
            { 0.0f, s, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
            { s, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f },
            { s, s, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f },
            { 0.0f, s, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f },
        };
        D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, quad, sizeof(VTX));
    }

    // -- Single readback at the end ---------------------------------------
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
            // Sampled render target: left half red, right half green. Reading
            // at the region centres keeps clear of the internal boundary so
            // texture filtering cannot smear the result.
            xt_chk_u32("d3d.rt_left_red", RT_LEFT & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 64, 128));
            xt_chk_u32("d3d.rt_right_green", RT_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 192, 128));
            // Untouched back-buffer background beyond the 256x256 quad.
            xt_chk_u32("d3d.bb_clear", BB_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 500, 400));
        }
    }

    xt_end_and_exit();
}
