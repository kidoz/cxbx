// d3d_shader_lifecycle -- XDK 4627 pixel-shader ownership regression.
// Turok - Evolution creates Xbox register-combiner shaders, receives CXBX's
// fallback marker handle, and later deletes it. The native Xbox delete routine
// treats that marker as a D3DResource pointer and faults unless HLE patches it.
#include "xdk_xtrace.h"

void __cdecl main()
{
    xt_begin("d3d_shader_lifecycle");

    xt_chk("d3d.create_pixel_shader_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_CreatePixelShader));
    xt_chk("d3d.set_pixel_shader_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_SetPixelShader));
    xt_chk("d3d.delete_pixel_shader_hle", 1,
           xt_is_hle_patched((const void*)D3DDevice_DeletePixelShader));

    if(!xt_is_hle_patched((const void*)D3DDevice_CreatePixelShader) ||
       !xt_is_hle_patched((const void*)D3DDevice_SetPixelShader) ||
       !xt_is_hle_patched((const void*)D3DDevice_DeletePixelShader))
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

    D3DPIXELSHADERDEF shader;
    ZeroMemory(&shader, sizeof(shader));

    DWORD handle = 0;
    hr = pDevice->CreatePixelShader(&shader, &handle);
    xt_chk("shader.create_hr", D3D_OK, hr);
    xt_chk("shader.handle_nonzero", 1, handle != 0);

    hr = pDevice->SetPixelShader(handle);
    xt_chk("shader.set_hr", D3D_OK, hr);

    hr = pDevice->DeletePixelShader(handle);
    xt_chk("shader.delete_hr", D3D_OK, hr);

    hr = pDevice->DeletePixelShader(handle);
    xt_chk("shader.delete_repeat_hr", D3D_OK, hr);

    hr = pDevice->DeletePixelShader(0);
    xt_chk("shader.delete_zero_hr", D3D_OK, hr);

    hr = pDevice->SetPixelShader(0);
    xt_chk("shader.unbind_hr", D3D_OK, hr);

    xt_end_and_exit();
}
