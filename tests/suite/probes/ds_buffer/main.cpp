// ds_buffer -- end-to-end DSOUND HLE static-buffer path on the HOST audio
// device: DirectSoundCreate -> CreateSoundBuffer -> SetBufferData (the
// title's PCM actually reaches the host buffer) -> Play (looping) -> the
// play cursor advances in real time -> Stop.
//
// Every call below runs genuine XDK 5849 dsound.lib code until the HLE patch
// (direct or XRef-chained signature) redirects it to the host implementation.
// The buffer methods are the XRef-chained hooks (IDirectSoundBuffer_* thin
// wrappers discriminated by their CDirectSoundBuffer/CMcpxBuffer callees).
#include "xdk_xtrace.h"

#define BUF_BYTES 32768u

static short g_pcm[BUF_BYTES / 2];

void __cdecl main()
{
    xt_begin("ds_buffer");

    LPDIRECTSOUND pDS = NULL;
    HRESULT hr = DirectSoundCreate(NULL, &pDS, NULL);
    xt_chk("ds.create_hr", 1, SUCCEEDED(hr) && pDS != NULL);
    if (FAILED(hr) || pDS == NULL)
        xt_end_and_exit();

    // 22050 Hz 16-bit mono PCM, a 440 Hz-ish square wave.
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 1;
    wfx.nSamplesPerSec  = 22050;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = 2;
    wfx.nAvgBytesPerSec = 22050 * 2;

    DSBUFFERDESC dsbd;
    ZeroMemory(&dsbd, sizeof(dsbd));
    dsbd.dwSize        = sizeof(dsbd);
    dsbd.dwFlags       = 0;
    dsbd.dwBufferBytes = BUF_BYTES;
    dsbd.lpwfxFormat   = &wfx;

    LPDIRECTSOUNDBUFFER pBuf = NULL;
    hr = IDirectSound_CreateSoundBuffer(pDS, &dsbd, &pBuf, NULL);
    xt_chk("ds.buffer_ok", 1, SUCCEEDED(hr) && pBuf != NULL);
    if (FAILED(hr) || pBuf == NULL)
        xt_end_and_exit();

    for (int i = 0; i < (int)(BUF_BYTES / 2); i++)
        g_pcm[i] = ((i / 25) & 1) ? 6000 : -6000;

    xt_chk("ds.setdata_hr", 1,
           SUCCEEDED(IDirectSoundBuffer_SetBufferData(pBuf, g_pcm, BUF_BYTES)));
    xt_chk("ds.setvolume_hr", 1,
           SUCCEEDED(IDirectSoundBuffer_SetVolume(pBuf, 0)));

    xt_chk("ds.play_hr", 1,
           SUCCEEDED(IDirectSoundBuffer_Play(pBuf, 0, 0, DSBPLAY_LOOPING)));

    Sleep(250);
    DWORD play1 = 0xFFFFFFFF, write1 = 0;
    hr = IDirectSoundBuffer_GetCurrentPosition(pBuf, &play1, &write1);
    xt_chk("ds.pos1_hr", 1, SUCCEEDED(hr));
    xt_emitf("EV   ds.pos1 play=%u write=%u", play1, write1);
    // ~250 ms at 44100 bytes/s should be well inside the 32 KiB loop and
    // clearly nonzero.
    xt_chk("ds.pos_advanced", 1, play1 != 0 && play1 != 0xFFFFFFFF);

    Sleep(150);
    DWORD play2 = play1, write2 = 0;
    IDirectSoundBuffer_GetCurrentPosition(pBuf, &play2, &write2);
    xt_emitf("EV   ds.pos2 play=%u write=%u", play2, write2);
    xt_chk("ds.pos_moving", 1, play2 != play1);

    xt_chk("ds.stop_hr", 1, SUCCEEDED(IDirectSoundBuffer_Stop(pBuf)));

    xt_end_and_exit();
}
