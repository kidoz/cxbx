// d3d_state -- D3D8 HLE state-path checks that need no rasterization:
// SetTransform/GetTransform round-trip through the host device (exact 64-byte
// matrix compare), survival of the whole hooked SetRenderState_* family, and
// GetDisplayMode sanity. A regression in transform-state conversion or a
// render-state handler turning into a hard abort shows up as a named check.
#include "xdk_xtrace.h"

static void fill_matrix(D3DMATRIX *m, float seed)
{
    float *f = (float *)m;
    for (int i = 0; i < 16; i++)
        f[i] = seed + (float)i * 0.25f;
}

void __cdecl main()
{
    xt_begin("d3d_state");

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

    // Transform round-trips: what the title sets must read back bit-exact.
    D3DMATRIX in, out;

    fill_matrix(&in, 1.0f);
    memset(&out, 0, sizeof(out));
    D3DDevice_SetTransform(D3DTS_VIEW, &in);
    D3DDevice_GetTransform(D3DTS_VIEW, &out);
    xt_chk("d3d.xform_view_roundtrip", 1, memcmp(&in, &out, sizeof(in)) == 0);

    fill_matrix(&in, -3.0f);
    memset(&out, 0, sizeof(out));
    D3DDevice_SetTransform(D3DTS_WORLD, &in);
    D3DDevice_GetTransform(D3DTS_WORLD, &out);
    xt_chk("d3d.xform_world_roundtrip", 1, memcmp(&in, &out, sizeof(in)) == 0);

    fill_matrix(&in, 7.5f);
    memset(&out, 0, sizeof(out));
    D3DDevice_SetTransform(D3DTS_PROJECTION, &in);
    D3DDevice_GetTransform(D3DTS_PROJECTION, &out);
    xt_chk("d3d.xform_proj_roundtrip", 1, memcmp(&in, &out, sizeof(in)) == 0);

    // The hooked SetRenderState_* family: every call must return (warn-level
    // gaps are fine; a hard abort or fault fails the probe by dying here).
    D3DDevice_SetRenderState_CullMode(D3DCULL_CCW);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetRenderState_FillMode(D3DFILL_SOLID);
    D3DDevice_SetRenderState_ZEnable(FALSE);
    D3DDevice_SetRenderState_StencilEnable(FALSE);
    D3DDevice_SetRenderState_FogColor(0x00808080);
    D3DDevice_SetRenderState_TextureFactor(0x80808080);
    D3DDevice_SetRenderState_ZBias(0);
    D3DDevice_SetRenderState_NormalizeNormals(TRUE);
    D3DDevice_SetRenderState_NormalizeNormals(FALSE);
    D3DDevice_SetRenderState_EdgeAntiAlias(FALSE);
    D3DDevice_SetRenderState_MultiSampleAntiAlias(FALSE);
    D3DDevice_SetRenderState_Dxt1NoiseEnable(FALSE);
    D3DDevice_SetRenderState_ShadowFunc(D3DCMP_NEVER);
    D3DDevice_SetRenderState_YuvEnable(FALSE);
    xt_chk("d3d.renderstates_survived", 1, 1);

    // GetDisplayMode: must fill a plausible mode (in windowed hosts this is
    // the desktop mode, so only sanity-check the bounds).
    D3DDISPLAYMODE mode;
    memset(&mode, 0, sizeof(mode));
    D3DDevice_GetDisplayMode(&mode);
    xt_chk("d3d.dispmode_plausible", 1,
           mode.Width >= 320 && mode.Width <= 16384 &&
           mode.Height >= 240 && mode.Height <= 16384);

    xt_end_and_exit();
}
