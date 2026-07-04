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
// *  along with this program; see the file LICENSE.md.
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
    printf("cxbx: batch opening %s.\n", szXbePath);

    Xbe i_Xbe(szXbePath);
    if(i_Xbe.GetError() != 0)
    {
        printf("cxbx: %s\n", i_Xbe.GetError());
        return 1;
    }

    char szExePath[260];
    BuildTempExePath(szXbePath, szExePath);
    printf("cxbx: batch converting to %s.\n", szExePath);

    char szDebugFilename[260] = {0};
    DebugMode DebugMode = DM_NONE;

    if(szLogFile != NULL && szLogFile[0] != '\0')
    {
        strncpy(szDebugFilename, szLogFile, sizeof(szDebugFilename) - 1);
        szDebugFilename[sizeof(szDebugFilename) - 1] = '\0';
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
    BuildXbeDirectory(szXbePath, szWorkingDirectory);

    SHELLEXECUTEINFO sei;
    memset(&sei, 0, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = "open";
    sei.lpFile = szExePath;
    sei.lpDirectory = szWorkingDirectory;
    sei.nShow = SW_SHOWDEFAULT;

    if(!ShellExecuteEx(&sei))
    {
        printf("cxbx: failed to launch %s (error=%lu).\n", szExePath, GetLastError());
        return 1;
    }

    printf("cxbx: batch launched %s.\n", szExePath);

    WaitForSingleObject(sei.hProcess, INFINITE);

    DWORD dwExitCode = 0;
    GetExitCodeProcess(sei.hProcess, &dwExitCode);
    CloseHandle(sei.hProcess);

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
