// DSound Version 1.0.5659 OOVPA table.
// Generated from XDK 5659 dsound.lib via reuse-then-generate.
// 9 reused, 0 fresh, 22 skipped.

OOVPATable DSound_1_0_5659[] =
{
    // DirectSoundCreate
    {
        (OOVPA*)&DirectSoundCreate_1_0_5849,
        XTL::EmuDirectSoundCreate,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreate"
        #endif
    },
    // IDirectSound8::Release
    {
        (OOVPA*)&IDirectSound_Release_1_0_5849,
        XTL::EmuIDirectSound8_Release,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_Release"
        #endif
    },
    // IDirectSound8::SynchPlayback
    {
        (OOVPA*)&IDirectSound_SynchPlayback_1_0_5849,
        XTL::EmuIDirectSound8_SynchPlayback,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SynchPlayback"
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
    // IDirectSound8::SetOrientation
    {
        (OOVPA*)&IDirectSound8_SetOrientation_1_0_5849,
        XTL::EmuIDirectSound8_SetOrientation,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetOrientation"
        #endif
    },
    // IDirectSound8::DownloadEffectsImage
    {
        (OOVPA*)&IDirectSound8_DownloadEffectsImage_1_0_5849,
        XTL::EmuIDirectSound8_DownloadEffectsImage,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_DownloadEffectsImage"
        #endif
    },
    // DirectSoundDoWork
    {
        (OOVPA*)&DirectSoundDoWork_1_0_4627,
        XTL::EmuDirectSoundDoWork,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundDoWork"
        #endif
    },
    // XAudioDownloadEffectsImage
    {
        (OOVPA*)&XAudioDownloadEffectsImage_1_0_5849,
        XTL::EmuXAudioDownloadEffectsImage,
        #ifdef _DEBUG_TRACE
        "EmuXAudioDownloadEffectsImage"
        #endif
    },
    // DirectSoundUseFullHRTF
    {
        (OOVPA*)&DirectSoundUseFullHRTF_1_0_5849,
        XTL::EmuDirectSoundUseFullHRTF,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundUseFullHRTF"
        #endif
    },
};

uint32 DSound_1_0_5659_SIZE = sizeof(DSound_1_0_5659);
