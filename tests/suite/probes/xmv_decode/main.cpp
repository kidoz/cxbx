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
//   3. verify the exact video/audio descriptors and audio stream ownership
//   4. create a YUY2 texture of that size and pump GetNextFrame until a
//      XMV_NEWFRAME comes back
//   5. reset and decode the first frame again, then disable the audio stream.
// A target missing XMV decode fails or faults here, pinpointing the gap.

#include "xdk_xtrace.h"
#include <xmv.h>

void __cdecl main()
{
    xt_begin("xmv_decode");

    // A D3D device -- the decoded frames land in a GPU texture surface.
    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("xmv.d3d_ok", 1, pD3D != NULL);
    if(pD3D == NULL)
    {
        xt_end_and_exit();
    }

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
    if(FAILED(hr) || pDevice == NULL)
    {
        xt_end_and_exit();
    }

    // Container parse + decoder spin-up.
    XMVDecoder *pDecoder = NULL;
    hr = XMVDecoder_CreateDecoderForFile(0, "D:\\Media\\Videos\\Test.xmv", &pDecoder);
    xt_chk("xmv.create_ok", 1, SUCCEEDED(hr) && pDecoder != NULL);
    if(FAILED(hr) || pDecoder == NULL)
    {
        xt_end_and_exit();
    }

    XMVVIDEO_DESC desc;
    memset(&desc, 0, sizeof(desc));
    XMVDecoder_GetVideoDescriptor(pDecoder, &desc);
    xt_chk_u32("xmv.video_width", 640, desc.Width);
    xt_chk_u32("xmv.video_height", 480, desc.Height);
    xt_chk_u32("xmv.video_fps", 0, desc.FramesPerSecond);
    xt_chk_u32("xmv.audio_stream_count", 1, desc.AudioStreamCount);
    if(desc.Width != 640 || desc.Height != 480 || desc.FramesPerSecond != 0 ||
       desc.AudioStreamCount != 1)
    {
        XMVDecoder_CloseDecoder(pDecoder);
        xt_end_and_exit();
    }

    XMVAUDIO_DESC audioDesc;
    memset(&audioDesc, 0, sizeof(audioDesc));
    XMVDecoder_GetAudioDescriptor(pDecoder, 0, &audioDesc);
    xt_chk_u32("xmv.audio_format", WAVE_FORMAT_PCM, audioDesc.WaveFormat);
    xt_chk_u32("xmv.audio_channels", 2, audioDesc.ChannelCount);
    xt_chk_u32("xmv.audio_rate", 44100, audioDesc.SamplesPerSecond);
    xt_chk_u32("xmv.audio_bits", 16, audioDesc.BitsPerSample);
    xt_chk_u32("xmv.audio_flags", 0, audioDesc.Flags);

    IDirectSoundStream* pAudioStream = NULL;
    hr = XMVDecoder_EnableAudioStream(pDecoder, 0, 0, NULL, &pAudioStream);
    xt_chk("xmv.audio_enable", 1, SUCCEEDED(hr) && pAudioStream != NULL);
    if(FAILED(hr) || pAudioStream == NULL)
    {
        XMVDecoder_CloseDecoder(pDecoder);
        xt_end_and_exit();
    }

    IDirectSoundStream* pQueriedStream = NULL;
    XMVDecoder_GetAudioStream(pDecoder, 0, &pQueriedStream);
    xt_chk("xmv.audio_stream_identity", 1,
           pQueriedStream == pAudioStream && pQueriedStream != NULL);
    if(pQueriedStream != NULL)
    {
        pQueriedStream->Release();
        pQueriedStream = NULL;
    }
    if(pAudioStream != NULL)
    {
        pAudioStream->Release();
    }
    xt_chk_u32("xmv.sync_stream_default", 0,
               XMVDecoder_GetSynchronizationStream(pDecoder));

    XMVDecoder_SetSynchronizationStream(pDecoder, (DWORD)-1);
    xt_chk_u32("xmv.sync_stream_disabled", (DWORD)-1,
               XMVDecoder_GetSynchronizationStream(pDecoder));
    XMVDecoder_SetSynchronizationStream(pDecoder, 0);
    xt_chk_u32("xmv.sync_stream_restored", 0,
               XMVDecoder_GetSynchronizationStream(pDecoder));

    // Frame target: a YUY2 texture the exact size of the video.
    IDirect3DTexture8 *pTex = NULL;
    hr = pDevice->CreateTexture(desc.Width, desc.Height, 1, 0, D3DFMT_YUY2, 0, &pTex);
    xt_chk("xmv.texture_ok", 1, SUCCEEDED(hr) && pTex != NULL);

    IDirect3DSurface8 *pSurface = NULL;
    if(pTex != NULL)
    {
        pTex->GetSurfaceLevel(0, &pSurface);
    }
    xt_chk("xmv.surface_ok", 1, pSurface != NULL);

    // Pump the decoder until it hands back a real frame (or we give up). Guard
    // it: a target whose WMV decoder faults reports a clean FAIL instead of
    // taking the probe down (this Cxbx access-violations inside the decoder).
    XMVRESULT xr = XMV_NOFRAME;
    int gotFrame = 0;
    DWORD frameTime = 0xFFFFFFFF;
    __try
    {
        for(int i = 0; i < 800 && !gotFrame; i++)
        {
            DirectSoundDoWork();
            XMVDecoder_GetNextFrame(pDecoder, pSurface, &xr, &frameTime);
            if(xr == XMV_NEWFRAME)
            {
                gotFrame = 1;
            }
            Sleep(2);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        gotFrame = 0;   // decoder faulted -> no frame produced
    }
    xt_chk("xmv.frame_decoded", 1, gotFrame);
    xt_chk("xmv.frame_time_written", 1, gotFrame && frameTime != 0xFFFFFFFF);

    hr = XMVDecoder_Reset(pDecoder);
    xt_chk_u32("xmv.reset", S_OK, hr);

    xr = XMV_NOFRAME;
    int gotFrameAfterReset = 0;
    __try
    {
        for(int i = 0; i < 800 && !gotFrameAfterReset; i++)
        {
            DirectSoundDoWork();
            XMVDecoder_GetNextFrame(pDecoder, pSurface, &xr, NULL);
            if(xr == XMV_NEWFRAME)
            {
                gotFrameAfterReset = 1;
            }
            Sleep(2);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        gotFrameAfterReset = 0;
    }
    xt_chk("xmv.frame_after_reset", 1, gotFrameAfterReset);

    XMVDecoder_DisableAudioStream(pDecoder, 0);
    pQueriedStream = (IDirectSoundStream*)1;
    XMVDecoder_GetAudioStream(pDecoder, 0, &pQueriedStream);
    xt_chk("xmv.audio_query_written", 1,
           pQueriedStream != (IDirectSoundStream*)1);
    xt_chk("xmv.audio_retained_after_disable", 1,
           pQueriedStream == pAudioStream);
    xt_chk_u32("xmv.sync_stream_after_disable", (DWORD)-1,
               XMVDecoder_GetSynchronizationStream(pDecoder));
    if(pQueriedStream != NULL)
    {
        pQueriedStream->Release();
    }

    XMVDecoder_CloseDecoder(pDecoder);
    xt_end_and_exit();
}
