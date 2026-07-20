// XACT 5849 one-track PCM cue prepare/play/stop lifecycle.

#include "xdk_xtrace.h"
#include "probe_banks.h"
#include <xact.h>

void __cdecl main()
{
    xt_begin("xact_cue");

    const int prepare_patched =
        xt_is_hle_patched((const void*)IXACTSoundBank_PrepareEx);
    const int play_patched =
        xt_is_hle_patched((const void*)IXACTSoundBank_PlayEx);
    const int stop_patched =
        xt_is_hle_patched((const void*)IXACTSoundBank_Stop);
    const int register_notification_patched =
        xt_is_hle_patched((const void*)IXACTEngine_RegisterNotification);
    const int unregister_notification_patched =
        xt_is_hle_patched((const void*)IXACTEngine_UnRegisterNotification);
    const int get_notification_patched =
        xt_is_hle_patched((const void*)IXACTEngine_GetNotification);
    const int flush_notification_patched =
        xt_is_hle_patched((const void*)IXACTEngine_FlushNotification);
    xt_chk("xact.cue_prepare_hle", 1, prepare_patched);
    xt_chk("xact.cue_play_hle", 1, play_patched);
    xt_chk("xact.cue_stop_hle", 1, stop_patched);
    xt_chk("xact.notification_register_hle", 1,
           register_notification_patched);
    xt_chk("xact.notification_unregister_hle", 1,
           unregister_notification_patched);
    xt_chk("xact.notification_get_hle", 1, get_notification_patched);
    xt_chk("xact.notification_flush_hle", 1, flush_notification_patched);
    if(!prepare_patched || !play_patched || !stop_patched ||
       !register_notification_patched || !unregister_notification_patched ||
       !get_notification_patched || !flush_notification_patched)
    {
        xt_emit("NOTE XACTENG 1.0.5849 cue lifecycle is not fully HLE-patched");
        xt_end_and_exit();
    }

    XACT_RUNTIME_PARAMETERS params;
    ZeroMemory(&params, sizeof(params));
    params.dwMax2DHwVoices = 64;
    params.dwMax3DHwVoices = 32;
    params.dwMaxConcurrentStreams = 4;
    params.dwMaxNotifications = 8;

    IXACTEngine* engine = NULL;
    HRESULT result = XACTEngineCreate(&params, &engine);
    xt_chk("xact.cue_engine_create", 1,
           SUCCEEDED(result) && engine != NULL);
    if(FAILED(result) || engine == NULL)
    {
        xt_end_and_exit();
    }

    IXACTWaveBank* wave_bank = NULL;
    result = IXACTEngine_RegisterWaveBank(
        engine, (PVOID)g_ProbeWaveBank, sizeof(g_ProbeWaveBank), &wave_bank);
    xt_chk("xact.cue_wavebank_register", 1,
           SUCCEEDED(result) && wave_bank != NULL);

    IXACTSoundBank* sound_bank = NULL;
    result = IXACTEngine_CreateSoundBank(
        engine, (PVOID)g_ProbeSoundBank, sizeof(g_ProbeSoundBank), &sound_bank);
    xt_chk("xact.cue_soundbank_create", 1,
           SUCCEEDED(result) && sound_bank != NULL);
    if(FAILED(result) || sound_bank == NULL || wave_bank == NULL)
    {
        if(wave_bank != NULL)
        {
            IXACTEngine_UnRegisterWaveBank(engine, wave_bank);
        }
        IXACTEngine_Release(engine);
        xt_end_and_exit();
    }

    DWORD cue_index = XACT_SOUNDCUE_INDEX_UNUSED;
    result = IXACTSoundBank_GetSoundCueIndexFromFriendlyName(
        sound_bank, "ProbeCue", &cue_index);
    xt_chk("xact.cue_lookup", 1,
           SUCCEEDED(result) && cue_index == 0);

    IXACTSoundCue* cue = NULL;
    result = IXACTSoundBank_Prepare(sound_bank, cue_index, 0, &cue);
    xt_chk_u32("xact.cue_prepare_result", S_OK, result);
    xt_chk("xact.cue_prepare", 1, SUCCEEDED(result) && cue != NULL);
    if(cue != NULL)
    {
        result = IXACTSoundBank_Stop(
            sound_bank, XACT_SOUNDCUE_INDEX_UNUSED, 0, cue);
        xt_chk("xact.cue_stop_prepared", 1, SUCCEEDED(result));
    }

    cue = NULL;
    result = IXACTSoundBank_Play(sound_bank, cue_index, NULL, 0, &cue);
    xt_chk_u32("xact.cue_play_result", S_OK, result);
    xt_chk("xact.cue_play", 1, SUCCEEDED(result) && cue != NULL);
    if(cue != NULL)
    {
        result = IXACTSoundBank_Stop(
            sound_bank, XACT_SOUNDCUE_INDEX_UNUSED, 0, cue);
        xt_chk("xact.cue_stop_instance", 1, SUCCEEDED(result));
    }

    IXACTSoundCue* first = NULL;
    IXACTSoundCue* second = NULL;
    HRESULT first_result = IXACTSoundBank_Play(
        sound_bank, cue_index, NULL, 0, &first);
    HRESULT second_result = IXACTSoundBank_Play(
        sound_bank, cue_index, NULL, 0, &second);
    xt_chk_u32("xact.cue_first_play_result", S_OK, first_result);
    xt_chk_u32("xact.cue_second_play_result", S_OK, second_result);
    xt_chk("xact.cue_distinct_instances", 1,
           SUCCEEDED(first_result) && SUCCEEDED(second_result) &&
           first != NULL && second != NULL && first != second);
    result = IXACTSoundBank_Stop(sound_bank, cue_index, 0, NULL);
    xt_chk("xact.cue_stop_index", 1, SUCCEEDED(result));

    XACT_NOTIFICATION_DESCRIPTION notification_desc;
    ZeroMemory(&notification_desc, sizeof(notification_desc));
    notification_desc.wType = eXACTNotification_Stop;
    notification_desc.wFlags = XACT_FLAG_NOTIFICATION_PERSIST |
                               XACT_FLAG_NOTIFICATION_USE_SOUNDCUE_INDEX;
    notification_desc.u.pSoundBank = sound_bank;
    notification_desc.dwSoundCueIndex = cue_index;
    notification_desc.pvContext = (PVOID)0x12345678;
    result = IXACTEngine_RegisterNotification(engine, &notification_desc);
    xt_chk_u32("xact.notification_register", S_OK, result);

    XACT_NOTIFICATION notification;
    ZeroMemory(&notification, sizeof(notification));
    result = IXACTEngine_GetNotification(
        engine, &notification_desc, &notification);
    xt_chk("xact.notification_initially_empty", 1, result != S_OK);

    result = IXACTSoundBank_Play(
        sound_bank, cue_index, NULL, XACT_FLAG_SOUNDCUE_AUTORELEASE, NULL);
    xt_chk("xact.cue_autorelease_play", 1, SUCCEEDED(result));
    Sleep(50);
    XACTEngineDoWork();

    result = IXACTEngine_GetNotification(
        engine, &notification_desc, &notification);
    xt_chk_u32("xact.notification_get_stop", S_OK, result);
    xt_chk_u32("xact.notification_type", eXACTNotification_Stop,
               notification.Header.wType);
    xt_chk("xact.notification_soundbank", 1,
           notification.Header.u.pSoundBank == sound_bank);
    xt_chk_u32("xact.notification_cue_index", cue_index,
               notification.Header.dwSoundCueIndex);
    xt_chk("xact.notification_cue_destroyed", 1,
           (notification.Header.wFlags &
            XACT_FLAG_NOTIFICATION_SOUNDCUE_DESTROYED) != 0);
    xt_chk("xact.notification_context", 1,
           notification.Header.pvContext == (PVOID)0x12345678);

    ZeroMemory(&notification, sizeof(notification));
    result = IXACTEngine_GetNotification(
        engine, &notification_desc, &notification);
    xt_chk("xact.notification_consumed", 1, result != S_OK);
    xt_chk_u32("xact.notification_flush", S_OK,
               IXACTEngine_FlushNotification(engine, &notification_desc));
    xt_chk_u32("xact.notification_unregister", S_OK,
               IXACTEngine_UnRegisterNotification(engine, &notification_desc));

    cue = (IXACTSoundCue*)1;
    result = IXACTSoundBank_Play(sound_bank, 1, NULL, 0, &cue);
    xt_chk("xact.cue_invalid_index", 1,
           FAILED(result) && cue == NULL);

    xt_chk_u32("xact.cue_soundbank_release", 0,
               IXACTSoundBank_Release(sound_bank));
    xt_chk("xact.cue_wavebank_unregister", 1,
           SUCCEEDED(IXACTEngine_UnRegisterWaveBank(engine, wave_bank)));
    xt_chk_u32("xact.cue_engine_release", 0,
               IXACTEngine_Release(engine));

    xt_end_and_exit();
}
