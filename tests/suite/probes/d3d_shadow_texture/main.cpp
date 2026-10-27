// d3d_shadow_texture -- pixel-exact verification of NV2A shadow-map
// sampling under the native Vulkan backend. Historically depth textures
// sampled white (the depth content lives in the backend target's depth
// image; locking a host depth texture faults inside the Vulkan ICD).
//
// The probe renders into a 64x64 render-target pair (colour + depth
// texture, exactly as d3d_render_to_texture creates them), clears depth to
// 0.5, then binds the DEPTH texture on stage 0 with
// D3DRS_SHADOWFUNC=LESSEQUAL and draws two quads whose 4-float texcoords
// carry the reference r: r=0.7 lights (0.5 <= 0.7) and r=0.3 stays dark.
//
// Vulkan-only: host D3D8 cannot sample a depth-stencil texture the Xbox
// way, so the pixel checks are pinned with CXBX_HOST_BACKEND=vulkan in
// probe.toml.
//
// Readback discipline (see d3d_clear_present): all rendering first, one
// LockRect readback at the end, no Present afterward.
#include "xdk_xtrace.h"

static const D3DCOLOR BB_CLEAR = 0xFF0000FF; // blue  -- untouched backbuffer

#define RT_SIZE 64

struct VTX
{
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v, r, q;
};
// Xbox's nonstandard FVF encoding: TEXCOORDSIZE4 = TEXTUREFORMAT4(2).
#define FVF_SHADOW (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 | \
                    (2u << 16))

static DWORD read_pixel(void* pBits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static void draw_shadow_quad(float x0, float y0, float ref)
{
    const float mid = 128.0f;
    VTX quad[6] = {
        { x0, y0, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 0.0f, ref, 1.0f },
        { x0 + mid, y0, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f, ref, 1.0f },
        { x0, y0 + mid, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f, ref, 1.0f },
        { x0 + mid, y0, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 0.0f, ref, 1.0f },
        { x0 + mid, y0 + mid, 0.0f, 1.0f, 0xFFFFFFFF, 1.0f, 1.0f, ref, 1.0f },
        { x0, y0 + mid, 0.0f, 1.0f, 0xFFFFFFFF, 0.0f, 1.0f, ref, 1.0f },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, quad, sizeof(VTX));
}

void __cdecl main()
{
    xt_begin("d3d_shadow_texture");

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

    // The render pair a title creates: RENDERTARGET colour + DEPTHSTENCIL
    // depth textures (see d3d_render_to_texture).
    D3DTexture* pRTTex = D3DDevice_CreateTexture2(RT_SIZE, RT_SIZE, 1, 1,
                                                  D3DUSAGE_RENDERTARGET,
                                                  D3DFMT_LIN_A8R8G8B8,
                                                  D3DRTYPE_TEXTURE);
    D3DTexture* pDSTex = D3DDevice_CreateTexture2(RT_SIZE, RT_SIZE, 1, 1,
                                                  D3DUSAGE_DEPTHSTENCIL,
                                                  D3DFMT_LIN_D24S8,
                                                  D3DRTYPE_TEXTURE);
    xt_chk("d3d.rt_tex_create_ok", 1, pRTTex != NULL);
    xt_chk("d3d.ds_tex_create_ok", 1, pDSTex != NULL);
    if(pRTTex == NULL || pDSTex == NULL)
        xt_end_and_exit();

    D3DSurface* pRTSurf = D3DTexture_GetSurfaceLevel2(pRTTex, 0);
    D3DSurface* pDSSurf = D3DTexture_GetSurfaceLevel2(pDSTex, 0);
    xt_chk("d3d.surfaces_ok", 1, pRTSurf != NULL && pDSSurf != NULL);
    if(pRTSurf == NULL || pDSSurf == NULL)
        xt_end_and_exit();

    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetRenderState_ZEnable(FALSE);

    // -- Depth pass: render into the pair, clear depth to 0.5 --------------
    D3DDevice_SetRenderTarget(pRTSurf, pDSSurf);

    D3DVIEWPORT8 vpRT = { 0, 0, RT_SIZE, RT_SIZE, 0.0f, 1.0f };
    D3DDevice_SetViewport(&vpRT);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000,
                     0.5f, 0);

    // -- Restore the back buffer and sample the depth texture --------------
    D3DDevice_SetRenderTarget(pOriginalRT, NULL);

    D3DVIEWPORT8 vpBB = { 0, 0, 640, 480, 0.0f, 1.0f };
    D3DDevice_SetViewport(&vpBB);

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, BB_CLEAR, 1.0f, 0);

    D3DDevice_SetRenderState_ShadowFunc(D3DCMP_LESSEQUAL);
    D3DDevice_SetTexture(0, pDSTex);
    D3DDevice_SetVertexShader(FVF_SHADOW);

    draw_shadow_quad(0.0f, 0.0f, 0.7f);   // 0.5 <= 0.7 -> white
    draw_shadow_quad(0.0f, 160.0f, 0.3f); // 0.5 <= 0.3 -> black

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
            xt_chk_u32("d3d.shadow_lit_white", 0x00FFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 64, 64));
            xt_chk_u32("d3d.shadow_dark_black", 0x00000000,
                       read_pixel(lr.pBits, lr.Pitch, 64, 224));
            // Untouched back-buffer background beyond the quads.
            xt_chk_u32("d3d.bb_clear", BB_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 500, 400));
        }
    }

    xt_end_and_exit();
}
