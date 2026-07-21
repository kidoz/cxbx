// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->xapi_emulation.cpp
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

#undef FIELD_OFFSET     // prevent macro redefinition warnings
#define POINTER_64 __ptr64

#include <windows.h>

#include <cstdint>
#include <limits>

#include "Emu.h"
#include "EmuFS.h"
#include "HostInput.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace NtDll
{
    #include "ntdll_emulation.h"
};

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
#include "xapi_emulation.h"
#include "dinput_emulation.h"
};

static bool EmuHeapTraceEnabled()
{
    static const bool Enabled = []
    {
        char Value[2] = {};
        return GetEnvironmentVariableA("CXBX_HEAP_TRACE", Value,
                                       sizeof(Value)) != 0;
    }();
    return Enabled;
}

struct EmuTrackedHeapAllocation
{
    PVOID Memory;
    HANDLE Heap;
};

static SRWLOCK g_EmuHeapAllocationLock = SRWLOCK_INIT;
static EmuTrackedHeapAllocation g_EmuHeapAllocations[65536] = {};
static PVOID const EmuHeapAllocationTombstone = reinterpret_cast<PVOID>(1);

static ULONG EmuHeapAllocationHash(PVOID Memory)
{
    return (reinterpret_cast<ULONG>(Memory) >> 3) &
           (static_cast<ULONG>(sizeof(g_EmuHeapAllocations) /
                               sizeof(g_EmuHeapAllocations[0])) - 1);
}

static void EmuTrackHeapAllocation(HANDLE Heap, PVOID Memory)
{
    if(Memory == NULL)
    {
        return;
    }

    AcquireSRWLockExclusive(&g_EmuHeapAllocationLock);
    const ULONG Capacity = static_cast<ULONG>(sizeof(g_EmuHeapAllocations) /
                                               sizeof(g_EmuHeapAllocations[0]));
    ULONG Slot = EmuHeapAllocationHash(Memory);
    ULONG Tombstone = Capacity;
    for(ULONG Probe = 0; Probe < Capacity; ++Probe)
    {
        EmuTrackedHeapAllocation& Entry = g_EmuHeapAllocations[Slot];
        if(Entry.Memory == Memory)
        {
            Entry.Heap = Heap;
            ReleaseSRWLockExclusive(&g_EmuHeapAllocationLock);
            return;
        }
        if(Entry.Memory == EmuHeapAllocationTombstone && Tombstone == Capacity)
        {
            Tombstone = Slot;
        }
        else if(Entry.Memory == NULL)
        {
            const ULONG Destination = Tombstone != Capacity ? Tombstone : Slot;
            g_EmuHeapAllocations[Destination].Memory = Memory;
            g_EmuHeapAllocations[Destination].Heap = Heap;
            ReleaseSRWLockExclusive(&g_EmuHeapAllocationLock);
            return;
        }
        Slot = (Slot + 1) & (Capacity - 1);
    }
    ReleaseSRWLockExclusive(&g_EmuHeapAllocationLock);
}

static bool EmuUntrackHeapAllocation(HANDLE Heap, PVOID Memory)
{
    if(Memory == NULL)
    {
        return true;
    }

    bool Found = false;
    AcquireSRWLockExclusive(&g_EmuHeapAllocationLock);
    const ULONG Capacity = static_cast<ULONG>(sizeof(g_EmuHeapAllocations) /
                                               sizeof(g_EmuHeapAllocations[0]));
    ULONG Slot = EmuHeapAllocationHash(Memory);
    for(ULONG Probe = 0; Probe < Capacity; ++Probe)
    {
        EmuTrackedHeapAllocation& Entry = g_EmuHeapAllocations[Slot];
        if(Entry.Memory == NULL)
        {
            break;
        }
        if(Entry.Memory == Memory && Entry.Heap == Heap)
        {
            Entry.Memory = EmuHeapAllocationTombstone;
            Entry.Heap = NULL;
            Found = true;
            break;
        }
        Slot = (Slot + 1) & (Capacity - 1);
    }
    ReleaseSRWLockExclusive(&g_EmuHeapAllocationLock);
    return Found;
}

static bool EmuProcessHeapOwns(PVOID Memory)
{
    if(Memory == NULL)
    {
        return false;
    }

    __try
    {
        return HeapValidate(GetProcessHeap(), 0, Memory) != FALSE;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool EmuXInputInjectionConfigured()
{
    char buffer[2] = {};
    return GetEnvironmentVariableA("CXBX_INPUT_STATE", buffer, sizeof(buffer)) != 0 ||
           GetEnvironmentVariableA("CXBX_INPUT_STATE_SEQUENCE", buffer,
                                   sizeof(buffer)) != 0;
}

struct EmuInputConnectionSnapshot
{
    DWORD currentMask;
    DWORD changedMask;
    DWORD generations[4];
};

struct EmuInjectedConnectionState
{
    bool initialized;
    bool firstRefreshPending;
    DWORD masks[32];
    DWORD maskCount;
    DWORD nextMask;
    EmuInputConnectionSnapshot snapshot;
};

static EmuInjectedConnectionState g_EmuInjectedConnections = {};

static void EmuXInputObserveInjectedMask(DWORD currentMask)
{
    currentMask &= 0x0Fu;
    const DWORD transitionMask =
        g_EmuInjectedConnections.snapshot.currentMask ^ currentMask;
    g_EmuInjectedConnections.snapshot.currentMask = currentMask;
    g_EmuInjectedConnections.snapshot.changedMask |= transitionMask;
    for(DWORD port = 0; port < 4; ++port)
    {
        if((transitionMask & (1u << port)) != 0)
        {
            ++g_EmuInjectedConnections.snapshot.generations[port];
            if(g_EmuInjectedConnections.snapshot.generations[port] == 0)
            {
                ++g_EmuInjectedConnections.snapshot.generations[port];
            }
        }
    }
}

static void EmuXInputInitializeInjectedConnections()
{
    if(g_EmuInjectedConnections.initialized)
    {
        return;
    }

    char buffer[256] = {};
    const DWORD length = GetEnvironmentVariableA(
        "CXBX_INPUT_MASK_SEQUENCE", buffer, sizeof(buffer));
    if(length != 0 && length < sizeof(buffer))
    {
        char* cursor = buffer;
        while(g_EmuInjectedConnections.maskCount < 32)
        {
            char* end = nullptr;
            const DWORD mask = static_cast<DWORD>(strtoul(cursor, &end, 0));
            if(end == cursor)
            {
                break;
            }

            g_EmuInjectedConnections.masks[g_EmuInjectedConnections.maskCount++] =
                mask & 0x0Fu;
            if(*end != ',')
            {
                break;
            }
            cursor = end + 1;
        }
    }

    if(g_EmuInjectedConnections.maskCount == 0)
    {
        g_EmuInjectedConnections.masks[0] = 1u;
        g_EmuInjectedConnections.maskCount = 1;
    }

    EmuXInputObserveInjectedMask(g_EmuInjectedConnections.masks[0]);
    g_EmuInjectedConnections.nextMask = 1;
    g_EmuInjectedConnections.firstRefreshPending = true;
    g_EmuInjectedConnections.initialized = true;
}

static EmuInputConnectionSnapshot EmuXInputInjectedConnectionSnapshot(
    bool advance, bool consumeChanges)
{
    EmuXInputInitializeInjectedConnections();
    if(advance)
    {
        if(g_EmuInjectedConnections.firstRefreshPending)
        {
            g_EmuInjectedConnections.firstRefreshPending = false;
        }
        else if(g_EmuInjectedConnections.nextMask <
                g_EmuInjectedConnections.maskCount)
        {
            EmuXInputObserveInjectedMask(
                g_EmuInjectedConnections.masks[g_EmuInjectedConnections.nextMask++]);
        }
    }

    EmuInputConnectionSnapshot snapshot = g_EmuInjectedConnections.snapshot;
    if(consumeChanges)
    {
        g_EmuInjectedConnections.snapshot.changedMask = 0;
    }
    return snapshot;
}

static EmuInputConnectionSnapshot EmuXInputConnectionSnapshot(
    bool advanceInjectedMask, bool consumeChanges)
{
    if(EmuXInputInjectionConfigured())
    {
        return EmuXInputInjectedConnectionSnapshot(advanceInjectedMask,
                                                   consumeChanges);
    }

    EmuInputConnectionSnapshot snapshot = {};
    __try
    {
        XTL::EmuDInputGetConnectionSnapshot(TRUE, consumeChanges ? TRUE : FALSE,
                                            &snapshot.currentMask,
                                            &snapshot.changedMask,
                                            snapshot.generations);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        static LONG s_Warned = 0;
        if(InterlockedExchange(&s_Warned, 1) == 0)
        {
            printf("EmuXapi (0x%X): *WARNING* host input query faulted (0x%08lX); reporting no devices.\n",
                   GetCurrentThreadId(), GetExceptionCode());
            fflush(stdout);
        }
    }
    return snapshot;
}

static void EmuXInputRefreshDeviceType(
    XTL::PXPP_DEVICE_TYPE deviceType,
    const EmuInputConnectionSnapshot& snapshot)
{
    const DWORD observedChanges =
        (deviceType->CurrentConnected ^ snapshot.currentMask) |
        snapshot.changedMask;
    deviceType->CurrentConnected = snapshot.currentMask;
    deviceType->ChangeConnected |= observedChanges;
}

static DWORD EmuXInputEncodedGeneration(DWORD generation)
{
    constexpr std::uintptr_t PortBits = 3;
    const std::uintptr_t generationMask = UINTPTR_MAX >> PortBits;
    DWORD encodedGeneration =
        generation & static_cast<DWORD>(generationMask);
    if(encodedGeneration == 0)
    {
        encodedGeneration = 1;
    }
    return encodedGeneration;
}

static HANDLE EmuXInputMakeHandle(DWORD port, DWORD generation)
{
    constexpr std::uintptr_t PortBits = 3;
    const std::uintptr_t encodedGeneration =
        EmuXInputEncodedGeneration(generation);
    const std::uintptr_t value =
        (encodedGeneration << PortBits) | static_cast<std::uintptr_t>(port + 1);
    return reinterpret_cast<HANDLE>(value);
}

static bool EmuXInputHandleToPort(HANDLE handle, DWORD* port, DWORD* generation)
{
    constexpr std::uintptr_t PortMask = 0x07u;
    const std::uintptr_t handleValue = reinterpret_cast<std::uintptr_t>(handle);
    const std::uintptr_t portValue = handleValue & PortMask;
    const std::uintptr_t generationValue = handleValue >> 3;
    if(port == nullptr || generation == nullptr || portValue < 1 ||
       portValue > 4 || generationValue == 0)
    {
        return false;
    }

    *port = static_cast<DWORD>(portValue - 1);
    *generation = static_cast<DWORD>(generationValue);
    return true;
}

enum class EmuInputHandleStatus
{
    Invalid,
    Disconnected,
    Connected,
};

static EmuInputHandleStatus EmuXInputValidateHandle(HANDLE handle, DWORD* port)
{
    DWORD handleGeneration = 0;
    if(!EmuXInputHandleToPort(handle, port, &handleGeneration))
    {
        return EmuInputHandleStatus::Invalid;
    }

    const EmuInputConnectionSnapshot snapshot =
        EmuXInputConnectionSnapshot(false, false);
    const DWORD currentGeneration =
        EmuXInputEncodedGeneration(snapshot.generations[*port]);
    if((snapshot.currentMask & (1u << *port)) == 0 ||
       currentGeneration != handleGeneration)
    {
        return EmuInputHandleStatus::Disconnected;
    }
    return EmuInputHandleStatus::Connected;
}

// ******************************************************************
// * func: EmuRtlCreateHeap
// ******************************************************************
PVOID WINAPI XTL::EmuRtlCreateHeap
(
    IN ULONG   Flags,
    IN PVOID   Base OPTIONAL,
    IN ULONG   Reserve OPTIONAL,
    IN ULONG   Commit,
    IN BOOLEAN Lock OPTIONAL,
    IN PVOID   RtlHeapParams OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuRtlCreateHeap\n"
               "(\n"
               "   Flags               : 0x%.08X\n"
               "   Base                : 0x%.08X\n"
               "   Reserve             : 0x%.08X\n"
               "   Commit              : 0x%.08X\n"
               "   Lock                : 0x%.08X\n"
               "   RtlHeapParams       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Flags, Base, Reserve, Commit, Lock, RtlHeapParams);
    }
    #endif

    NtDll::RTL_HEAP_DEFINITION RtlHeapDefinition;

    ZeroMemory(&RtlHeapDefinition, sizeof(RtlHeapDefinition));

    RtlHeapDefinition.Length = sizeof(RtlHeapDefinition);

    PVOID pRet = NtDll::RtlCreateHeap(Flags, Base, Reserve, Commit, Lock, &RtlHeapDefinition);

    if(EmuHeapTraceEnabled())
    {
        printf("HEAP| create flags=0x%.08lX reserve=0x%.08lX commit=0x%.08lX heap=0x%.08lX\n",
               Flags, Reserve, Commit, reinterpret_cast<ULONG>(pRet));
    }

    EmuSwapFS();   // XBox FS

    return pRet;
}

// ******************************************************************
// * func: EmuRtlAllocateHeap
// ******************************************************************
PVOID WINAPI XTL::EmuRtlAllocateHeap
(
    IN HANDLE hHeap,
    IN DWORD  dwFlags,
    IN SIZE_T dwBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuRtlAllocateHeap\n"
               "(\n"
               "   hHeap               : 0x%.08X\n"
               "   dwFlags             : 0x%.08X\n"
               "   dwBytes             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hHeap, dwFlags, dwBytes);
    }
    #endif

    PVOID pRet = NtDll::RtlAllocateHeap(hHeap, dwFlags, dwBytes);
    EmuTrackHeapAllocation(hHeap, pRet);

    if(EmuHeapTraceEnabled())
    {
        printf("HEAP| alloc heap=0x%.08lX flags=0x%.08lX size=0x%.08lX ptr=0x%.08lX\n",
               reinterpret_cast<ULONG>(hHeap), dwFlags,
               static_cast<ULONG>(dwBytes), reinterpret_cast<ULONG>(pRet));
    }

    EmuSwapFS();   // XBox FS

    return pRet;
}

// ******************************************************************
// * func: EmuRtlReAllocateHeap
// ******************************************************************
PVOID WINAPI XTL::EmuRtlReAllocateHeap
(
    IN HANDLE hHeap,
    IN DWORD  dwFlags,
    IN PVOID  lpMem,
    IN SIZE_T dwBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    const bool Tracked = EmuUntrackHeapAllocation(hHeap, lpMem);
    PVOID pRet = NtDll::RtlReAllocateHeap(hHeap, dwFlags, lpMem, dwBytes);
    if(pRet != NULL)
    {
        EmuTrackHeapAllocation(hHeap, pRet);
    }
    else if(Tracked)
    {
        EmuTrackHeapAllocation(hHeap, lpMem);
    }

    if(EmuHeapTraceEnabled())
    {
        printf("HEAP| realloc heap=0x%.08lX flags=0x%.08lX old=0x%.08lX size=0x%.08lX ptr=0x%.08lX\n",
               reinterpret_cast<ULONG>(hHeap), dwFlags,
               reinterpret_cast<ULONG>(lpMem), static_cast<ULONG>(dwBytes),
               reinterpret_cast<ULONG>(pRet));
    }

    EmuSwapFS();   // XBox FS

    return pRet;
}

// ******************************************************************
// * func: EmuRtlFreeHeap
// ******************************************************************
BOOL WINAPI XTL::EmuRtlFreeHeap
(
    IN HANDLE hHeap,
    IN DWORD  dwFlags,
    IN PVOID  lpMem
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuRtlFreeHeap\n"
               "(\n"
               "   hHeap               : 0x%.08X\n"
               "   dwFlags             : 0x%.08X\n"
               "   lpMem               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hHeap, dwFlags, lpMem);
    }
    #endif

    const bool Tracked = EmuUntrackHeapAllocation(hHeap, lpMem);
    const bool ProcessOwned = !Tracked && EmuProcessHeapOwns(lpMem);
    bool Valid = true;
    if(EmuHeapTraceEnabled() && lpMem != NULL)
    {
        __try
        {
            Valid = HeapValidate(hHeap, 0, lpMem) != FALSE;
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            Valid = false;
        }
        printf("HEAP| free heap=0x%.08lX flags=0x%.08lX ptr=0x%.08lX valid=%u tracked=%u process=%u\n",
               reinterpret_cast<ULONG>(hHeap), dwFlags,
               reinterpret_cast<ULONG>(lpMem), Valid ? 1u : 0u,
               Tracked ? 1u : 0u, ProcessOwned ? 1u : 0u);
        fflush(stdout);
    }

    BOOL bRet = FALSE;
    if(ProcessOwned)
    {
        bRet = HeapFree(GetProcessHeap(), dwFlags, lpMem);
    }
    else if(Valid)
    {
        bRet = NtDll::RtlFreeHeap(hHeap, dwFlags, lpMem);
        if(!bRet && Tracked)
        {
            EmuTrackHeapAllocation(hHeap, lpMem);
        }
    }

    EmuSwapFS();   // XBox FS

    return bRet;
}

// ******************************************************************
// * func: EmuRtlSizeHeap
// ******************************************************************
SIZE_T WINAPI XTL::EmuRtlSizeHeap
(
    IN HANDLE hHeap,
    IN DWORD  dwFlags,
    IN PVOID  lpMem
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuRtlSizeHeap\n"
               "(\n"
               "   hHeap               : 0x%.08X\n"
               "   dwFlags             : 0x%.08X\n"
               "   lpMem               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hHeap, dwFlags, lpMem);
    }
    #endif

    SIZE_T ret = NtDll::RtlSizeHeap(hHeap, dwFlags, lpMem);

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: XapiUnknownBad1
// ******************************************************************
// NOTE: This does some hard disk verification and other things
VOID WINAPI XTL::EmuXapiUnknownBad1
(
    IN DWORD dwUnknown
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXapiUnknownBad1\n"
               "(\n"
               "   dwUnknown           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), dwUnknown);
    }
    #endif

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuQueryPerformanceCounter
// ******************************************************************
BOOL WINAPI XTL::EmuQueryPerformanceCounter
(
    PLARGE_INTEGER lpPerformanceCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuQueryPerformanceCounter\n"
               "(\n"
               "   lpPerformanceCount  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), lpPerformanceCount);
    }
    #endif

    static const LONGLONG XboxTscFrequency = 733333333;
    static LONGLONG HostStart = 0;
    static LONGLONG HostFrequency = 0;
    static volatile LONG InitState = 0;
    LARGE_INTEGER HostCounter;

    if(InterlockedCompareExchange(&InitState, 2, 2) != 2)
    {
        if(InterlockedCompareExchange(&InitState, 1, 0) == 0)
        {
            LARGE_INTEGER Frequency;
            QueryPerformanceFrequency(&Frequency);
            QueryPerformanceCounter(&HostCounter);
            HostStart = HostCounter.QuadPart;
            HostFrequency = Frequency.QuadPart;
            InterlockedExchange(&InitState, 2);
        }
        else
        {
            while(InterlockedCompareExchange(&InitState, 2, 2) != 2)
                Sleep(0);
        }
    }

    QueryPerformanceCounter(&HostCounter);

    LONGLONG Elapsed = HostCounter.QuadPart - HostStart;
    LONGLONG Whole = (Elapsed / HostFrequency) * XboxTscFrequency;
    LONGLONG Part = (Elapsed % HostFrequency) * XboxTscFrequency / HostFrequency;
    lpPerformanceCount->QuadPart = Whole + Part;

    EmuSwapFS();   // XBox FS

    return TRUE;
}

// ******************************************************************
// * func: EmuQueryPerformanceFrequency
// ******************************************************************
BOOL WINAPI XTL::EmuQueryPerformanceFrequency
(
    PLARGE_INTEGER lpFrequency
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuQueryPerformanceFrequency\n"
               "(\n"
               "   lpFrequency         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), lpFrequency);
    }
    #endif

    lpFrequency->QuadPart = 733333333;

    EmuSwapFS();   // XBox FS

    return TRUE;
}

// ******************************************************************
// * func: EmuXMountUtilityDrive
// ******************************************************************
BOOL WINAPI XTL::EmuXMountUtilityDrive
(
    BOOL    fFormatClean
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuXapi (0x%X): EmuXMountUtilityDrive\n"
               "(\n"
               "   fFormatClean        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), fFormatClean);
        EmuSwapFS();   // XBox FS
    }
    #endif

    return TRUE;
}

// ******************************************************************
// * func: EmuXInitDevices
// ******************************************************************
VOID WINAPI XTL::EmuXInitDevices
(
    DWORD   Unknown1,
    PVOID   Unknown2
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXInitDevices\n"
               "(\n"
               "   Unknown1            : 0x%.08X\n"
               "   Unknown2            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Unknown1, Unknown2);
    }
    #endif

    // TODO: Initialize devices if/when necessary

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuXGetDevices
// ******************************************************************
DWORD WINAPI XTL::EmuXGetDevices
(
    PXPP_DEVICE_TYPE DeviceType
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXGetDevices\n"
               "(\n"
               "   DeviceType          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), DeviceType);
    }
    #endif

    DWORD ret = 0;

    if (DeviceType != nullptr) {
        const EmuInputConnectionSnapshot snapshot =
            EmuXInputConnectionSnapshot(true, true);
        EmuXInputRefreshDeviceType(DeviceType, snapshot);
        ret = DeviceType->CurrentConnected;
        DeviceType->ChangeConnected = 0;
        DeviceType->PreviousConnected = DeviceType->CurrentConnected;
    } else {
        EmuCleanup("Unknown DeviceType");
    }

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuXGetDeviceChanges
// ******************************************************************
BOOL WINAPI XTL::EmuXGetDeviceChanges
(
    PXPP_DEVICE_TYPE DeviceType,
    PDWORD           pdwInsertions,                  
    PDWORD           pdwRemovals                     
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXGetDeviceChanges\n"
               "(\n"
               "   DeviceType          : 0x%.08X\n"
               "   pdwInsertions       : 0x%.08X\n"
               "   pdwRemovals         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), DeviceType, pdwInsertions, pdwRemovals);
    }
    #endif

    BOOL changed = FALSE;
    if (DeviceType != nullptr && pdwInsertions != nullptr && pdwRemovals != nullptr) {
        const EmuInputConnectionSnapshot snapshot =
            EmuXInputConnectionSnapshot(true, true);
        EmuXInputRefreshDeviceType(DeviceType, snapshot);
        if (DeviceType->ChangeConnected == 0) {
            *pdwInsertions = 0;
            *pdwRemovals = 0;
        } else {
            *pdwInsertions = DeviceType->CurrentConnected &
                             ~DeviceType->PreviousConnected;
            *pdwRemovals = DeviceType->PreviousConnected &
                           ~DeviceType->CurrentConnected;

            const DWORD removeInsert = DeviceType->ChangeConnected &
                                       DeviceType->CurrentConnected &
                                       DeviceType->PreviousConnected;
            *pdwInsertions |= removeInsert;
            *pdwRemovals |= removeInsert;
            changed = (*pdwInsertions | *pdwRemovals) != 0 ? TRUE : FALSE;

            DeviceType->ChangeConnected = 0;
            DeviceType->PreviousConnected = DeviceType->CurrentConnected;
        }
    }

    EmuSwapFS();   // XBox FS

    return changed;
}

// ******************************************************************
// * func: EmuXInputOpen
// ******************************************************************
HANDLE WINAPI XTL::EmuXInputOpen
(
    IN PXPP_DEVICE_TYPE             DeviceType,
    IN DWORD                        dwPort,
    IN DWORD                        dwSlot,
    IN PXINPUT_POLLING_PARAMETERS   pPollingParameters OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXInputOpen\n"
               "(\n"
               "   DeviceType          : 0x%.08X\n"
               "   dwPort              : 0x%.08X\n"
               "   dwSlot              : 0x%.08X\n"
               "   pPollingParameters  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), DeviceType, dwPort, dwSlot, pPollingParameters);
    }
    #endif

    HANDLE ret = nullptr;

    if (DeviceType != nullptr && dwPort < 4) {
        const EmuInputConnectionSnapshot snapshot =
            EmuXInputConnectionSnapshot(false, false);
        if ((snapshot.currentMask & (1u << dwPort)) != 0) {
            ret = EmuXInputMakeHandle(dwPort, snapshot.generations[dwPort]);
        }
    }

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuXInputClose
// ******************************************************************
VOID WINAPI XTL::EmuXInputClose
(
    IN HANDLE hDevice
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXInputClose\n"
               "(\n"
               "   hDevice             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hDevice);
    }
    #endif

    // TODO: Actually clean up the device when/if necessary

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuXInputGetCapabilities
// ******************************************************************
DWORD WINAPI XTL::EmuXInputGetCapabilities
(
    IN  HANDLE               hDevice,
    OUT PXINPUT_CAPABILITIES pCapabilities
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXInputGetCapabilities\n"
               "(\n"
               "   hDevice             : 0x%.08X\n"
               "   pCapabilities       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hDevice, pCapabilities);
    }
    #endif

    DWORD ret = ERROR_INVALID_HANDLE;
    DWORD port = 0;

    if (pCapabilities == nullptr) {
        ret = ERROR_INVALID_PARAMETER;
    } else {
        const EmuInputHandleStatus status =
            EmuXInputValidateHandle(hDevice, &port);
        if (status == EmuInputHandleStatus::Disconnected) {
            ret = ERROR_DEVICE_NOT_CONNECTED;
        } else if (status == EmuInputHandleStatus::Connected) {
            ZeroMemory(pCapabilities, sizeof(*pCapabilities));
            pCapabilities->SubType = XINPUT_DEVSUBTYPE_GC_GAMEPAD;
            pCapabilities->In.Gamepad.wButtons = 0x00FFu;
            memset(pCapabilities->In.Gamepad.bAnalogButtons, 0xFF,
                   sizeof(pCapabilities->In.Gamepad.bAnalogButtons));
            pCapabilities->In.Gamepad.sThumbLX = 32767;
            pCapabilities->In.Gamepad.sThumbLY = 32767;
            pCapabilities->In.Gamepad.sThumbRX = 32767;
            pCapabilities->In.Gamepad.sThumbRY = 32767;
            pCapabilities->Out.Rumble.wLeftMotorSpeed = 0xFFFFu;
            pCapabilities->Out.Rumble.wRightMotorSpeed = 0xFFFFu;
            ret = ERROR_SUCCESS;
        }
    }

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * EmuXInputInjectState
// ******************************************************************
// Headless input injection returns a synthetic pad state instead of polling
// the host controller. Static format:
//   CXBX_INPUT_STATE="0x<wButtons>[,A,B,X,Y,Black,White,LT,RT]"
// Timed format (milliseconds relative to the first XInputGetState call):
//   CXBX_INPUT_STATE_SEQUENCE="0@0x0;8000@0x10;8200@0x0"
static bool EmuXInputInjectState(XTL::PXINPUT_STATE pState)
{
    enum class InjectionMode
    {
        Unchecked,
        Off,
        Static,
        Sequence,
    };
    static InjectionMode mode = InjectionMode::Unchecked;
    static HostInput::GamepadState staticState{};
    static HostInput::InjectedGamepadSequence sequence;
    static DWORD sequenceStartTick = 0;
    static DWORD packetNumber = 0;
    static std::size_t lastFrame = (std::numeric_limits<std::size_t>::max)();

    if(mode == InjectionMode::Unchecked)
    {
        char sequenceText[4096] = {};
        const DWORD sequenceLength = GetEnvironmentVariableA(
            "CXBX_INPUT_STATE_SEQUENCE", sequenceText, sizeof(sequenceText));
        if(sequenceLength != 0 && sequenceLength < sizeof(sequenceText))
        {
            if(sequence.Parse(sequenceText))
            {
                mode = InjectionMode::Sequence;
                sequenceStartTick = GetTickCount();
                printf("EmuXapi: timed input injection active (%zu frames)\n",
                       sequence.Size());
            }
            else
            {
                mode = InjectionMode::Off;
                printf("EmuXapi: invalid CXBX_INPUT_STATE_SEQUENCE; input injection disabled\n");
            }
        }
        else
        {
            char stateText[256] = {};
            const DWORD stateLength = GetEnvironmentVariableA(
                "CXBX_INPUT_STATE", stateText, sizeof(stateText));
            if(stateLength != 0 && stateLength < sizeof(stateText) &&
               HostInput::ParseInjectedGamepadState(stateText, staticState))
            {
                mode = InjectionMode::Static;
                printf("EmuXapi: input injection active (wButtons=0x%04X)\n",
                       staticState.buttons);
            }
            else
            {
                mode = InjectionMode::Off;
            }
        }
        fflush(stdout);
    }

    if(mode == InjectionMode::Off)
    {
        return false;
    }

    HostInput::GamepadState injectedState = staticState;
    std::size_t frame = 0;
    if(mode == InjectionMode::Sequence)
    {
        const DWORD elapsedMs = GetTickCount() - sequenceStartTick;
        frame = sequence.FrameIndexAt(elapsedMs);
        injectedState = sequence.StateAt(elapsedMs);
        if(frame != lastFrame)
        {
            printf("EmuXapi: timed input frame=%zu elapsed=%lu buttons=0x%04X\n",
                   frame, elapsedMs, injectedState.buttons);
            fflush(stdout);
        }
    }
    if(frame != lastFrame)
    {
        ++packetNumber;
        lastFrame = frame;
    }

    ZeroMemory(pState, sizeof(*pState));
    pState->dwPacketNumber = packetNumber;
    pState->Gamepad.wButtons = injectedState.buttons;
    memcpy(pState->Gamepad.bAnalogButtons, injectedState.analogButtons.data(),
           injectedState.analogButtons.size());
    pState->Gamepad.sThumbLX = injectedState.leftThumbX;
    pState->Gamepad.sThumbLY = injectedState.leftThumbY;
    pState->Gamepad.sThumbRX = injectedState.rightThumbX;
    pState->Gamepad.sThumbRY = injectedState.rightThumbY;

    return true;
}

// ******************************************************************
// * func: EmuInputGetState
// ******************************************************************
DWORD WINAPI XTL::EmuXInputGetState
(
    IN  HANDLE         hDevice,
    OUT PXINPUT_STATE  pState
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXInputGetState\n"
               "(\n"
               "   hDevice             : 0x%.08X\n"
               "   pState              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hDevice, pState);
    }
    #endif

    DWORD ret = ERROR_INVALID_HANDLE;
    DWORD port = 0;

    if (pState == nullptr) {
        ret = ERROR_INVALID_PARAMETER;
    } else {
        const EmuInputHandleStatus status =
            EmuXInputValidateHandle(hDevice, &port);
        if (status == EmuInputHandleStatus::Disconnected) {
            ret = ERROR_DEVICE_NOT_CONNECTED;
        } else if (status == EmuInputHandleStatus::Connected &&
                   port == 0 && EmuXInputInjectState(pState)) {
            ret = ERROR_SUCCESS;
        } else if (status == EmuInputHandleStatus::Connected) {
            __try {
                if (EmuDInputPoll(port, pState)) {
                    ret = ERROR_SUCCESS;
                } else {
                    ret = ERROR_DEVICE_NOT_CONNECTED;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                ret = ERROR_DEVICE_NOT_CONNECTED;
            }
        }
    }

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuInputGetState
// ******************************************************************
DWORD WINAPI XTL::EmuXInputSetState
(
    IN     HANDLE           hDevice,
    IN OUT PXINPUT_FEEDBACK pFeedback
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXInputSetState\n"
               "(\n"
               "   hDevice             : 0x%.08X\n"
               "   pFeedback           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hDevice, pFeedback);
    }
    #endif

    DWORD ret = ERROR_INVALID_HANDLE;
    DWORD port = 0;

    if (pFeedback == nullptr) {
        ret = ERROR_INVALID_PARAMETER;
    } else {
        const EmuInputHandleStatus status =
            EmuXInputValidateHandle(hDevice, &port);
        if (status == EmuInputHandleStatus::Disconnected) {
            ret = ERROR_DEVICE_NOT_CONNECTED;
        } else if (status == EmuInputHandleStatus::Connected &&
                   port == 0 && EmuXInputInjectionConfigured()) {
            ret = ERROR_SUCCESS;
        } else if (status == EmuInputHandleStatus::Connected) {
            ret = EmuDInputSetState(port, pFeedback->Rumble.wLeftMotorSpeed,
                                    pFeedback->Rumble.wRightMotorSpeed);
        }
        pFeedback->Header.dwStatus = ret;
    }

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * func: EmuCreateMutex
// ******************************************************************
HANDLE WINAPI XTL::EmuCreateMutex
(
    LPSECURITY_ATTRIBUTES   lpMutexAttributes,
    BOOL                    bInitialOwner,
    LPCSTR                  lpName
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuCreateMutex\n"
               "(\n"
               "   lpMutexAttributes   : 0x%.08X\n"
               "   bInitialOwner       : 0x%.08X\n"
               "   lpName              : 0x%.08X (%s)\n"
               ");\n",
               GetCurrentThreadId(), lpMutexAttributes, bInitialOwner, lpName, lpName);
    }
    #endif

    HANDLE hRet = CreateMutex((SECURITY_ATTRIBUTES *)lpMutexAttributes, bInitialOwner, lpName);

    EmuSwapFS();   // XBox FS

    return hRet;
}

// ******************************************************************
// * func: EmuCloseHandle
// ******************************************************************
BOOL WINAPI XTL::EmuCloseHandle
(
    HANDLE hObject
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuCloseHandle\n"
               "(\n"
               "   hObject             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hObject);
    }
    #endif

    BOOL bRet = CloseHandle(hObject);

    EmuSwapFS();   // XBox FS

    return bRet;
}

// ******************************************************************
// * func: EmuSetThreadPriority
// ******************************************************************
BOOL WINAPI XTL::EmuSetThreadPriority
(
    HANDLE  hThread,
    int     nPriority
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuSetThreadPriority\n"
               "(\n"
               "   hThread             : 0x%.08X\n"
               "   nPriority           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hThread, nPriority);
    }
    #endif

    BOOL bRet = SetThreadPriority(hThread, nPriority);

    EmuSwapFS();   // XBox FS

    return bRet;
}

// ******************************************************************
// * func: EmuGetExitCodeThread
// ******************************************************************
BOOL WINAPI XTL::EmuGetExitCodeThread
(
    HANDLE  hThread,
    LPDWORD lpExitCode
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuGetExitCodeThread\n"
               "(\n"
               "   hThread             : 0x%.08X\n"
               "   lpExitCode          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), hThread, lpExitCode);
    }
    #endif

    BOOL bRet = GetExitCodeThread(hThread, lpExitCode);

    EmuSwapFS();   // XBox FS

    return bRet;
}

// ******************************************************************
// * func: EmuXapiInitProcess
// ******************************************************************
VOID WINAPI XTL::EmuXapiInitProcess()
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXapiInitProcess();\n", GetCurrentThreadId());
    }
    #endif

    // ******************************************************************
	// * Call RtlCreateHeap
    // ******************************************************************
	{
        RTL_HEAP_PARAMETERS HeapParameters;

		ZeroMemory(&HeapParameters, sizeof(HeapParameters));

        HeapParameters.Length = sizeof(HeapParameters);

		EmuSwapFS();   // XBox FS

		uint32 dwPeHeapReserve = g_pXbeHeader->dwPeHeapReserve;
		uint32 dwPeHeapCommit  = g_pXbeHeader->dwPeHeapCommit;

        PVOID dwResult = 0;

        #define HEAP_GROWABLE 0x00000002

        *XTL::EmuXapiProcessHeap = XTL::g_pRtlCreateHeap(HEAP_GROWABLE, 0, dwPeHeapReserve, dwPeHeapCommit, 0, &HeapParameters);
	}

    return;
}

// ******************************************************************
// * data: EmuXapiProcessHeap
// ******************************************************************
PVOID* XTL::EmuXapiProcessHeap;

// ******************************************************************
// * func: g_pRtlCreateHeap
// ******************************************************************
XTL::pfRtlCreateHeap XTL::g_pRtlCreateHeap;

// ******************************************************************
// * func: EmuXapiThreadStartup
// ******************************************************************
VOID WINAPI XTL::EmuXapiThreadStartup
(
    DWORD dwDummy1,
    DWORD dwDummy2
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXapiThreadStartup\n"
               "(\n"
               "   dwDummy1            : 0x%.08X\n"
               "   dwDummy2            : 0x%.08X\n"
               ");\n",
                GetCurrentThreadId(), dwDummy1, dwDummy2);
    }
    #endif

    EmuSwapFS();   // XBox FS

	// TODO: Call thread notify routines ?

    __asm
    {
        push dwDummy2
        call dwDummy1
    }

    return;
}

/* Too High Level!
// ******************************************************************
// * func: XapiSetupPerTitleDriveLetters
// ******************************************************************
XTL::NTSTATUS CDECL XTL::XapiSetupPerTitleDriveLetters(DWORD dwTitleId, LPCWSTR wszTitleName)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): XapiSetupPerTitleDriveLetters\n"
               "(\n"
               "   dwTitleId           : 0x%.08X\n"
               "   wszTitleName        : 0x%.08X\n"
               ");\n",
                GetCurrentThreadId(), dwTitleId, wszTitleName);
    }
    #endif

    NTSTATUS ret = STATUS_SUCCESS;

    EmuSwapFS();   // XBox FS

    return ret;
}
*/
// ******************************************************************
// * func: EmuXapiBootDash
// ******************************************************************
VOID WINAPI XTL::EmuXapiBootDash(DWORD UnknownA, DWORD UnknownB, DWORD UnknownC)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXapiBootDash\n"
               "(\n"
               "   UnknownA            : 0x%.08X\n"
               "   UnknownB            : 0x%.08X\n"
               "   UnknownC            : 0x%.08X\n"
               ");\n",
                GetCurrentThreadId(), UnknownA, UnknownB, UnknownC);
    }
    #endif

    EmuCleanup("Emulation Terminated (XapiBootDash)");

    EmuSwapFS();   // XBox FS

    return;
}

// ******************************************************************
// * func: EmuXCalculateSignatureBegin
// ******************************************************************
HANDLE WINAPI XTL::EmuXCalculateSignatureBegin(DWORD dwFlags)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): XCalculateSignatureBegin\n"
               "(\n"
               "   dwFlags             : 0x%.08X\n"
               ");\n",
                GetCurrentThreadId(), dwFlags);
    }
    #endif

    EmuSwapFS();   // XBox FS

    // return a fake handle value for now
    return (PVOID)0xAAAAAAAA;
}
