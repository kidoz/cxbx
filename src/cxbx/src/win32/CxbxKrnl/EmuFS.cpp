// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->Win32->cxbxkrnl->EmuFS.cpp
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

#include "Emu.h"
#include "EmuFS.h"
#include "EmuLDT.h"

#undef FIELD_OFFSET     // prevent macro redefinition warnings
#include <windows.h>
#include <cstdio>

// ******************************************************************
// * data: EmuAutoSleepRate
// ******************************************************************
uint32 EmuAutoSleepRate = -1;
bool g_bEmuFSUnavailable = false;

static thread_local xboxkrnl::ETHREAD *g_pEmuCurrentThread = NULL;
static thread_local uint08 *g_pEmuCurrentTLS = NULL;
static thread_local uint08 *g_pEmuCurrentTLSAllocation = NULL;
static thread_local PVOID g_pEmuHostStackBase = NULL;

static void EmuSetCurrentThread(uint08 *pTLSData, uint08 *pTLSAllocation)
{
    if(g_pEmuCurrentThread != NULL)
        delete g_pEmuCurrentThread;

    g_pEmuCurrentThread = new xboxkrnl::ETHREAD();
    memset(g_pEmuCurrentThread, 0, sizeof(*g_pEmuCurrentThread));

    g_pEmuCurrentThread->Tcb.TlsData = pTLSData;
    g_pEmuCurrentThread->UniqueThread = GetCurrentThreadId();
    g_pEmuCurrentTLS = pTLSData;
    g_pEmuCurrentTLSAllocation = pTLSAllocation;
}

void *EmuGetCurrentThread()
{
    if(g_pEmuCurrentThread == NULL)
        EmuSetCurrentThread(NULL, NULL);

    return &g_pEmuCurrentThread->Tcb;
}

void EmuAdjustCurrentThreadKernelApcDisable(long Delta)
{
    long *KernelApcDisable = (long*)((uint08*)EmuGetCurrentThread() + 0x68);
    *KernelApcDisable += Delta;
}

// ******************************************************************
// * func: EmuInitFS
// ******************************************************************
void EmuInitFS()
{
    EmuInitLDT();
}

// ******************************************************************
// * func: EmuGenerateFS
// ******************************************************************
void EmuGenerateFS(Xbe::TLS *pTLS, void *pTLSData)
{
    NT_TIB         *OrgNtTib;
    xboxkrnl::KPCR *NewPcr;

    uint08 *pNewTLS = NULL;
    uint08 *pNewTLSAllocation = NULL;

    uint16 NewFS = -1, OrgFS = -1;

    // ******************************************************************
    // * Copy Global TLS to Local
    // ******************************************************************
    {
        uint32 dwCopySize = 0;
        uint32 dwZeroSize = 0;

        if(pTLS != NULL)
        {
            if(pTLS->dwDataEndAddr < pTLS->dwDataStartAddr)
                EmuCleanup("Invalid TLS range: start=0x%.08X end=0x%.08X", pTLS->dwDataStartAddr, pTLS->dwDataEndAddr);

            dwCopySize = pTLS->dwDataEndAddr - pTLS->dwDataStartAddr;
            dwZeroSize = pTLS->dwSizeofZeroFill;

            if(dwCopySize != 0 && pTLSData == NULL)
                EmuCleanup("TLS data is missing for non-empty TLS range");
        }

        printf("EmuFS (0x%X): GenerateFS pTLS=0x%.08X pTLSData=0x%.08X copy=0x%.08X zero=0x%.08X\n",
               (uint32)GetCurrentThreadId(), (uint32)pTLS, (uint32)pTLSData, dwCopySize, dwZeroSize);

        if(dwCopySize + dwZeroSize != 0)
        {
            uint32 dwTLSSize = dwCopySize + dwZeroSize;
            pNewTLSAllocation = new uint08[dwTLSSize + 0x10];
            pNewTLS = (uint08*)((((uint32)pNewTLSAllocation + 4 + 0x0F) & ~0x0F) - 4);

            if(dwCopySize != 0)
                memcpy(pNewTLS, pTLSData, dwCopySize);

            ZeroMemory(pNewTLS + dwCopySize, dwZeroSize);

            *(void**)pNewTLS = pNewTLS;
        }
    }

    if(pTLS != NULL && pTLS->dwTLSIndexAddr != 0)
        *(uint32*)pTLS->dwTLSIndexAddr = 0;

    // ******************************************************************
    // * Dump Raw TLS data
    // ******************************************************************
    {
        #ifdef _DEBUG_TRACE
		if(pNewTLS == 0)
		{
			printf("EmuFS (0x%X): TLS Non-Existant (OK)\n", GetCurrentThreadId());
		}
		else
		{
			printf("EmuFS (0x%X): TLS Data Dump... \n  0x%.08X: ", GetCurrentThreadId(), pNewTLS);

			uint32 stop = pTLS->dwDataEndAddr - pTLS->dwDataStartAddr + pTLS->dwSizeofZeroFill;

			for(uint32 v=0;v<stop;v++)
			{
				uint08 *bByte = (uint08*)pNewTLS + v;

				printf("%.01X", (uint32)*bByte);

				if((v+1) % 0x10 == 0)
					printf("\n  0x%.08X: ", ((uint32)pNewTLS + v));
			}

			printf("\n");
		}
        #endif
    }

    // ******************************************************************
    // * Obtain "OrgFS"
    // ******************************************************************
    __asm
    {
        // Obtain "OrgFS"
        mov ax, fs
        mov OrgFS, ax

        // Obtain "OrgNtTib"
        mov eax, fs:[0x18]
        mov OrgNtTib, eax
    }

    // ******************************************************************
    // * Allocate LDT entry
    // ******************************************************************
    {
        uint32 dwSize = sizeof(xboxkrnl::KPCR);

        NewPcr = (xboxkrnl::KPCR*)new char[dwSize];

        memset(NewPcr, 0, sizeof(*NewPcr));

        NewFS = EmuAllocateLDT((uint32)NewPcr, (uint32)NewPcr + dwSize);

        if(NewFS == 0)
        {
            printf("EmuFS (0x%X): LDT unavailable; running without Xbox FS selector.\n", (uint32)GetCurrentThreadId());
            delete[] (char*)NewPcr;

            if(g_pEmuHostStackBase == NULL)
                g_pEmuHostStackBase = OrgNtTib->StackBase;

            OrgNtTib->StackBase = pNewTLS;
            EmuSetCurrentThread(pNewTLS, pNewTLSAllocation);
            return;
        }
    }

    // ******************************************************************
    // * Update "OrgFS" with NewFS and (bIsBoxFs = false)
    // ******************************************************************
    {
        __asm
        {
            mov ax, NewFS
            mov bh, 0

            mov fs:[0x14], ax
            mov fs:[0x16], bh
        }
    }

    // ******************************************************************
    // * Generate TIB
    // ******************************************************************
    {
        xboxkrnl::ETHREAD *EThread = new xboxkrnl::ETHREAD();

        EThread->Tcb.TlsData  = (void*)pNewTLS;
        EThread->UniqueThread = GetCurrentThreadId();
        g_pEmuCurrentThread = EThread;
        g_pEmuCurrentTLS = pNewTLS;
        g_pEmuCurrentTLSAllocation = pNewTLSAllocation;

        memcpy(&NewPcr->NtTib, OrgNtTib, sizeof(NT_TIB));

        NewPcr->NtTib.Self = &NewPcr->NtTib;

        NewPcr->PrcbData.CurrentThread = (xboxkrnl::KTHREAD*)EThread;

        NewPcr->Prcb = &NewPcr->PrcbData;
    }

    // ******************************************************************
    // * Swap into the "NewFS"
    // ******************************************************************
    EmuSwapFS();

    // ******************************************************************
    // * Update "NewFS" with OrgFS and (bIsBoxFs = true)
    // ******************************************************************
    {
        __asm
        {
            mov ax, OrgFS
            mov bh, 1

            mov fs:[0x14], ax
            mov fs:[0x16], bh
        }
    }

    // ******************************************************************
    // * Save "TLSPtr" inside NewFS.StackBase
    // ******************************************************************
    {
        __asm
        {
            mov eax, pNewTLS
            mov fs:[0x04], eax
        }
    }

    // ******************************************************************
    // * Swap back into the "OrgFS"
    // ******************************************************************
    EmuSwapFS();

    // ******************************************************************
    // * Debug output
    // ******************************************************************
	#ifdef _DEBUG_TRACE
    printf("EmuFS (0x%X): OrgFS=%d NewFS=%d pTLS=0x%.08X\n", GetCurrentThreadId(), OrgFS, NewFS, pTLS);
	#endif
}

// ******************************************************************
// * func: EmuCleanupFS
// ******************************************************************
void EmuCleanupFS()
{
    if(g_bEmuFSUnavailable)
    {
        NT_TIB *OrgNtTib;

        __asm
        {
            mov eax, fs:[0x18]
            mov OrgNtTib, eax
        }

        if(g_pEmuHostStackBase != NULL)
        {
            OrgNtTib->StackBase = g_pEmuHostStackBase;
            g_pEmuHostStackBase = NULL;
        }

        delete[] g_pEmuCurrentTLSAllocation;
        g_pEmuCurrentTLSAllocation = NULL;
        g_pEmuCurrentTLS = NULL;

        delete g_pEmuCurrentThread;
        g_pEmuCurrentThread = NULL;
        return;
    }

    uint16 wSwapFS = 0;

    __asm
    {
        mov ax, fs:[0x14]   // FS.ArbitraryUserPointer
        mov wSwapFS, ax
    }

    if(wSwapFS == 0)
        return;

    if(!EmuIsXboxFS())
        EmuSwapFS();    // Xbox FS

    uint08 *pTLSData = NULL;

    __asm
    {
        mov eax, fs:[0x04]
        mov pTLSData, eax
    }

    EmuSwapFS(); // Win2k/XP FS

    if(pTLSData != 0)
        delete[] g_pEmuCurrentTLSAllocation;

    EmuDeallocateLDT(wSwapFS);

    delete g_pEmuCurrentThread;
    g_pEmuCurrentThread = NULL;
    g_pEmuCurrentTLS = NULL;
    g_pEmuCurrentTLSAllocation = NULL;
}
