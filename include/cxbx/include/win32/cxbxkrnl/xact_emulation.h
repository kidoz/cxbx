// Xbox Audio Creation Tool engine HLE boundary.
#ifndef EMUXACT_H
#define EMUXACT_H

struct X_XACT_RUNTIME_PARAMETERS
{
    DWORD dwMax2DHwVoices;
    DWORD dwMax3DHwVoices;
    DWORD dwMaxConcurrentStreams;
    DWORD dwMaxNotifications;
    DWORD dwInteractiveAudioLookaheadTime;
};

static_assert(sizeof(X_XACT_RUNTIME_PARAMETERS) == 0x14,
              "Xbox XACT_RUNTIME_PARAMETERS ABI size changed");

struct X_XACTEngine;
struct X_XACTWaveBank;
struct X_XACTSoundBank;
struct X_XACTSoundCue;

struct X_XACT_PREPARE_SOUNDCUE
{
    DWORD dwFlags;
    DWORD dwCueIndex;
    PVOID pSoundSource;
    const void* pParameterControls;
};

static_assert(sizeof(X_XACT_PREPARE_SOUNDCUE) == 0x10,
              "Xbox XACT_PREPARE_SOUNDCUE ABI size changed");

struct X_XACT_NOTIFICATION_DESCRIPTION
{
    WORD wType;
    WORD wFlags;
    union
    {
        X_XACTSoundBank* pSoundBank;
        X_XACTWaveBank* pWaveBank;
    } u;
    DWORD dwSoundCueIndex;
    X_XACTSoundCue* pSoundCue;
    PVOID pvContext;
    HANDLE hEvent;
};

static_assert(sizeof(X_XACT_NOTIFICATION_DESCRIPTION) == 0x18,
              "Xbox XACT_NOTIFICATION_DESCRIPTION ABI size changed");

struct X_XACT_NOTIFICATION
{
    X_XACT_NOTIFICATION_DESCRIPTION Header;
    DWORD MarkerData;
    LONGLONG rtTimeStamp;
};

static_assert(sizeof(X_XACT_NOTIFICATION) == 0x28,
              "Xbox XACT_NOTIFICATION ABI size changed");

HRESULT WINAPI EmuXACTEngineCreate(
    const X_XACT_RUNTIME_PARAMETERS* pParams,
    X_XACTEngine** ppEngine);
VOID WINAPI EmuXACTEngineDoWork();
ULONG WINAPI EmuIXACTEngine_AddRef(X_XACTEngine* pEngine);
ULONG WINAPI EmuIXACTEngine_Release(X_XACTEngine* pEngine);
HRESULT WINAPI EmuIXACTEngine_RegisterWaveBank(
    X_XACTEngine* pEngine,
    PVOID pvData,
    DWORD dwSize,
    X_XACTWaveBank** ppWaveBank);
HRESULT WINAPI EmuIXACTEngine_UnRegisterWaveBank(
    X_XACTEngine* pEngine,
    X_XACTWaveBank* pWaveBank);
HRESULT WINAPI EmuIXACTEngine_CreateSoundBank(
    X_XACTEngine* pEngine,
    PVOID pvData,
    DWORD dwSize,
    X_XACTSoundBank** ppSoundBank);
HRESULT WINAPI EmuIXACTEngine_RegisterNotification(
    X_XACTEngine* pEngine,
    const X_XACT_NOTIFICATION_DESCRIPTION* pNotificationDesc);
HRESULT WINAPI EmuIXACTEngine_UnRegisterNotification(
    X_XACTEngine* pEngine,
    const X_XACT_NOTIFICATION_DESCRIPTION* pNotificationDesc);
HRESULT WINAPI EmuIXACTEngine_GetNotification(
    X_XACTEngine* pEngine,
    const X_XACT_NOTIFICATION_DESCRIPTION* pNotificationDesc,
    X_XACT_NOTIFICATION* pNotification);
HRESULT WINAPI EmuIXACTEngine_FlushNotification(
    X_XACTEngine* pEngine,
    const X_XACT_NOTIFICATION_DESCRIPTION* pNotificationDesc);
ULONG WINAPI EmuIXACTSoundBank_AddRef(X_XACTSoundBank* pSoundBank);
ULONG WINAPI EmuIXACTSoundBank_Release(X_XACTSoundBank* pSoundBank);
HRESULT WINAPI EmuIXACTSoundBank_GetSoundCueIndexFromFriendlyName(
    X_XACTSoundBank* pSoundBank,
    PCSTR pFriendlyName,
    PDWORD pdwSoundCueIndex);
HRESULT WINAPI EmuIXACTSoundBank_PrepareEx(
    X_XACTSoundBank* pSoundBank,
    const X_XACT_PREPARE_SOUNDCUE* pPrepareData,
    X_XACTSoundCue** ppSoundCue);
HRESULT WINAPI EmuIXACTSoundBank_PlayEx(
    X_XACTSoundBank* pSoundBank,
    const X_XACT_PREPARE_SOUNDCUE* pPrepareData,
    X_XACTSoundCue** ppSoundCue);
HRESULT WINAPI EmuIXACTSoundBank_Stop(
    X_XACTSoundBank* pSoundBank,
    DWORD dwSoundCueIndex,
    DWORD dwFlags,
    X_XACTSoundCue* pSoundCue);

#endif
