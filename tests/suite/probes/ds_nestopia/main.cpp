// ds_nestopia -- replicates NestopiaX's audio lifecycle API-for-API, taken
// from its released source (other/source/NestopiaX_1.0_Source/soundNES.cpp):
//
//   DirectSoundCreate                                  (DirectSoundInit)
//   CreateSoundBuffer  8 KiB "primary"                 (DxSoundInit)
//   CreateSoundBuffer  segmented loop buffer           (DxSoundInit)
//   SetMixBins         8-pair surround matrix          (DxSoundInit)
//   Lock/zero/Unlock   whole loop buffer               (DxBlankSound)
//   SetVolume + Play(DSBPLAY_LOOPING)                  (DxSoundPlay)
//   ring updates: GetCurrentPosition -> Lock segment,
//                 write PCM, Unlock                    (DxSoundCheck)
//   GetStatus, Stop                                    (DxSoundStop)
//
// This is the exact call sequence the NestopiaX 1.3 binary drives its menu
// audio with -- the subsystem it currently crashes in when run un-HLE'd.
// Green here means the 5849 HLE covers the full pattern coherently
// (IDirectSoundBuffer_Unlock intentionally runs the guest's own call-free
// no-op; see hle_resolve).
#include "xdk_xtrace.h"

// soundNES.cpp: 60 fps NTSC path, 44.1 kHz stereo 16-bit.
#define SAMPLE_RATE 44100
#define SEG_LEN     735            // (44100*100 + 3000) / 6000
#define SEG_COUNT   6
#define LOOP_BYTES  (SEG_LEN * SEG_COUNT * 4)

void __cdecl main()
{
    xt_begin("ds_nestopia");

    // DirectSoundInit()
    LPDIRECTSOUND pDS = NULL;
    HRESULT hr = DirectSoundCreate(NULL, &pDS, NULL);
    xt_chk("nx.create_hr", 1, SUCCEEDED(hr) && pDS != NULL);
    if (FAILED(hr) || pDS == NULL)
        xt_end_and_exit();

    // DxSoundInit(): format shared by both buffers.
    WAVEFORMATEX wfx;
    ZeroMemory(&wfx, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 2;
    wfx.nSamplesPerSec  = SAMPLE_RATE;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = 4;
    wfx.nAvgBytesPerSec = SAMPLE_RATE * 4;

    DSBUFFERDESC dsbd;
    ZeroMemory(&dsbd, sizeof(dsbd));
    dsbd.dwSize        = sizeof(dsbd);
    dsbd.dwBufferBytes = 1024 * 8;
    dsbd.lpwfxFormat   = &wfx;
    LPDIRECTSOUNDBUFFER pPrim = NULL;
    hr = IDirectSound_CreateSoundBuffer(pDS, &dsbd, &pPrim, NULL);
    xt_chk("nx.prim_ok", 1, SUCCEEDED(hr) && pPrim != NULL);

    ZeroMemory(&dsbd, sizeof(dsbd));
    dsbd.dwSize        = sizeof(dsbd);
    dsbd.dwBufferBytes = LOOP_BYTES;
    dsbd.lpwfxFormat   = &wfx;
    LPDIRECTSOUNDBUFFER pLoop = NULL;
    hr = IDirectSound_CreateSoundBuffer(pDS, &dsbd, &pLoop, NULL);
    xt_chk("nx.loop_ok", 1, SUCCEEDED(hr) && pLoop != NULL);
    if (FAILED(hr) || pLoop == NULL)
        xt_end_and_exit();

    // SurroundSound path: 8-pair mix-bin matrix.
    DSMIXBINVOLUMEPAIR dsmbvp[8] = {
        { DSMIXBIN_FRONT_LEFT, DSBVOLUME_MAX },
        { DSMIXBIN_FRONT_RIGHT, DSBVOLUME_MAX },
        { DSMIXBIN_FRONT_CENTER, DSBVOLUME_MAX },
        { DSMIXBIN_FRONT_CENTER, DSBVOLUME_MAX },
        { DSMIXBIN_BACK_LEFT, DSBVOLUME_MAX },
        { DSMIXBIN_BACK_RIGHT, DSBVOLUME_MAX },
        { DSMIXBIN_LOW_FREQUENCY, DSBVOLUME_MAX },
        { DSMIXBIN_LOW_FREQUENCY, DSBVOLUME_MAX } };
    DSMIXBINS dsmb;
    dsmb.dwMixBinCount = 8;
    dsmb.lpMixBinVolumePairs = dsmbvp;
    xt_chk("nx.setmixbins_hr", 1,
           SUCCEEDED(IDirectSoundBuffer_SetMixBins(pLoop, &dsmb)));

    // DxBlankSound(): zero the whole loop buffer through Lock/Unlock.
    LPVOID p1 = NULL, p2 = NULL;
    DWORD cb1 = 0, cb2 = 0;
    hr = IDirectSoundBuffer_Lock(pLoop, 0, LOOP_BYTES, &p1, &cb1, &p2, &cb2, 0);
    xt_chk("nx.blank_lock_ok", 1, SUCCEEDED(hr) && p1 != NULL && cb1 != 0);
    if (SUCCEEDED(hr)) {
        if (p1) memset(p1, 0, cb1);
        if (p2) memset(p2, 0, cb2);
        IDirectSoundBuffer_Unlock(pLoop, p1, cb1, p2, cb2);
    }
    xt_chk("nx.blank_unlock_survived", 1, 1);

    // DxSoundPlay()
    IDirectSoundBuffer_SetVolume(pLoop, 0);
    xt_chk("nx.play_hr", 1,
           SUCCEEDED(IDirectSoundBuffer_Play(pLoop, 0, 0, DSBPLAY_LOOPING)));

    // DxSoundCheck(): three ring updates -- read the play cursor, fill the
    // next segment behind it.
    int ring_locks = 0;
    DWORD lastPlay = 0xFFFFFFFF;
    int moved = 0;
    for (int i = 0; i < 3; i++) {
        Sleep(60);
        DWORD play = 0, write = 0;
        if (FAILED(IDirectSoundBuffer_GetCurrentPosition(pLoop, &play, &write)))
            break;
        if (lastPlay != 0xFFFFFFFF && play != lastPlay)
            moved++;
        lastPlay = play;

        int seg = (int)(play / (SEG_LEN * 4));
        int next = (seg + 2) % SEG_COUNT;
        p1 = p2 = NULL; cb1 = cb2 = 0;
        hr = IDirectSoundBuffer_Lock(pLoop, next * SEG_LEN * 4, SEG_LEN * 4,
                                     &p1, &cb1, &p2, &cb2, 0);
        if (SUCCEEDED(hr) && p1 != NULL) {
            short *dst = (short *)p1;
            for (DWORD s = 0; s < cb1 / 2; s++)
                dst[s] = ((s / 25) & 1) ? 5000 : -5000;
            IDirectSoundBuffer_Unlock(pLoop, p1, cb1, p2, cb2);
            ring_locks++;
        }
    }
    xt_emitf("EV   nx.ring locks=%d moved=%d lastPlay=%u", ring_locks, moved, lastPlay);
    xt_chk("nx.ring_locks", 3, ring_locks);
    xt_chk("nx.cursor_moving", 1, moved >= 1);

    // Status + DxSoundStop()
    DWORD status = 0;
    hr = IDirectSoundBuffer_GetStatus(pLoop, &status);
    xt_chk("nx.status_playing", 1,
           SUCCEEDED(hr) && (status & DSBSTATUS_PLAYING) != 0);
    xt_chk("nx.stop_hr", 1, SUCCEEDED(IDirectSoundBuffer_Stop(pLoop)));

    xt_end_and_exit();
}
