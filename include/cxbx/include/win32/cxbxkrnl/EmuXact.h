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
ULONG WINAPI EmuIXACTSoundBank_AddRef(X_XACTSoundBank* pSoundBank);
ULONG WINAPI EmuIXACTSoundBank_Release(X_XACTSoundBank* pSoundBank);
HRESULT WINAPI EmuIXACTSoundBank_GetSoundCueIndexFromFriendlyName(
    X_XACTSoundBank* pSoundBank,
    PCSTR pFriendlyName,
    PDWORD pdwSoundCueIndex);

#endif
