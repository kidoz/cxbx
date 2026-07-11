// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->EmuXapi.cpp
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

#include "Emu.h"
#include "EmuFS.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace NtDll
{
    #include "EmuNtDll.h"
};

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
    #include "EmuXTL.h"
};

static bool EmuXInputInjectionConfigured()
{
    char buffer[2] = {};
    return GetEnvironmentVariableA("CXBX_INPUT_STATE", buffer, sizeof(buffer)) != 0;
}

static DWORD EmuXInputConnectedMask()
{
    // Injected input needs no host devices -- and skipping the host query
    // keeps headless runs off the host-input path entirely.
    if(EmuXInputInjectionConfigured()) {
        return 1u;
    }

    // The host XInput backend is initialized during EmuInit (before the
    // FS-swap is active), so LoadLibrary/GetProcAddress has already resolved.
    // The per-call XInputGetState is a plain C ABI call that does not throw.
    // The SEH guard remains as defense-in-depth: if a driver-level fault
    // occurs, we report zero devices rather than killing the process.
    DWORD connectedMask = 0;
    __try {
        connectedMask = XTL::EmuDInputGetConnectedMask();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        static LONG s_Warned = 0;
        if(InterlockedExchange(&s_Warned, 1) == 0) {
            printf("EmuXapi (0x%X): *WARNING* host input query faulted (0x%08lX); reporting no devices.\n",
                   GetCurrentThreadId(), GetExceptionCode());
            fflush(stdout);
        }
    }
    return connectedMask;
}

static bool EmuXInputHandleToPort(HANDLE handle, DWORD* port)
{
    const std::uintptr_t handleValue = reinterpret_cast<std::uintptr_t>(handle);
    if (port == nullptr || handleValue < 1 || handleValue > 4) {
        return false;
    }

    *port = static_cast<DWORD>(handleValue - 1);
    return true;
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

    BOOL bRet = NtDll::RtlFreeHeap(hHeap, dwFlags, lpMem);

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

    if (DeviceType != nullptr && DeviceType->Reserved[0] == 0 &&
        DeviceType->Reserved[1] == 0 && DeviceType->Reserved[2] == 0) {
        ret = EmuXInputConnectedMask();
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

    static DWORD previousConnectedMask = 0;
    static bool hasPreviousMask = false;

    BOOL changed = FALSE;
    if (DeviceType != nullptr && pdwInsertions != nullptr && pdwRemovals != nullptr) {
        const DWORD connectedMask = EmuXInputConnectedMask();
        const DWORD oldMask = hasPreviousMask ? previousConnectedMask : 0;
        *pdwInsertions = connectedMask & ~oldMask;
        *pdwRemovals = oldMask & ~connectedMask;
        changed = (*pdwInsertions != 0 || *pdwRemovals != 0) ? TRUE : FALSE;
        previousConnectedMask = connectedMask;
        hasPreviousMask = true;
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

    if (DeviceType != nullptr && dwPort < 4 &&
        (EmuXInputConnectedMask() & (1u << dwPort)) != 0) {
        ret = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(dwPort + 1));
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
    } else if (EmuXInputHandleToPort(hDevice, &port) &&
               (EmuXInputConnectedMask() & (1u << port)) != 0) {
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

    EmuSwapFS();   // XBox FS

    return ret;
}

// ******************************************************************
// * EmuXInputInjectState
// ******************************************************************
// Headless input injection: when CXBX_INPUT_STATE is set, XInputGetState
// returns a synthetic pad state instead of polling host DirectInput -- the
// hook that lets conformance probes (and eventually scripted menu
// navigation) run without a physical controller. Format:
//   CXBX_INPUT_STATE="0x<wButtons>[,A,B,X,Y,Black,White,LT,RT]"
// (wButtons hex word, then up to 8 decimal analog-button bytes.)
static bool EmuXInputInjectState(XTL::PXINPUT_STATE pState)
{
    static int  nMode = 0; // 0 = unchecked, 1 = off, 2 = injecting
    static WORD wButtons = 0;
    static BYTE bAnalog[8] = {0};
    static DWORD dwPacket = 0;

    if(nMode == 0)
    {
        char szBuf[128];
        DWORD dwLen = GetEnvironmentVariableA("CXBX_INPUT_STATE", szBuf, sizeof(szBuf));
        if(dwLen == 0 || dwLen >= sizeof(szBuf))
            nMode = 1;
        else
        {
            wButtons = (WORD)strtoul(szBuf, 0, 0);
            char *p = strchr(szBuf, ',');
            for(int i=0;i<8 && p != 0;i++)
            {
                bAnalog[i] = (BYTE)strtoul(p+1, 0, 10);
                p = strchr(p+1, ',');
            }
            nMode = 2;
            printf("EmuXapi: input injection active (wButtons=0x%04X)\n", wButtons);
        }
    }

    if(nMode != 2)
        return false;

    ZeroMemory(pState, sizeof(*pState));
    pState->dwPacketNumber = ++dwPacket;
    pState->Gamepad.wButtons = wButtons;
    memcpy(pState->Gamepad.bAnalogButtons, bAnalog, sizeof(bAnalog));

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
    } else if (EmuXInputHandleToPort(hDevice, &port)) {
        if (port == 0 && EmuXInputInjectState(pState)) {
            ret = ERROR_SUCCESS;
        } else {
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
    } else if (EmuXInputHandleToPort(hDevice, &port)) {
        if (port == 0 && EmuXInputInjectionConfigured()) {
            ret = ERROR_SUCCESS;
        } else {
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
