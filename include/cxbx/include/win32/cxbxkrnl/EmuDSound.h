// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->EmuDSound.h
// *
// *  This file is part of the cxbx project.
// *
// *  cxbx and cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file LICENSE.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#ifndef EMUDSOUND_H
#define EMUDSOUND_H

#include "core/Xbe.h"

#undef FIELD_OFFSET     // prevent macro redefinition warnings
#include <windows.h>

#include <dsound.h>

// ******************************************************************
// * X_CDirectSound
// ******************************************************************
struct X_CDirectSound
{
    // TODO: Fill this in?
};

// ******************************************************************
// * X_CDirectSoundBuffer
// ******************************************************************
struct X_CDirectSoundBuffer
{
    BYTE    UnknownA[0x20];     // Offset: 0x00

    union                       // Offset: 0x20
    {
        PVOID                pMpcxBuffer;
        IDirectSoundBuffer  *EmuDirectSoundBuffer8;
    };

    BYTE    UnknownB[0x0C];     // Offset: 0x24

    // The title believes this is a full guest CDirectSoundBuffer, which is
    // considerably larger than the fields above; un-hooked inline guest code
    // that stores into later fields must land in memory we own, not in the
    // next heap block.
    BYTE    EmuGuardTail[0x200];

    // Host-only state used when Xbox SetBufferData replaces the storage of a
    // static buffer. Keep it after the guest-visible guard area so unhooked
    // guest code cannot overwrite it through a CDirectSoundBuffer offset.
    DWORD   EmuBufferFlags;
    DWORD   EmuBufferBytes;
    DWORD   EmuWaveFormatBytes;
    BYTE    EmuWaveFormat[64];
};

// ******************************************************************
// * X_CDirectSoundStream
// ******************************************************************
struct X_DSoundStreamState;

struct X_XMEDIAPACKET
{
    LPVOID pvBuffer;
    DWORD dwMaxSize;
    LPDWORD pdwCompletedSize;
    LPDWORD pdwStatus;
    union
    {
        HANDLE hCompletionEvent;
        LPVOID pContext;
    };
    LONGLONG* prtTimestamp;
};

struct X_XMEDIAINFO
{
    DWORD dwFlags;
    DWORD dwInputSize;
    DWORD dwOutputSize;
    DWORD dwMaxLookahead;
};

typedef VOID(CALLBACK* X_LPFNXMEDIAOBJECTCALLBACK)(LPVOID pStreamContext, LPVOID pPacketContext, DWORD dwStatus);

class X_CDirectSoundStream
{
    public:
        // ******************************************************************
        // * Construct VTable (or grab ptr to existing)
        // ******************************************************************
      X_CDirectSoundStream() : pVtbl(&vtbl), EmuState(NULL), EmuRefCount(1) {};

      // ******************************************************************
      // * VTable (cached by each instance, via constructor)
      // ******************************************************************
      struct _vtbl
      {
          ULONG(WINAPI* AddRef)(X_CDirectSoundStream* pThis);                         // 0x00
          ULONG(WINAPI* Release)(X_CDirectSoundStream* pThis);                        // 0x04
          HRESULT(WINAPI* GetInfo)(X_CDirectSoundStream* pThis, X_XMEDIAINFO* pInfo); // 0x08
          HRESULT(WINAPI* GetStatus)(X_CDirectSoundStream* pThis, LPDWORD pdwStatus); // 0x0C
          HRESULT(WINAPI* Process)                                                    // 0x10
          (
              X_CDirectSoundStream* pThis,
              const X_XMEDIAPACKET* pInputBuffer,
              const X_XMEDIAPACKET* pOutputBuffer);
          HRESULT(WINAPI* Discontinuity)(X_CDirectSoundStream* pThis); // 0x14
          HRESULT(WINAPI* Flush)(X_CDirectSoundStream* pThis);         // 0x18
      }* pVtbl;

      // Host-only state. Guest addresses and host DirectSound objects never
      // share a representation.
      X_DSoundStreamState* EmuState;
      volatile LONG EmuRefCount;

      // ******************************************************************
      // * Global Vtbl for this class
      // ******************************************************************
      static _vtbl vtbl;

      // ******************************************************************
      // * Debug Mode guard for detecting naughty data accesses
      // ******************************************************************
      BYTE EmuGuardTail[0x200];
};

static_assert(sizeof(X_XMEDIAPACKET) == 0x18, "Xbox XMEDIAPACKET ABI size changed");
static_assert(offsetof(X_XMEDIAPACKET, prtTimestamp) == 0x14, "Xbox XMEDIAPACKET ABI layout changed");
static_assert(offsetof(X_CDirectSoundStream, pVtbl) == 0x00, "Xbox stream vtable must remain first");

// ******************************************************************
// * X_DSBUFFERDESC
// ******************************************************************
struct X_DSBUFFERDESC
{
    DWORD           dwSize; 
    DWORD           dwFlags; 
    DWORD           dwBufferBytes; 
    LPWAVEFORMATEX  lpwfxFormat; 
    LPVOID          lpMixBins;      // TODO: Implement
    DWORD           dwInputMixBin;
};

// ******************************************************************
// * X_DSSTREAMDESC
// ******************************************************************
struct X_DSSTREAMDESC
{
    DWORD                       dwFlags;
    DWORD                       dwMaxAttachedPackets;
    LPWAVEFORMATEX              lpwfxFormat;
    X_LPFNXMEDIAOBJECTCALLBACK lpfnCallback;
    LPVOID                      lpvContext;
    PVOID                       lpMixBins;      // TODO: Correct Parameter
};

static_assert(sizeof(X_DSSTREAMDESC) == 0x18, "Xbox DSSTREAMDESC ABI size changed");

// ******************************************************************
// * func: EmuDirectSoundCreate
// ******************************************************************
HRESULT WINAPI EmuDirectSoundCreate
(
    LPVOID          pguidDeviceId,
    LPDIRECTSOUND8 *ppDirectSound,
    LPUNKNOWN       pUnknown
);

// ******************************************************************
// * func: EmuDirectSoundCreateBuffer
// ******************************************************************
HRESULT WINAPI EmuDirectSoundCreateBuffer
(
    X_DSBUFFERDESC         *pdsbd,
    X_CDirectSoundBuffer  **ppBuffer
);

// ******************************************************************
// * func: EmuDirectSoundCreateStream
// ******************************************************************
HRESULT WINAPI EmuDirectSoundCreateStream
(
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream
);

// ******************************************************************
// * func: EmuIDirectSound8_CreateStream
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_CreateStream
(
    LPDIRECTSOUND8          pThis,
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream,
    PVOID                   pUnknown
);

// ******************************************************************
// * func: EmuIDirectSound8_CreateBuffer
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_CreateBuffer
(
    LPDIRECTSOUND8          pThis,
    X_DSBUFFERDESC         *pdssd,
    X_CDirectSoundBuffer  **ppBuffer,
    PVOID                   pUnknown
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetVolume
// ******************************************************************
ULONG WINAPI EmuCDirectSoundStream_SetVolume(X_CDirectSoundStream *pThis, LONG lVolume);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetRolloffFactor
(
    X_CDirectSoundStream *pThis,
    FLOAT                 fRolloffFactor,
    DWORD                 dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_AddRef
// ******************************************************************
ULONG WINAPI EmuCDirectSoundStream_AddRef(X_CDirectSoundStream *pThis);

// ******************************************************************
// * func: EmuCDirectSoundStream_Release
// ******************************************************************
ULONG WINAPI EmuCDirectSoundStream_Release(X_CDirectSoundStream *pThis);

// ******************************************************************
// * func: EmuCDirectSoundStream_Process
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_Process(
    X_CDirectSoundStream* pThis,
    const X_XMEDIAPACKET* pInputBuffer,
    const X_XMEDIAPACKET* pOutputBuffer);

HRESULT WINAPI EmuCDirectSoundStream_GetInfo(
    X_CDirectSoundStream* pThis,
    X_XMEDIAINFO* pInfo);

HRESULT WINAPI EmuCDirectSoundStream_GetStatus(
    X_CDirectSoundStream* pThis,
    LPDWORD pdwStatus);

// ******************************************************************
// * func: EmuCDirectSoundStream_Discontinuity
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_Discontinuity(X_CDirectSoundStream *pThis);

// ******************************************************************
// * func: EmuCDirectSoundStream_Pause
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_Pause
(
    PVOID   pStream,
    DWORD   dwPause
);

// ******************************************************************
// * func: EmuIDirectSound8_AddRef
// ******************************************************************
ULONG WINAPI EmuIDirectSound8_AddRef
(
    LPDIRECTSOUND8          pThis
);

// ******************************************************************
// * func: EmuIDirectSound8_Release
// ******************************************************************
ULONG WINAPI EmuIDirectSound8_Release
(
    LPDIRECTSOUND8          pThis
);

// ******************************************************************
// * func: EmuIDirectSound8_SynchPlayback
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SynchPlayback
(
    LPDIRECTSOUND8          pThis
);

// ******************************************************************
// * func: EmuIDirectSound8_GetOutputLevels
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_GetOutputLevels
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pOutputLevels,
    BOOL                    fResetPeakValues
);

// ******************************************************************
// * func: EmuIDirectSound8_DownloadEffectsImage
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_DownloadEffectsImage
(
    LPDIRECTSOUND8          pThis,
    LPCVOID                 pvImageBuffer,
    DWORD                   dwImageSize,
    PVOID                   pImageLoc,      // TODO: Use this param
    PVOID                  *ppImageDesc     // TODO: Use this param
);

// ******************************************************************
// * func: EmuIDirectSoundStream_SetHeadroom
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundStream_SetHeadroom
(
    PVOID   pThis,
    DWORD   dwHeadroom
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetAllParameters
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetAllParameters
(
    PVOID   pThis,
    PVOID   pUnknown,
    DWORD   dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeAngles
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetConeAngles
(
    PVOID   pThis,
    DWORD   dwInsideConeAngle,
    DWORD   dwOutsideConeAngle,
    DWORD   dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeOutsideVolume
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetConeOutsideVolume
(
    PVOID   pThis,
    LONG    lConeOutsideVolume,
    DWORD   dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetMaxDistance
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetMaxDistance
(
    PVOID    pThis,
    D3DVALUE fMaxDistance,
    DWORD    dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetMinDistance
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetMinDistance
(
    PVOID    pThis,
    D3DVALUE fMinDistance,
    DWORD    dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetVelocity
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetVelocity
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeOrientation
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetConeOrientation
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetPosition
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetPosition
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundStream_SetFrequency
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_SetFrequency
(
    PVOID   pThis,
    DWORD   dwFrequency
);

// ******************************************************************
// * func: EmuIDirectSoundStream_SetI3DL2Source
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundStream_SetI3DL2Source
(
    PVOID   pThis,
    PVOID   pds3db,
    DWORD   dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetOrientation
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetOrientation
(
    LPDIRECTSOUND8  pThis,
    FLOAT           xFront,
    FLOAT           yFront,
    FLOAT           zFront,
    FLOAT           xTop,
    FLOAT           yTop,
    FLOAT           zTop,
    DWORD           dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetDistanceFactor
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetDistanceFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fDistanceFactor,
    DWORD           dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetRolloffFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fRolloffFactor,
    DWORD           dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetDopplerFactor
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetDopplerFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fDopplerFactor,
    DWORD           dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetI3DL2Listener
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetI3DL2Listener
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pDummy, // TODO: fill this out
    DWORD                   dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetMixBinHeadroom
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetMixBinHeadroom
(
    LPDIRECTSOUND8          pThis,
    DWORD                   dwMixBinMask,
    DWORD                   dwHeadroom
);

// ******************************************************************
// * func: EmuIDirectSound8_SetPosition
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetPosition
(
    LPDIRECTSOUND8          pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetVelocity
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetVelocity
(
    LPDIRECTSOUND8          pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_SetAllParameters
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_SetAllParameters
(
    LPDIRECTSOUND8          pThis,
    LPVOID                  pTodo,  // TODO: LPCDS3DLISTENER
    DWORD                   dwApply
);

// ******************************************************************
// * func: EmuIDirectSound8_CreateSoundBuffer
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_CreateSoundBuffer
(
    LPDIRECTSOUND8          pThis,
    X_DSBUFFERDESC         *pdsbd,
    X_CDirectSoundBuffer  **ppBuffer,
    LPUNKNOWN               pUnkOuter
);

// ******************************************************************
// * func: EmuIDirectSound8_CreateSoundStream
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_CreateSoundStream
(
    LPDIRECTSOUND8          pThis,
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream,
    LPUNKNOWN               pUnkOuter
);


// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetBufferData
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetBufferData
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pvBufferData,
    DWORD                   dwBufferBytes
);

HRESULT WINAPI EmuIDirectSoundBuffer8_SetNotificationPositions
(
    X_CDirectSoundBuffer       *pThis,
    DWORD                       dwNotifyCount,
    const DSBPOSITIONNOTIFY    *paNotifies
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetOutputBuffer
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetOutputBuffer
(
    X_CDirectSoundBuffer   *pThis,
    X_CDirectSoundBuffer   *pOutputBuffer
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Use3DVoiceData
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_Use3DVoiceData
(
    X_CDirectSoundBuffer   *pThis,
    BOOL                    bUse3DVoiceData
);

HRESULT WINAPI EmuIDirectSoundStream_Use3DVoiceData
(
    X_CDirectSoundStream   *pThis,
    BOOL                    bUse3DVoiceData
);

HRESULT WINAPI EmuCDirectSoundVoice_SetPitch
(
    PVOID                   pThis,
    LONG                    lPitch
);

HRESULT WINAPI EmuCDirectSoundVoice_GetVoiceProperties
(
    PVOID                   pThis,
    PVOID                   pVoiceProperties
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetFormat
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetFormat
(
    X_CDirectSoundBuffer   *pThis,
    const WAVEFORMATEX     *pWaveFormat
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetPlayRegion
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetPlayRegion
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwPlayStart,
    DWORD                   dwPlayLength
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetLoopRegion
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetLoopRegion
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwLoopStart,
    DWORD                   dwLoopLength
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetVolume
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetVolume
(
    X_CDirectSoundBuffer   *pThis,
    LONG                    lVolume
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetCurrentPosition
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetCurrentPosition
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwNewPosition
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_GetCurrentPosition
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_GetCurrentPosition
(
    X_CDirectSoundBuffer   *pThis,
    PDWORD                  pdwCurrentPlayCursor,
    PDWORD                  pdwCurrentWriteCursor
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Stop
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_Stop
(
    X_CDirectSoundBuffer   *pThis
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_StopEx
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_StopEx
(
    X_CDirectSoundBuffer   *pThis,
    LONGLONG                rtTimeStamp,
    DWORD                   dwFlags
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Play
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_Play
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwReserved1,
    DWORD                   dwReserved2,
    DWORD                   dwFlags
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_PlayEx
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_PlayEx
(
    X_CDirectSoundBuffer   *pThis,
    LONGLONG                rtTimeStamp,
    DWORD                   dwFlags
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Lock
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_Lock
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwOffset,
    DWORD                   dwBytes,
    LPVOID                 *ppvAudioPtr1,
    LPDWORD                 pdwAudioBytes1,
    LPVOID                 *ppvAudioPtr2,
    LPDWORD                 pdwAudioBytes2,
    DWORD                   dwFlags
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Unlock
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_Unlock
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pvLock1,
    DWORD                   dwLockSize1,
    LPVOID                  pvLock2,
    DWORD                   dwLockSize2
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMixBins
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetMixBins
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pMixBins
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_GetStatus
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_GetStatus
(
    X_CDirectSoundBuffer   *pThis,
    LPDWORD                 pdwStatus
);

// ******************************************************************
// * func: EmuDirectSoundUseFullHRTF
// ******************************************************************
VOID WINAPI EmuDirectSoundUseFullHRTF();

// ******************************************************************
// * func: EmuXAudioDownloadEffectsImage
// ******************************************************************
HRESULT WINAPI EmuXAudioDownloadEffectsImage
(
    LPCSTR                  pszImageName,
    LPVOID                  pImageLoc,
    DWORD                   dwFlags,
    LPVOID                 *ppImageDesc
);

// ******************************************************************
// * func: EmuDirectSoundDoWork
// ******************************************************************
VOID WINAPI EmuDirectSoundDoWork();

// ******************************************************************
// * func: EmuCDirectSoundStream_Flush
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundStream_Flush(X_CDirectSoundStream *pThis);

// ******************************************************************
// * func: EmuIDirectSoundStream_FlushEx
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundStream_FlushEx
(
    X_CDirectSoundStream   *pThis,
    DWORD                   dwFlags,
    LONGLONG                rtTimeStamp
);

// ******************************************************************
// * func: EmuIDirectSoundStream_SetEG
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundStream_SetEG
(
    X_CDirectSoundStream   *pThis,
    LPVOID                  pEnvelopeDesc
);

// ******************************************************************
// * func: EmuIDirectSoundStream_SetMixBinsS
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundStream_SetMixBinsS
(
    X_CDirectSoundStream   *pThis,
    LPVOID                  pMixBins
);

// ******************************************************************
// * func: EmuIDirectSoundStream_SetOutputBuffer
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundStream_SetOutputBuffer
(
    X_CDirectSoundStream   *pThis,
    X_CDirectSoundBuffer   *pOutputBuffer
);

// ******************************************************************
// * func: EmuIDirectSoundStream_SetFormat
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundStream_SetFormat
(
    X_CDirectSoundStream   *pThis,
    const WAVEFORMATEX     *pWaveFormat
);

// ******************************************************************
// * func: EmuCDirectSound_CommitDeferredSettings
// ******************************************************************
HRESULT WINAPI EmuCDirectSound_CommitDeferredSettings
(
    X_CDirectSound         *pThis
);

// ******************************************************************
// * func: EmuIDirectSound8_GetCaps
// ******************************************************************
HRESULT WINAPI EmuIDirectSound8_GetCaps
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pDSCaps
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Release
// ******************************************************************
ULONG WINAPI EmuIDirectSoundBuffer8_Release
(
    X_CDirectSoundBuffer   *pThis
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMixBinVolumes
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetMixBinVolumes
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pMixBins
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetFrequency
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetFrequency
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwFrequency
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetConeAngles
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetConeAngles
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwInsideConeAngle,
    DWORD                   dwOutsideConeAngle,
    DWORD                   dwApply
);

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetI3DL2Source
// ******************************************************************
HRESULT WINAPI EmuIDirectSoundBuffer8_SetI3DL2Source
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pds3db,
    DWORD                   dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundBuffer_SetDeferred3dVector
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundBuffer_SetDeferred3dVector
(
    X_CDirectSoundBuffer   *pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
);

// ******************************************************************
// * func: EmuCDirectSoundBuffer_SetDeferred3dParam
// ******************************************************************
HRESULT WINAPI EmuCDirectSoundBuffer_SetDeferred3dParam
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwArg1,
    DWORD                   dwArg2
);

#endif
