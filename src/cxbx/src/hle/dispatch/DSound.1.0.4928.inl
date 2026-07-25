// XDK 4928 DirectSound public entry points verified against a retail image.
//
// These image-derived signatures intentionally cover the complete public
// device, static-buffer, and stream boundary present in the image. Some compact
// wrappers require forward-looking pairs for uniqueness, so keep the table in
// ascending image-address order: later bodies must still be unpatched when an
// earlier signature is located.

SOOVPA<8> DirectSoundCreate_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x24, 0x08 }, { 0x48, 0x8B }, { 0x6D, 0x00 }, { 0x8A, 0x68 }, { 0xB5, 0x15 }, { 0xDA, 0xE1 }, { 0xF8, 0x25 } } };
SOOVPA<8> DirectSoundCreateStream_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x24, 0x10 }, { 0x43, 0x68 }, { 0x6D, 0xFF }, { 0x91, 0xCA }, { 0xB9, 0x24 }, { 0xD5, 0x15 }, { 0xFF, 0x8B } } };
SOOVPA<8> DirectSoundDoWork_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x56 }, { 0x1D, 0x68 }, { 0x48, 0xE8 }, { 0x73, 0x56 }, { 0x8D, 0xE8 }, { 0xAF, 0x68 }, { 0xDC, 0xFB }, { 0xFF, 0xC5 } } };
SOOVPA<8> IDirectSound8_CommitDeferredSettings_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0xE4 }, { 0x48, 0xC9 }, { 0x6D, 0x23 }, { 0x91, 0x23 }, { 0xB6, 0xE4 }, { 0xDA, 0x8B }, { 0xFF, 0xFE } } };
SOOVPA<8> IDirectSound8_CreateSoundBuffer_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xFF }, { 0x24, 0x55 }, { 0x48, 0x89 }, { 0x6C, 0xE8 }, { 0x91, 0x0F }, { 0xB6, 0x6A }, { 0xDA, 0x00 }, { 0xFF, 0x75 } } };
SOOVPA<8> IDirectSound8_CreateSoundStream_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xFF }, { 0x24, 0x53 }, { 0x4F, 0x24 }, { 0x6D, 0xF6 }, { 0x91, 0xC7 }, { 0xBB, 0x5F }, { 0xDA, 0x00 }, { 0xFF, 0x55 } } };
SOOVPA<8> IDirectSound8_DownloadEffectsImage_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x26, 0x00 }, { 0x48, 0xC8 }, { 0x6D, 0xC8 }, { 0x91, 0xC8 }, { 0xB6, 0x04 }, { 0xDA, 0xC0 }, { 0xEB, 0xE9 } } };
SOOVPA<8> IDirectSound8_Release_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0x08 }, { 0x48, 0x68 }, { 0x6D, 0xEB }, { 0x91, 0x4D }, { 0xB6, 0xC8 }, { 0xDB, 0x89 }, { 0xF3, 0x68 } } };
SOOVPA<8> IDirectSound8_SetDopplerFactor_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xFF }, { 0x24, 0x55 }, { 0x48, 0x1B }, { 0x6D, 0x00 }, { 0x91, 0x0C }, { 0xB6, 0xC0 }, { 0xDA, 0x8B }, { 0xFF, 0x04 } } };
SOOVPA<8> IDirectSound8_SetI3DL2Listener_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0xFF }, { 0x4A, 0x3D }, { 0x6C, 0xE8 }, { 0x8B, 0xE6 }, { 0xB6, 0x57 }, { 0xDC, 0x08 }, { 0xF4, 0x06 } } };
SOOVPA<8> IDirectSound8_SetOrientation_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x24, 0x1B }, { 0x49, 0x00 }, { 0x6D, 0x0C }, { 0x91, 0x83 }, { 0xB6, 0x8B }, { 0xDA, 0x24 }, { 0xFF, 0x0C } } };
SOOVPA<8> IDirectSound8_SetPosition_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x24, 0x1B }, { 0x48, 0xC0 }, { 0x6D, 0xC8 }, { 0x91, 0x04 }, { 0xB6, 0x8B }, { 0xD8, 0x3D }, { 0xFA, 0xE8 } } };
SOOVPA<8> IDirectSound8_SetRolloffFactor_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xFF }, { 0x24, 0x55 }, { 0x48, 0x1B }, { 0x6D, 0xC9 }, { 0x91, 0xE8 }, { 0xB1, 0x68 }, { 0xDD, 0x8B }, { 0xFF, 0x7C } } };
SOOVPA<8> IDirectSound8_SetVelocity_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x24, 0x1B }, { 0x48, 0x1B }, { 0x6D, 0xE8 }, { 0x8D, 0x68 }, { 0xB9, 0x8B }, { 0xDA, 0xF6 }, { 0xF3, 0x68 } } };
SOOVPA<8> IDirectSound8_SynchPlayback_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0xE4 }, { 0x48, 0xE8 }, { 0x6C, 0xE8 }, { 0x91, 0x74 }, { 0xB6, 0xD9 }, { 0xDC, 0xA1 }, { 0xFF, 0xE8 } } };
SOOVPA<8> IDirectSoundBuffer_GetStatus_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0x8B }, { 0x50, 0xA1 }, { 0x72, 0xF1 }, { 0x8F, 0xE8 }, { 0xBA, 0x56 }, { 0xDC, 0x47 }, { 0xFF, 0x1A } } };
SOOVPA<8> IDirectSoundBuffer_Play_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xFF }, { 0x24, 0x8B }, { 0x48, 0xE4 }, { 0x6C, 0xE8 }, { 0x94, 0x06 }, { 0xB7, 0x08 }, { 0xDA, 0x89 }, { 0xFF, 0x0D } } };
SOOVPA<8> IDirectSoundBuffer_Release_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x25, 0xF0 }, { 0x46, 0xE8 }, { 0x72, 0x24 }, { 0x91, 0x8B }, { 0xB6, 0x00 }, { 0xDA, 0xC9 }, { 0xFF, 0xD8 } } };
SOOVPA<8> IDirectSoundBuffer_SetBufferData_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x22, 0xE8 }, { 0x4B, 0x28 }, { 0x6B, 0xE6 }, { 0x91, 0xFF }, { 0xB4, 0xE8 }, { 0xD4, 0x06 }, { 0xF9, 0xA9 } } };
SOOVPA<8> IDirectSoundBuffer_SetConeOutsideVolume_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0xD9 }, { 0x48, 0xD9 }, { 0x6D, 0x18 }, { 0x91, 0x1C }, { 0xB6, 0xC0 }, { 0xDA, 0xC7 }, { 0xFF, 0xC8 } } };
SOOVPA<8> IDirectSoundBuffer_SetCurrentPosition_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x1C, 0xE9 }, { 0x48, 0x68 }, { 0x6D, 0x04 }, { 0x8B, 0x68 }, { 0xB6, 0xEC }, { 0xDA, 0x68 }, { 0xFF, 0xE8 } } };
SOOVPA<8> IDirectSoundBuffer_SetFrequency_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0x8B }, { 0x48, 0x51 }, { 0x6D, 0x8B }, { 0x91, 0xEC }, { 0xB8, 0x00 }, { 0xDA, 0xD9 }, { 0xFF, 0x44 } } };
SOOVPA<8> IDirectSoundBuffer_SetHeadroom_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0xFF }, { 0x48, 0xE4 }, { 0x6C, 0xE8 }, { 0x90, 0xE9 }, { 0xB2, 0xE8 }, { 0xD8, 0xE8 }, { 0xFF, 0x68 } } };
SOOVPA<8> IDirectSoundBuffer_SetMaxDistance_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xFF }, { 0x24, 0xFF }, { 0x48, 0x55 }, { 0x6D, 0xC9 }, { 0x91, 0xC8 }, { 0xB6, 0xD9 }, { 0xD7, 0xBE }, { 0xFF, 0x6A } } };
SOOVPA<8> IDirectSoundBuffer_SetMinDistance_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xFF }, { 0x24, 0x55 }, { 0x48, 0x1B }, { 0x6D, 0xC8 }, { 0x91, 0x8B }, { 0xB3, 0xBE }, { 0xDB, 0x6A }, { 0xFF, 0x44 } } };
SOOVPA<8> IDirectSoundBuffer_SetPosition_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x24, 0x1B }, { 0x48, 0x8B }, { 0x6D, 0x8B }, { 0x8F, 0xBE }, { 0xB7, 0x6A }, { 0xDA, 0xF6 }, { 0xFB, 0xE8 } } };
SOOVPA<8> IDirectSoundBuffer_SetVelocity_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x55 }, { 0x24, 0x1B }, { 0x45, 0x03 }, { 0x6D, 0x00 }, { 0x91, 0xAA }, { 0xB6, 0xE8 }, { 0xDA, 0x85 }, { 0xF9, 0xE8 } } };
SOOVPA<8> IDirectSoundBuffer_SetVolume_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0x8B }, { 0x48, 0x24 }, { 0x6C, 0xE8 }, { 0x91, 0x44 }, { 0xAC, 0xE9 }, { 0xD8, 0x68 }, { 0xFF, 0x55 } } };
SOOVPA<8> IDirectSoundBuffer_Stop_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0x8B }, { 0x24, 0xE4 }, { 0x48, 0xE8 }, { 0x70, 0x06 }, { 0x93, 0x08 }, { 0xB6, 0x89 }, { 0xDA, 0xE8 }, { 0xFF, 0x21 } } };
SOOVPA<8> IDirectSoundStream_Pause_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xE9 }, { 0x22, 0x68 }, { 0x48, 0x00 }, { 0x65, 0x68 }, { 0x91, 0x50 }, { 0xB4, 0x68 }, { 0xD9, 0xE8 }, { 0xFF, 0x26 } } };
SOOVPA<8> IDirectSoundStream_SetHeadroom_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xE9 }, { 0x25, 0x74 }, { 0x4B, 0xC2 }, { 0x6A, 0x68 }, { 0x91, 0xEB }, { 0xB6, 0x5B }, { 0xDA, 0x0C }, { 0xFF, 0xF8 } } };
SOOVPA<8> IDirectSoundStream_SetVolume_1_0_4928 = { 0, 8, -1, 0, { { 0x00, 0xE9 }, { 0x22, 0xE8 }, { 0x48, 0xE8 }, { 0x6D, 0x74 }, { 0x92, 0x47 }, { 0xB6, 0x7D }, { 0xDA, 0x08 }, { 0xFF, 0x89 } } };

#ifdef _DEBUG_TRACE
#define DSOUND_4928_TRACE_NAME(Name) , #Name
#else
#define DSOUND_4928_TRACE_NAME(Name)
#endif

OOVPATable DSound_1_0_4928[] = {
    { (OOVPA*)&IDirectSound8_Release_1_0_4928, XTL::EmuIDirectSound8_Release DSOUND_4928_TRACE_NAME(EmuIDirectSound8_Release) },
    { (OOVPA*)&IDirectSoundBuffer_Release_1_0_4928, XTL::EmuIDirectSoundBuffer8_Release DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_Release) },
    { (OOVPA*)&IDirectSound8_DownloadEffectsImage_1_0_4928, XTL::EmuIDirectSound8_DownloadEffectsImage DSOUND_4928_TRACE_NAME(EmuIDirectSound8_DownloadEffectsImage) },
    { (OOVPA*)&IDirectSound8_SynchPlayback_1_0_4928, XTL::EmuIDirectSound8_SynchPlayback DSOUND_4928_TRACE_NAME(EmuIDirectSound8_SynchPlayback) },
    { (OOVPA*)&IDirectSoundBuffer_SetVolume_1_0_4928, XTL::EmuIDirectSoundBuffer8_SetVolume DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_SetVolume) },
    { (OOVPA*)&IDirectSoundBuffer_SetHeadroom_1_0_4928, XTL::EmuIDirectSoundBuffer8_SetHeadroom DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_SetHeadroom) },
    { (OOVPA*)&IDirectSoundBuffer_Play_1_0_4928, XTL::EmuIDirectSoundBuffer8_Play DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_Play) },
    { (OOVPA*)&IDirectSoundBuffer_Stop_1_0_4928, XTL::EmuIDirectSoundBuffer8_Stop DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_Stop) },
    { (OOVPA*)&IDirectSoundBuffer_GetStatus_1_0_4928, XTL::EmuIDirectSoundBuffer8_GetStatus DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_GetStatus) },
    { (OOVPA*)&IDirectSoundBuffer_SetCurrentPosition_1_0_4928, XTL::EmuIDirectSoundBuffer8_SetCurrentPosition DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_SetCurrentPosition) },
    { (OOVPA*)&IDirectSoundStream_SetVolume_1_0_4928, XTL::EmuCDirectSoundStream_SetVolume DSOUND_4928_TRACE_NAME(EmuCDirectSoundStream_SetVolume) },
    { (OOVPA*)&IDirectSoundStream_SetHeadroom_1_0_4928, XTL::EmuIDirectSoundStream_SetHeadroom DSOUND_4928_TRACE_NAME(EmuIDirectSoundStream_SetHeadroom) },
    { (OOVPA*)&IDirectSoundStream_Pause_1_0_4928, XTL::EmuCDirectSoundStream_Pause DSOUND_4928_TRACE_NAME(EmuCDirectSoundStream_Pause) },
    { (OOVPA*)&DirectSoundDoWork_1_0_4928, XTL::EmuDirectSoundDoWork DSOUND_4928_TRACE_NAME(EmuDirectSoundDoWork) },
    { (OOVPA*)&IDirectSound8_CommitDeferredSettings_1_0_4928, XTL::EmuCDirectSound_CommitDeferredSettings DSOUND_4928_TRACE_NAME(EmuCDirectSound_CommitDeferredSettings) },
    { (OOVPA*)&IDirectSoundBuffer_SetFrequency_1_0_4928, XTL::EmuIDirectSoundBuffer8_SetFrequency DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_SetFrequency) },
    { (OOVPA*)&IDirectSoundBuffer_SetConeOutsideVolume_1_0_4928, XTL::EmuCDirectSoundBuffer_SetDeferred3dParam DSOUND_4928_TRACE_NAME(EmuCDirectSoundBuffer_SetDeferred3dParam) },
    { (OOVPA*)&IDirectSoundBuffer_SetMaxDistance_1_0_4928, XTL::EmuCDirectSoundBuffer_SetDeferred3dParam DSOUND_4928_TRACE_NAME(EmuCDirectSoundBuffer_SetDeferred3dParam) },
    { (OOVPA*)&IDirectSoundBuffer_SetMinDistance_1_0_4928, XTL::EmuCDirectSoundBuffer_SetDeferred3dParam DSOUND_4928_TRACE_NAME(EmuCDirectSoundBuffer_SetDeferred3dParam) },
    { (OOVPA*)&IDirectSoundBuffer_SetPosition_1_0_4928, XTL::EmuCDirectSoundBuffer_SetDeferred3dVector DSOUND_4928_TRACE_NAME(EmuCDirectSoundBuffer_SetDeferred3dVector) },
    { (OOVPA*)&IDirectSoundBuffer_SetVelocity_1_0_4928, XTL::EmuCDirectSoundBuffer_SetDeferred3dVector DSOUND_4928_TRACE_NAME(EmuCDirectSoundBuffer_SetDeferred3dVector) },
    { (OOVPA*)&IDirectSound8_SetDopplerFactor_1_0_4928, XTL::EmuIDirectSound8_SetDopplerFactor DSOUND_4928_TRACE_NAME(EmuIDirectSound8_SetDopplerFactor) },
    { (OOVPA*)&IDirectSound8_SetOrientation_1_0_4928, XTL::EmuIDirectSound8_SetOrientation DSOUND_4928_TRACE_NAME(EmuIDirectSound8_SetOrientation) },
    { (OOVPA*)&IDirectSound8_SetPosition_1_0_4928, XTL::EmuIDirectSound8_SetPosition DSOUND_4928_TRACE_NAME(EmuIDirectSound8_SetPosition) },
    { (OOVPA*)&IDirectSound8_SetRolloffFactor_1_0_4928, XTL::EmuIDirectSound8_SetRolloffFactor DSOUND_4928_TRACE_NAME(EmuIDirectSound8_SetRolloffFactor) },
    { (OOVPA*)&IDirectSound8_SetVelocity_1_0_4928, XTL::EmuIDirectSound8_SetVelocity DSOUND_4928_TRACE_NAME(EmuIDirectSound8_SetVelocity) },
    { (OOVPA*)&IDirectSound8_SetI3DL2Listener_1_0_4928, XTL::EmuIDirectSound8_SetI3DL2Listener DSOUND_4928_TRACE_NAME(EmuIDirectSound8_SetI3DL2Listener) },
    { (OOVPA*)&IDirectSoundBuffer_SetBufferData_1_0_4928, XTL::EmuIDirectSoundBuffer8_SetBufferData DSOUND_4928_TRACE_NAME(EmuIDirectSoundBuffer8_SetBufferData) },
    { (OOVPA*)&IDirectSound8_CreateSoundStream_1_0_4928, XTL::EmuIDirectSound8_CreateSoundStream DSOUND_4928_TRACE_NAME(EmuIDirectSound8_CreateSoundStream) },
    { (OOVPA*)&IDirectSound8_CreateSoundBuffer_1_0_4928, XTL::EmuIDirectSound8_CreateSoundBuffer DSOUND_4928_TRACE_NAME(EmuIDirectSound8_CreateSoundBuffer) },
    { (OOVPA*)&DirectSoundCreate_1_0_4928, XTL::EmuDirectSoundCreate DSOUND_4928_TRACE_NAME(EmuDirectSoundCreate) },
    { (OOVPA*)&DirectSoundCreateStream_1_0_4928, XTL::EmuDirectSoundCreateStream DSOUND_4928_TRACE_NAME(EmuDirectSoundCreateStream) }
};

#undef DSOUND_4928_TRACE_NAME

uint32 DSound_1_0_4928_SIZE = sizeof(DSound_1_0_4928);
