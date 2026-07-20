// Xbox Audio Creation Tool engine lifecycle HLE.
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

#include "Emu.h"
#include "EmuFS.h"

#include <cstring>
#include <limits>
#include <mutex>
#include <new>

namespace XTL
{
#include "EmuXTL.h"
};

struct XTL::X_XACTEngine
{
    X_XACT_RUNTIME_PARAMETERS Parameters;
    ULONG ReferenceCount;
    X_XACTWaveBank* WaveBanks;
    X_XACTSoundBank* SoundBanks;
    X_XACTEngine* Next;
};

struct XTL::X_XACTWaveBank
{
    X_XACTEngine* Engine;
    const BYTE* Data;
    DWORD Size;
    X_XACTWaveBank* Next;
};

struct XTL::X_XACTSoundBank
{
    X_XACTEngine* Engine;
    const BYTE* Data;
    DWORD Size;
    ULONG ReferenceCount;
    X_XACTSoundCue* Cues;
    X_XACTSoundBank* Next;
};

struct XTL::X_XACTSoundCue
{
    X_XACTSoundBank* SoundBank;
    DWORD CueIndex;
    DWORD Flags;
    IDirectSoundBuffer* HostBuffer;
    X_XACTSoundCue* Next;
};

namespace
{
constexpr DWORD XactWaveBankSignature = 0x444E4257; // "WBND" in guest memory.
constexpr DWORD XactWaveBankVersion = 3;
constexpr DWORD XactWaveBankHeaderSize = 40;
constexpr DWORD XactWaveBankDataSize = 40;
constexpr DWORD XactWaveBankEntrySize = 24;
constexpr DWORD XactWaveBankCompactEntrySize = 4;
constexpr DWORD XactWaveBankSegmentCount = 4;
constexpr DWORD XactWaveBankBankDataSegment = 0;
constexpr DWORD XactWaveBankMetadataSegment = 1;
constexpr DWORD XactWaveBankEntryNamesSegment = 2;
constexpr DWORD XactWaveBankWaveDataSegment = 3;
constexpr DWORD XactWaveBankTypeMask = 0x00000001;
constexpr DWORD XactWaveBankEntryNamesFlag = 0x00010000;
constexpr DWORD XactWaveBankCompactFlag = 0x00020000;
constexpr DWORD XactWaveBankMinimumAlignment = 4;
constexpr DWORD XactSoundBankSignature = 0x4B424453; // "SDBK" in guest memory.
constexpr WORD XactSoundBankVersion = 11;
constexpr DWORD XactSoundBankHeaderSize = 0x3C;
constexpr DWORD XactSoundBankCueTableOffset = 0x38;
constexpr DWORD XactSoundBankCueEntrySize = 0x14;
constexpr DWORD XactSoundBankNamesUnavailableFlag = 0x00000001;
constexpr DWORD XactSoundBankInvalidCueIndex = 0xFFFFFFFF;
constexpr DWORD XactSoundBankWaveBankNameSize = 0x10;
constexpr DWORD XactSoundBankDirectSoundFlag = 0x08;
constexpr DWORD XactSoundBankSoundFlagMask = 0x18;
constexpr DWORD XactSoundCueAutoReleaseFlag = 0x0001;

struct XactWaveBankRegion
{
    DWORD Offset;
    DWORD Length;
};

std::mutex g_XactEngineMutex;
XTL::X_XACTEngine* g_XactEngines = nullptr;

bool ReadXactDword(
    const BYTE* data,
    DWORD size,
    DWORD offset,
    DWORD* value)
{
    if(offset > size || sizeof(*value) > size - offset)
    {
        return false;
    }

    std::memcpy(value, data + offset, sizeof(*value));
    return true;
}

bool ReadXactWord(
    const BYTE* data,
    DWORD size,
    DWORD offset,
    WORD* value)
{
    if(offset > size || sizeof(*value) > size - offset)
    {
        return false;
    }

    std::memcpy(value, data + offset, sizeof(*value));
    return true;
}

bool ReadWaveBankRegion(
    const BYTE* data,
    DWORD size,
    DWORD index,
    XactWaveBankRegion* region)
{
    const DWORD regionOffset = 8 + index * 8;
    return ReadXactDword(data, size, regionOffset, &region->Offset) &&
           ReadXactDword(data, size, regionOffset + 4, &region->Length);
}

bool IsWaveBankRegionValid(const XactWaveBankRegion& region, DWORD size)
{
    return region.Offset <= size && region.Length <= size - region.Offset;
}

bool IsWaveBankFormatValid(DWORD format)
{
    const DWORD formatTag = format & 0x3;
    const DWORD channels = (format >> 2) & 0x7;
    const DWORD samplesPerSecond = (format >> 5) & 0x03FFFFFF;
    return formatTag <= 2 && channels > 0 && channels <= 6 &&
           samplesPerSecond > 0;
}

bool IsWaveBankEntryValid(
    const BYTE* data,
    DWORD size,
    DWORD entryOffset,
    const XactWaveBankRegion& waveData)
{
    DWORD format = 0;
    DWORD playOffset = 0;
    DWORD playLength = 0;
    DWORD loopOffset = 0;
    DWORD loopLength = 0;
    if(!ReadXactDword(data, size, entryOffset + 4, &format) ||
       !ReadXactDword(data, size, entryOffset + 8, &playOffset) ||
       !ReadXactDword(data, size, entryOffset + 12, &playLength) ||
       !ReadXactDword(data, size, entryOffset + 16, &loopOffset) ||
       !ReadXactDword(data, size, entryOffset + 20, &loopLength))
    {
        return false;
    }

    if(!IsWaveBankFormatValid(format))
    {
        return false;
    }
    if(playOffset > waveData.Length || playLength > waveData.Length - playOffset)
    {
        return false;
    }
    return loopOffset <= playLength && loopLength <= playLength - loopOffset;
}

bool IsInMemoryWaveBankValid(const BYTE* data, DWORD size)
{
    if(data == nullptr || size < XactWaveBankHeaderSize)
    {
        return false;
    }

    DWORD signature = 0;
    DWORD version = 0;
    if(!ReadXactDword(data, size, 0, &signature) ||
       !ReadXactDword(data, size, 4, &version) ||
       signature != XactWaveBankSignature || version != XactWaveBankVersion)
    {
        return false;
    }

    XactWaveBankRegion segments[XactWaveBankSegmentCount] = {};
    for(DWORD index = 0; index < XactWaveBankSegmentCount; ++index)
    {
        if(!ReadWaveBankRegion(data, size, index, &segments[index]) ||
           !IsWaveBankRegionValid(segments[index], size))
        {
            return false;
        }
    }

    const XactWaveBankRegion& bankData =
        segments[XactWaveBankBankDataSegment];
    const XactWaveBankRegion& metadata =
        segments[XactWaveBankMetadataSegment];
    const XactWaveBankRegion& entryNames =
        segments[XactWaveBankEntryNamesSegment];
    const XactWaveBankRegion& waveData =
        segments[XactWaveBankWaveDataSegment];
    if(bankData.Length < XactWaveBankDataSize)
    {
        return false;
    }

    DWORD flags = 0;
    DWORD entryCount = 0;
    DWORD metadataElementSize = 0;
    DWORD entryNameElementSize = 0;
    DWORD alignment = 0;
    if(!ReadXactDword(data, size, bankData.Offset, &flags) ||
       !ReadXactDword(data, size, bankData.Offset + 4, &entryCount) ||
       !ReadXactDword(
           data, size, bankData.Offset + 24, &metadataElementSize) ||
       !ReadXactDword(
           data, size, bankData.Offset + 28, &entryNameElementSize) ||
       !ReadXactDword(data, size, bankData.Offset + 32, &alignment))
    {
        return false;
    }
    if((flags & XactWaveBankTypeMask) != 0 ||
       alignment < XactWaveBankMinimumAlignment ||
       (alignment & (alignment - 1)) != 0)
    {
        return false;
    }

    const bool compact = (flags & XactWaveBankCompactFlag) != 0;
    const DWORD minimumMetadataSize = compact ? XactWaveBankCompactEntrySize
                                              : XactWaveBankEntrySize;
    if(metadataElementSize < minimumMetadataSize ||
       entryCount > metadata.Length / metadataElementSize)
    {
        return false;
    }
    if((flags & XactWaveBankEntryNamesFlag) != 0 &&
       (entryNameElementSize == 0 ||
        entryCount > entryNames.Length / entryNameElementSize))
    {
        return false;
    }

    if(compact)
    {
        DWORD compactFormat = 0;
        return ReadXactDword(
                   data, size, bankData.Offset + 36, &compactFormat) &&
               IsWaveBankFormatValid(compactFormat);
    }

    for(DWORD index = 0; index < entryCount; ++index)
    {
        const DWORD entryOffset =
            metadata.Offset + index * metadataElementSize;
        if(!IsWaveBankEntryValid(data, size, entryOffset, waveData))
        {
            return false;
        }
    }
    return true;
}

bool FixedXactNameEquals(
    const BYTE* left,
    const BYTE* right,
    DWORD length)
{
    return std::memcmp(left, right, length) == 0;
}

bool ResolveWaveBankPcmEntry(
    XTL::X_XACTWaveBank* waveBank,
    DWORD waveEntryIndex,
    WAVEFORMATEX* format,
    const BYTE** audioData,
    DWORD* audioSize)
{
    XactWaveBankRegion bankData = {};
    XactWaveBankRegion metadata = {};
    XactWaveBankRegion waveData = {};
    if(!ReadWaveBankRegion(
           waveBank->Data,
           waveBank->Size,
           XactWaveBankBankDataSegment,
           &bankData) ||
       !ReadWaveBankRegion(
           waveBank->Data,
           waveBank->Size,
           XactWaveBankMetadataSegment,
           &metadata) ||
       !ReadWaveBankRegion(
           waveBank->Data,
           waveBank->Size,
           XactWaveBankWaveDataSegment,
           &waveData))
    {
        return false;
    }

    DWORD flags = 0;
    DWORD entryCount = 0;
    DWORD metadataElementSize = 0;
    if(!ReadXactDword(
           waveBank->Data, waveBank->Size, bankData.Offset, &flags) ||
       !ReadXactDword(
           waveBank->Data,
           waveBank->Size,
           bankData.Offset + 4,
           &entryCount) ||
       !ReadXactDword(
           waveBank->Data,
           waveBank->Size,
           bankData.Offset + 24,
           &metadataElementSize) ||
       (flags & XactWaveBankCompactFlag) != 0 ||
       metadataElementSize < XactWaveBankEntrySize ||
       waveEntryIndex >= entryCount)
    {
        return false;
    }

    const DWORD entryOffset =
        metadata.Offset + waveEntryIndex * metadataElementSize;
    DWORD encodedFormat = 0;
    DWORD playOffset = 0;
    DWORD playLength = 0;
    if(!ReadXactDword(
           waveBank->Data,
           waveBank->Size,
           entryOffset + 4,
           &encodedFormat) ||
       !ReadXactDword(
           waveBank->Data,
           waveBank->Size,
           entryOffset + 8,
           &playOffset) ||
       !ReadXactDword(
           waveBank->Data,
           waveBank->Size,
           entryOffset + 12,
           &playLength) ||
       (encodedFormat & 0x3) != 0 || playLength == 0 ||
       playOffset > waveData.Length ||
       playLength > waveData.Length - playOffset)
    {
        return false;
    }

    const WORD channels = static_cast<WORD>((encodedFormat >> 2) & 0x7);
    const DWORD samplesPerSecond =
        (encodedFormat >> 5) & 0x03FFFFFF;
    const WORD bitsPerSample =
        (encodedFormat & 0x80000000) != 0 ? 16 : 8;
    const WORD blockAlign =
        static_cast<WORD>(channels * bitsPerSample / 8);
    if(channels == 0 || samplesPerSecond == 0 || blockAlign == 0 ||
       playLength % blockAlign != 0)
    {
        return false;
    }

    *format = {};
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->nChannels = channels;
    format->nSamplesPerSec = samplesPerSecond;
    format->nBlockAlign = blockAlign;
    format->nAvgBytesPerSec = samplesPerSecond * blockAlign;
    format->wBitsPerSample = bitsPerSample;
    format->cbSize = 0;
    *audioData = waveBank->Data + waveData.Offset + playOffset;
    *audioSize = playLength;
    return true;
}

bool ResolveSoundCuePcm(
    XTL::X_XACTSoundBank* soundBank,
    DWORD cueIndex,
    WAVEFORMATEX* format,
    const BYTE** audioData,
    DWORD* audioSize)
{
    WORD cueCount = 0;
    if(!ReadXactWord(
           soundBank->Data, soundBank->Size, 0x1E, &cueCount) ||
       cueIndex >= cueCount)
    {
        return false;
    }

    const DWORD cueEntryOffset =
        XactSoundBankCueTableOffset +
        cueIndex * XactSoundBankCueEntrySize;
    WORD soundIndex = 0;
    if(!ReadXactWord(
           soundBank->Data,
           soundBank->Size,
           cueEntryOffset + 2,
           &soundIndex))
    {
        return false;
    }

    const DWORD soundEntryOffset =
        XactSoundBankCueTableOffset +
        (static_cast<DWORD>(cueCount) + soundIndex) *
            XactSoundBankCueEntrySize;
    DWORD packedWaveIndex = 0;
    if(soundEntryOffset > soundBank->Size ||
       XactSoundBankCueEntrySize > soundBank->Size - soundEntryOffset ||
       !ReadXactDword(
           soundBank->Data,
           soundBank->Size,
           soundEntryOffset,
           &packedWaveIndex) ||
       soundBank->Data[soundEntryOffset + 8] != 1 ||
       (soundBank->Data[soundEntryOffset + 0x0B] &
        XactSoundBankSoundFlagMask) != XactSoundBankDirectSoundFlag)
    {
        return false;
    }

    const DWORD waveEntryIndex = packedWaveIndex & 0xFFFF;
    const DWORD waveBankIndex = packedWaveIndex >> 16;
    DWORD waveBankNamesOffset = 0;
    WORD waveBankCount = 0;
    if(!ReadXactDword(
           soundBank->Data,
           soundBank->Size,
           0x08,
           &waveBankNamesOffset) ||
       !ReadXactWord(
           soundBank->Data,
           soundBank->Size,
           0x22,
           &waveBankCount) ||
       waveBankIndex >= waveBankCount ||
       waveBankNamesOffset > soundBank->Size ||
       XactSoundBankWaveBankNameSize >
           soundBank->Size - waveBankNamesOffset ||
       waveBankIndex >
           (soundBank->Size - waveBankNamesOffset -
            XactSoundBankWaveBankNameSize) /
               XactSoundBankWaveBankNameSize)
    {
        return false;
    }

    const BYTE* waveBankName =
        soundBank->Data + waveBankNamesOffset +
        waveBankIndex * XactSoundBankWaveBankNameSize;
    for(XTL::X_XACTWaveBank* waveBank = soundBank->Engine->WaveBanks;
        waveBank != nullptr;
        waveBank = waveBank->Next)
    {
        XactWaveBankRegion bankData = {};
        if(ReadWaveBankRegion(
               waveBank->Data,
               waveBank->Size,
               XactWaveBankBankDataSegment,
               &bankData) &&
           bankData.Length >= XactWaveBankDataSize &&
           FixedXactNameEquals(
               waveBankName,
               waveBank->Data + bankData.Offset + 8,
               XactSoundBankWaveBankNameSize))
        {
            return ResolveWaveBankPcmEntry(
                waveBank, waveEntryIndex, format, audioData, audioSize);
        }
    }
    return false;
}

bool GetSoundBankSize(const BYTE* data, DWORD size, DWORD* soundBankSize)
{
    if(data == nullptr || size < XactSoundBankHeaderSize)
    {
        return false;
    }

    DWORD signature = 0;
    WORD version = 0;
    DWORD declaredSize = 0;
    DWORD flags = 0;
    WORD cueCount = 0;
    if(!ReadXactDword(data, size, 0x00, &signature) ||
       !ReadXactWord(data, size, 0x04, &version) ||
       !ReadXactDword(data, size, 0x14, &declaredSize) ||
       !ReadXactDword(data, size, 0x18, &flags) ||
       !ReadXactWord(data, size, 0x1E, &cueCount) ||
       signature != XactSoundBankSignature ||
       version != XactSoundBankVersion ||
       declaredSize < XactSoundBankHeaderSize || declaredSize > size ||
       cueCount >
           (declaredSize - XactSoundBankHeaderSize) /
               XactSoundBankCueEntrySize)
    {
        return false;
    }

    if((flags & XactSoundBankNamesUnavailableFlag) == 0)
    {
        for(DWORD index = 0; index < cueCount; ++index)
        {
            DWORD nameOffset = 0;
            const DWORD cueOffset =
                XactSoundBankHeaderSize + index * XactSoundBankCueEntrySize;
            if(!ReadXactDword(data, declaredSize, cueOffset, &nameOffset) ||
               nameOffset >= declaredSize ||
               std::memchr(
                   data + nameOffset, '\0', declaredSize - nameOffset) ==
                   nullptr)
            {
                return false;
            }
        }
    }

    *soundBankSize = declaredSize;
    return true;
}

XTL::X_XACTEngine* FindXactEngine(XTL::X_XACTEngine* engine)
{
    for(XTL::X_XACTEngine* current = g_XactEngines;
        current != nullptr;
        current = current->Next)
    {
        if(current == engine)
        {
            return current;
        }
    }
    return nullptr;
}

XTL::X_XACTSoundBank* FindXactSoundBank(XTL::X_XACTSoundBank* soundBank)
{
    for(XTL::X_XACTEngine* engine = g_XactEngines;
        engine != nullptr;
        engine = engine->Next)
    {
        for(XTL::X_XACTSoundBank* current = engine->SoundBanks;
            current != nullptr;
            current = current->Next)
        {
            if(current == soundBank)
            {
                return current;
            }
        }
    }
    return nullptr;
}

bool AddXactEngineReferenceLocked(XTL::X_XACTEngine* engine)
{
    if(engine->ReferenceCount == (std::numeric_limits<ULONG>::max)())
    {
        return false;
    }
    ++engine->ReferenceCount;
    return true;
}

XTL::X_XACTEngine* ReleaseXactEngineReferenceLocked(
    XTL::X_XACTEngine* engine,
    ULONG* referenceCount)
{
    if(engine->ReferenceCount > 1)
    {
        *referenceCount = --engine->ReferenceCount;
        return nullptr;
    }

    XTL::X_XACTEngine** link = &g_XactEngines;
    while(*link != nullptr && *link != engine)
    {
        link = &(*link)->Next;
    }
    if(*link == nullptr)
    {
        *referenceCount = 0;
        return nullptr;
    }

    *link = engine->Next;
    engine->ReferenceCount = 0;
    *referenceCount = 0;
    return engine;
}

void DestroyXactCue(XTL::X_XACTSoundCue* cue)
{
    if(cue->HostBuffer != nullptr)
    {
        cue->HostBuffer->Stop();
        cue->HostBuffer->SetCurrentPosition(0);
        cue->HostBuffer->Release();
    }
    delete cue;
}

void DestroyXactCueList(XTL::X_XACTSoundCue* cues)
{
    while(cues != nullptr)
    {
        XTL::X_XACTSoundCue* next = cues->Next;
        DestroyXactCue(cues);
        cues = next;
    }
}

HRESULT CreateXactCueLocked(
    XTL::X_XACTSoundBank* soundBank,
    const XTL::X_XACT_PREPARE_SOUNDCUE* prepareData,
    bool play,
    XTL::X_XACTSoundCue** soundCue)
{
    if(prepareData->pSoundSource != nullptr ||
       prepareData->pParameterControls != nullptr)
    {
        return E_NOTIMPL;
    }

    WAVEFORMATEX format = {};
    const BYTE* audioData = nullptr;
    DWORD audioSize = 0;
    WORD cueCount = 0;
    if(!ReadXactWord(
           soundBank->Data, soundBank->Size, 0x1E, &cueCount) ||
       prepareData->dwCueIndex >= cueCount)
    {
        return E_INVALIDARG;
    }
    if(!ResolveSoundCuePcm(
           soundBank,
           prepareData->dwCueIndex,
           &format,
           &audioData,
           &audioSize))
    {
        return E_NOTIMPL;
    }

    XTL::X_XACTSoundCue* cue =
        new(std::nothrow) XTL::X_XACTSoundCue{};
    if(cue == nullptr)
    {
        return E_OUTOFMEMORY;
    }

    HRESULT result = XTL::EmuDSoundCreateHostBuffer(
        &format, audioData, audioSize, &cue->HostBuffer);
    if(SUCCEEDED(result) && play)
    {
        result = cue->HostBuffer->SetCurrentPosition(0);
        if(SUCCEEDED(result))
        {
            result = cue->HostBuffer->Play(0, 0, 0);
        }
    }
    if(FAILED(result))
    {
        DestroyXactCue(cue);
        return result;
    }

    cue->SoundBank = soundBank;
    cue->CueIndex = prepareData->dwCueIndex;
    cue->Flags = prepareData->dwFlags;
    cue->Next = soundBank->Cues;
    soundBank->Cues = cue;
    if(soundCue != nullptr)
    {
        *soundCue = cue;
    }
    return S_OK;
}
} // namespace

HRESULT WINAPI XTL::EmuXACTEngineCreate(
    const X_XACT_RUNTIME_PARAMETERS* pParams,
    X_XACTEngine** ppEngine)
{
    EmuSwapFS(); // Win2k/XP FS

    if(ppEngine == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_POINTER;
    }
    *ppEngine = nullptr;

    if(pParams == nullptr || pParams->dwMax3DHwVoices > 64)
    {
        EmuSwapFS(); // Xbox FS
        return E_INVALIDARG;
    }

    X_XACTEngine* engine = new(std::nothrow) X_XACTEngine{};
    if(engine == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_OUTOFMEMORY;
    }

    engine->Parameters = *pParams;
    engine->ReferenceCount = 1;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        engine->Next = g_XactEngines;
        g_XactEngines = engine;
    }
    *ppEngine = engine;

#ifdef _DEBUG_TRACE
    printf("EmuXact: created engine %p\n", static_cast<void*>(engine));
#endif

    EmuSwapFS(); // Xbox FS
    return S_OK;
}

VOID WINAPI XTL::EmuXACTEngineDoWork()
{
    // DirectSound owns stream progress. Its wrapper performs and balances the
    // required FS transitions itself.
    EmuDirectSoundDoWork();
}

ULONG WINAPI XTL::EmuIXACTEngine_AddRef(X_XACTEngine* pEngine)
{
    EmuSwapFS(); // Win2k/XP FS

    ULONG referenceCount = 0;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTEngine* engine = FindXactEngine(pEngine);
        if(engine != nullptr)
        {
            AddXactEngineReferenceLocked(engine);
            referenceCount = engine->ReferenceCount;
        }
    }

    EmuSwapFS(); // Xbox FS
    return referenceCount;
}

ULONG WINAPI XTL::EmuIXACTEngine_Release(X_XACTEngine* pEngine)
{
    EmuSwapFS(); // Win2k/XP FS

    ULONG referenceCount = 0;
    X_XACTEngine* releasedEngine = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTEngine* engine = FindXactEngine(pEngine);
        if(engine != nullptr)
        {
            releasedEngine =
                ReleaseXactEngineReferenceLocked(engine, &referenceCount);
        }
    }

    delete releasedEngine;
    EmuSwapFS(); // Xbox FS
    return referenceCount;
}

HRESULT WINAPI XTL::EmuIXACTEngine_RegisterWaveBank(
    X_XACTEngine* pEngine,
    PVOID pvData,
    DWORD dwSize,
    X_XACTWaveBank** ppWaveBank)
{
    EmuSwapFS(); // Win2k/XP FS

    if(ppWaveBank == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_POINTER;
    }
    *ppWaveBank = nullptr;

    const BYTE* data = static_cast<const BYTE*>(pvData);
    if(!IsInMemoryWaveBankValid(data, dwSize))
    {
        EmuSwapFS(); // Xbox FS
        return E_INVALIDARG;
    }

    X_XACTWaveBank* waveBank = new(std::nothrow) X_XACTWaveBank{};
    if(waveBank == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_OUTOFMEMORY;
    }

    HRESULT result = E_INVALIDARG;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTEngine* engine = FindXactEngine(pEngine);
        if(engine != nullptr && AddXactEngineReferenceLocked(engine))
        {
            waveBank->Engine = engine;
            waveBank->Data = data;
            waveBank->Size = dwSize;
            waveBank->Next = engine->WaveBanks;
            engine->WaveBanks = waveBank;
            *ppWaveBank = waveBank;
            result = S_OK;
        }
    }

    if(FAILED(result))
    {
        delete waveBank;
    }
    EmuSwapFS(); // Xbox FS
    return result;
}

HRESULT WINAPI XTL::EmuIXACTEngine_UnRegisterWaveBank(
    X_XACTEngine* pEngine,
    X_XACTWaveBank* pWaveBank)
{
    EmuSwapFS(); // Win2k/XP FS

    X_XACTWaveBank* releasedWaveBank = nullptr;
    X_XACTEngine* releasedEngine = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTEngine* engine = FindXactEngine(pEngine);
        if(engine != nullptr)
        {
            X_XACTWaveBank** link = &engine->WaveBanks;
            while(*link != nullptr && *link != pWaveBank)
            {
                link = &(*link)->Next;
            }
            if(*link != nullptr)
            {
                releasedWaveBank = *link;
                *link = releasedWaveBank->Next;
                ULONG referenceCount = 0;
                releasedEngine = ReleaseXactEngineReferenceLocked(
                    engine, &referenceCount);
            }
        }
    }

    const bool found = releasedWaveBank != nullptr;
    delete releasedWaveBank;
    delete releasedEngine;
    EmuSwapFS(); // Xbox FS
    return found ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI XTL::EmuIXACTEngine_CreateSoundBank(
    X_XACTEngine* pEngine,
    PVOID pvData,
    DWORD dwSize,
    X_XACTSoundBank** ppSoundBank)
{
    EmuSwapFS(); // Win2k/XP FS

    if(ppSoundBank == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_POINTER;
    }
    *ppSoundBank = nullptr;

    const BYTE* data = static_cast<const BYTE*>(pvData);
    DWORD soundBankSize = 0;
    if(!GetSoundBankSize(data, dwSize, &soundBankSize))
    {
        EmuSwapFS(); // Xbox FS
        return E_INVALIDARG;
    }

    X_XACTSoundBank* soundBank = new(std::nothrow) X_XACTSoundBank{};
    if(soundBank == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_OUTOFMEMORY;
    }

    HRESULT result = E_INVALIDARG;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTEngine* engine = FindXactEngine(pEngine);
        if(engine != nullptr && AddXactEngineReferenceLocked(engine))
        {
            soundBank->Engine = engine;
            soundBank->Data = data;
            soundBank->Size = soundBankSize;
            soundBank->ReferenceCount = 1;
            soundBank->Next = engine->SoundBanks;
            engine->SoundBanks = soundBank;
            *ppSoundBank = soundBank;
            result = S_OK;
        }
    }

    if(FAILED(result))
    {
        delete soundBank;
    }
    EmuSwapFS(); // Xbox FS
    return result;
}

ULONG WINAPI XTL::EmuIXACTSoundBank_AddRef(X_XACTSoundBank* pSoundBank)
{
    EmuSwapFS(); // Win2k/XP FS

    ULONG referenceCount = 0;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTSoundBank* soundBank = FindXactSoundBank(pSoundBank);
        if(soundBank != nullptr)
        {
            if(soundBank->ReferenceCount <
               (std::numeric_limits<ULONG>::max)())
            {
                ++soundBank->ReferenceCount;
            }
            referenceCount = soundBank->ReferenceCount;
        }
    }

    EmuSwapFS(); // Xbox FS
    return referenceCount;
}

ULONG WINAPI XTL::EmuIXACTSoundBank_Release(X_XACTSoundBank* pSoundBank)
{
    EmuSwapFS(); // Win2k/XP FS

    ULONG referenceCount = 0;
    X_XACTSoundBank* releasedSoundBank = nullptr;
    X_XACTSoundCue* releasedCues = nullptr;
    X_XACTEngine* releasedEngine = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTSoundBank* soundBank = FindXactSoundBank(pSoundBank);
        if(soundBank != nullptr)
        {
            if(soundBank->ReferenceCount > 1)
            {
                referenceCount = --soundBank->ReferenceCount;
            }
            else
            {
                X_XACTEngine* engine = soundBank->Engine;
                X_XACTSoundBank** link = &engine->SoundBanks;
                while(*link != nullptr && *link != soundBank)
                {
                    link = &(*link)->Next;
                }
                if(*link != nullptr)
                {
                    *link = soundBank->Next;
                    releasedCues = soundBank->Cues;
                    soundBank->Cues = nullptr;
                    releasedSoundBank = soundBank;
                    ULONG engineReferenceCount = 0;
                    releasedEngine = ReleaseXactEngineReferenceLocked(
                        engine, &engineReferenceCount);
                }
            }
        }
    }

    DestroyXactCueList(releasedCues);
    delete releasedSoundBank;
    delete releasedEngine;
    EmuSwapFS(); // Xbox FS
    return referenceCount;
}

HRESULT WINAPI XTL::EmuIXACTSoundBank_GetSoundCueIndexFromFriendlyName(
    X_XACTSoundBank* pSoundBank,
    PCSTR pFriendlyName,
    PDWORD pdwSoundCueIndex)
{
    EmuSwapFS(); // Win2k/XP FS

    if(pdwSoundCueIndex == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_POINTER;
    }
    *pdwSoundCueIndex = XactSoundBankInvalidCueIndex;
    if(pFriendlyName == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_INVALIDARG;
    }

    HRESULT result = E_FAIL;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTSoundBank* soundBank = FindXactSoundBank(pSoundBank);
        DWORD soundBankSize = 0;
        DWORD flags = 0;
        WORD cueCount = 0;
        if(soundBank != nullptr &&
           GetSoundBankSize(
               soundBank->Data, soundBank->Size, &soundBankSize) &&
           ReadXactDword(soundBank->Data, soundBankSize, 0x18, &flags) &&
           ReadXactWord(soundBank->Data, soundBankSize, 0x1E, &cueCount) &&
           (flags & XactSoundBankNamesUnavailableFlag) == 0)
        {
            for(DWORD index = 0; index < cueCount; ++index)
            {
                DWORD nameOffset = 0;
                const DWORD cueOffset = XactSoundBankHeaderSize +
                                        index * XactSoundBankCueEntrySize;
                if(ReadXactDword(
                       soundBank->Data,
                       soundBankSize,
                       cueOffset,
                       &nameOffset) &&
                   std::strcmp(
                       reinterpret_cast<const char*>(
                           soundBank->Data + nameOffset),
                       pFriendlyName) == 0)
                {
                    *pdwSoundCueIndex = index;
                    result = S_OK;
                    break;
                }
            }
        }
    }

    EmuSwapFS(); // Xbox FS
    return result;
}

HRESULT WINAPI XTL::EmuIXACTSoundBank_PrepareEx(
    X_XACTSoundBank* pSoundBank,
    const X_XACT_PREPARE_SOUNDCUE* pPrepareData,
    X_XACTSoundCue** ppSoundCue)
{
    EmuSwapFS(); // Win2k/XP FS

    if(ppSoundCue == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_POINTER;
    }
    *ppSoundCue = nullptr;
    if(pPrepareData == nullptr)
    {
        EmuSwapFS(); // Xbox FS
        return E_INVALIDARG;
    }

    HRESULT result = E_INVALIDARG;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTSoundBank* soundBank = FindXactSoundBank(pSoundBank);
        if(soundBank != nullptr)
        {
            result = CreateXactCueLocked(
                soundBank, pPrepareData, false, ppSoundCue);
        }
    }

    EmuSwapFS(); // Xbox FS
    return result;
}

HRESULT WINAPI XTL::EmuIXACTSoundBank_PlayEx(
    X_XACTSoundBank* pSoundBank,
    const X_XACT_PREPARE_SOUNDCUE* pPrepareData,
    X_XACTSoundCue** ppSoundCue)
{
    EmuSwapFS(); // Win2k/XP FS

    if(ppSoundCue != nullptr)
    {
        *ppSoundCue = nullptr;
    }
    if(pPrepareData == nullptr ||
       (ppSoundCue == nullptr &&
        (pPrepareData->dwFlags & XactSoundCueAutoReleaseFlag) == 0))
    {
        EmuSwapFS(); // Xbox FS
        return pPrepareData == nullptr ? E_INVALIDARG : E_POINTER;
    }

    HRESULT result = E_INVALIDARG;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTSoundBank* soundBank = FindXactSoundBank(pSoundBank);
        if(soundBank != nullptr)
        {
            result = CreateXactCueLocked(
                soundBank, pPrepareData, true, ppSoundCue);
        }
    }

    EmuSwapFS(); // Xbox FS
    return result;
}

HRESULT WINAPI XTL::EmuIXACTSoundBank_Stop(
    X_XACTSoundBank* pSoundBank,
    DWORD dwSoundCueIndex,
    DWORD dwFlags,
    X_XACTSoundCue* pSoundCue)
{
    EmuSwapFS(); // Win2k/XP FS
    (void)dwFlags;

    HRESULT result = E_INVALIDARG;
    X_XACTSoundCue* releasedCues = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_XactEngineMutex);
        X_XACTSoundBank* soundBank = FindXactSoundBank(pSoundBank);
        if(soundBank != nullptr)
        {
            result = S_OK;
            X_XACTSoundCue** link = &soundBank->Cues;
            while(*link != nullptr)
            {
                X_XACTSoundCue* cue = *link;
                const bool matches = pSoundCue != nullptr
                                         ? cue == pSoundCue
                                         : dwSoundCueIndex ==
                                                   XactSoundBankInvalidCueIndex ||
                                               cue->CueIndex ==
                                                   dwSoundCueIndex;
                if(matches)
                {
                    *link = cue->Next;
                    cue->Next = releasedCues;
                    releasedCues = cue;
                    if(pSoundCue != nullptr)
                    {
                        break;
                    }
                }
                else
                {
                    link = &cue->Next;
                }
            }
            if(pSoundCue != nullptr && releasedCues == nullptr)
            {
                result = E_INVALIDARG;
            }
        }
    }

    DestroyXactCueList(releasedCues);
    EmuSwapFS(); // Xbox FS
    return result;
}
