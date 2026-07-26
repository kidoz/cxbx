// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->xg_emulation.cpp
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

#include <vector>

#include "emulation_runtime.h"
#include "fs_emulation.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace NtDll
{
    #include "ntdll_emulation.h"
};

using VOID = void;

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
#define DIRECT3D_VERSION 0x0800
#include <d3d8.h>
#include "xg_emulation.h"
};

extern "C" bool EmuWritePhysicalMapBytesFromHle(ULONG Address, const BYTE *Data, ULONG Size);

static void EmuXGCopyToGuest(void *Destination, const void *Source, size_t Size)
{
    if(Size == 0)
    {
        return;
    }

    if(EmuWritePhysicalMapBytesFromHle((ULONG)(ULONG_PTR)Destination,
                                       (const BYTE *)Source, (ULONG)Size))
    {
        return;
    }

    memcpy(Destination, Source, Size);
}

static bool EmuXGTextureTraceEnabled()
{
    static int enabled = -1;
    if(enabled < 0)
    {
        char value[2] = {};
        enabled = GetEnvironmentVariableA(
                      "CXBX_TEX_TRACE", value, sizeof(value)) != 0
                      ? 1
                      : 0;
    }
    return enabled == 1;
}

// ******************************************************************
// * func: EmuXGIsSwizzledFormat
// ******************************************************************
PVOID WINAPI XTL::EmuXGIsSwizzledFormat
(
    XTL::D3DFORMAT Format
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuXapi (0x%X): EmuXGIsSwizzledFormat\n"
               "(\n"
               "   Format              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Format);
        EmuSwapFS();   // Xbox FS
    }
    #endif

    return FALSE;
}

// ******************************************************************
// * func: EmuXGSwizzleRect
// ******************************************************************
VOID WINAPI XTL::EmuXGSwizzleRect
(
    LPCVOID       pSource, 
    DWORD         Pitch,
    LPCRECT       pRect,
    LPVOID        pDest,
    DWORD         Width,
    DWORD         Height,
    CONST LPPOINT pPoint,
    DWORD         BytesPerPixel
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(EmuXGTextureTraceEnabled())
    {
        printf("TEX| xg-swizzle source=0x%.08lX pitch=%lu dest=0x%.08lX "
               "%lux%lu bpp=%lu rect=0x%.08lX point=0x%.08lX\n",
               reinterpret_cast<unsigned long>(pSource),
               static_cast<unsigned long>(Pitch),
               reinterpret_cast<unsigned long>(pDest),
               static_cast<unsigned long>(Width),
               static_cast<unsigned long>(Height),
               static_cast<unsigned long>(BytesPerPixel),
               reinterpret_cast<unsigned long>(pRect),
               reinterpret_cast<unsigned long>(pPoint));
        fflush(stdout);
    }

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuXapi (0x%X): EmuXGSwizzleRect\n"
               "(\n"
               "   pSource             : 0x%.08X\n"
               "   Pitch               : 0x%.08X\n"
               "   pRect               : 0x%.08X\n"
               "   pDest               : 0x%.08X\n"
               "   Width               : 0x%.08X\n"
               "   Height              : 0x%.08X\n"
               "   pPoint              : 0x%.08X\n"
               "   BytesPerPixel       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pSource, Pitch, pRect, pDest, Width, Height,
               pPoint, BytesPerPixel);
    }
    #endif

    const LONG sourceLeft = (pRect != NULL) ? pRect->left : 0;
    const LONG sourceTop = (pRect != NULL) ? pRect->top : 0;
    LONG copyWidth = (pRect != NULL) ? pRect->right - pRect->left
                                     : static_cast<LONG>(Width);
    LONG copyHeight = (pRect != NULL) ? pRect->bottom - pRect->top
                                      : static_cast<LONG>(Height);
    const LONG destinationLeft = (pPoint != NULL) ? pPoint->x : 0;
    const LONG destinationTop = (pPoint != NULL) ? pPoint->y : 0;

    if(pSource == nullptr || pDest == nullptr || sourceLeft < 0 || sourceTop < 0 ||
       destinationLeft < 0 || destinationTop < 0 || copyWidth <= 0 ||
       copyHeight <= 0 || destinationLeft >= static_cast<LONG>(Width) ||
       destinationTop >= static_cast<LONG>(Height) || BytesPerPixel == 0)
    {
        EmuSwapFS();   // Xbox FS
        return;
    }

    if(destinationLeft + copyWidth > static_cast<LONG>(Width))
    {
        copyWidth = static_cast<LONG>(Width) - destinationLeft;
    }
    if(destinationTop + copyHeight > static_cast<LONG>(Height))
    {
        copyHeight = static_cast<LONG>(Height) - destinationTop;
    }

    // Host D3D textures expose linear LockRect storage. XGSwizzleRect normally
    // produces Xbox Morton-order bytes, but the HLE hook must translate that
    // result into the host representation instead. Copy the selected source
    // rectangle into the packed linear destination and preserve the physical
    // map fallback used when the destination is guest-visible memory.
    const DWORD sourcePitch =
        Pitch != 0 ? Pitch : static_cast<DWORD>(copyWidth) * BytesPerPixel;
    const DWORD destinationPitch = Width * BytesPerPixel;
    const std::size_t rowBytes =
        static_cast<std::size_t>(copyWidth) * BytesPerPixel;
    for(LONG y = 0; y < copyHeight; ++y)
    {
        BYTE* destination =
            static_cast<BYTE*>(pDest) +
            static_cast<std::size_t>(destinationTop + y) * destinationPitch +
            static_cast<std::size_t>(destinationLeft) * BytesPerPixel;
        const BYTE* source =
            static_cast<const BYTE*>(pSource) +
            static_cast<std::size_t>(sourceTop + y) * sourcePitch +
            static_cast<std::size_t>(sourceLeft) * BytesPerPixel;
        EmuXGCopyToGuest(destination, source, rowBytes);
    }

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * func: EmuXGUnswizzleRect
// ******************************************************************
VOID WINAPI XTL::EmuXGUnswizzleRect
(
    PVOID           pSrcBuff,
    DWORD           dwWidth,
    DWORD           dwHeight,
    DWORD           dwDepth,
    PVOID           pDstBuff,
    DWORD           dwPitch,
    RECT            rSrc,
    POINT           poDst,
    DWORD           dwBPP
)
{
	DWORD dwOffsetU = 0, dwMaskU = 0;
    DWORD dwOffsetV = 0, dwMaskV = 0;
    DWORD dwOffsetW = 0, dwMaskW = 0;

	DWORD i = 1;
	DWORD j = 1;

	while( (i < dwWidth) || (i < dwHeight) || (i < dwDepth) )
    {
        if(i < dwWidth)
        {
			dwMaskU |= j;
			j<<=1;
		}

        if(i < dwHeight)
        {
			dwMaskV |= j;
			j<<=1;
		}

        if(i < dwDepth)
        {
			dwMaskW |= j;   
            j<<=1;  
        }

        i<<=1;
	}

    DWORD dwSU = 0;
	DWORD dwSV = 0;
	DWORD dwSW = 0;
	DWORD dwMaskMax=0;

	// get the biggest mask
	if(dwMaskU > dwMaskV)
		dwMaskMax=dwMaskU;
	else
		dwMaskMax=dwMaskV;
	if(dwMaskW > dwMaskMax)
		dwMaskMax=dwMaskW;

	for(i = 1; i <= dwMaskMax; i<<=1)
    {
		if(i<=dwMaskU)
        {
			if(dwMaskU & i) dwSU |= (dwOffsetU & i);
			else            dwOffsetU<<=1;
		}

        if(i<=dwMaskV)
        {
			if(dwMaskV & i) dwSV |= (dwOffsetV & i);
			else            dwOffsetV<<=1;
		}
		
        if(i<=dwMaskW)
        {
			if(dwMaskW & i) dwSW |= (dwOffsetW & i);
			else            dwOffsetW<<=1;
		}
	}

	DWORD dwW = dwSW;
	DWORD dwV = dwSV;
	DWORD dwU = dwSU;

	// A title can register a texture whose declared dimensions imply more
	// swizzled source bytes than its actual data buffer holds; the Morton walk
	// below then reads past the source and faults (Turok's Resource_Register
	// path, EmuXGUnswizzleRect source overrun). pSrcBuff is a host pointer above
	// the guest-physical window, so an SEH guard catches the overrun without
	// disturbing the MMIO/physical fault path -- abandon the copy and keep the
	// partially unswizzled texture rather than killing the process (one wrong
	// texture beats a dead title, matching Resource_Register's other guards).
	__try
	{
	for(DWORD z=0; z<dwDepth; z++)
	{
		dwV = dwSV;

		for(DWORD y=0; y<dwHeight; y++)
		{
			dwU = dwSU;

			for (DWORD x=0; x<dwWidth; x++)
			{
				memcpy(pDstBuff, &((BYTE*)pSrcBuff)[(dwU|dwV|dwW)*dwBPP], dwBPP);
				pDstBuff=(PVOID)(((DWORD)pDstBuff)+dwBPP);

				dwU = (dwU - dwMaskU) & dwMaskU;
			}
			pDstBuff=(PVOID)(((DWORD)pDstBuff)+(dwPitch-dwWidth*dwBPP));
			dwV = (dwV - dwMaskV) & dwMaskV;
		}
		dwW = (dwW - dwMaskW) & dwMaskW;
	}
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
	}
}
