// ds_status -- DSOUND HLE buffer state-machine conformance: the GetStatus
// playing/looping/stopped contract and the Lock/Unlock dynamic-write path.
//
// ds_buffer covers the static-buffer cursor-advance-on-Play path; this probe
// isolates the buffer *state* transitions titles query to drive their audio
// logic (GetStatus before/after Play/Stop, looping vs non-looping), plus a
// Lock/write/Unlock/re-Lock readback proving the host buffer persists
// dynamically-written PCM (the streaming-audio primitive).
//
// Every call runs genuine XDK 5849 dsound.lib code until the HLE patch
// (direct or XRef-chained signature) redirects it to the host implementation.
// The buffer methods are the XRef-chained hooks.
#include "xdk_xtrace.h"

#define BUF_BYTES 32768u

static short g_pcm[BUF_BYTES / 2];

void __cdecl main()
{
    xt_begin("ds_status");

    LPDIRECTSOUND pDS = NULL;
    HRESULT hr = DirectSoundCreate(NULL, &pDS, NULL);
    xt_chk("ds.create_hr", 1, SUCCEEDED(hr) && pDS != NULL);
    if(FAILED(hr) || pDS == NULL)
        xt_end_and_exit();

    // 22050 Hz 16-bit mono PCM, a 440 Hz-ish square wave (same as ds_buffer).
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
    if(FAILED(hr) || pBuf == NULL)
        xt_end_and_exit();

    for(int i = 0; i < (int)(BUF_BYTES / 2); i++)
        g_pcm[i] = ((i / 25) & 1) ? 6000 : -6000;

    xt_chk("ds.setdata_hr", 1,
           SUCCEEDED(IDirectSoundBuffer_SetBufferData(pBuf, g_pcm, BUF_BYTES)));

    // --- Block A: a freshly created buffer is not playing -----------------
    DWORD status0 = 0xFFFFFFFF;
    hr = IDirectSoundBuffer_GetStatus(pBuf, &status0);
    xt_chk("ds.status0_hr", 1, SUCCEEDED(hr));
    xt_emitf("EV   ds.status0 = 0x%08X", status0);
    xt_chk_u32("ds.status0_stopped", 0u, status0 & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING));

    // --- Block B: looping play sets PLAYING + LOOPING ---------------------
    hr = IDirectSoundBuffer_Play(pBuf, 0, 0, DSBPLAY_LOOPING);
    xt_chk("ds.play_loop_hr", 1, SUCCEEDED(hr));

    DWORD statusL = 0;
    IDirectSoundBuffer_GetStatus(pBuf, &statusL);
    xt_emitf("EV   ds.status_looping = 0x%08X", statusL);
    xt_chk_u32("ds.status_looping_bits", (DWORD)(DSBSTATUS_PLAYING | DSBSTATUS_LOOPING),
               statusL & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING));

    // --- Block C: Stop clears the state -----------------------------------
    hr = IDirectSoundBuffer_Stop(pBuf);
    xt_chk("ds.stop_hr", 1, SUCCEEDED(hr));

    DWORD statusS = 0xFFFFFFFF;
    IDirectSoundBuffer_GetStatus(pBuf, &statusS);
    xt_emitf("EV   ds.status_stopped = 0x%08X", statusS);
    xt_chk_u32("ds.status_after_stop", 0u, statusS & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING));

    // --- Block D: non-looping play sets PLAYING only ----------------------
    // The HLE Play wrapper accepts only DSBPLAY_LOOPING or 0; any other flag
    // triggers EmuCleanup. Non-looping play is flags == 0.
    hr = IDirectSoundBuffer_Play(pBuf, 0, 0, 0);
    xt_chk("ds.play_once_hr", 1, SUCCEEDED(hr));

    DWORD statusP = 0;
    IDirectSoundBuffer_GetStatus(pBuf, &statusP);
    xt_emitf("EV   ds.status_once = 0x%08X", statusP);
    xt_chk("ds.status_once_playing", 1, (statusP & DSBSTATUS_PLAYING) != 0);
    xt_chk("ds.status_once_not_looping", 0, (statusP & DSBSTATUS_LOOPING) != 0);

    IDirectSoundBuffer_Stop(pBuf);

    // --- Block E: Lock/write/Unlock/re-Lock readback ----------------------
    // Lock the whole buffer, fill it with a known byte pattern, unlock, then
    // re-lock and verify the bytes persisted -- the host buffer must hold
    // dynamically-written PCM (the streaming-audio primitive).
    VOID *p1a = NULL, *p2a = NULL;
    DWORD cb1a = 0, cb2a = 0;
    hr = IDirectSoundBuffer_Lock(pBuf, 0, BUF_BYTES, &p1a, &cb1a, &p2a, &cb2a, DSBLOCK_ENTIREBUFFER);
    xt_chk("ds.lock_hr", 1, SUCCEEDED(hr) && p1a != NULL);
    if(SUCCEEDED(hr) && p1a != NULL)
    {
        // Fill the primary locked region with a distinct pattern; ignore the
        // (usually empty) wrap region p2a.
        memset(p1a, 0xAA, cb1a);
        xt_emitf("EV   ds.lock1 cb1=%u cb2=%u", cb1a, cb2a);
    }
    hr = IDirectSoundBuffer_Unlock(pBuf, p1a, cb1a, p2a, cb2a);
    xt_chk("ds.unlock_hr", 1, SUCCEEDED(hr));

    // Re-lock and verify the pattern persisted.
    VOID *p1b = NULL, *p2b = NULL;
    DWORD cb1b = 0, cb2b = 0;
    hr = IDirectSoundBuffer_Lock(pBuf, 0, BUF_BYTES, &p1b, &cb1b, &p2b, &cb2b, DSBLOCK_ENTIREBUFFER);
    xt_chk("ds.relock_hr", 1, SUCCEEDED(hr) && p1b != NULL);
    if(SUCCEEDED(hr) && p1b != NULL)
    {
        int mismatches = 0;
        unsigned char *pb = (unsigned char *)p1b;
        DWORD check = cb1b > 256 ? 256 : cb1b;
        for(DWORD k = 0; k < check; k++)
            if(pb[k] != 0xAA)
                mismatches++;
        xt_emitf("EV   ds.readback checked=%u mismatches=%d", check, mismatches);
        xt_chk("ds.readback_persisted", 0, mismatches);
        IDirectSoundBuffer_Unlock(pBuf, p1b, cb1b, p2b, cb2b);
    }

    xt_end_and_exit();
}
