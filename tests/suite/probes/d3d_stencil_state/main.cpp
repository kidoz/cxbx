// d3d_stencil_state -- XDK 4627 native state-entry regression. Turok calls the
// specialized stencil-fail and variable-count vertex-constant setters after
// HLE device creation; without patches they enter D3D::MakeRequestedSpace with
// a null guest device pointer.
#include "xdk_xtrace.h"

void __cdecl main()
{
    xt_begin("d3d_stencil_state");

    xt_chk("d3d.stencil_fail_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetRenderState_StencilFail));
    xt_chk("d3d.vs_constant_notinline_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetVertexShaderConstantNotInline));
    xt_chk("d3d.vs_constant_fast_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetVertexShaderConstantNotInlineFast));
    xt_chk("d3d.begin_hle", 1, xt_is_hle_patched((const void*)D3DDevice_Begin));
    xt_chk("d3d.end_hle", 1, xt_is_hle_patched((const void*)D3DDevice_End));
    xt_chk("d3d.vertex_data2f_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetVertexData2f));
    xt_chk("d3d.vertex_data4f_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetVertexData4f));
    xt_chk("d3d.ps_constant_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetPixelShaderConstant));
    xt_chk("d3d.border_color_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetTextureState_BorderColor));
    xt_chk("d3d.begin_visibility_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_BeginVisibilityTest));
    xt_chk("d3d.end_visibility_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_EndVisibilityTest));
    xt_chk("d3d.get_visibility_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_GetVisibilityTestResult));
    if(!xt_is_hle_patched((const void*)D3DDevice_SetRenderState_StencilFail) ||
       !xt_is_hle_patched((const void*)D3DDevice_SetVertexShaderConstantNotInline) ||
       !xt_is_hle_patched((const void*)D3DDevice_SetVertexShaderConstantNotInlineFast) ||
       !xt_is_hle_patched((const void*)D3DDevice_Begin) ||
       !xt_is_hle_patched((const void*)D3DDevice_End) ||
       !xt_is_hle_patched((const void*)D3DDevice_SetVertexData2f) ||
       !xt_is_hle_patched((const void*)D3DDevice_SetVertexData4f) ||
       !xt_is_hle_patched((const void*)D3DDevice_SetPixelShaderConstant) ||
       !xt_is_hle_patched((const void*)D3DDevice_SetTextureState_BorderColor) ||
       !xt_is_hle_patched((const void*)D3DDevice_BeginVisibilityTest) ||
       !xt_is_hle_patched((const void*)D3DDevice_EndVisibilityTest) ||
       !xt_is_hle_patched((const void*)D3DDevice_GetVisibilityTestResult))
        xt_end_and_exit();

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

    static const DWORD operations[] = {
        D3DSTENCILOP_KEEP, D3DSTENCILOP_ZERO, D3DSTENCILOP_REPLACE,
        D3DSTENCILOP_INCRSAT, D3DSTENCILOP_DECRSAT,
        D3DSTENCILOP_INVERT, D3DSTENCILOP_INCR, D3DSTENCILOP_DECR
    };

    for(DWORD i = 0; i < sizeof(operations) / sizeof(operations[0]); ++i)
    {
        D3DDevice_SetRenderState_StencilFail(operations[i]);
        xt_chk("d3d.stencil_fail_survives", 1, 1);
    }

    D3DDevice_SetRenderState_StencilFail(D3DSTENCILOP_KEEP);
    xt_chk("d3d.stencil_fail_repeat", 1, 1);

    float constants[8] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    D3DDevice_SetVertexShaderConstantNotInline(96, constants, 8);
    xt_chk("d3d.vs_constant_notinline_survives", 1, 1);

    D3DDevice_SetVertexShaderConstantNotInlineFast(100, constants, 8);
    xt_chk("d3d.vs_constant_fast_survives", 1, 1);

    D3DDevice_SetPixelShaderConstant(0, constants, 1);
    xt_chk("d3d.ps_constant_survives", 1, 1);

    D3DDevice_SetTextureState_BorderColor(3, 0xFF102030);
    xt_chk("d3d.border_color_survives", 1, 1);

    D3DDevice_BeginVisibilityTest();
    xt_chk("d3d.begin_visibility_survives", 1, 1);
    D3DDevice_EndVisibilityTest(0);
    xt_chk("d3d.end_visibility_survives", 1, 1);
    UINT visibility = 0;
    ULONGLONG timestamp = ~0ULL;
    D3DDevice_GetVisibilityTestResult(0, &visibility, &timestamp);
    xt_chk("d3d.get_visibility_visible", 1, visibility != 0 && timestamp == 0);

    D3DDevice_Begin(D3DPT_TRIANGLELIST);
    D3DDevice_SetVertexData2f(D3DVSDE_TEXCOORD0, 0.0f, 0.0f);
    D3DDevice_SetVertexData4f(D3DVSDE_POSITION, -0.5f, -0.5f, 0.5f, 1.0f);
    D3DDevice_SetVertexData2f(D3DVSDE_TEXCOORD0, 1.0f, 0.0f);
    D3DDevice_SetVertexData4f(D3DVSDE_POSITION, 0.5f, -0.5f, 0.5f, 1.0f);
    D3DDevice_SetVertexData2f(D3DVSDE_TEXCOORD0, 0.5f, 1.0f);
    D3DDevice_SetVertexData4f(D3DVSDE_POSITION, 0.0f, 0.5f, 0.5f, 1.0f);
    D3DDevice_End();
    xt_chk("d3d.immediate_triangle_survives", 1, 1);

    xt_end_and_exit();
}
