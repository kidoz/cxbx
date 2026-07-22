// XDK 5233 DirectSound entry points verified against the target title.

#ifdef _DEBUG_TRACE
#define DSOUND_5233_TRACE_NAME(Name) , #Name
#else
#define DSOUND_5233_TRACE_NAME(Name)
#endif

OOVPATable DSound_1_0_5233[] = {
    { (OOVPA*)&DirectSoundCreate_1_0_5849, XTL::EmuDirectSoundCreate DSOUND_5233_TRACE_NAME(EmuDirectSoundCreate) },
    { (OOVPA*)&IDirectSound8_CreateSoundBuffer_1_0_5849, XTL::EmuIDirectSound8_CreateSoundBuffer DSOUND_5233_TRACE_NAME(EmuIDirectSound8_CreateSoundBuffer) },
    { (OOVPA*)&CDirectSound_CreateSoundStream_1_0_5849, 0 DSOUND_5233_TRACE_NAME(CDirectSound_CreateSoundStream) },
    { (OOVPA*)&DirectSoundCreateStream_1_0_5849, XTL::EmuDirectSoundCreateStream DSOUND_5233_TRACE_NAME(EmuDirectSoundCreateStream) },
    { (OOVPA*)&DirectSoundDoWork_1_0_5849, XTL::EmuDirectSoundDoWork DSOUND_5233_TRACE_NAME(EmuDirectSoundDoWork) },
    { (OOVPA*)&IDirectSoundBuffer_Release_1_0_4627, XTL::EmuIDirectSoundBuffer8_Release DSOUND_5233_TRACE_NAME(EmuIDirectSoundBuffer8_Release) },
    { (OOVPA*)&IDirectSound8_DownloadEffectsImage_1_0_5849, XTL::EmuIDirectSound8_DownloadEffectsImage DSOUND_5233_TRACE_NAME(EmuIDirectSound8_DownloadEffectsImage) },
    { (OOVPA*)&CDirectSound_SetI3DL2Listener_1_0_4627, 0 DSOUND_5233_TRACE_NAME(CDirectSound_SetI3DL2Listener) },
    { (OOVPA*)&IDirectSound8_SetI3DL2Listener_1_0_4627, XTL::EmuIDirectSound8_SetI3DL2Listener DSOUND_5233_TRACE_NAME(EmuIDirectSound8_SetI3DL2Listener) },
    { (OOVPA*)&IDirectSound8_SetOrientation_1_0_5849, XTL::EmuIDirectSound8_SetOrientation DSOUND_5233_TRACE_NAME(EmuIDirectSound8_SetOrientation) },
    { (OOVPA*)&CDirectSound_SetDistanceFactorA_1_0_4627, 0 DSOUND_5233_TRACE_NAME(CDirectSound_SetDistanceFactor) },
    { (OOVPA*)&IDirectSound8_SetDistanceFactor_1_0_4627, XTL::EmuIDirectSound8_SetDistanceFactor DSOUND_5233_TRACE_NAME(EmuIDirectSound8_SetDistanceFactor) },
    { (OOVPA*)&CDirectSound_SetRolloffFactor_1_0_4627, 0 DSOUND_5233_TRACE_NAME(CDirectSound_SetRolloffFactor) },
    { (OOVPA*)&IDirectSound8_SetRolloffFactor_1_0_4627, XTL::EmuIDirectSound8_SetRolloffFactor DSOUND_5233_TRACE_NAME(EmuIDirectSound8_SetRolloffFactor) },
    { (OOVPA*)&CDirectSound_SetDopplerFactor_1_0_4627, 0 DSOUND_5233_TRACE_NAME(CDirectSound_SetDopplerFactor) },
    { (OOVPA*)&IDirectSound8_SetDopplerFactor_1_0_4627, XTL::EmuIDirectSound8_SetDopplerFactor DSOUND_5233_TRACE_NAME(EmuIDirectSound8_SetDopplerFactor) },
    { (OOVPA*)&CSetPosition_1_0_4627, 0 DSOUND_5233_TRACE_NAME(CDirectSound_SetPosition) },
    { (OOVPA*)&IDirectSound_SetPosition_1_0_4627, XTL::EmuIDirectSound8_SetPosition DSOUND_5233_TRACE_NAME(EmuIDirectSound8_SetPosition) },
    { (OOVPA*)&CSetVelocity_1_0_4627, 0 DSOUND_5233_TRACE_NAME(CDirectSound_SetVelocity) },
    { (OOVPA*)&IDirectSound_SetVelocity_1_0_4627, XTL::EmuIDirectSound8_SetVelocity DSOUND_5233_TRACE_NAME(EmuIDirectSound8_SetVelocity) },
    { (OOVPA*)&CDirectSound_CommitDeferredSettings_1_0_4627, XTL::EmuCDirectSound_CommitDeferredSettings DSOUND_5233_TRACE_NAME(EmuCDirectSound_CommitDeferredSettings) }
};

#undef DSOUND_5233_TRACE_NAME

uint32 DSound_1_0_5233_SIZE = sizeof(DSound_1_0_5233);
