// xmv_decode -- Xbox Media Video (XMV) decode conformance. NestopiaX 1.3 and
// many XDK titles play their menu/intro through the XMV library (<xmv.h>): open
// an .xmv, read its descriptor, then pump XMVDecoder_GetNextFrame to decode WMV
// frames into a YUY2 surface the GPU shows via the overlay. This build links the
// genuine XDK xmv.lib, so every call runs real decoder code until the emulator's
// HLE takes over -- exactly what a title does.
//
// It goes past creation into an actual frame decode (the heavy WMV path):
//   1. create a D3D device (the frame surfaces are GPU textures)
//   2. XMVDecoder_CreateDecoderForFile on the SDK's Test.xmv (staged to
//      D:\Media\Videos by build.ps1)
//   3. read the video descriptor (frame size)
//   4. create a YUY2 texture of that size and pump GetNextFrame until a
//      XMV_NEWFRAME comes back -- proving the decoder produces real frames.
// A target missing XMV decode fails or faults here, pinpointing the gap.

#include "xdk_xtrace.h"
#include <xmv.h>

void __cdecl main()
{
    xt_begin("xmv_decode");

    // A D3D device -- the decoded frames land in a GPU texture surface.
    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("xmv.d3d_ok", 1, pD3D != NULL);
    if (pD3D == NULL) xt_end_and_exit();

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth  = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount  = 1;
    pp.SwapEffect       = D3DSWAPEFFECT_DISCARD;

    D3DDevice *pDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &pDevice);
    xt_chk("xmv.device_ok", 1, SUCCEEDED(hr) && pDevice != NULL);
    if (FAILED(hr) || pDevice == NULL) xt_end_and_exit();

    // Container parse + decoder spin-up.
    XMVDecoder *pDecoder = NULL;
    hr = XMVDecoder_CreateDecoderForFile(0, "D:\\Media\\Videos\\Test.xmv", &pDecoder);
    xt_chk("xmv.create_ok", 1, SUCCEEDED(hr) && pDecoder != NULL);
    if (FAILED(hr) || pDecoder == NULL) xt_end_and_exit();

    XMVVIDEO_DESC desc;
    memset(&desc, 0, sizeof(desc));
    XMVDecoder_GetVideoDescriptor(pDecoder, &desc);
    xt_chk("xmv.size_plausible", 1,
           desc.Width > 0 && desc.Width <= 1920 && desc.Height > 0 && desc.Height <= 1080);
    if (desc.Width == 0 || desc.Height == 0) xt_end_and_exit();

    // Frame target: a YUY2 texture the exact size of the video.
    IDirect3DTexture8 *pTex = NULL;
    hr = pDevice->CreateTexture(desc.Width, desc.Height, 1, 0, D3DFMT_YUY2, 0, &pTex);
    xt_chk("xmv.texture_ok", 1, SUCCEEDED(hr) && pTex != NULL);

    IDirect3DSurface8 *pSurface = NULL;
    if (pTex != NULL)
        pTex->GetSurfaceLevel(0, &pSurface);

    // Pump the decoder until it hands back a real frame (or we give up). Guard
    // it: a target whose WMV decoder faults reports a clean FAIL instead of
    // taking the probe down (this Cxbx access-violations inside the decoder).
    XMVRESULT xr = XMV_NOFRAME;
    int gotFrame = 0;
    __try
    {
        for (int i = 0; i < 800 && !gotFrame; i++)
        {
            XMVDecoder_GetNextFrame(pDecoder, pSurface, &xr, NULL);
            if (xr == XMV_NEWFRAME)
                gotFrame = 1;
            Sleep(2);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        gotFrame = 0;   // decoder faulted -> no frame produced
    }
    xt_chk("xmv.frame_decoded", 1, gotFrame);

    XMVDecoder_CloseDecoder(pDecoder);
    xt_end_and_exit();
}
