// DSound Version 1.0.5344 OOVPA table.
// Generated from XDK 5344 dsound.lib via reuse-then-generate.
// 7 reused, 1 fresh, 24 skipped.

// Fresh signature definitions
SOOVPA<16> IDirectSound_GetCaps_1_0_5344 =
{
    0, 16, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x01, 0x44 },
        { 0x02, 0x24 },
        { 0x03, 0x04 },
        { 0x05, 0x74 },
        { 0x07, 0x08 },
        { 0x09, 0xC8 },
        { 0x0A, 0x83 },
        { 0x0C, 0xF8 },
        { 0x0E, 0xD9 },
        { 0x10, 0xC9 },
        { 0x12, 0xC8 },
        { 0x13, 0x51 },
        { 0x14, 0xE8 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};


OOVPATable DSound_1_0_5344[] =
{
    // DirectSoundCreate (* unchanged since 4361 *)
    {
        (OOVPA*)&DirectSoundCreate_1_0_5849,
        XTL::EmuDirectSoundCreate,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreate"
        #endif
    },
    // IDirectSound8::CreateSoundBuffer
    {
        (OOVPA*)&IDirectSound8_CreateSoundBuffer_1_0_5849,
        XTL::EmuIDirectSound8_CreateSoundBuffer,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_CreateSoundBuffer"
        #endif
    },
    // IDirectSound8::DownloadEffectsImage (* unchanged since 3936 *)
    {
        (OOVPA*)&IDirectSound8_DownloadEffectsImage_1_0_5849,
        XTL::EmuIDirectSound8_DownloadEffectsImage,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_DownloadEffectsImage"
        #endif
    },
    // IDirectSound8::SetOrientation (* unchanged since 3936 *)
    {
        (OOVPA*)&IDirectSound8_SetOrientation_1_0_5849,
        XTL::EmuIDirectSound8_SetOrientation,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetOrientation"
        #endif
    },
    // DirectSoundDoWork_1_0_4627
    {
        (OOVPA*)&DirectSoundDoWork_1_0_4627,
        XTL::EmuDirectSoundDoWork,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundDoWork"
        #endif
    },
    // IDirectSoundBuffer_Release_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_Release_1_0_4627,
        XTL::EmuIDirectSoundBuffer8_Release,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Release"
        #endif
    },
    // IDirectSoundBuffer_Lock_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_Lock_1_0_4627,
        XTL::EmuIDirectSoundBuffer8_Lock,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Lock"
        #endif
    },
    // IDirectSound_GetCaps_1_0_4627
    {
        (OOVPA*)&IDirectSound_GetCaps_1_0_5344,
        XTL::EmuIDirectSound8_GetCaps,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_GetCaps"
        #endif
    },
};

uint32 DSound_1_0_5344_SIZE = sizeof(DSound_1_0_5344);
