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
    X_XACTEngine* Next;
};

struct XTL::X_XACTWaveBank
{
    X_XACTEngine* Engine;
    const BYTE* Data;
    DWORD Size;
    X_XACTWaveBank* Next;
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

struct XactWaveBankRegion
{
    DWORD Offset;
    DWORD Length;
};

std::mutex g_XactEngineMutex;
XTL::X_XACTEngine* g_XactEngines = nullptr;

bool ReadWaveBankDword(
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

bool ReadWaveBankRegion(
    const BYTE* data,
    DWORD size,
    DWORD index,
    XactWaveBankRegion* region)
{
    const DWORD regionOffset = 8 + index * 8;
    return ReadWaveBankDword(data, size, regionOffset, &region->Offset) &&
           ReadWaveBankDword(data, size, regionOffset + 4, &region->Length);
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
    if(!ReadWaveBankDword(data, size, entryOffset + 4, &format) ||
       !ReadWaveBankDword(data, size, entryOffset + 8, &playOffset) ||
       !ReadWaveBankDword(data, size, entryOffset + 12, &playLength) ||
       !ReadWaveBankDword(data, size, entryOffset + 16, &loopOffset) ||
       !ReadWaveBankDword(data, size, entryOffset + 20, &loopLength))
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
    if(!ReadWaveBankDword(data, size, 0, &signature) ||
       !ReadWaveBankDword(data, size, 4, &version) ||
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
    if(!ReadWaveBankDword(data, size, bankData.Offset, &flags) ||
       !ReadWaveBankDword(data, size, bankData.Offset + 4, &entryCount) ||
       !ReadWaveBankDword(
           data, size, bankData.Offset + 24, &metadataElementSize) ||
       !ReadWaveBankDword(
           data, size, bankData.Offset + 28, &entryNameElementSize) ||
       !ReadWaveBankDword(data, size, bankData.Offset + 32, &alignment))
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
        return ReadWaveBankDword(
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
            if(engine->ReferenceCount < (std::numeric_limits<ULONG>::max)())
            {
                ++engine->ReferenceCount;
            }
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
        X_XACTEngine** link = &g_XactEngines;
        while(*link != nullptr && *link != pEngine)
        {
            link = &(*link)->Next;
        }

        if(*link != nullptr)
        {
            X_XACTEngine* engine = *link;
            if(engine->ReferenceCount > 1)
            {
                referenceCount = --engine->ReferenceCount;
            }
            else
            {
                *link = engine->Next;
                releasedEngine = engine;
            }
        }
    }

    if(releasedEngine != nullptr)
    {
        X_XACTWaveBank* waveBank = releasedEngine->WaveBanks;
        while(waveBank != nullptr)
        {
            X_XACTWaveBank* next = waveBank->Next;
            delete waveBank;
            waveBank = next;
        }
        delete releasedEngine;
    }
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
        if(engine != nullptr)
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
            }
        }
    }

    const bool found = releasedWaveBank != nullptr;
    delete releasedWaveBank;
    EmuSwapFS(); // Xbox FS
    return found ? S_OK : E_INVALIDARG;
}
