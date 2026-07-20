// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->EmuDSound.cpp
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
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace xboxkrnl
{
    #include <xboxkrnl/xboxkrnl.h>
};

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

#include "Emu.h"
#include "EmuFS.h"
#include "EmuShared.h"
#include "core/PcmConverter.h"
#include "core/XboxAdpcmDecoder.h"
#include "core/trace.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
    #include "EmuXTL.h"
};

#include "ResCxbxDll.h"

#include <process.h>
#include <clocale>

// ******************************************************************
// * Static Variable(s)
// ******************************************************************
static XTL::LPDIRECTSOUND8 g_pDSound8 = NULL;

namespace
{
constexpr DWORD kStreamBufferMinimumBytes = 64 * 1024;
constexpr DWORD kStreamBufferMaximumBytes = 4 * 1024 * 1024;
constexpr DWORD kStreamMaximumPackets = 1024;
constexpr DWORD kXMediaPacketStatusPending = static_cast<DWORD>(E_PENDING);
constexpr DWORD kXMediaPacketStatusSuccess = static_cast<DWORD>(S_OK);
constexpr DWORD kXMediaPacketStatusFlushed = static_cast<DWORD>(E_ABORT);
constexpr DWORD kXMediaPacketStatusFailure = static_cast<DWORD>(E_FAIL);
constexpr DWORD kXmoStatusAcceptInput = 0x00000001;
constexpr DWORD kDsStreamStatusPlaying = 0x00010000;
constexpr DWORD kDsStreamStatusPaused = 0x00020000;
constexpr DWORD kDsStreamStatusStarved = 0x00040000;
constexpr DWORD kDsStreamPauseResume = 0;
constexpr DWORD kDsStreamPausePause = 1;
constexpr DWORD kDsStreamPauseSynchPlayback = 2;
constexpr DWORD kDsStreamPauseNoActivate = 3;
constexpr DWORD kStaticBufferWaveFormatCapacity = 64;
constexpr WORD kWaveFormatXboxAdpcm = 0x0069;

struct StreamPacket
{
    LPDWORD CompletedSize;
    LPDWORD Status;
    HANDLE CompletionEvent;
    LPVOID Context;
    DWORD Size;
    uint64_t EndPosition;
};
} // namespace

static bool CacheStaticBufferFormat(XTL::X_CDirectSoundBuffer* buffer,
                                    const WAVEFORMATEX* format)
{
    if(buffer == NULL || format == NULL ||
       IsBadReadPtr(format, sizeof(WAVEFORMATEX)))
    {
        return false;
    }

    const DWORD formatBytes = sizeof(WAVEFORMATEX) + format->cbSize;
    if(formatBytes > kStaticBufferWaveFormatCapacity ||
       IsBadReadPtr(format, formatBytes))
    {
        return false;
    }

    std::memcpy(buffer->EmuWaveFormat, format, formatBytes);
    buffer->EmuWaveFormatBytes = formatBytes;
    return true;
}

static bool ConfigureStaticHostFormat(const WAVEFORMATEX* guestFormat,
                                      WAVEFORMATEX& hostFormat)
{
    if(guestFormat == NULL || guestFormat->wFormatTag != kWaveFormatXboxAdpcm)
        return false;

    if(guestFormat->nChannels == 0 || guestFormat->nChannels > 2 ||
       guestFormat->nBlockAlign != guestFormat->nChannels * 36)
    {
        return false;
    }

    hostFormat = {};
    hostFormat.wFormatTag = WAVE_FORMAT_PCM;
    hostFormat.nChannels = guestFormat->nChannels;
    hostFormat.nSamplesPerSec = guestFormat->nSamplesPerSec;
    hostFormat.wBitsPerSample = 16;
    hostFormat.nBlockAlign = hostFormat.nChannels * static_cast<WORD>(sizeof(std::int16_t));
    hostFormat.nAvgBytesPerSec = hostFormat.nSamplesPerSec * hostFormat.nBlockAlign;
    return true;
}

struct XTL::X_DSoundStreamState
{
    X_CDirectSoundStream* Owner;
    IDirectSoundBuffer* HostBuffer;
    WAVEFORMATEX* Format;
    WAVEFORMATEX HostFormat;
    X_LPFNXMEDIAOBJECTCALLBACK Callback;
    LPVOID CallbackContext;
    StreamPacket* Packets;
    DWORD PacketCapacity;
    DWORD PacketHead;
    DWORD PacketCount;
    DWORD BufferBytes;
    DWORD LastPlayCursor;
    uint64_t PlayedBytes;
    uint64_t WrittenBytes;
    LONG Volume;
    bool Playing;
    bool Paused;
    bool Synchronized;
    bool DownmixPcmToStereo;
    CRITICAL_SECTION Lock;
    X_DSoundStreamState* Next;
};

namespace
{
SRWLOCK g_DSoundStreamListLock = SRWLOCK_INIT;
XTL::X_DSoundStreamState* g_DSoundStreams = NULL;

bool ConfigureHostStreamFormat(XTL::X_DSoundStreamState* state)
{
    state->HostFormat = *state->Format;
    if(state->Format->wFormatTag != WAVE_FORMAT_PCM ||
       state->Format->nChannels <= 2)
    {
        return true;
    }

    if(state->Format->wBitsPerSample != 16 ||
       !cxbx::audio::CanDownmixPcm16ToStereo(state->Format->nChannels) ||
       state->Format->nBlockAlign != state->Format->nChannels * sizeof(int16_t))
    {
        return false;
    }

    state->HostFormat.nChannels = 2;
    state->HostFormat.nBlockAlign = 2 * sizeof(int16_t);
    state->HostFormat.nAvgBytesPerSec =
        state->HostFormat.nSamplesPerSec * state->HostFormat.nBlockAlign;
    state->HostFormat.cbSize = 0;
    state->DownmixPcmToStereo = true;
    return true;
}

HRESULT EnsureDirectSoundDevice()
{
    if(g_pDSound8 != NULL)
    {
        return DS_OK;
    }

    HRESULT result = DirectSoundCreate8(NULL, &g_pDSound8, NULL);
    if(SUCCEEDED(result) && g_pDSound8 != NULL)
    {
        result = g_pDSound8->SetCooperativeLevel(XTL::g_hEmuWindow, DSSCL_PRIORITY);
    }

    return result;
}

void RegisterStream(XTL::X_DSoundStreamState* state)
{
    AcquireSRWLockExclusive(&g_DSoundStreamListLock);
    state->Next = g_DSoundStreams;
    g_DSoundStreams = state;
    ReleaseSRWLockExclusive(&g_DSoundStreamListLock);
}

void UnregisterStream(XTL::X_DSoundStreamState* state)
{
    AcquireSRWLockExclusive(&g_DSoundStreamListLock);

    XTL::X_DSoundStreamState** link = &g_DSoundStreams;
    while(*link != NULL)
    {
        if(*link == state)
        {
            *link = state->Next;
            break;
        }

        link = &((*link)->Next);
    }

    ReleaseSRWLockExclusive(&g_DSoundStreamListLock);
}

XTL::X_CDirectSoundStream* ResolveStream(const void* streamPointer)
{
    if(streamPointer == NULL)
    {
        return NULL;
    }

    const uintptr_t candidate = reinterpret_cast<uintptr_t>(streamPointer);
    XTL::X_CDirectSoundStream* result = NULL;

    AcquireSRWLockShared(&g_DSoundStreamListLock);
    for(XTL::X_DSoundStreamState* state = g_DSoundStreams; state != NULL; state = state->Next)
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(state->Owner);
        if(candidate >= base && candidate < base + sizeof(*state->Owner))
        {
            result = state->Owner;
            break;
        }
    }
    ReleaseSRWLockShared(&g_DSoundStreamListLock);

    return result;
}

HRESULT StartStreamPlaybackLocked(XTL::X_DSoundStreamState* state)
{
    if(state->PacketCount == 0)
    {
        return DS_OK;
    }

    DWORD playCursor = 0;
    DWORD writeCursor = 0;
    state->HostBuffer->GetCurrentPosition(&playCursor, &writeCursor);
    state->LastPlayCursor = playCursor;

    const HRESULT result = state->HostBuffer->Play(0, 0, DSBPLAY_LOOPING);
    state->Playing = SUCCEEDED(result);
    return result;
}

void ClearStreamBuffer(XTL::X_DSoundStreamState* state)
{
    LPVOID first = NULL;
    LPVOID second = NULL;
    DWORD firstSize = 0;
    DWORD secondSize = 0;

    const HRESULT result = state->HostBuffer->Lock(
        0, state->BufferBytes, &first, &firstSize, &second, &secondSize, DSBLOCK_ENTIREBUFFER);
    if(FAILED(result))
    {
        return;
    }

    const int silence = state->Format->wBitsPerSample == 8 ? 0x80 : 0x00;
    if(first != NULL)
    {
        std::memset(first, silence, firstSize);
    }
    if(second != NULL)
    {
        std::memset(second, silence, secondSize);
    }

    state->HostBuffer->Unlock(first, firstSize, second, secondSize);
}

void FinishStreamPacket(XTL::X_DSoundStreamState* state, const StreamPacket& packet, DWORD status)
{
    if(packet.CompletedSize != NULL)
    {
        *packet.CompletedSize = status == kXMediaPacketStatusSuccess ? packet.Size : 0;
    }
    if(packet.Status != NULL)
    {
        *packet.Status = status;
    }

    if(status == kXMediaPacketStatusSuccess &&
       cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
    {
        cxbx::trace::RecordBinary(cxbx::trace::Event::AudioPacketCompleted,
                                  packet.Size);
    }

    if(state->Callback != NULL)
    {
        EmuSwapFS();
        state->Callback(state->CallbackContext, packet.Context, status);
        EmuSwapFS();
    }
    else if(packet.CompletionEvent != NULL)
    {
        SetEvent(packet.CompletionEvent);
    }
}

void ResetStoppedStream(XTL::X_DSoundStreamState* state)
{
    state->HostBuffer->Stop();
    state->HostBuffer->SetCurrentPosition(0);
    state->LastPlayCursor = 0;
    state->PlayedBytes = 0;
    state->WrittenBytes = 0;
    state->Playing = false;
    ClearStreamBuffer(state);
}

void PumpStream(XTL::X_DSoundStreamState* state)
{
    for(;;)
    {
        StreamPacket completed = {};
        bool hasCompletion = false;

        EnterCriticalSection(&state->Lock);

        if(state->Playing && !state->Paused)
        {
            DWORD playCursor = 0;
            DWORD writeCursor = 0;
            if(SUCCEEDED(state->HostBuffer->GetCurrentPosition(&playCursor, &writeCursor)))
            {
                const DWORD delta = playCursor >= state->LastPlayCursor
                                        ? playCursor - state->LastPlayCursor
                                        : state->BufferBytes - state->LastPlayCursor + playCursor;
                state->PlayedBytes += delta;
                state->LastPlayCursor = playCursor;
            }
        }

        if(state->PacketCount != 0)
        {
            StreamPacket& front = state->Packets[state->PacketHead];
            if(front.EndPosition <= state->PlayedBytes)
            {
                completed = front;
                state->PacketHead = (state->PacketHead + 1) % state->PacketCapacity;
                state->PacketCount--;
                hasCompletion = true;
            }
        }

        if(!hasCompletion && state->PacketCount == 0 && state->Playing && state->PlayedBytes >= state->WrittenBytes)
        {
            if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
            {
                cxbx::trace::RecordBinary(
                    cxbx::trace::Event::AudioStarved,
                    static_cast<std::uint32_t>(state->PlayedBytes));
            }
            ResetStoppedStream(state);
        }

        LeaveCriticalSection(&state->Lock);

        if(!hasCompletion)
        {
            break;
        }

        FinishStreamPacket(state, completed, kXMediaPacketStatusSuccess);
    }
}

void FlushStream(XTL::X_DSoundStreamState* state, bool invokeCallbacks)
{
    for(;;)
    {
        StreamPacket flushed = {};
        bool hasPacket = false;

        EnterCriticalSection(&state->Lock);
        if(state->PacketCount != 0)
        {
            flushed = state->Packets[state->PacketHead];
            state->PacketHead = (state->PacketHead + 1) % state->PacketCapacity;
            state->PacketCount--;
            hasPacket = true;
        }
        else
        {
            ResetStoppedStream(state);
            state->Paused = false;
            state->Synchronized = false;
        }
        LeaveCriticalSection(&state->Lock);

        if(!hasPacket)
        {
            break;
        }

        if(invokeCallbacks)
        {
            FinishStreamPacket(state, flushed, kXMediaPacketStatusFlushed);
        }
        else
        {
            if(flushed.CompletedSize != NULL)
            {
                *flushed.CompletedSize = 0;
            }
            if(flushed.Status != NULL)
            {
                *flushed.Status = kXMediaPacketStatusFlushed;
            }
            if(state->Callback == NULL && flushed.CompletionEvent != NULL)
            {
                SetEvent(flushed.CompletionEvent);
            }
        }
    }
}

void DestroyStream(XTL::X_CDirectSoundStream* stream)
{
    XTL::X_DSoundStreamState* state = stream->EmuState;
    if(state == NULL)
    {
        delete stream;
        return;
    }

    UnregisterStream(state);
    stream->EmuState = NULL;
    FlushStream(state, false);

    if(state->HostBuffer != NULL)
    {
        state->HostBuffer->Release();
    }

    DeleteCriticalSection(&state->Lock);
    delete[] reinterpret_cast<BYTE*>(state->Format);
    delete[] state->Packets;
    delete state;
    delete stream;
}

ULONG ReleaseStreamReference(XTL::X_CDirectSoundStream* stream)
{
    const LONG count = InterlockedDecrement(&stream->EmuRefCount);
    if(count == 0)
    {
        DestroyStream(stream);
    }

    return count > 0 ? static_cast<ULONG>(count) : 0;
}

HRESULT CreateHostStream(const XTL::X_DSSTREAMDESC* descriptor, XTL::X_CDirectSoundStream** streamOut)
{
    if(descriptor == NULL || streamOut == NULL || descriptor->lpwfxFormat == NULL)
    {
        return E_INVALIDARG;
    }

    *streamOut = NULL;

    HRESULT result = EnsureDirectSoundDevice();
    if(FAILED(result))
    {
        return result;
    }

    const DWORD packetCapacity = descriptor->dwMaxAttachedPackets == 0
                                     ? 1
                                     : descriptor->dwMaxAttachedPackets;
    if(packetCapacity > kStreamMaximumPackets)
    {
        return E_INVALIDARG;
    }

    const size_t formatBytes = sizeof(WAVEFORMATEX) + descriptor->lpwfxFormat->cbSize;
    if(formatBytes > sizeof(WAVEFORMATEX) + 256)
    {
        return E_INVALIDARG;
    }

    XTL::X_CDirectSoundStream* stream = new(std::nothrow) XTL::X_CDirectSoundStream();
    XTL::X_DSoundStreamState* state = new(std::nothrow) XTL::X_DSoundStreamState();
    BYTE* formatStorage = new(std::nothrow) BYTE[formatBytes];
    StreamPacket* packets = new(std::nothrow) StreamPacket[packetCapacity];
    if(stream == NULL || state == NULL || formatStorage == NULL || packets == NULL)
    {
        delete stream;
        delete state;
        delete[] formatStorage;
        delete[] packets;
        return E_OUTOFMEMORY;
    }

    std::memset(state, 0, sizeof(*state));
    std::memset(packets, 0, sizeof(*packets) * packetCapacity);
    std::memcpy(formatStorage, descriptor->lpwfxFormat, formatBytes);

    state->Owner = stream;
    state->Format = reinterpret_cast<WAVEFORMATEX*>(formatStorage);
    state->Callback = descriptor->lpfnCallback;
    state->CallbackContext = descriptor->lpvContext;
    state->Packets = packets;
    state->PacketCapacity = packetCapacity;
    state->Volume = DSBVOLUME_MAX;
    if(!ConfigureHostStreamFormat(state))
    {
        delete stream;
        delete state;
        delete[] formatStorage;
        delete[] packets;
        return DSERR_BADFORMAT;
    }
    InitializeCriticalSection(&state->Lock);

    uint64_t desiredBytes =
        static_cast<uint64_t>(state->HostFormat.nAvgBytesPerSec) * 2;
    if(desiredBytes < kStreamBufferMinimumBytes)
    {
        desiredBytes = kStreamBufferMinimumBytes;
    }
    if(desiredBytes > kStreamBufferMaximumBytes)
    {
        desiredBytes = kStreamBufferMaximumBytes;
    }

    const DWORD blockAlign =
        state->HostFormat.nBlockAlign == 0 ? 1 : state->HostFormat.nBlockAlign;
    state->BufferBytes = static_cast<DWORD>(desiredBytes) / blockAlign * blockAlign;

    XTL::DSBUFFERDESC hostDescriptor = {};
    hostDescriptor.dwSize = sizeof(hostDescriptor);
    hostDescriptor.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
    hostDescriptor.dwBufferBytes = state->BufferBytes;
    hostDescriptor.lpwfxFormat = &state->HostFormat;

    result = g_pDSound8->CreateSoundBuffer(&hostDescriptor, &state->HostBuffer, NULL);
    if(FAILED(result))
    {
        DeleteCriticalSection(&state->Lock);
        delete stream;
        delete state;
        delete[] formatStorage;
        delete[] packets;
        return result;
    }

    ClearStreamBuffer(state);
    stream->EmuState = state;
    RegisterStream(state);
    *streamOut = stream;

    if(state->DownmixPcmToStereo)
    {
        printf("EmuDSound: downmixing %u-channel PCM stream to stereo (%lu Hz)\n",
               state->Format->nChannels,
               state->Format->nSamplesPerSec);
    }

    return DS_OK;
}
} // namespace

HRESULT XTL::EmuDSoundCreateHostBuffer(
    const WAVEFORMATEX* pFormat,
    const BYTE* pData,
    DWORD dwSize,
    IDirectSoundBuffer** ppBuffer)
{
    if(ppBuffer == NULL)
    {
        return E_POINTER;
    }
    *ppBuffer = NULL;
    if(pFormat == NULL || pData == NULL || dwSize == 0 ||
       pFormat->wFormatTag != WAVE_FORMAT_PCM)
    {
        return E_INVALIDARG;
    }

    HRESULT result = EnsureDirectSoundDevice();
    if(FAILED(result))
    {
        return result;
    }

    DSBUFFERDESC descriptor = {};
    descriptor.dwSize = sizeof(descriptor);
    descriptor.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY |
                         DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
    descriptor.dwBufferBytes = dwSize;
    descriptor.lpwfxFormat = const_cast<WAVEFORMATEX*>(pFormat);

    IDirectSoundBuffer* buffer = NULL;
    result = g_pDSound8->CreateSoundBuffer(&descriptor, &buffer, NULL);
    if(FAILED(result))
    {
        return result;
    }

    LPVOID first = NULL;
    LPVOID second = NULL;
    DWORD firstSize = 0;
    DWORD secondSize = 0;
    result = buffer->Lock(
        0, dwSize, &first, &firstSize, &second, &secondSize,
        DSBLOCK_ENTIREBUFFER);
    if(SUCCEEDED(result))
    {
        std::memcpy(first, pData, firstSize);
        if(second != NULL)
        {
            std::memcpy(second, pData + firstSize, secondSize);
        }
        result = buffer->Unlock(first, firstSize, second, secondSize);
    }
    if(FAILED(result))
    {
        buffer->Release();
        return result;
    }

    *ppBuffer = buffer;
    return DS_OK;
}

// ******************************************************************
// * EmuStatic Variable(s)
// ******************************************************************
XTL::X_CDirectSoundStream::_vtbl XTL::X_CDirectSoundStream::vtbl = {
    &XTL::EmuCDirectSoundStream_AddRef,        // 0x00 - AddRef
    &XTL::EmuCDirectSoundStream_Release,       // 0x04
    &XTL::EmuCDirectSoundStream_GetInfo,       // 0x08 - GetInfo
    &XTL::EmuCDirectSoundStream_GetStatus,     // 0x0C - GetStatus
    &XTL::EmuCDirectSoundStream_Process,       // 0x10 - Process
    &XTL::EmuCDirectSoundStream_Discontinuity, // 0x14 - Discontinuity
    &XTL::EmuCDirectSoundStream_Flush          // 0x18 - Flush
};

// ******************************************************************
// * func: EmuDirectSoundCreate
// ******************************************************************
HRESULT WINAPI XTL::EmuDirectSoundCreate
(
    LPVOID          pguidDeviceId,
    LPDIRECTSOUND8 *ppDirectSound,
    LPUNKNOWN       pUnknown
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuDirectSoundCreate\n"
               "(\n"
               "   pguidDeviceId             : 0x%.08X\n"
               "   ppDirectSound             : 0x%.08X\n"
               "   pUnknown                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pguidDeviceId, ppDirectSound, pUnknown);
    }
    #endif

    // Xbox DirectSound is a process singleton. Hand back the existing host
    // device (AddRef'd) rather than leaking a second one -- XMV video decode
    // calls DirectSoundCreate every frame for A/V pacing, and a title that owns
    // the device must not have it clobbered/released out from under it.
    HRESULT hRet = DS_OK;

    if(g_pDSound8 == NULL)
    {
        hRet = DirectSoundCreate8(NULL, &g_pDSound8, NULL);

        if(SUCCEEDED(hRet) && g_pDSound8 != NULL)
            hRet = g_pDSound8->SetCooperativeLevel(g_hEmuWindow, DSSCL_PRIORITY);
    }
    else
    {
        g_pDSound8->AddRef();
    }

    *ppDirectSound = g_pDSound8;

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuDirectSoundCreateBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuDirectSoundCreateBuffer
(
    X_DSBUFFERDESC         *pdsbd,
    X_CDirectSoundBuffer  **ppBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuDirectSoundCreateBuffer\n"
               "(\n"
               "   pdsbd                     : 0x%.08X\n"
               "   ppBuffer                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pdsbd, ppBuffer);
    }
    #endif

    DSBUFFERDESC DSBufferDesc;
    WAVEFORMATEX hostFormat = {};

    // ******************************************************************
    // * Convert from Xbox to PC DSound
    // ******************************************************************
    {
        DWORD dwAcceptableMask = 0x00000010 | 0x00000020 | 0x00000080 | 0x00000100 | 0x00002000 | 0x00040000 | 0x00080000;

        if(pdsbd->dwFlags & (~dwAcceptableMask))
            printf("*Warning* use of unsupported pdsbd->dwFlags mask(s) (0x%.08X)\n", pdsbd->dwFlags & (~dwAcceptableMask));

        DSBufferDesc.dwSize = sizeof(DSBufferDesc);
        DSBufferDesc.dwFlags = pdsbd->dwFlags & dwAcceptableMask;

        if(pdsbd->dwBufferBytes < DSBSIZE_MIN)
            DSBufferDesc.dwBufferBytes = 16384; // NOTE: HACK: TEMPORARY FOR STELLA
        else
            DSBufferDesc.dwBufferBytes = pdsbd->dwBufferBytes;

        DSBufferDesc.dwReserved = 0;
        DSBufferDesc.lpwfxFormat = pdsbd->lpwfxFormat;
        if(pdsbd->lpwfxFormat != NULL && pdsbd->lpwfxFormat->wFormatTag == kWaveFormatXboxAdpcm)
        {
            if(!ConfigureStaticHostFormat(pdsbd->lpwfxFormat, hostFormat))
            {
                EmuSwapFS();   // XBox FS
                return DSERR_BADFORMAT;
            }

            DSBufferDesc.lpwfxFormat = &hostFormat;
            const std::size_t decodedBytes = cxbx::audio::XboxAdpcmDecodedBytes(
                pdsbd->dwBufferBytes, pdsbd->lpwfxFormat->nChannels);
            if(decodedBytes >= DSBSIZE_MIN && decodedBytes <= MAXDWORD)
                DSBufferDesc.dwBufferBytes = static_cast<DWORD>(decodedBytes);
        }
        DSBufferDesc.guid3DAlgorithm = DS3DALG_DEFAULT;
    }

    // Todo: Garbage Collection
    *ppBuffer = new X_CDirectSoundBuffer();
    (*ppBuffer)->EmuBufferFlags = DSBufferDesc.dwFlags;
    (*ppBuffer)->EmuBufferBytes = DSBufferDesc.dwBufferBytes;
    CacheStaticBufferFormat(*ppBuffer, pdsbd->lpwfxFormat);

    HRESULT hRet = g_pDSound8->CreateSoundBuffer(&DSBufferDesc, &((*ppBuffer)->EmuDirectSoundBuffer8), NULL);

    if(FAILED(hRet))
        EmuWarning("CreateSoundBuffer FAILED");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuDirectSoundCreateStream
// ******************************************************************
HRESULT WINAPI XTL::EmuDirectSoundCreateStream
(
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuDirectSoundCreateStream\n"
               "(\n"
               "   pdssd                     : 0x%.08X\n"
               "   ppStream                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pdssd, ppStream);
    }
    #endif

    HRESULT hRet = CreateHostStream(pdssd, ppStream);

#ifdef _DEBUG_TRACE
    if(pdssd != NULL && pdssd->lpwfxFormat != NULL)
    {
        printf("EmuDSound: stream format tag=0x%.04X channels=%u rate=%u bits=%u packet_cap=%u result=0x%.08X\n",
               pdssd->lpwfxFormat->wFormatTag,
               pdssd->lpwfxFormat->nChannels,
               pdssd->lpwfxFormat->nSamplesPerSec,
               pdssd->lpwfxFormat->wBitsPerSample,
               pdssd->dwMaxAttachedPackets,
               hRet);
    }
#endif

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_CreateStream
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_CreateStream
(
    LPDIRECTSOUND8          pThis,
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream,
    PVOID                   pUnknown
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_CreateStream\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pdssd                     : 0x%.08X\n"
               "   ppStream                  : 0x%.08X\n"
               "   pUnknown                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pdssd, ppStream, pUnknown);
    }
    #endif

    HRESULT hRet = CreateHostStream(pdssd, ppStream);

    EmuSwapFS();   // XBox FS

    return hRet;
}


// ******************************************************************
// * func: EmuIDirectSound8_CreateBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_CreateBuffer
(
    LPDIRECTSOUND8          pThis,
    X_DSBUFFERDESC         *pdssd,
    X_CDirectSoundBuffer  **ppBuffer,
    PVOID                   pUnknown
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
       EmuSwapFS();   // Win2k/XP FS
       printf("EmuDSound (0x%X): EmuIDirectSound8_CreateBuffer\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pdssd                     : 0x%.08X\n"
               "   ppBuffer                  : 0x%.08X\n"
               "   pUnknown                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pdssd, ppBuffer, pUnknown);
       EmuSwapFS();   // XBox FS
    }
    #endif

    EmuDirectSoundCreateBuffer(pdssd, ppBuffer);

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetVolume
// ******************************************************************
ULONG WINAPI XTL::EmuCDirectSoundStream_SetVolume(X_CDirectSoundStream *pThis, LONG lVolume)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetVolume\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   lVolume                   : %d\n"
               ");\n",
               GetCurrentThreadId(), pThis, lVolume);
    }
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    HRESULT hRet = E_INVALIDARG;
    if(stream != NULL && stream->EmuState != NULL)
    {
        X_DSoundStreamState* state = stream->EmuState;
        EnterCriticalSection(&state->Lock);
        state->Volume = lVolume;
        hRet = state->HostBuffer->SetVolume(lVolume);
        LeaveCriticalSection(&state->Lock);
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetRolloffFactor
(
    X_CDirectSoundStream *pThis,
    FLOAT                 fRolloffFactor,
    DWORD                 dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetRolloffFactor\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   fRolloffFactor            : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, fRolloffFactor, dwApply);
    }
    #endif

    // TODO: Actually SetRolloffFactor

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuCDirectSoundStream_AddRef(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_AddRef\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    ULONG count = 0;
    if(stream != NULL)
    {
        count = static_cast<ULONG>(InterlockedIncrement(&stream->EmuRefCount));
    }

    EmuSwapFS();   // XBox FS

    return count;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Release
// ******************************************************************
ULONG WINAPI XTL::EmuCDirectSoundStream_Release(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_Release\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    ULONG count = 0;
    if(stream != NULL)
    {
        count = ReleaseStreamReference(stream);
    }

    EmuSwapFS();   // XBox FS

    return count;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Process
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Process(
    X_CDirectSoundStream* pThis,
    const X_XMEDIAPACKET* pInputBuffer,
    const X_XMEDIAPACKET* pOutputBuffer)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_Process\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pInputBuffer              : 0x%.08X\n"
               "   pOutputBuffer             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pInputBuffer, pOutputBuffer);
    }
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    if(stream == NULL || stream->EmuState == NULL || pInputBuffer == NULL || pInputBuffer->pvBuffer == NULL || pOutputBuffer != NULL)
    {
        EmuSwapFS(); // XBox FS
        return E_INVALIDARG;
    }

    InterlockedIncrement(&stream->EmuRefCount);
    X_DSoundStreamState* state = stream->EmuState;
    PumpStream(state);

    DWORD hostPacketBytes = pInputBuffer->dwMaxSize;
    if(state->DownmixPcmToStereo)
    {
        const DWORD sourceBlockAlign = state->Format->nBlockAlign;
        if(sourceBlockAlign == 0 || pInputBuffer->dwMaxSize % sourceBlockAlign != 0)
        {
            ReleaseStreamReference(stream);
            EmuSwapFS(); // Xbox FS
            return E_INVALIDARG;
        }

        const uint64_t frameCount = pInputBuffer->dwMaxSize / sourceBlockAlign;
        const uint64_t convertedBytes =
            frameCount * state->HostFormat.nBlockAlign;
        if(convertedBytes > MAXDWORD)
        {
            ReleaseStreamReference(stream);
            EmuSwapFS(); // Xbox FS
            return E_INVALIDARG;
        }
        hostPacketBytes = static_cast<DWORD>(convertedBytes);
    }

    HRESULT hRet = DS_OK;
    EnterCriticalSection(&state->Lock);

    const uint64_t bufferedBytes = state->WrittenBytes - state->PlayedBytes;
    if(state->PacketCount >= state->PacketCapacity ||
       hostPacketBytes > state->BufferBytes ||
       bufferedBytes + hostPacketBytes > state->BufferBytes)
    {
        hRet = DSERR_OUTOFMEMORY;
    }
    else
    {
        LPVOID first = NULL;
        LPVOID second = NULL;
        DWORD firstSize = 0;
        DWORD secondSize = 0;
        const DWORD writeOffset = static_cast<DWORD>(state->WrittenBytes % state->BufferBytes);

        hRet = state->HostBuffer->Lock(
            writeOffset,
            hostPacketBytes,
            &first,
            &firstSize,
            &second,
            &secondSize,
            0);
        if(SUCCEEDED(hRet))
        {
            const BYTE* source = static_cast<const BYTE*>(pInputBuffer->pvBuffer);
            if(state->DownmixPcmToStereo)
            {
                const DWORD hostBlockAlign = state->HostFormat.nBlockAlign;
                const DWORD firstFrames = firstSize / hostBlockAlign;
                cxbx::audio::DownmixPcm16ToStereo(source,
                                                  firstFrames,
                                                  state->Format->nChannels,
                                                  static_cast<BYTE*>(first));
                if(second != NULL && secondSize != 0)
                {
                    const DWORD sourceOffset =
                        firstFrames * state->Format->nBlockAlign;
                    cxbx::audio::DownmixPcm16ToStereo(
                        source + sourceOffset,
                        secondSize / hostBlockAlign,
                        state->Format->nChannels,
                        static_cast<BYTE*>(second));
                }
            }
            else
            {
                std::memcpy(first, source, firstSize);
                if(second != NULL && secondSize != 0)
                {
                    std::memcpy(second, source + firstSize, secondSize);
                }
            }

            hRet = state->HostBuffer->Unlock(first, firstSize, second, secondSize);
        }

        if(SUCCEEDED(hRet))
        {
            const DWORD packetIndex = (state->PacketHead + state->PacketCount) % state->PacketCapacity;
            StreamPacket& packet = state->Packets[packetIndex];
            packet.CompletedSize = pInputBuffer->pdwCompletedSize;
            packet.Status = pInputBuffer->pdwStatus;
            packet.CompletionEvent = state->Callback == NULL ? pInputBuffer->hCompletionEvent : NULL;
            packet.Context = state->Callback != NULL ? pInputBuffer->pContext : NULL;
            packet.Size = pInputBuffer->dwMaxSize;
            state->WrittenBytes += hostPacketBytes;
            packet.EndPosition = state->WrittenBytes;
            state->PacketCount++;

            if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
            {
                cxbx::trace::RecordBinary(cxbx::trace::Event::AudioPacketQueued,
                                          pInputBuffer->dwMaxSize);
            }

            if(packet.CompletedSize != NULL)
            {
                *packet.CompletedSize = 0;
            }
            if(packet.Status != NULL)
            {
                *packet.Status = kXMediaPacketStatusPending;
            }

            if(!state->Playing && !state->Paused)
            {
                hRet = StartStreamPlaybackLocked(state);
            }

            if(FAILED(hRet))
            {
                state->PacketCount--;
                state->WrittenBytes -= hostPacketBytes;
                if(packet.Status != NULL)
                {
                    *packet.Status = kXMediaPacketStatusFailure;
                }
            }
        }
    }

    LeaveCriticalSection(&state->Lock);
    ReleaseStreamReference(stream);

    EmuSwapFS(); // XBox FS

    return hRet;
}

HRESULT WINAPI XTL::EmuCDirectSoundStream_GetInfo(
    X_CDirectSoundStream* pThis,
    X_XMEDIAINFO* pInfo)
{
    EmuSwapFS(); // Win2k/XP FS

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    if(stream == NULL || stream->EmuState == NULL || pInfo == NULL)
    {
        EmuSwapFS(); // XBox FS
        return E_INVALIDARG;
    }

    X_DSoundStreamState* state = stream->EmuState;
    pInfo->dwFlags = 0x00000004;
    pInfo->dwInputSize = state->Format->nBlockAlign;
    pInfo->dwOutputSize = 0;
    pInfo->dwMaxLookahead = 0;

    EmuSwapFS();   // XBox FS
    return DS_OK;
}

HRESULT WINAPI XTL::EmuCDirectSoundStream_GetStatus(
    X_CDirectSoundStream* pThis,
    LPDWORD pdwStatus)
{
    EmuSwapFS(); // Win2k/XP FS

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    if(stream == NULL || stream->EmuState == NULL || pdwStatus == NULL)
    {
        EmuSwapFS(); // XBox FS
        return E_INVALIDARG;
    }

    InterlockedIncrement(&stream->EmuRefCount);
    X_DSoundStreamState* state = stream->EmuState;
    PumpStream(state);

    EnterCriticalSection(&state->Lock);
    DWORD status = 0;
    if(state->PacketCount < state->PacketCapacity)
    {
        status |= kXmoStatusAcceptInput;
    }
    if(state->Playing)
    {
        status |= kDsStreamStatusPlaying;
    }
    if(state->Paused)
    {
        status |= kDsStreamStatusPaused;
    }
    if(state->PacketCount == 0)
    {
        status |= kDsStreamStatusStarved;
    }
    *pdwStatus = status;
    LeaveCriticalSection(&state->Lock);

    ReleaseStreamReference(stream);
    EmuSwapFS(); // XBox FS
    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Discontinuity
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Discontinuity(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_Discontinuity\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    if(stream == NULL || stream->EmuState == NULL)
    {
        EmuSwapFS(); // XBox FS
        return E_INVALIDARG;
    }

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Pause
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Pause
(
    PVOID   pStream,
    DWORD   dwPause
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_Pause\n"
               "(\n"
               "   pStream                   : 0x%.08X\n"
               "   dwPause                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pStream, dwPause);
    }
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pStream);
    if(stream == NULL || stream->EmuState == NULL || dwPause > kDsStreamPauseNoActivate)
    {
        EmuSwapFS(); // XBox FS
        return E_INVALIDARG;
    }

    InterlockedIncrement(&stream->EmuRefCount);
    X_DSoundStreamState* state = stream->EmuState;
    PumpStream(state);

    EnterCriticalSection(&state->Lock);
    HRESULT hRet = DS_OK;
    switch(dwPause)
    {
    case kDsStreamPausePause:
    case kDsStreamPauseSynchPlayback:
    case kDsStreamPauseNoActivate:
        hRet = state->HostBuffer->Stop();
        if(SUCCEEDED(hRet))
        {
            state->Paused = true;
            state->Synchronized = dwPause == kDsStreamPauseSynchPlayback;
        }
        break;

    case kDsStreamPauseResume:
        state->Paused = false;
        state->Synchronized = false;
        hRet = StartStreamPlaybackLocked(state);
        break;
    }
    LeaveCriticalSection(&state->Lock);

    ReleaseStreamReference(stream);
    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_AddRef
// ******************************************************************
ULONG WINAPI XTL::EmuIDirectSound8_AddRef
(
    LPDIRECTSOUND8          pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_AddRef\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    ULONG uRet = pThis->AddRef();

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirectSound8_Release
(
    LPDIRECTSOUND8          pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_Release\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    ULONG uRet = pThis->Release();

    // Drop the singleton cache once the host device is really gone, so a later
    // DirectSoundCreate re-creates it instead of handing back a freed pointer.
    if(uRet == 0 && pThis == g_pDSound8)
        g_pDSound8 = NULL;

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_SynchPlayback
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SynchPlayback
(
    LPDIRECTSOUND8          pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_SynchPlayback\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    HRESULT hRet = DS_OK;

    AcquireSRWLockShared(&g_DSoundStreamListLock);
    for(X_DSoundStreamState* state = g_DSoundStreams; state != NULL; state = state->Next)
    {
        EnterCriticalSection(&state->Lock);
        if(state->Synchronized)
        {
            state->Synchronized = false;
            state->Paused = false;

            const HRESULT streamResult = StartStreamPlaybackLocked(state);
            if(FAILED(streamResult) && SUCCEEDED(hRet))
            {
                hRet = streamResult;
            }
        }
        LeaveCriticalSection(&state->Lock);
    }
    ReleaseSRWLockShared(&g_DSoundStreamListLock);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSound8_GetOutputLevels
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_GetOutputLevels
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pOutputLevels,
    BOOL                    fResetPeakValues
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_GetOutputLevels\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pOutputLevels             : 0x%.08X\n"
               "   fResetPeakValues          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pOutputLevels, fResetPeakValues);
    }
    #endif

    if(pOutputLevels != NULL)
    {
        ZeroMemory(pOutputLevels, 16 * sizeof(DWORD));
    }

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_DownloadEffectsImage
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_DownloadEffectsImage
(
    LPDIRECTSOUND8          pThis,
    LPCVOID                 pvImageBuffer,
    DWORD                   dwImageSize,
    PVOID                   pImageLoc,      // TODO: Use this param
    PVOID                  *ppImageDesc     // TODO: Use this param
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_DownloadEffectsImage\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pvImageBuffer             : 0x%.08X\n"
               "   dwImageSize               : 0x%.08X\n"
               "   pImageLoc                 : 0x%.08X\n"
               "   ppImageDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pvImageBuffer, dwImageSize, pImageLoc, ppImageDesc);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetHeadroom
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetHeadroom
(
    PVOID   pThis,
    DWORD   dwHeadroom
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundStream_SetHeadroom\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwHeadroom                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwHeadroom);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeAngles
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetConeAngles
(
    PVOID   pThis,
    DWORD   dwInsideConeAngle,
    DWORD   dwOutsideConeAngle,
    DWORD   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetConeAngles\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwInsideConeAngle         : 0x%.08X\n"
               "   dwOutsideConeAngle        : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwInsideConeAngle, dwOutsideConeAngle, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeOutsideVolume
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetConeOutsideVolume
(
    PVOID   pThis,
    LONG    lConeOutsideVolume,
    DWORD   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetConeOutsideVolume\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   lConeOutsideVolume        : %d\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, lConeOutsideVolume, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetAllParameters
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetAllParameters
(
    PVOID    pThis,
    PVOID    pUnknown,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetAllParameters\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pUnknown                  : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pUnknown, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetMaxDistance
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetMaxDistance
(
    PVOID    pThis,
    D3DVALUE fMaxDistance,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetMaxDistance\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   fMaxDistance              : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, fMaxDistance, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetMinDistance
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetMinDistance
(
    PVOID    pThis,
    D3DVALUE fMinDistance,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetMinDistance\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   fMinDistance              : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, fMinDistance, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetVelocity
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetVelocity
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetVelocity\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   x                         : %f\n"
               "   y                         : %f\n"
               "   z                         : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, x, y, z, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetConeOrientation
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetConeOrientation
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetConeOrientation\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   x                         : %f\n"
               "   y                         : %f\n"
               "   z                         : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, x, y, z, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetPosition
(
    PVOID    pThis,
    D3DVALUE x,
    D3DVALUE y,
    D3DVALUE z,
    DWORD    dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetPosition\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   x                         : %f\n"
               "   y                         : %f\n"
               "   z                         : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, x, y, z, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundStream_SetFrequency
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_SetFrequency
(
    PVOID   pThis,
    DWORD   dwFrequency
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundStream_SetFrequency\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwFrequency               : %d\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwFrequency);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetI3DL2Source
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetI3DL2Source
(
    PVOID   pThis,
    PVOID   pds3db,
    DWORD   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundStream_SetI3DL2Source\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pds3db                    : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pds3db, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetOrientation
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetOrientation
(
    LPDIRECTSOUND8  pThis,
    FLOAT           xFront,
    FLOAT           yFront,
    FLOAT           zFront,
    FLOAT           xTop,
    FLOAT           yTop,
    FLOAT           zTop,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetOrientation\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   xFront                    : %f\n"
               "   yFront                    : %f\n"
               "   zFront                    : %f\n"
               "   xTop                      : %f\n"
               "   yTop                      : %f\n"
               "   zTop                      : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, xFront, yFront, zFront, xTop, yTop, zTop, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetDistanceFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetDistanceFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fDistanceFactor,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetDistanceFactor\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   fDistanceFactor           : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, fDistanceFactor, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetRolloffFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetRolloffFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fRolloffFactor,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetRolloffFactor\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   fRolloffFactor            : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, fRolloffFactor, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetDopplerFactor
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetDopplerFactor
(
    LPDIRECTSOUND8  pThis,
    FLOAT           fDopplerFactor,
    DWORD           dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetDopplerFactor\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   fDopplerFactor            : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, fDopplerFactor, dwApply);
    }
    #endif

    // TODO: Actually implement this

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_CreateSoundBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_CreateSoundBuffer
(
    LPDIRECTSOUND8          pThis,
    X_DSBUFFERDESC         *pdsbd,
    X_CDirectSoundBuffer  **ppBuffer,
    LPUNKNOWN               pUnkOuter
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuDSound (0x%X): EmuIDirectSound8_CreateSoundBuffer\n"
               "(\n"
               "   pdsbd                     : 0x%.08X\n"
               "   ppBuffer                  : 0x%.08X\n"
               "   pUnkOuter                 : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pdsbd, ppBuffer, pUnkOuter);
        EmuSwapFS();   // XBox FS
    }
    #endif

    return EmuDirectSoundCreateBuffer(pdsbd, ppBuffer);
}

// ******************************************************************
// * func: EmuIDirectSound8_CreateSoundStream
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_CreateSoundStream
(
    LPDIRECTSOUND8          pThis,
    X_DSSTREAMDESC         *pdssd,
    X_CDirectSoundStream  **ppStream,
    LPUNKNOWN               pUnkOuter
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuDSound (0x%X): EmuIDirectSound8_CreateSoundStream\n"
               "(\n"
               "   pdssd                     : 0x%.08X\n"
               "   ppStream                  : 0x%.08X\n"
               "   pUnkOuter                 : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pdssd, ppStream, pUnkOuter);
        EmuSwapFS();   // XBox FS
    }
    #endif

    return EmuDirectSoundCreateStream(pdssd, ppStream);
}

// ******************************************************************
// * func: EmuIDirectSound8_SetI3DL2Listener
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetI3DL2Listener
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pDummy, // TODO: fill this out
    DWORD                   dwApply
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetI3DL2Listener\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pDummy                    : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pDummy, dwApply);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetMixBinHeadroom
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetMixBinHeadroom
(
    LPDIRECTSOUND8          pThis,
    DWORD                   dwMixBinMask,
    DWORD                   dwHeadroom
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetMixBinHeadroom\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwMixBinMask              : 0x%.08X\n"
               "   dwHeadroom                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwMixBinMask, dwHeadroom);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetPosition
(
    LPDIRECTSOUND8          pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetPosition\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   x                         : %f\n"
               "   y                         : %f\n"
               "   z                         : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, x, y, z, dwApply);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetVelocity
(
    LPDIRECTSOUND8          pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetVelocity\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   x                         : %f\n"
               "   y                         : %f\n"
               "   z                         : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, x, y, z, dwApply);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_SetAllParameters
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_SetAllParameters
(
    LPDIRECTSOUND8          pThis,
    LPVOID                  pTodo,  // TODO: LPCDS3DLISTENER
    DWORD                   dwApply
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuDSound (0x%X): EmuIDirectSound8_SetAllParameters\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pTodo                     : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pTodo, dwApply);
        EmuSwapFS();   // XBox FS
    }
    #endif

    // TODO: Actually do something

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetBufferData
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetBufferData
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pvBufferData,
    DWORD                   dwBufferBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetBufferData\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pvBufferData              : 0x%.08X\n"
               "   dwBufferBytes             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pvBufferData, dwBufferBytes);
    }
    #endif

    if(pThis == NULL)
    {
        EmuSwapFS();   // XBox FS

        return DSERR_INVALIDPARAM;
    }

    if(dwBufferBytes == 0)
    {
        EmuSwapFS();   // XBox FS

        return DS_OK;
    }

    const WAVEFORMATEX* guestFormat = pThis->EmuWaveFormatBytes >= sizeof(WAVEFORMATEX)
                                         ? reinterpret_cast<const WAVEFORMATEX*>(pThis->EmuWaveFormat)
                                         : NULL;
    WAVEFORMATEX hostFormat = {};
    const WAVEFORMATEX* replacementFormat = guestFormat;
    const void* hostData = pvBufferData;
    DWORD hostDataBytes = dwBufferBytes;
    std::unique_ptr<std::uint8_t[]> decodedData;

    if(guestFormat != NULL && guestFormat->wFormatTag == kWaveFormatXboxAdpcm)
    {
        const std::size_t decodedBytes = cxbx::audio::XboxAdpcmDecodedBytes(
            dwBufferBytes, guestFormat->nChannels);
        if(!ConfigureStaticHostFormat(guestFormat, hostFormat) || decodedBytes == 0 ||
           decodedBytes > MAXDWORD || pvBufferData == NULL ||
           IsBadReadPtr(pvBufferData, dwBufferBytes))
        {
            EmuSwapFS();   // XBox FS
            return DSERR_BADFORMAT;
        }

        decodedData.reset(new(std::nothrow) std::uint8_t[decodedBytes]);
        if(decodedData == NULL)
        {
            EmuSwapFS();   // XBox FS
            return E_OUTOFMEMORY;
        }

        if(!cxbx::audio::DecodeXboxAdpcm(
               static_cast<const std::uint8_t*>(pvBufferData), dwBufferBytes,
               guestFormat->nChannels, decodedData.get(), decodedBytes))
        {
            EmuSwapFS();   // XBox FS
            return DSERR_BADFORMAT;
        }

        replacementFormat = &hostFormat;
        hostData = decodedData.get();
        hostDataBytes = static_cast<DWORD>(decodedBytes);
    }

    const DWORD hostBufferBytes = max(hostDataBytes, static_cast<DWORD>(DSBSIZE_MIN));
    if(hostBufferBytes != pThis->EmuBufferBytes || pThis->EmuDirectSoundBuffer8 == NULL)
    {
        DSBUFFERDESC descriptor = {};
        descriptor.dwSize = sizeof(descriptor);
        descriptor.dwFlags = pThis->EmuBufferFlags;
        descriptor.dwBufferBytes = hostBufferBytes;
        descriptor.lpwfxFormat = const_cast<WAVEFORMATEX*>(replacementFormat);
        descriptor.guid3DAlgorithm = DS3DALG_DEFAULT;

        if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
        {
            const WAVEFORMATEX* format = descriptor.lpwfxFormat;
            printf("AUDIO| static-buffer-resize this=0x%p old=0x%lX new=0x%lX flags=0x%lX format=%u/%u/%lu/%u\n",
                   pThis, pThis->EmuBufferBytes, hostBufferBytes, descriptor.dwFlags,
                   format != NULL ? format->wFormatTag : 0,
                   format != NULL ? format->nChannels : 0,
                   format != NULL ? format->nSamplesPerSec : 0,
                   format != NULL ? format->wBitsPerSample : 0);
        }

        IDirectSoundBuffer* replacement = NULL;
        HRESULT resizeResult = g_pDSound8->CreateSoundBuffer(&descriptor, &replacement, NULL);
        if(FAILED(resizeResult))
        {
            EmuSwapFS();   // XBox FS

            return resizeResult;
        }

        pThis->EmuDirectSoundBuffer8->Release();
        pThis->EmuDirectSoundBuffer8 = replacement;
        pThis->EmuBufferBytes = hostBufferBytes;
    }

    PVOID pAudioPtr, pAudioPtr2;
    DWORD dwAudioBytes, dwAudioBytes2;

    if(pThis->EmuDirectSoundBuffer8 == NULL)
    {
        EmuSwapFS();   // XBox FS

        return DSERR_GENERIC;
    }

    HRESULT hRet = pThis->EmuDirectSoundBuffer8->Lock(0, hostDataBytes, &pAudioPtr, &dwAudioBytes, &pAudioPtr2, &dwAudioBytes2, 0);

    if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
    {
        printf("AUDIO| static-buffer-lock this=0x%p source=0x%p requested=0x%lX result=0x%lX first=0x%p/0x%lX second=0x%p/0x%lX\n",
               pThis, pvBufferData, hostDataBytes, hRet,
               pAudioPtr, dwAudioBytes, pAudioPtr2, dwAudioBytes2);
    }

    if(SUCCEEDED(hRet))
    {
        if(hostData != NULL && !IsBadReadPtr(hostData, hostDataBytes))
        {
            if(pAudioPtr != 0 && dwAudioBytes != 0)
                memcpy(pAudioPtr, hostData, dwAudioBytes);
            if(pAudioPtr2 != 0 && dwAudioBytes2 != 0)
                memcpy(pAudioPtr2,
                       static_cast<const std::uint8_t*>(hostData) + dwAudioBytes,
                       dwAudioBytes2);
        }

        if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
            printf("AUDIO| static-buffer-copy-complete this=0x%p\n", pThis);

        pThis->EmuDirectSoundBuffer8->Unlock(pAudioPtr, dwAudioBytes, pAudioPtr2, dwAudioBytes2);

        if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
            printf("AUDIO| static-buffer-unlock-complete this=0x%p\n", pThis);
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetNotificationPositions
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetNotificationPositions
(
    X_CDirectSoundBuffer       *pThis,
    DWORD                       dwNotifyCount,
    const DSBPOSITIONNOTIFY    *paNotifies
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(pThis == NULL || pThis->EmuDirectSoundBuffer8 == NULL ||
       (dwNotifyCount != 0 && paNotifies == NULL))
    {
        EmuSwapFS();   // XBox FS
        return DSERR_INVALIDPARAM;
    }

    IDirectSoundNotify* notify = NULL;
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->QueryInterface(
        IID_IDirectSoundNotify, reinterpret_cast<void**>(&notify));
    if(SUCCEEDED(hRet))
    {
        hRet = notify->SetNotificationPositions(dwNotifyCount, paNotifies);
        notify->Release();
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetOutputBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetOutputBuffer
(
    X_CDirectSoundBuffer   *pThis,
    X_CDirectSoundBuffer   *pOutputBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetOutputBuffer\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pOutputBuffer             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pOutputBuffer);
    }
    #endif

    // Host DirectSound has no Xbox output-buffer routing equivalent. Accept
    // the request; a null destination selects the default hardware output.
    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Use3DVoiceData
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Use3DVoiceData
(
    X_CDirectSoundBuffer   *pThis,
    BOOL                    bUse3DVoiceData
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Use3DVoiceData\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   bUse3DVoiceData           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, bUse3DVoiceData);
    }
    #endif

    // Xbox voice-data reuse has no host DirectSound equivalent. The HLE
    // buffer already owns its host-side 3D state, so accept the mode change.
    EmuSwapFS();   // XBox FS

    return DS_OK;
}

HRESULT WINAPI XTL::EmuIDirectSoundStream_Use3DVoiceData
(
    X_CDirectSoundStream   *pThis,
    BOOL                    bUse3DVoiceData
)
{
    EmuSwapFS();   // Win2k/XP FS

    // Xbox voice-data reuse has no host DirectSound equivalent. Stream state
    // is already owned by the HLE object, so accept the mode change.
    (void)pThis;
    (void)bUse3DVoiceData;

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

HRESULT WINAPI XTL::EmuCDirectSoundVoice_SetPitch
(
    PVOID                   pThis,
    LONG                    lPitch
)
{
    EmuSwapFS();   // Win2k/XP FS

    // Xbox pitch is a relative voice parameter, not a host DirectSound
    // frequency. Accept it until voice pitch translation is implemented.
    (void)pThis;
    (void)lPitch;

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

HRESULT WINAPI XTL::EmuCDirectSoundVoice_GetVoiceProperties
(
    PVOID                   pThis,
    PVOID                   pVoiceProperties
)
{
    EmuSwapFS();   // Win2k/XP FS

    // DSVOICEPROPS is one count, eight DSMIXBINVOLUMEPAIR entries, and six
    // LONG values in the 5849 SDK. Report a valid, inactive voice state.
    const size_t voicePropertiesSize = sizeof(DWORD) + 8 * (sizeof(DWORD) + sizeof(LONG)) + 6 * sizeof(LONG);
    if (pVoiceProperties != NULL) {
        ZeroMemory(pVoiceProperties, voicePropertiesSize);
    }
    (void)pThis;

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetFormat
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetFormat
(
    X_CDirectSoundBuffer   *pThis,
    const WAVEFORMATEX     *pWaveFormat
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetFormat\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pWaveFormat               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pWaveFormat);
    }
    #endif

    // Secondary host DirectSound buffers cannot change format in place. Cache
    // the requested format so a later SetBufferData resize can recreate the
    // buffer with the format selected by the title.
    CacheStaticBufferFormat(pThis, pWaveFormat);

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetPlayRegion
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetPlayRegion
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwPlayStart,
    DWORD                   dwPlayLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetPlayRegion\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwPlayStart               : 0x%.08X\n"
               "   dwPlayLength              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwPlayStart, dwPlayLength);
    }
    #endif

    // Todo: Translate params, then make the PC DirectSound call

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetLoopRegion
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetLoopRegion
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwLoopStart,
    DWORD                   dwLoopLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetLoopRegion\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwLoopStart               : 0x%.08X\n"
               "   dwLoopLength              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwLoopStart, dwLoopLength);
    }
    #endif

    // Todo: Translate params, then make the PC DirectSound call

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetVolume
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetVolume
(
    X_CDirectSoundBuffer   *pThis,
    LONG                    lVolume
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetVolume\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   lVolume                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, lVolume);
    }
    #endif

    // Todo: Translate params, then make the PC DirectSound call

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetCurrentPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetCurrentPosition
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwNewPosition
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetCurrentPosition\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwNewPosition             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwNewPosition);
    }
    #endif

    if(pThis == NULL || pThis->EmuDirectSoundBuffer8 == NULL)
    {
        EmuSwapFS();   // XBox FS

        return DS_OK;
    }

    // NOTE: TODO: This call *will* (by MSDN) fail on primary buffers!
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->SetCurrentPosition(dwNewPosition);

    if(FAILED(hRet))
        EmuWarning("SetCurrentPosition FAILED");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_GetCurrentPosition
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_GetCurrentPosition
(
    X_CDirectSoundBuffer   *pThis,
    PDWORD                  pdwCurrentPlayCursor,
    PDWORD                  pdwCurrentWriteCursor
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_GetCurrentPosition\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pdwCurrentPlayCursor      : 0x%.08X\n"
               "   pdwCurrentWriteCursor     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pdwCurrentPlayCursor, pdwCurrentWriteCursor);
    }
    #endif

    if(pThis == NULL || pThis->EmuDirectSoundBuffer8 == NULL)
    {
        if(pdwCurrentPlayCursor != NULL)
            *pdwCurrentPlayCursor = 0;
        if(pdwCurrentWriteCursor != NULL)
            *pdwCurrentWriteCursor = 0;

        EmuSwapFS();   // XBox FS

        return DS_OK;
    }

    // NOTE: TODO: This call always seems to fail on primary buffers!
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->GetCurrentPosition(pdwCurrentPlayCursor, pdwCurrentWriteCursor);

    if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
    {
        static thread_local X_CDirectSoundBuffer* s_LastBuffer = NULL;
        static thread_local DWORD s_LastPlayCursor = MAXDWORD;
        static thread_local DWORD s_LastWriteCursor = MAXDWORD;
        const DWORD playCursor = pdwCurrentPlayCursor != NULL ? *pdwCurrentPlayCursor : 0;
        const DWORD writeCursor = pdwCurrentWriteCursor != NULL ? *pdwCurrentWriteCursor : 0;

        if(FAILED(hRet) || pThis != s_LastBuffer || playCursor != s_LastPlayCursor ||
           writeCursor != s_LastWriteCursor)
        {
            printf("AUDIO| static-buffer-position this=0x%p result=0x%lX play=0x%lX write=0x%lX\n",
                   pThis, hRet, playCursor, writeCursor);
            s_LastBuffer = pThis;
            s_LastPlayCursor = playCursor;
            s_LastWriteCursor = writeCursor;
        }
    }

    if(FAILED(hRet))
        EmuWarning("GetCurrentPosition FAILED");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Play
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Play
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwReserved1,
    DWORD                   dwReserved2,
    DWORD                   dwFlags
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Play\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwReserved1               : 0x%.08X\n"
               "   dwReserved2               : 0x%.08X\n"
               "   dwFlags                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwReserved1, dwReserved2, dwFlags);
    }
    #endif

    // Xbox-only flags (FROMSTART etc.) have no host bit; keep LOOPING and
    // play anyway rather than killing the process over a flag.
    if(dwFlags & (~DSBPLAY_LOOPING))
    {
        EmuWarning("Unsupported Playing Flags (0x%.08X)", dwFlags);
        dwFlags &= DSBPLAY_LOOPING;
    }

    if(pThis == NULL || pThis->EmuDirectSoundBuffer8 == NULL)
    {
        EmuSwapFS();   // XBox FS

        return DS_OK;
    }

    HRESULT hRet = pThis->EmuDirectSoundBuffer8->Play(0, 0, dwFlags);

    if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
    {
        printf("AUDIO| static-buffer-play this=0x%p flags=0x%lX result=0x%lX\n",
               pThis, dwFlags, hRet);
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_PlayEx
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_PlayEx
(
    X_CDirectSoundBuffer   *pThis,
    LONGLONG                rtTimeStamp,
    DWORD                   dwFlags
)
{
    // Host DirectSound has no timestamped static-buffer start. NestopiaX
    // passes zero and only needs the Xbox flags translated by Play.
    (void)rtTimeStamp;
    return EmuIDirectSoundBuffer8_Play(pThis, 0, 0, dwFlags);
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Stop
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Stop
(
    X_CDirectSoundBuffer   *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Stop\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    HRESULT hRet = DS_OK;

    if(pThis != NULL && pThis->EmuDirectSoundBuffer8 != NULL)
        hRet = pThis->EmuDirectSoundBuffer8->Stop();

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_StopEx
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_StopEx
(
    X_CDirectSoundBuffer   *pThis,
    LONGLONG                rtTimeStamp,
    DWORD                   dwFlags
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_StopEx\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   rtTimeStamp               : 0x%.016I64X\n"
               "   dwFlags                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, rtTimeStamp, dwFlags);
    }
    #endif

    HRESULT hRet = DS_OK;
    if(pThis != NULL && pThis->EmuDirectSoundBuffer8 != NULL)
    {
        hRet = pThis->EmuDirectSoundBuffer8->Stop();
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuDirectSoundUseFullHRTF
// ******************************************************************
VOID WINAPI XTL::EmuDirectSoundUseFullHRTF()
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuDirectSoundUseFullHRTF();\n", GetCurrentThreadId());
    #endif

    // Selects the full HRTF 3D-audio filter set on the Xbox DSP. The host
    // has no DSP; the guest implementation initializes MCPX state that
    // NULL-derefs against the register-level APU model, so accept and ignore.

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuXAudioDownloadEffectsImage
// ******************************************************************
HRESULT WINAPI XTL::EmuXAudioDownloadEffectsImage
(
    LPCSTR                  pszImageName,
    LPVOID                  pImageLoc,
    DWORD                   dwFlags,
    LPVOID                 *ppImageDesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuXAudioDownloadEffectsImage\n"
               "(\n"
               "   pszImageName              : 0x%.08X\n"
               "   pImageLoc                 : 0x%.08X\n"
               "   dwFlags                   : 0x%.08X\n"
               "   ppImageDesc               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pszImageName, pImageLoc, dwFlags, ppImageDesc);
    }
    #endif

    // Loads a DSP effects image (dsstdfx.bin) into the Xbox GP DSP. The host
    // has no DSP; MUST be hooked -- the guest implementation cold-boots the
    // whole MCPX core (CMcpxCore::SetupEncodeProcessor NULL-derefs against
    // the register-level APU model). Hand back a zeroed image description so
    // callers that read effect indices get benign state.
    static DWORD s_ZeroImageDesc[64] = {0};

    if(ppImageDesc != 0)
        *ppImageDesc = s_ZeroImageDesc;

    EmuSwapFS();   // XBox FS

    return S_OK;
}

// ******************************************************************
// * func: EmuDirectSoundDoWork
// ******************************************************************
VOID WINAPI XTL::EmuDirectSoundDoWork()
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuDirectSoundDoWork();\n", GetCurrentThreadId());
    #endif

    X_DSoundStreamState* state = NULL;
    AcquireSRWLockShared(&g_DSoundStreamListLock);
    state = g_DSoundStreams;
    if(state != NULL)
    {
        InterlockedIncrement(&state->Owner->EmuRefCount);
    }
    ReleaseSRWLockShared(&g_DSoundStreamListLock);

    while(state != NULL)
    {
        X_DSoundStreamState* next = NULL;
        AcquireSRWLockShared(&g_DSoundStreamListLock);
        next = state->Next;
        if(next != NULL)
        {
            InterlockedIncrement(&next->Owner->EmuRefCount);
        }
        ReleaseSRWLockShared(&g_DSoundStreamListLock);

        X_CDirectSoundStream* owner = state->Owner;
        PumpStream(state);
        ReleaseStreamReference(owner);
        state = next;
    }

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuCDirectSoundStream_Flush
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSoundStream_Flush(X_CDirectSoundStream *pThis)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuCDirectSoundStream_Flush(0x%.08X);\n", GetCurrentThreadId(), pThis);
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    if(stream == NULL || stream->EmuState == NULL)
    {
        EmuSwapFS(); // XBox FS
        return E_INVALIDARG;
    }

    InterlockedIncrement(&stream->EmuRefCount);
    FlushStream(stream->EmuState, true);
    ReleaseStreamReference(stream);

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_FlushEx
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_FlushEx
(
    X_CDirectSoundStream   *pThis,
    DWORD                   dwFlags,
    LONGLONG                rtTimeStamp
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuIDirectSoundStream_FlushEx(0x%.08X, 0x%.08X);\n", GetCurrentThreadId(), pThis, dwFlags);
    #endif

    X_CDirectSoundStream* stream = ResolveStream(pThis);
    if(stream == NULL || stream->EmuState == NULL)
    {
        EmuSwapFS(); // XBox FS
        return E_INVALIDARG;
    }

    InterlockedIncrement(&stream->EmuRefCount);
    FlushStream(stream->EmuState, true);
    ReleaseStreamReference(stream);

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetEG
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetEG
(
    X_CDirectSoundStream   *pThis,
    LPVOID                  pEnvelopeDesc
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuIDirectSoundStream_SetEG(0x%.08X, 0x%.08X);\n", GetCurrentThreadId(), pThis, pEnvelopeDesc);
    #endif

    // Xbox-only DSP envelope generator; accept and ignore.

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetMixBinsS
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetMixBinsS
(
    X_CDirectSoundStream   *pThis,
    LPVOID                  pMixBins
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuIDirectSoundStream_SetMixBinsS(0x%.08X, 0x%.08X);\n", GetCurrentThreadId(), pThis, pMixBins);
    #endif

    // Xbox-only speaker-routing matrix; accept and ignore.

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetOutputBuffer
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetOutputBuffer
(
    X_CDirectSoundStream   *pThis,
    X_CDirectSoundBuffer   *pOutputBuffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuIDirectSoundStream_SetOutputBuffer(0x%.08X, 0x%.08X);\n", GetCurrentThreadId(), pThis, pOutputBuffer);
    #endif

    // Host DirectSound has no Xbox output-buffer (submix) routing equivalent.
    // Accept the request; a null destination selects the default output.

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundStream_SetFormat
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundStream_SetFormat
(
    X_CDirectSoundStream   *pThis,
    const WAVEFORMATEX     *pWaveFormat
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuDSound (0x%X): EmuIDirectSoundStream_SetFormat(0x%.08X, 0x%.08X);\n", GetCurrentThreadId(), pThis, pWaveFormat);
    #endif

    // The host stream buffer keeps the format selected at creation; accept
    // the Xbox format update (same policy as the buffer SetFormat above).

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * EmuBufferLockRecord
// ******************************************************************
// Xbox buffer memory is CPU-visible, so the guest-side IDirectSoundBuffer::
// Unlock is a call-free no-op we cannot signature (see DSound.1.0.5849.inl)
// -- the host lock from a previous EmuIDirectSoundBuffer8_Lock is therefore
// never released by the title. Mirror the surface/texture LockRect pattern:
// remember each buffer's outstanding host lock and release it on the NEXT
// Lock of that buffer (the data commits one cycle late, which host DSound
// tolerates for the ring-update pattern titles use).
struct EmuBufferLockRecord
{
    XTL::IDirectSoundBuffer *pBuffer;
    PVOID pv1; DWORD cb1;
    PVOID pv2; DWORD cb2;
};
static EmuBufferLockRecord g_EmuBufferLocks[16] = {0};

static void EmuBufferUnlockPrevious(XTL::IDirectSoundBuffer *pBuffer)
{
    // A NULL argument would otherwise "match" every EMPTY record (their
    // pBuffer is 0) and Unlock would be called on NULL.
    if(pBuffer == NULL)
        return;

    for(int i=0;i<16;i++)
    {
        if(g_EmuBufferLocks[i].pBuffer == pBuffer)
        {
            pBuffer->Unlock(g_EmuBufferLocks[i].pv1, g_EmuBufferLocks[i].cb1,
                            g_EmuBufferLocks[i].pv2, g_EmuBufferLocks[i].cb2);
            g_EmuBufferLocks[i].pBuffer = 0;
            return;
        }
    }
}

static void EmuBufferRecordLock(XTL::IDirectSoundBuffer *pBuffer, PVOID pv1, DWORD cb1, PVOID pv2, DWORD cb2)
{
    for(int i=0;i<16;i++)
    {
        if(g_EmuBufferLocks[i].pBuffer == 0)
        {
            g_EmuBufferLocks[i].pBuffer = pBuffer;
            g_EmuBufferLocks[i].pv1 = pv1; g_EmuBufferLocks[i].cb1 = cb1;
            g_EmuBufferLocks[i].pv2 = pv2; g_EmuBufferLocks[i].cb2 = cb2;
            return;
        }
    }
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Lock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Lock
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwOffset,
    DWORD                   dwBytes,
    LPVOID                 *ppvAudioPtr1,
    LPDWORD                 pdwAudioBytes1,
    LPVOID                 *ppvAudioPtr2,
    LPDWORD                 pdwAudioBytes2,
    DWORD                   dwFlags
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Lock\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwOffset                  : 0x%.08X\n"
               "   dwBytes                   : 0x%.08X\n"
               "   dwFlags                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwOffset, dwBytes, dwFlags);
    }
    #endif

    // A buffer whose host creation failed (Xbox-only wave format the host
    // rejects) has no host object. Hand the title writable scratch memory so
    // its ring update proceeds -- that voice stays silent instead of the
    // process dying on a NULL host call. One scratch region per title
    // buffer, and a region is NEVER freed or moved once handed out: the
    // title keeps writing through old lock pointers long after the call.
    if(pThis == NULL || pThis->EmuDirectSoundBuffer8 == NULL)
    {
        struct EmuBufferScratch { X_CDirectSoundBuffer *pThis; BYTE *pMem; DWORD dwSize; };
        static EmuBufferScratch s_Scratch[32] = {0};

        BYTE *pMem = NULL;
        int iFree = -1;

        for(int i=0;i<32;i++)
        {
            if(s_Scratch[i].pThis == pThis && s_Scratch[i].dwSize >= dwBytes)
            {
                pMem = s_Scratch[i].pMem;
                break;
            }

            if(iFree < 0 && s_Scratch[i].pThis == NULL)
                iFree = i;
        }

        if(pMem == NULL)
        {
            // New buffer (or one that grew): allocate generously so repeat
            // locks reuse the region; the old region (if any) is deliberately
            // leaked because the title may still write through it. Page
            // allocations (not the CRT heap) with a trailing PAGE_NOACCESS
            // guard: an overrunning writer faults AT the write instead of
            // silently corrupting the host heap.
            DWORD dwAlloc = (dwBytes < 0x10000) ? 0x10000 : ((dwBytes + 0xFFF) & ~0xFFFu);
            pMem = (BYTE*)VirtualAlloc(NULL, dwAlloc + 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

            if(pMem != NULL)
            {
                DWORD dwOld;
                VirtualProtect(pMem + dwAlloc, 0x1000, PAGE_NOACCESS, &dwOld);
            }

            if(pMem != NULL && iFree >= 0)
            {
                s_Scratch[iFree].pThis = pThis;
                s_Scratch[iFree].pMem = pMem;
                s_Scratch[iFree].dwSize = dwAlloc;
            }
        }

        if(ppvAudioPtr1 != NULL)
            *ppvAudioPtr1 = pMem;
        if(pdwAudioBytes1 != NULL)
            *pdwAudioBytes1 = dwBytes;
        if(ppvAudioPtr2 != NULL)
            *ppvAudioPtr2 = NULL;
        if(pdwAudioBytes2 != NULL)
            *pdwAudioBytes2 = 0;

        EmuSwapFS();   // XBox FS

        return DS_OK;
    }

    // Release the previous host lock on this buffer (guest Unlock is an
    // un-hookable no-op; see EmuBufferLockRecord above).
    EmuBufferUnlockPrevious(pThis->EmuDirectSoundBuffer8);

    // The Xbox and PC lock semantics line up (offset/bytes -> up to two
    // wrap-around regions); flags pass through (FROMWRITECURSOR/ENTIREBUFFER
    // share values).
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->Lock(dwOffset, dwBytes,
                                                      ppvAudioPtr1, pdwAudioBytes1,
                                                      ppvAudioPtr2, pdwAudioBytes2,
                                                      dwFlags);

    if(cxbx::trace::IsEnabled(cxbx::trace::Channel::Audio))
    {
        printf("AUDIO| static-buffer-ring-lock this=0x%p offset=0x%lX requested=0x%lX flags=0x%lX result=0x%lX first=0x%p/0x%lX second=0x%p/0x%lX\n",
               pThis, dwOffset, dwBytes, dwFlags, hRet,
               ppvAudioPtr1 != NULL ? *ppvAudioPtr1 : NULL,
               pdwAudioBytes1 != NULL ? *pdwAudioBytes1 : 0,
               ppvAudioPtr2 != NULL ? *ppvAudioPtr2 : NULL,
               pdwAudioBytes2 != NULL ? *pdwAudioBytes2 : 0);
    }

    if(SUCCEEDED(hRet))
        EmuBufferRecordLock(pThis->EmuDirectSoundBuffer8,
                            *ppvAudioPtr1, *pdwAudioBytes1,
                            ppvAudioPtr2 ? *ppvAudioPtr2 : 0,
                            pdwAudioBytes2 ? *pdwAudioBytes2 : 0);
    else
        EmuWarning("Buffer Lock FAILED");

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Unlock
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_Unlock
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pvLock1,
    DWORD                   dwLockSize1,
    LPVOID                  pvLock2,
    DWORD                   dwLockSize2
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Unlock\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pvLock1                   : 0x%.08X\n"
               "   dwLockSize1               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pvLock1, dwLockSize1);
    }
    #endif

    HRESULT hRet = DS_OK;

    if(pThis != NULL && pThis->EmuDirectSoundBuffer8 != NULL)
        hRet = pThis->EmuDirectSoundBuffer8->Unlock(pvLock1, dwLockSize1,
                                                    pvLock2, dwLockSize2);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMixBins
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetMixBins
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pMixBins
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetMixBins\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pMixBins                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pMixBins);
    }
    #endif

    // Xbox-only speaker-routing matrix; the host stereo path has no
    // equivalent, so accept and ignore.

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_GetStatus
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_GetStatus
(
    X_CDirectSoundBuffer   *pThis,
    LPDWORD                 pdwStatus
)
{
    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_GetStatus\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pdwStatus                 : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pdwStatus);
    }
    #endif

    if(pThis == NULL || pThis->EmuDirectSoundBuffer8 == NULL)
    {
        if(pdwStatus != NULL)
            *pdwStatus = 0;

        EmuSwapFS();   // XBox FS

        return DS_OK;
    }

    // PLAYING (0x1) and LOOPING (0x4) share values between Xbox and PC.
    HRESULT hRet = pThis->EmuDirectSoundBuffer8->GetStatus(pdwStatus);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuCDirectSound_CommitDeferredSettings
// ******************************************************************
HRESULT WINAPI XTL::EmuCDirectSound_CommitDeferredSettings
(
    X_CDirectSound         *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSound_CommitDeferredSettings\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    // Todo: Translate params, then make the PC DirectSound call

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSound8_GetCaps
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSound8_GetCaps
(
    LPDIRECTSOUND8          pThis,
    PVOID                   pDSCaps
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSound8_GetCaps\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pDSCaps                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pDSCaps);
    }
    #endif

    // Xbox DSCAPS reports hardware voice/memory headroom. The host mixes in
    // software, so report generous free resources and no memory pressure.
    if(pDSCaps != NULL && !IsBadWritePtr(pDSCaps, 16))
    {
        DWORD *pCaps = (DWORD*)pDSCaps;

        pCaps[0] = 0x100;   // dwFree2DBuffers
        pCaps[1] = 0x100;   // dwFree3DBuffers
        pCaps[2] = 0x100;   // dwFreeBufferSGEs
        pCaps[3] = 0;       // dwMemoryAllocated
    }

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_Release
// ******************************************************************
ULONG WINAPI XTL::EmuIDirectSoundBuffer8_Release
(
    X_CDirectSoundBuffer   *pThis
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_Release\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis);
    }
    #endif

    ULONG uRet = 0;

    if(pThis != NULL && pThis->EmuDirectSoundBuffer8 != NULL)
    {
        uRet = pThis->EmuDirectSoundBuffer8->Release();

        if(uRet == 0)
            pThis->EmuDirectSoundBuffer8 = NULL;
    }

    EmuSwapFS();   // XBox FS

    return uRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetMixBinVolumes
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetMixBinVolumes
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pMixBins
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetMixBinVolumes\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pMixBins                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pMixBins);
    }
    #endif

    // Xbox-only per-mixbin volume routing; the host stereo path has no
    // equivalent, so accept and ignore (same policy as SetMixBins).

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetFrequency
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetFrequency
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwFrequency
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetFrequency\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwFrequency               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwFrequency);
    }
    #endif

    HRESULT hRet = DS_OK;

    if(pThis != NULL && pThis->EmuDirectSoundBuffer8 != NULL)
    {
        hRet = pThis->EmuDirectSoundBuffer8->SetFrequency(dwFrequency);

        // The host buffer may lack DSBCAPS_CTRLFREQUENCY; a pitch mismatch is
        // not worth failing the title over.
        if(FAILED(hRet))
        {
            EmuWarning("SetFrequency FAILED");
            hRet = DS_OK;
        }
    }

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetConeAngles
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetConeAngles
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwInsideConeAngle,
    DWORD                   dwOutsideConeAngle,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetConeAngles\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwInsideConeAngle         : 0x%.08X\n"
               "   dwOutsideConeAngle        : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwInsideConeAngle, dwOutsideConeAngle, dwApply);
    }
    #endif

    // Deferred 3D voice parameter; no host equivalent on the stereo path.

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuIDirectSoundBuffer8_SetI3DL2Source
// ******************************************************************
HRESULT WINAPI XTL::EmuIDirectSoundBuffer8_SetI3DL2Source
(
    X_CDirectSoundBuffer   *pThis,
    LPVOID                  pds3db,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuIDirectSoundBuffer8_SetI3DL2Source\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   pds3db                    : 0x%.08X\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, pds3db, dwApply);
    }
    #endif

    // I3DL2 reverb source parameters; the host path has no DSP to feed.

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundBuffer_SetDeferred3dVector
// ******************************************************************
// Shared accept-and-ignore body for the byte-identical @20 buffer wrappers
// SetPosition / SetVelocity / SetConeOrientation (x,y,z + dwApply). The
// OOVPA_FLAG_PATCH_ALL entry routes every twin here.
HRESULT WINAPI XTL::EmuCDirectSoundBuffer_SetDeferred3dVector
(
    X_CDirectSoundBuffer   *pThis,
    FLOAT                   x,
    FLOAT                   y,
    FLOAT                   z,
    DWORD                   dwApply
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundBuffer_SetDeferred3dVector\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   x                         : %f\n"
               "   y                         : %f\n"
               "   z                         : %f\n"
               "   dwApply                   : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, x, y, z, dwApply);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return DS_OK;
}

// ******************************************************************
// * func: EmuCDirectSoundBuffer_SetDeferred3dParam
// ******************************************************************
// Shared accept-and-ignore body for the byte-identical @12 buffer wrappers
// (SetMaxDistance / SetMinDistance / SetDistanceFactor / SetDopplerFactor /
// SetRolloffFactor and the SetConeOutsideVolume shape). Two dword-sized
// arguments; the float ones arrive bit-cast, which an ignore body never reads.
HRESULT WINAPI XTL::EmuCDirectSoundBuffer_SetDeferred3dParam
(
    X_CDirectSoundBuffer   *pThis,
    DWORD                   dwArg1,
    DWORD                   dwArg2
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuDSound (0x%X): EmuCDirectSoundBuffer_SetDeferred3dParam\n"
               "(\n"
               "   pThis                     : 0x%.08X\n"
               "   dwArg1                    : 0x%.08X\n"
               "   dwArg2                    : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pThis, dwArg1, dwArg2);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return DS_OK;
}
