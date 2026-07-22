// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->OOVPA.h
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
#ifndef OOVPA_H
#define OOVPA_H

#include "cxbx.h"

// ******************************************************************
// * Take THIS C++ !!
// ******************************************************************
template <class BaseClass, typename MFT> inline void *MFPtoFP( MFT pMemFunc)
{
    union
    {
        MFT pMemFunc;
        void (*pFunc)();
    }
    ThisConv;

    ThisConv.pMemFunc = pMemFunc;

    return ThisConv.pFunc;
}

#pragma pack(1)

// ******************************************************************
// * Optimized (Order,Value)-Pair Array
// ******************************************************************
struct OOVPA
{
    uint16 Large : 1;
    uint16 Count : 15;

    uint08 XRefSaveIndex;
    uint08 XRefCount;
};

// ******************************************************************
// * Large Optimized (Offset,Value)-Pair Array
// ******************************************************************
template <uint16 COUNT> struct LOOVPA
{
    uint16 Large : 1;
    uint16 Count : 15;

    uint08 XRefSaveIndex;
    uint08 XRefCount;

    // Large (Offset,Value)-Pair(s)
    struct LOVP
    {
        uint16 Offset;
        uint08 Value;
    }
    Lovp[COUNT];
};

// ******************************************************************
// * Small Optimized (Offset,Value)-Pair Array
// ******************************************************************
template <uint16 COUNT> struct SOOVPA
{
    uint16 Large : 1;
    uint16 Count : 15;

    uint08 XRefSaveIndex;
    uint08 XRefCount;

    // Small (Offset,Value)-Pair(s)
    struct SOVP
    {
        uint08 Offset;
        uint08 Value;
    }
    Sovp[COUNT];
};

// Patch EVERY match of the signature, not just the first. For families of
// byte-identical thin wrappers whose members all deserve the SAME wrapper
// (e.g. the 4627 CDirectSoundBuffer deferred-3D setters), one deliberately
// ambiguous signature + this flag replaces per-member XRef chains that
// cannot discriminate twins anyway. Place such entries AFTER any specific
// entries for family members: an already-patched prologue starts with the
// E9 jmp, so it no longer matches and is skipped naturally.
#define OOVPA_FLAG_PATCH_ALL 0x1

// ******************************************************************
// * OOVPATable
// ******************************************************************
struct OOVPATable
{
    OOVPA *Oovpa;

    void  *lpRedirect;

    #ifdef _DEBUG_TRACE
    char  *szFuncName;
    #endif

    // OOVPA_FLAG_* bits; existing aggregate initializers leave this 0.
    uint32 Flags;
};

#pragma pack()

#endif
