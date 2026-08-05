// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->platform->win32->shared_runtime_storage.cpp
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

#include "emulation_runtime.h"
#include "shared_runtime_storage.h"

#undef FIELD_OFFSET // prevent macro redefinition warnings
#include <windows.h>
#include <cstdio>

// ******************************************************************
// * exported globals
// ******************************************************************
CXBXKRNL_API EmuShared* g_EmuShared = NULL;
CXBXKRNL_API int g_EmuSharedRefCount = 0;

// ******************************************************************
// * static/global
// ******************************************************************
HANDLE hMapObject = NULL;

// The guest process opts out of persisting configuration at exit (see
// EmuShared::DisablePersist below).
static bool g_EmuSharedPersistDisabled = false;

// ******************************************************************
// * func: EmuShared::EmuSharedInit
// ******************************************************************
CXBXKRNL_API void EmuShared::Init()
{
    // ******************************************************************
    // * Ensure initialization only occurs once
    // ******************************************************************
    bool init = true;

    // ******************************************************************
    // * Prevent multiple initializations
    // ******************************************************************
    if(hMapObject != NULL)
        return;

    // ******************************************************************
    // * Create the shared memory "file"
    // ******************************************************************
    {
        hMapObject = CreateFileMapping(
            INVALID_HANDLE_VALUE, // Paging file
            NULL,                 // default security attributes
            PAGE_READWRITE,       // read/write access
            0,                    // size: high 32 bits
            sizeof(EmuShared),    // size: low 32 bits
            "Local\\EmuShared"    // name of map object
        );

        if(hMapObject == NULL)
            EmuCleanup("Could not map shared memory!");

        if(GetLastError() == ERROR_ALREADY_EXISTS)
            init = false;
    }

    // ******************************************************************
    // * Memory map this file
    // ******************************************************************
    {
        // Prefer a high base: the default view lands in the lowest free 64 KiB
        // region (observed at 0x000E0000), squarely inside guest address space
        // that a self-relocating title (EvolutionX) legitimately re-allocates
        // over -- and a mapped view can be neither released nor made writable
        // for the guest by the XBE-image alias path. Each candidate is only a
        // preference; fall back to letting the OS choose.
        const void* PreferredBases[] = {
            (void*)0x7F000000,
            (void*)0x7E000000,
            (void*)0x7D000000,
            NULL,
        };
        for(unsigned i = 0; i < sizeof(PreferredBases) / sizeof(PreferredBases[0]); i++)
        {
            g_EmuShared = (EmuShared*)MapViewOfFileEx(
                hMapObject,     // object to map view of
                FILE_MAP_WRITE, // read/write access
                0,              // high offset:  map from
                0,              // low offset:   beginning
                0,              // default: map entire file
                (LPVOID)PreferredBases[i]);
            if(g_EmuShared != NULL)
                break;
        }

        if(g_EmuShared == NULL)
            EmuCleanup("Could not map view of shared memory!");
    }

    // ******************************************************************
    // * Executed only on first initialization of shared memory
    // ******************************************************************
    if(init)
        g_EmuShared->EmuShared::EmuShared();

    g_EmuSharedRefCount++;
}

// ******************************************************************
// * func: EmuShared::DisablePersist
// ******************************************************************
CXBXKRNL_API void EmuShared::DisablePersist()
{
    g_EmuSharedPersistDisabled = true;
}

// ******************************************************************
// * func: EmuSharedCleanup
// ******************************************************************
CXBXKRNL_API void EmuShared::Cleanup()
{
    g_EmuSharedRefCount--;

    // The refcount is per-process, so the destructor -- which persists the
    // controller/video configuration to the registry via XBController::Save --
    // used to run in EVERY process, including the guest. The guest never edits
    // configuration, and by exit time arbitrary title code has been running in
    // this address space for minutes: XBController's lookup tables live in
    // plain Cxbx.dll .data a runaway guest write can stomp. When that
    // happened, Save's sprintf("%s") faulted on the stomped pointer, the fault
    // re-entered Save through exception dispatch until the stack was
    // exhausted, and the process died with 0xC0000005 as its exit code --
    // masking the title's real exit status on every voluntary exit (observed
    // with NFS Underground). Persisting is the launcher/GUI's job; the guest
    // opts out via DisablePersist at EmuInit.
    if(g_EmuSharedRefCount == 0 && !g_EmuSharedPersistDisabled)
        g_EmuShared->EmuShared::~EmuShared();

    UnmapViewOfFile(g_EmuShared);
}

// ******************************************************************
// * Constructor
// ******************************************************************
CXBXKRNL_API EmuShared::EmuShared()
{
    m_XBController.Load("Software\\cxbx\\XBController");
    m_XBVideo.Load("Software\\cxbx\\XBVideo");
}

// ******************************************************************
// * Deconstructor
// ******************************************************************
CXBXKRNL_API EmuShared::~EmuShared()
{
    m_XBController.Save("Software\\cxbx\\XBController");
    m_XBVideo.Save("Software\\cxbx\\XBVideo");
}
