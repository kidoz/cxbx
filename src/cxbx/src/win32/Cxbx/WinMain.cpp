// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbx->WinMain.cpp
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
#include "Emu.h"
#include "WndMain.h"
#include "EmuShared.h"
#include "EmuExe.h"
#include "core/Xbe.h"

#include <cstdio>
#include <cstring>

static void AppendLogLine(const char *szLogFile, const char *szLine)
{
    if(szLogFile == NULL || szLogFile[0] == '\0')
        return;

    HANDLE hFile = CreateFile(szLogFile, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if(hFile == INVALID_HANDLE_VALUE)
        return;

    DWORD dwWritten = 0;
    WriteFile(hFile, szLine, (DWORD)strlen(szLine), &dwWritten, NULL);
    WriteFile(hFile, "\r\n", 2, &dwWritten, NULL);
    CloseHandle(hFile);
}

static void ConfigureLogFile(const char *szLogFile)
{
    if(szLogFile == NULL || szLogFile[0] == '\0')
        return;

    AppendLogLine(szLogFile, "--- cxbx launcher pre-crt log ---");

    SetEnvironmentVariable("CXBX_LOG_FILE", szLogFile);

    FILE *out = freopen(szLogFile, "a", stdout);
    if(out != NULL)
        setvbuf(out, NULL, _IONBF, 0);

    FILE *err = freopen(szLogFile, "a", stderr);
    if(err != NULL)
        setvbuf(err, NULL, _IONBF, 0);

    printf("\n--- cxbx launcher start ---\n");
}

static void GetModuleDirectory(char *szDirectory)
{
    GetModuleFileName(NULL, szDirectory, 260);

    sint32 spot=-1;
    for(int v=0; v<260; v++)
    {
        if(szDirectory[v] == '\\')
            spot = v;
        else if(szDirectory[v] == '\0')
            break;
    }

    if(spot != -1)
        szDirectory[spot] = '\0';
}

static void BuildTempExePath(const char *szXbePath, char *szExePath)
{
    char szTempRoot[260];
    char szBaseName[260];

    GetTempPath(259, szTempRoot);

    const char *szName = strrchr(szXbePath, '\\');
    if(szName == NULL)
        szName = strrchr(szXbePath, '/');
    if(szName != NULL)
        szName++;
    else
        szName = szXbePath;

    strncpy(szBaseName, szName, sizeof(szBaseName) - 1);
    szBaseName[sizeof(szBaseName) - 1] = '\0';

    char *szExtension = strrchr(szBaseName, '.');
    if(szExtension != NULL)
        strcpy(szExtension, ".exe");
    else
        strcat(szBaseName, ".exe");

    snprintf(szExePath, 260, "%s%s", szTempRoot, szBaseName);
}

static void BuildXbeDirectory(const char *szXbePath, char *szDirectory)
{
    strncpy(szDirectory, szXbePath, 259);
    szDirectory[259] = '\0';

    char *szSlash = strrchr(szDirectory, '\\');
    if(szSlash == NULL)
        szSlash = strrchr(szDirectory, '/');

    if(szSlash != NULL)
        *szSlash = '\0';
    else
        GetModuleDirectory(szDirectory);
}

static void BuildAbsolutePath(const char *szPath, char *szAbsolutePath)
{
    DWORD dwAbsolutePath = GetFullPathName(szPath, 260, szAbsolutePath, NULL);
    if(dwAbsolutePath == 0 || dwAbsolutePath >= 260)
    {
        strncpy(szAbsolutePath, szPath, 259);
        szAbsolutePath[259] = '\0';
    }
}

static void CopyRuntimeDllNextToExe(const char *szExePath)
{
    char szModuleDirectory[260];
    char szSourceDll[260];
    char szTargetDll[260];

    GetModuleDirectory(szModuleDirectory);
    snprintf(szSourceDll, sizeof(szSourceDll), "%s\\Cxbx.dll", szModuleDirectory);

    strncpy(szTargetDll, szExePath, sizeof(szTargetDll) - 1);
    szTargetDll[sizeof(szTargetDll) - 1] = '\0';

    char *szSlash = strrchr(szTargetDll, '\\');
    if(szSlash == NULL)
        szSlash = strrchr(szTargetDll, '/');

    if(szSlash != NULL)
        strcpy(szSlash + 1, "Cxbx.dll");
    else
        strcpy(szTargetDll, "Cxbx.dll");

    if(CopyFile(szSourceDll, szTargetDll, FALSE))
        printf("cxbx: copied runtime DLL to %s.\n", szTargetDll);
    else
        printf("cxbx: failed to copy runtime DLL from %s to %s (error=%lu).\n", szSourceDll, szTargetDll, GetLastError());
}

static int RunXbeBatch(const char *szXbePath, const char *szLogFile)
{
    char szAbsoluteXbePath[260];
    BuildAbsolutePath(szXbePath, szAbsoluteXbePath);

    printf("cxbx: batch opening %s.\n", szAbsoluteXbePath);

    Xbe i_Xbe(szAbsoluteXbePath);
    if(i_Xbe.GetError() != 0)
    {
        printf("cxbx: %s\n", i_Xbe.GetError());
        return 1;
    }

    char szExePath[260];
    BuildTempExePath(szAbsoluteXbePath, szExePath);
    printf("cxbx: batch converting to %s.\n", szExePath);

    char szDebugFilename[260] = {0};
    DebugMode DebugMode = DM_NONE;

    if(szLogFile != NULL && szLogFile[0] != '\0')
    {
        BuildAbsolutePath(szLogFile, szDebugFilename);
        DebugMode = DM_FILE;
    }

    EmuExe i_EmuExe(&i_Xbe, DebugMode, szDebugFilename);
    i_EmuExe.Export(szExePath);

    if(i_EmuExe.GetError() != 0)
    {
        printf("cxbx: %s\n", i_EmuExe.GetError());
        return 1;
    }

    CopyRuntimeDllNextToExe(szExePath);

    g_EmuShared->SetXbePath(i_Xbe.m_szPath);

    char szWorkingDirectory[260];
    BuildXbeDirectory(szAbsoluteXbePath, szWorkingDirectory);

    // Spawn suspended so the low Xbox-RAM window (< 0x04000000) can be reserved
    // in the child before its loader/CRT fragments it. Contiguous ("physical")
    // guest memory is committed from that window, which keeps host==physical in
    // the low 28 address bits -- required by the NV2A DMA (nxdk pbkit and titles
    // that program the pushbuffer with masked-physical addresses).
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if(!CreateProcessA(szExePath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED,
                       NULL, szWorkingDirectory, &si, &pi))
    {
        printf("cxbx: failed to launch %s (error=%lu).\n", szExePath, GetLastError());
        return 1;
    }

    // Xbox RAM window for contiguous memory: 0x01000000..0x04000000 (48 MiB),
    // above the XBE image (loads at 0x00010000) and below the 64 MiB boundary.
    // Reserve as much of the free region at 0x01000000 as is contiguously free
    // (it typically runs to ~0x02000000). Guest contiguous memory is committed
    // from it, keeping those "physical" pointers below the 64 MiB Xbox RAM line.
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    void *pReserved = NULL;
    ULONG reserveSize = 0;
    if(VirtualQueryEx(pi.hProcess, (void*)(uintptr_t)0x01000000, &mbi, sizeof(mbi)) != 0 &&
       mbi.State == MEM_FREE && (uintptr_t)mbi.BaseAddress == 0x01000000)
    {
        reserveSize = (ULONG)mbi.RegionSize & ~0xFFFFul;   // 64 KiB granularity
        if(reserveSize > 0x03000000)
            reserveSize = 0x03000000;                      // cap at the 64 MiB line
        if(reserveSize >= 0x00100000)
            pReserved = VirtualAllocEx(pi.hProcess, (void*)(uintptr_t)0x01000000,
                                       reserveSize, MEM_RESERVE, PAGE_READWRITE);
    }
    printf("cxbx: child Xbox-RAM window 0x01000000 size 0x%lX (%s).\n",
           reserveSize, pReserved ? "ok" : "failed");

    // The generated exe is large-address-aware so the emulator can back parts
    // of the Xbox physical window with real memory (EmuInit's page-0 window).
    // LAA also makes the high half allocatable by the child's own heap, which
    // must never land inside a trap-emulated aperture -- the emu range checks
    // would misclassify such host pointers. Fence the trapped windows off with
    // PAGE_NOACCESS reservations before the child runs: reserved-untouchable
    // pages still fault on guest access, so trap-and-emulate is unchanged.
    // Best-effort per 16 MiB chunk; on a non-LAA image these fail closed.
    {
        static const struct { ULONG Base; ULONG Size; } kFences[] = {
            { 0x80000000, 0x10000000 },   // Xbox physical identity view
            { 0xF0000000, 0x04000000 },   // physical shadow aperture
            { 0xFD000000, 0x01000000 },   // NV2A MMIO
            { 0xFE800000, 0x00100000 },   // APU
            { 0xFEC00000, 0x00100000 },   // ACI/AC97
            { 0xFED00000, 0x00100000 },   // USB0 OHCI
        };
        for(unsigned f = 0; f < sizeof(kFences) / sizeof(kFences[0]); f++)
        {
            ULONG Done = 0;
            for(ULONG Off = 0; Off < kFences[f].Size; Off += 0x01000000)
            {
                ULONG Chunk = kFences[f].Size - Off;
                if(Chunk > 0x01000000)
                    Chunk = 0x01000000;
                if(VirtualAllocEx(pi.hProcess, (void*)(uintptr_t)(kFences[f].Base + Off),
                                  Chunk, MEM_RESERVE, PAGE_NOACCESS) != NULL)
                    Done += Chunk;
            }
            printf("cxbx: child trap fence 0x%08lX size 0x%lX reserved 0x%lX.\n",
                   kFences[f].Base, kFences[f].Size, Done);
        }
    }

    ResumeThread(pi.hThread);

    printf("cxbx: batch launched %s.\n", szExePath);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD dwExitCode = 0;
    GetExitCodeProcess(pi.hProcess, &dwExitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    printf("cxbx: batch process exited with code %lu.\n", dwExitCode);

    return (int)dwExitCode;
}

// ******************************************************************
// * func : WinMain
// ******************************************************************
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    bool bBatchRun = false;
    const char *szXbeArg = NULL;
    const char *szLogFile = NULL;
    char szDefaultLogFile[260] = {0};

    for(int v=1; v<__argc; v++)
    {
        if(strcmp(__argv[v], "--run") == 0)
        {
            bBatchRun = true;

            if(v + 1 < __argc)
                szXbeArg = __argv[++v];

            continue;
        }

        if(strcmp(__argv[v], "--log") == 0)
        {
            if(v + 1 < __argc)
                szLogFile = __argv[++v];

            continue;
        }

        if(__argv[v][0] != '-' && szXbeArg == NULL)
            szXbeArg = __argv[v];
    }

    if(bBatchRun && szLogFile == NULL)
    {
        GetModuleFileName(NULL, szDefaultLogFile, 260);

        sint32 spot=-1;
        for(int v=0; v<260; v++)
        {
            if(szDefaultLogFile[v] == '\\')
                spot = v;
            else if(szDefaultLogFile[v] == '\0')
                break;
        }

        if(spot != -1)
            strcpy(&szDefaultLogFile[spot + 1], "cxbx-run.log");
        else
            strcpy(szDefaultLogFile, "cxbx-run.log");

        szLogFile = szDefaultLogFile;
    }

    ConfigureLogFile(szLogFile);

    if(bBatchRun && szXbeArg == NULL)
    {
        printf("cxbx: --run requires an .xbe path.\n");
        return 2;
    }

    if(!EmuVerifyVersion(_CXBX_VERSION))
    {
        MessageBox(NULL, "cxbx.dll is the incorrect version", "cxbx", MB_OK);
        return 1;
    }

    EmuShared::Init();

    if(bBatchRun)
    {
        int ret = RunXbeBatch(szXbeArg, szLogFile);
        EmuShared::Cleanup();
        return ret;
    }

    WndMain *MainWindow = new WndMain(hInstance);

    while(!MainWindow->isCreated() && MainWindow->ProcessMessages())
        Sleep(10);

    if(szXbeArg != NULL && MainWindow->GetError() == 0)
    {
        MainWindow->OpenXbe(szXbeArg);

        MainWindow->StartEmulation(AUTO_CONVERT_WINDOWS_TEMP);
    }

    while(MainWindow->GetError() == 0 && MainWindow->ProcessMessages())
        Sleep(10);

    if(MainWindow->GetError() != 0)
        MessageBox(NULL, MainWindow->GetError(), "cxbx", MB_ICONSTOP | MB_OK);

    delete MainWindow;

    EmuShared::Cleanup();

    return 0;
}
