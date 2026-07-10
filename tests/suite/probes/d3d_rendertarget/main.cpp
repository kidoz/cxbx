// d3d_rendertarget -- D3D8 HLE render-target / depth-stencil surface checks
// that need no rasterization. Exercises the SetRenderTarget -> GetRenderTarget2
// / GetDepthStencilSurface2 round-trip, the NULL-depth path (render-target
// only), and full restoration of the original surfaces. A mispatched or missing
// SetRenderTarget HLE entry leaves the call in the native 4627 D3D runtime,
// whose uninitialized guest device pointer faults in CommonSetRenderTarget.
#include "xdk_xtrace.h"

void __cdecl main()
{
    xt_begin("d3d_rendertarget");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.BackBufferWidth = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice* pDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &d3dpp, &pDevice);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && pDevice != NULL);
    if(FAILED(hr) || pDevice == NULL)
        xt_end_and_exit();

    // Snapshot the implicit back buffer / render target / depth surfaces that
    // CreateDevice set up. These are what we must hand back at the end.
    D3DSurface* pBackBuffer = NULL;
    hr = pDevice->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer);
    xt_chk("d3d.get_backbuffer_ok", 1, SUCCEEDED(hr) && pBackBuffer != NULL);

    D3DSurface* pOriginalRT = D3DDevice_GetRenderTarget2();
    xt_chk("d3d.get_rt2_initial", 1, pOriginalRT != NULL);

    D3DSurface* pOriginalDS = D3DDevice_GetDepthStencilSurface2();
    xt_chk("d3d.get_ds2_initial", 1, pOriginalDS != NULL);

    // Rebind the registered implicit surfaces through SetRenderTarget. This
    // proves that the 4627 entry was patched into the host wrapper without
    // depending on separate CreateRenderTarget HLE coverage.
    hr = pDevice->SetRenderTarget(pBackBuffer, pOriginalDS);
    xt_chk("d3d.set_rt_ds_hr", 0, SUCCEEDED(hr));

    D3DSurface* pReadRT = D3DDevice_GetRenderTarget2();
    xt_chk("d3d.rt_after_set", 1, pReadRT == pBackBuffer);

    D3DSurface* pReadDS = D3DDevice_GetDepthStencilSurface2();
    xt_chk("d3d.ds_after_set", 1, pReadDS == pOriginalDS);

    // NULL-depth path: SetRenderTarget accepts a NULL depth-stencil to detach
    // depth testing. This must not fault and must keep the render target.
    hr = pDevice->SetRenderTarget(pBackBuffer, NULL);
    xt_chk("d3d.set_rt_null_ds_hr", 0, SUCCEEDED(hr));

    pReadRT = D3DDevice_GetRenderTarget2();
    xt_chk("d3d.rt_after_null_ds", 1, pReadRT == pBackBuffer);

    pReadDS = D3DDevice_GetDepthStencilSurface2();
    xt_chk("d3d.ds_after_null", 1, pReadDS == NULL);

    // Restore the original implicit surfaces, and confirm the device is back to
    // its initial state. This is the path a title runs every frame when it
    // returns to the back buffer after rendering to a texture.
    hr = pDevice->SetRenderTarget(pOriginalRT, pOriginalDS);
    xt_chk("d3d.restore_hr", 0, SUCCEEDED(hr));

    pReadRT = D3DDevice_GetRenderTarget2();
    xt_chk("d3d.rt_restored", 1, pReadRT == pOriginalRT);

    pReadDS = D3DDevice_GetDepthStencilSurface2();
    xt_chk("d3d.ds_restored", 1, pReadDS == pOriginalDS);

    xt_end_and_exit();
}
