// Xbox Audio Creation Tool engine lifecycle HLE.
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

#include "Emu.h"
#include "EmuFS.h"

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
    X_XACTEngine* Next;
};

namespace
{
std::mutex g_XactEngineMutex;
XTL::X_XACTEngine* g_XactEngines = nullptr;

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

    delete releasedEngine;
    EmuSwapFS(); // Xbox FS
    return referenceCount;
}
