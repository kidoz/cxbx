// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbx->win_main.cpp
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
#include "emulation_runtime.h"
#include "shared_runtime_state.h"
#include "../launcher_session.h"
#include "../nodalkit/launcher_ui.h"
#include <cstdio>
#include <cstring>

// ******************************************************************
// * func : WinMain
// ******************************************************************
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    bool bBatchRun = false;
    const char* szXbeArg = NULL;
    const char* szLogFile = NULL;
    char szDefaultLogFile[260] = { 0 };

    for(int v = 1; v < __argc; v++)
    {
        if(strcmp(__argv[v], "--run") == 0)
        {
            bBatchRun = true;

            if(v + 1 < __argc)
            {
                szXbeArg = __argv[++v];
            }

            continue;
        }

        if(strcmp(__argv[v], "--log") == 0)
        {
            if(v + 1 < __argc)
            {
                szLogFile = __argv[++v];
            }

            continue;
        }

        if(__argv[v][0] != '-' && szXbeArg == NULL)
        {
            szXbeArg = __argv[v];
        }
    }

    if(bBatchRun && szLogFile == NULL)
    {
        GetModuleFileName(NULL, szDefaultLogFile, 260);

        sint32 spot = -1;
        for(int v = 0; v < 260; v++)
        {
            if(szDefaultLogFile[v] == '\\')
            {
                spot = v;
            }
            else if(szDefaultLogFile[v] == '\0')
            {
                break;
            }
        }

        if(spot != -1)
        {
            strcpy(&szDefaultLogFile[spot + 1], "cxbx-run.log");
        }
        else
        {
            strcpy(szDefaultLogFile, "cxbx-run.log");
        }

        szLogFile = szDefaultLogFile;
    }

    cxbx::frontend::configure_log_file(szLogFile);

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

    cxbx::platform::InitializeSharedRuntime();

    if(bBatchRun)
    {
        int ret = cxbx::frontend::run_xbe_batch(szXbeArg, szLogFile);
        cxbx::platform::ShutdownSharedRuntime();
        return ret;
    }

    bool smoke_test = false;
    for(int v = 1; v < __argc; ++v)
    {
        smoke_test = smoke_test || strcmp(__argv[v], "--ui-smoke-test") == 0;
    }
    if(smoke_test)
    {
        cxbx::platform::DisableSharedRuntimePersist();
    }
    int result = cxbx::frontend::run_nodalkit(szXbeArg, szLogFile, smoke_test);

    cxbx::platform::ShutdownSharedRuntime();

    return result;
}
