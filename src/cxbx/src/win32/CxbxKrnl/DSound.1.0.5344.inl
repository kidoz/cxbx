// DSound Version 1.0.5344 OOVPA table.
// Mirrors the 5849 DSound registration array: the 5849 signatures (both plain
// and XRef-consuming) are version-agnostic at the registration level -- the
// runtime scanner matches what it can against this version's linked dsound.lib.
OOVPATable DSound_1_0_5344[] =
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
    // CDirectSound::CreateSoundBuffer (XREF save)
    {
        (OOVPA*)&CDirectSound_CreateSoundBuffer_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::CreateSoundBuffer (XREF)"
        #endif
    },
    // DirectSoundCreateBuffer
    {
        (OOVPA*)&DirectSoundCreateBuffer_1_0_5849,
        XTL::EmuDirectSoundCreateBuffer,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreateBuffer"
        #endif
    },
    // CDirectSound::CreateSoundStream (XREF save)
    {
        (OOVPA*)&CDirectSound_CreateSoundStream_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::CreateSoundStream (XREF)"
        #endif
    },
    // DirectSoundCreateStream
    {
        (OOVPA*)&DirectSoundCreateStream_1_0_5849,
        XTL::EmuDirectSoundCreateStream,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreateStream"
        #endif
    },
    // CDirectSound::SetI3DL2Listener (XREF save)
    {
        (OOVPA*)&CDirectSound_SetI3DL2Listener_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetI3DL2Listener (XREF)"
        #endif
    },
    // IDirectSound8::SetI3DL2Listener
    {
        (OOVPA*)&IDirectSound8_SetI3DL2Listener_1_0_5849,
        XTL::EmuIDirectSound8_SetI3DL2Listener,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetI3DL2Listener"
        #endif
    },
    // CDirectSound::SetMixBinHeadroom (XREF save)
    {
        (OOVPA*)&CDirectSound_SetMixBinHeadroom_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetMixBinHeadroom (XREF)"
        #endif
    },
    // IDirectSound8::SetMixBinHeadroom
    {
        (OOVPA*)&IDirectSound8_SetMixBinHeadroom_1_0_5849,
        XTL::EmuIDirectSound8_SetMixBinHeadroom,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetMixBinHeadroom"
        #endif
    },
    // CDirectSound::SetDistanceFactor (XREF save)
    {
        (OOVPA*)&CDirectSound_SetDistanceFactor_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetDistanceFactor (XREF)"
        #endif
    },
    // IDirectSound8::SetDistanceFactor
    {
        (OOVPA*)&IDirectSound8_SetDistanceFactor_1_0_5849,
        XTL::EmuIDirectSound8_SetDistanceFactor,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetDistanceFactor"
        #endif
    },
    // CDirectSound::SetRolloffFactor (XREF save)
    {
        (OOVPA*)&CDirectSound_SetRolloffFactor_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetRolloffFactor (XREF)"
        #endif
    },
    // IDirectSound8::SetRolloffFactor
    {
        (OOVPA*)&IDirectSound8_SetRolloffFactor_1_0_5849,
        XTL::EmuIDirectSound8_SetRolloffFactor,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetRolloffFactor"
        #endif
    },
    // CDirectSound::SetDopplerFactor (XREF save)
    {
        (OOVPA*)&CDirectSound_SetDopplerFactor_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetDopplerFactor (XREF)"
        #endif
    },
    // IDirectSound8::SetDopplerFactor
    {
        (OOVPA*)&IDirectSound8_SetDopplerFactor_1_0_5849,
        XTL::EmuIDirectSound8_SetDopplerFactor,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetDopplerFactor"
        #endif
    },
    // CDirectSound::SetPosition (XREF save)
    {
        (OOVPA*)&CDirectSound_SetPosition_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetPosition (XREF)"
        #endif
    },
    // IDirectSound8::SetPosition
    {
        (OOVPA*)&IDirectSound8_SetPosition_1_0_5849,
        XTL::EmuIDirectSound8_SetPosition,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetPosition"
        #endif
    },
    // CDirectSound::SetVelocity (XREF save)
    {
        (OOVPA*)&CDirectSound_SetVelocity_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetVelocity (XREF)"
        #endif
    },
    // IDirectSound8::SetVelocity
    {
        (OOVPA*)&IDirectSound8_SetVelocity_1_0_5849,
        XTL::EmuIDirectSound8_SetVelocity,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetVelocity"
        #endif
    },
    // CDirectSound::CommitDeferredSettings internal (XREF save)
    {
        (OOVPA*)&CDirectSound_CommitDeferredSettingsI_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::CommitDeferredSettings (XREF)"
        #endif
    },
    // IDirectSound::CommitDeferredSettings
    {
        (OOVPA*)&CDirectSound_CommitDeferredSettings_1_0_5849,
        XTL::EmuCDirectSound_CommitDeferredSettings,
        #ifdef _DEBUG_TRACE
        "EmuCDirectSound_CommitDeferredSettings"
        #endif
    },
    // CDirectSoundBuffer::SetBufferData (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetBufferData_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetBufferData (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetBufferData
    {
        (OOVPA*)&IDirectSoundBuffer8_SetBufferData_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetBufferData,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetBufferData"
        #endif
    },
    // CDirectSoundBuffer::Play chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_PlayT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Play leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::Play (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_Play_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Play (XREF)"
        #endif
    },
    // IDirectSoundBuffer::Play
    {
        (OOVPA*)&IDirectSoundBuffer8_Play_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_Play,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Play"
        #endif
    },
    // CDirectSoundBuffer::Stop (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_Stop_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Stop (XREF)"
        #endif
    },
    // IDirectSoundBuffer::Stop
    {
        (OOVPA*)&IDirectSoundBuffer8_Stop_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_Stop,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Stop"
        #endif
    },
    // CDirectSoundBuffer::SetPlayRegion (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetPlayRegion_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetPlayRegion (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetPlayRegion
    {
        (OOVPA*)&IDirectSoundBuffer8_SetPlayRegion_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetPlayRegion,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetPlayRegion"
        #endif
    },
    // CDirectSoundBuffer::SetLoopRegion (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetLoopRegion_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetLoopRegion (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetLoopRegion
    {
        (OOVPA*)&IDirectSoundBuffer8_SetLoopRegion_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetLoopRegion,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetLoopRegion"
        #endif
    },
    // CDirectSoundBuffer::SetVolume chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetVolumeT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetVolume leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetVolume (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetVolume_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetVolume (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetVolume
    {
        (OOVPA*)&IDirectSoundBuffer8_SetVolume_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetVolume,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetVolume"
        #endif
    },
    // CDirectSoundBuffer::SetCurrentPosition chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetCurrentPositionT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetCurrentPosition leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetCurrentPosition (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetCurrentPosition_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetCurrentPosition (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetCurrentPosition
    {
        (OOVPA*)&IDirectSoundBuffer8_SetCurrentPosition_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetCurrentPosition,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetCurrentPosition"
        #endif
    },
    // CDirectSoundBuffer::GetCurrentPosition (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_GetCurrentPosition_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::GetCurrentPosition (XREF)"
        #endif
    },
    // IDirectSoundBuffer::GetCurrentPosition
    {
        (OOVPA*)&IDirectSoundBuffer8_GetCurrentPosition_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_GetCurrentPosition,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_GetCurrentPosition"
        #endif
    },
    // CDirectSoundBuffer::Lock (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_Lock_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Lock (XREF)"
        #endif
    },
    // IDirectSoundBuffer::Lock
    {
        (OOVPA*)&IDirectSoundBuffer8_Lock_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_Lock,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Lock"
        #endif
    },
    // CDirectSoundBuffer::SetMixBins chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetMixBinsT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetMixBins leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetMixBins (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetMixBins_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetMixBins (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetMixBins
    {
        (OOVPA*)&IDirectSoundBuffer8_SetMixBins_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetMixBins,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetMixBins"
        #endif
    },
    // CDirectSoundBuffer::GetStatus chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_GetStatusT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::GetStatus leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::GetStatus (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_GetStatus_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::GetStatus (XREF)"
        #endif
    },
    // IDirectSoundBuffer::GetStatus
    {
        (OOVPA*)&IDirectSoundBuffer8_GetStatus_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_GetStatus,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_GetStatus"
        #endif
    },
    // CDirectSound::DoWork (XREF save)
    {
        (OOVPA*)&CDirectSound_DoWork_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::DoWork (XREF)"
        #endif
    },
    // DirectSoundDoWork
    {
        (OOVPA*)&DirectSoundDoWork_1_0_5849,
        XTL::EmuDirectSoundDoWork,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundDoWork"
        #endif
    },
    // CDirectSoundStream::FlushEx (XREF save)
    {
        (OOVPA*)&CDirectSoundStream_FlushExI_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundStream::FlushEx (XREF)"
        #endif
    },
    // IDirectSoundStream::FlushEx
    {
        (OOVPA*)&IDirectSoundStream_FlushEx_1_0_5849,
        XTL::EmuIDirectSoundStream_FlushEx,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_FlushEx"
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

uint32 DSound_1_0_5344_SIZE = sizeof(DSound_1_0_5344);
