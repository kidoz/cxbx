// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->EmuFS.h
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
#ifndef EMUFS_H
#define EMUFS_H

#include "core/Xbe.h"

// word @ FS:[0x14] := wSwapFS
// byte @ FS:[0x16] := bIsXboxFS

#undef FIELD_OFFSET     // prevent macro redefinition warnings
#include <windows.h>

// ******************************************************************
// * func: EmuGenerateFS
// ******************************************************************
extern void EmuGenerateFS(Xbe::TLS *pTLS, void *pTLSData);

// ******************************************************************
// * func: EmuCleanupFS
// ******************************************************************
extern void EmuCleanupFS();

// ******************************************************************
// * func: EmuInitFS
// ******************************************************************
extern void EmuInitFS();

// ******************************************************************
// * data: g_bEmuFSUnavailable
// ******************************************************************
extern bool g_bEmuFSUnavailable;

// ******************************************************************
// * LDT-less FS content-swap (opt-in via CXBX_FS_SWAP)
// ******************************************************************
// * On 64-bit Windows NtSetLdtEntries is unavailable, so there is no
// * separate Xbox FS selector and EmuSwapFS cannot swap selectors. The
// * default fallback shares one selector (the host TEB) with the Xbox KPCR
// * fields overlaid permanently at fs:[0x1C/0x20/0x24/0x28] -- which corrupts
// * those slots for host code (the CRT / ntdll / Cxbx.dll read the host TEB's
// * EnvironmentPointer/PID/TID there). When enabled, EmuSwapFS instead SAVES
// * the current 4 slots and LOADS the other role's values on each swap, so
// * host code sees the host TEB and guest code sees the Xbox KPCR -- emulating
// * the selector swap with the one shared selector.
struct EmuFsSwapState
{
    bool          Active;    // this thread installed the swap (via EmuGenerateFS)
    bool          IsXbox;    // true: slots hold the Xbox KPCR; false: the host TEB
    unsigned long Xbox[5];   // SelfPcr(0x1C), Prcb(0x20), Irql/ClientId(0x24), CurrentThread(0x28), TlsPtr(0x04)
    unsigned long Host[5];   // original host TEB values at those offsets (0x04 = the real StackBase,
                             // which host SEH dispatch validates handler frames against)
};
extern bool g_bEmuFSContentSwap;
extern thread_local EmuFsSwapState g_EmuFsSwap;

// Returns the role the shared slots ACTUALLY hold right now, decided by content:
// fs:[0x1C] is SelfPcr (a unique heap pointer) in the Xbox role and the TEB's
// EnvironmentPointer in the host role, and neither side ever rewrites it. The
// IsXbox flag alone can go stale -- an SEH unwind (e.g. a guest __except catching
// a fault raised in host code) skips the balancing EmuSwapFS calls -- so every
// swap decides by content and self-heals the flag.
static inline bool EmuFsSwapActualIsXbox()
{
    unsigned long v;
    __asm
    {
        mov eax, fs:[0x1C]
        mov v, eax
    }
    return v == g_EmuFsSwap.Xbox[0];
}

// Exchange the 4 overlaid KPCR/TEB slots: save the current contents into the
// array of the role they actually belong to (picking up any runtime drift, e.g.
// the guest writing the Irql byte at fs:[0x24]), then load the other role.
static inline void EmuFsSwapExchange()
{
    bool Actual = EmuFsSwapActualIsXbox();
    unsigned long s0, s1, s2, s3, s4;
    __asm
    {
        mov eax, fs:[0x1C]
        mov s0, eax
        mov eax, fs:[0x20]
        mov s1, eax
        mov eax, fs:[0x24]
        mov s2, eax
        mov eax, fs:[0x28]
        mov s3, eax
        mov eax, fs:[0x04]
        mov s4, eax
    }
    unsigned long *save = Actual ? g_EmuFsSwap.Xbox : g_EmuFsSwap.Host;
    unsigned long *load = Actual ? g_EmuFsSwap.Host : g_EmuFsSwap.Xbox;
    save[0] = s0; save[1] = s1; save[2] = s2; save[3] = s3; save[4] = s4;
    s0 = load[0]; s1 = load[1]; s2 = load[2]; s3 = load[3]; s4 = load[4];
    __asm
    {
        mov eax, s0
        mov fs:[0x1C], eax
        mov eax, s1
        mov fs:[0x20], eax
        mov eax, s2
        mov fs:[0x24], eax
        mov eax, s3
        mov fs:[0x28], eax
        mov eax, s4
        mov fs:[0x04], eax
    }
    g_EmuFsSwap.IsXbox = !Actual;
}

// Force the slots into a specific role (no-op when they already hold it). Used
// by the exception paths to re-anchor the role to the code that was actually
// interrupted (the faulting EIP says which side was running), repairing any
// inversion an unbalanced SEH unwind left behind.
static inline void EmuFsSwapEnsureRole(bool Xbox)
{
    if(!g_bEmuFSContentSwap || !g_EmuFsSwap.Active)
        return;

    if(EmuFsSwapActualIsXbox() != Xbox)
        EmuFsSwapExchange();
    else
        g_EmuFsSwap.IsXbox = Xbox;
}

// ******************************************************************
// * func: EmuGetCurrentThread
// ******************************************************************
extern void *EmuGetCurrentThread();

// ******************************************************************
// * func: EmuAdjustCurrentThreadKernelApcDisable
// ******************************************************************
extern void EmuAdjustCurrentThreadKernelApcDisable(long Delta);

// ******************************************************************
// * func: EmuIsXboxFS
// ******************************************************************
// *
// * This function will return true if the current FS register is
// * the Xbox emulation variety. Alternatively, false means the
// * Win2k/XP FS register is currently loaded.
// *
// ******************************************************************
static inline bool EmuIsXboxFS()
{
    if(g_bEmuFSUnavailable)
        return (g_bEmuFSContentSwap && g_EmuFsSwap.Active) ? EmuFsSwapActualIsXbox() : false;

    unsigned char chk;

    __asm
    {
        mov ah, fs:[0x16]
        mov chk, ah
    }

    return (chk == 1);
}

// ******************************************************************
// * data: EmuAutoSleepRate
// ******************************************************************
// *
// * Xbox is a single process system, and because of this fact, demos
// * and games are likely to suffer from Xbox-Never-Sleeps syndrome.
// *
// * Basically, there are situations where the Xbe will have no
// * reason to bother yielding to other threads. One solution to this
// * problem is to keep track of the number of function intercepts,
// * and every so often, force a sleep. This is the rate at which
// * those forced sleeps occur.
// *
// ******************************************************************
extern uint32 EmuAutoSleepRate;

// ******************************************************************
// * func: EmuSwapFS
// ******************************************************************
// *
// * This function is used to swap between the native Win2k/XP FS:
// * structure, and the Emu FS: structure. Before running Windows
// * code, you *must* swap over to Win2k/XP FS. Similarly, before
// * running Xbox code, you *must* swap back over to Emu FS.
// *
// ******************************************************************
static inline void EmuSwapFS()
{
    if(g_bEmuFSUnavailable)
    {
        // LDT-less content-swap: exchange the 4 overlaid KPCR/TEB slots so the
        // side about to run (host after this call, or guest) sees its own values.
        if(g_bEmuFSContentSwap && g_EmuFsSwap.Active)
            EmuFsSwapExchange();
        return;
    }

    // Note that this is only the *approximate* interception count,
    // because not all interceptions swap the FS register, and some
    // non-interception code uses it
    static uint32 dwInterceptionCount = 0;

    __asm
    {
        mov ax, fs:[0x14]
        mov fs, ax
    }

    // ******************************************************************
    // * Every "N" interceptions, perform various periodic services
    // ******************************************************************
    if(dwInterceptionCount++ >= EmuAutoSleepRate)
    {
        // If we're in the Xbox FS, wait until the next swap
        if(EmuIsXboxFS())
        {
            dwInterceptionCount--;
            return;
        }

        // Yield!
        Sleep(1);

        // Back to Zero!
        dwInterceptionCount = 0;
    }
}

#endif
