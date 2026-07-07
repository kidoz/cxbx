// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->Emu.cpp
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
#include "EmuNV2ALogging.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
    #include "EmuXTL.h"
};

#include <clocale>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "EmuShared.h"
#include "HLEDataBase.h"

// ******************************************************************
// * global / static
// ******************************************************************
Xbe::TLS    *g_pTLS       = NULL;
void        *g_pTLSData   = NULL;
Xbe::Header *g_pXbeHeader = NULL;
HANDLE		 g_hCurDir    = NULL;
HANDLE       g_hTDrive    = NULL;
HANDLE       g_hUDrive    = NULL;
HANDLE       g_hZDrive    = NULL;

// ******************************************************************
// * static
// ******************************************************************
static void *EmuLocateFunction(OOVPA *Oovpa, uint32 lower, uint32 upper);
static void  EmuInstallWrappers(OOVPATable *OovpaTable, uint32 OovpaTableSize, void (*Entry)(), Xbe::Header *pXbeHeader);
static void  EmuInstallNestopiaX13Bootstrap(Xbe::Header *pXbeHeader);
static void  EmuInstallFceultraBootstrap(Xbe::Header *pXbeHeader);
static bool  EmuBytesMatch(uint32 Address, const uint08 *Bytes, uint32 Count, Xbe::Header *pXbeHeader);
static void  EmuWriteBytes(uint32 Address, const uint08 *Bytes, uint32 Count);
static void  EmuInstallAutoBootLaunchData();
static void  EmuInstallFakeKernelImage();
static void  EmuXRefFailure();
static int   ExitException(LPEXCEPTION_POINTERS e);
static void  EmuConfigureLogFile();

static bool EmuGetLogFile(char *szLogFile, DWORD dwLogFileSize)
{
    DWORD dwLogFile = GetEnvironmentVariable("CXBX_LOG_FILE", szLogFile, dwLogFileSize);

    return dwLogFile != 0 && dwLogFile < dwLogFileSize;
}

static void EmuRedirectStdStream(DWORD StdHandle, int FileDescriptor, FILE *Stream, const char *Path, DWORD CreationDisposition)
{
    HANDLE hFile = CreateFile(Path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);

    if(hFile == INVALID_HANDLE_VALUE)
        return;

    if(CreationDisposition == OPEN_ALWAYS)
        SetFilePointer(hFile, 0, NULL, FILE_END);

    int NewDescriptor = _open_osfhandle((intptr_t)hFile, _O_TEXT);

    if(NewDescriptor < 0)
    {
        CloseHandle(hFile);
        return;
    }

    if(_dup2(NewDescriptor, FileDescriptor) == 0)
    {
        SetStdHandle(StdHandle, (HANDLE)_get_osfhandle(FileDescriptor));
        setvbuf(Stream, NULL, _IONBF, 0);
    }

    if(NewDescriptor != FileDescriptor)
        _close(NewDescriptor);
}

static void EmuConfigureLogFile()
{
    char szLogFile[260];

    if(EmuGetLogFile(szLogFile, sizeof(szLogFile)))
    {
        EmuRedirectStdStream(STD_OUTPUT_HANDLE, 1, stdout, szLogFile, OPEN_ALWAYS);
        EmuRedirectStdStream(STD_ERROR_HANDLE, 2, stderr, szLogFile, OPEN_ALWAYS);
        return;
    }

    EmuRedirectStdStream(STD_OUTPUT_HANDLE, 1, stdout, "NUL", OPEN_EXISTING);
    EmuRedirectStdStream(STD_ERROR_HANDLE, 2, stderr, "NUL", OPEN_EXISTING);
}

static bool EmuTryEmulateRdmsr(LPEXCEPTION_POINTERS e)
{
    if(e->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
        return false;

    __try
    {
        BYTE *Instruction = (BYTE*)e->ContextRecord->Eip;

        if(Instruction[0] == 0x0F && Instruction[1] == 0x32 && e->ContextRecord->Ecx == 0x2A)
        {
            e->ContextRecord->Eax = 0;
            e->ContextRecord->Edx = 0;
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated RDMSR 0x0000002A.\n", GetCurrentThreadId());
            fflush(stdout);

            return true;
        }

        if(Instruction[0] == 0x0F && Instruction[1] == 0x09)
        {
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated WBINVD.\n", GetCurrentThreadId());
            fflush(stdout);

            return true;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return false;
}

static bool EmuReadPhysicalMap(ULONG Address, ULONG Size, ULONG *Value);

static ULONG EmuContextRegister(const CONTEXT *ContextRecord, ULONG RegisterIndex)
{
    switch(RegisterIndex & 0x07)
    {
        case 0: return ContextRecord->Eax;
        case 1: return ContextRecord->Ecx;
        case 2: return ContextRecord->Edx;
        case 3: return ContextRecord->Ebx;
        case 4: return ContextRecord->Esp;
        case 5: return ContextRecord->Ebp;
        case 6: return ContextRecord->Esi;
        case 7: return ContextRecord->Edi;
    }

    return 0;
}

static void EmuSetContextRegister(CONTEXT *ContextRecord, ULONG RegisterIndex, ULONG Value)
{
    switch(RegisterIndex & 0x07)
    {
        case 0: ContextRecord->Eax = Value; break;
        case 1: ContextRecord->Ecx = Value; break;
        case 2: ContextRecord->Edx = Value; break;
        case 3: ContextRecord->Ebx = Value; break;
        case 4: ContextRecord->Esp = Value; break;
        case 5: ContextRecord->Ebp = Value; break;
        case 6: ContextRecord->Esi = Value; break;
        case 7: ContextRecord->Edi = Value; break;
    }
}

static WORD EmuContextWordRegister(const CONTEXT *ContextRecord, ULONG RegisterIndex)
{
    return (WORD)EmuContextRegister(ContextRecord, RegisterIndex);
}

static void EmuSetContextWordRegister(CONTEXT *ContextRecord, ULONG RegisterIndex, WORD Value)
{
    ULONG OldValue = EmuContextRegister(ContextRecord, RegisterIndex);
    EmuSetContextRegister(ContextRecord, RegisterIndex, (OldValue & 0xFFFF0000) | Value);
}

static BYTE EmuContextByteRegister(const CONTEXT *ContextRecord, ULONG RegisterIndex)
{
    switch(RegisterIndex & 0x07)
    {
        case 0: return (BYTE)ContextRecord->Eax;
        case 1: return (BYTE)ContextRecord->Ecx;
        case 2: return (BYTE)ContextRecord->Edx;
        case 3: return (BYTE)ContextRecord->Ebx;
        case 4: return (BYTE)(ContextRecord->Eax >> 8);
        case 5: return (BYTE)(ContextRecord->Ecx >> 8);
        case 6: return (BYTE)(ContextRecord->Edx >> 8);
        case 7: return (BYTE)(ContextRecord->Ebx >> 8);
    }

    return 0;
}

static void EmuSetContextByteRegister(CONTEXT *ContextRecord, ULONG RegisterIndex, BYTE Value)
{
    switch(RegisterIndex & 0x07)
    {
        case 0: ContextRecord->Eax = (ContextRecord->Eax & 0xFFFFFF00) | Value; break;
        case 1: ContextRecord->Ecx = (ContextRecord->Ecx & 0xFFFFFF00) | Value; break;
        case 2: ContextRecord->Edx = (ContextRecord->Edx & 0xFFFFFF00) | Value; break;
        case 3: ContextRecord->Ebx = (ContextRecord->Ebx & 0xFFFFFF00) | Value; break;
        case 4: ContextRecord->Eax = (ContextRecord->Eax & 0xFFFF00FF) | ((ULONG)Value << 8); break;
        case 5: ContextRecord->Ecx = (ContextRecord->Ecx & 0xFFFF00FF) | ((ULONG)Value << 8); break;
        case 6: ContextRecord->Edx = (ContextRecord->Edx & 0xFFFF00FF) | ((ULONG)Value << 8); break;
        case 7: ContextRecord->Ebx = (ContextRecord->Ebx & 0xFFFF00FF) | ((ULONG)Value << 8); break;
    }
}

static bool EmuReadContextXmmRegister(const CONTEXT *ContextRecord, ULONG RegisterIndex, BYTE Value[16])
{
#if defined(_X86_) || defined(_M_IX86) || defined(__i386__)
    static const ULONG EmuFxSaveXmmBase = 160;
    static const ULONG EmuFxSaveXmmRegisterSize = 16;

    if(ContextRecord == NULL || Value == NULL || RegisterIndex >= 8 ||
       EmuFxSaveXmmBase + ((RegisterIndex + 1) * EmuFxSaveXmmRegisterSize) > MAXIMUM_SUPPORTED_EXTENSION)
    {
        return false;
    }

    CopyMemory(Value,
               &ContextRecord->ExtendedRegisters[EmuFxSaveXmmBase + (RegisterIndex * EmuFxSaveXmmRegisterSize)],
               EmuFxSaveXmmRegisterSize);
    return true;
#else
    (void)ContextRecord;
    (void)RegisterIndex;
    (void)Value;
    return false;
#endif
}

static bool EmuHasEvenParity(BYTE Value)
{
    bool Even = true;

    while(Value != 0)
    {
        Even = !Even;
        Value &= Value - 1;
    }

    return Even;
}

static void EmuSetTestFlags(CONTEXT *ContextRecord, ULONG Result, ULONG SignFlagMask)
{
    static const ULONG CarryFlag = 0x00000001;
    static const ULONG ParityFlag = 0x00000004;
    static const ULONG AdjustFlag = 0x00000010;
    static const ULONG ZeroFlag = 0x00000040;
    static const ULONG SignFlag = 0x00000080;
    static const ULONG OverflowFlag = 0x00000800;

    ULONG EFlags = ContextRecord->EFlags;
    EFlags &= ~(CarryFlag | ParityFlag | AdjustFlag | ZeroFlag | SignFlag | OverflowFlag);

    if(Result == 0)
        EFlags |= ZeroFlag;

    if((Result & SignFlagMask) != 0)
        EFlags |= SignFlag;

    if(EmuHasEvenParity((BYTE)Result))
        EFlags |= ParityFlag;

    ContextRecord->EFlags = EFlags;
}

static void EmuSetSubtractFlags(CONTEXT *ContextRecord, ULONG Left, ULONG Right, ULONG Result, ULONG SignFlagMask)
{
    static const ULONG CarryFlag = 0x00000001;
    static const ULONG ParityFlag = 0x00000004;
    static const ULONG AdjustFlag = 0x00000010;
    static const ULONG ZeroFlag = 0x00000040;
    static const ULONG SignFlag = 0x00000080;
    static const ULONG OverflowFlag = 0x00000800;
    ULONG Mask = SignFlagMask | (SignFlagMask - 1);
    Left &= Mask;
    Right &= Mask;
    Result &= Mask;

    ULONG EFlags = ContextRecord->EFlags;
    EFlags &= ~(CarryFlag | ParityFlag | AdjustFlag | ZeroFlag | SignFlag | OverflowFlag);

    if(Left < Right)
        EFlags |= CarryFlag;

    if(Result == 0)
        EFlags |= ZeroFlag;

    if((Result & SignFlagMask) != 0)
        EFlags |= SignFlag;

    if(EmuHasEvenParity((BYTE)Result))
        EFlags |= ParityFlag;

    if(((Left ^ Right ^ Result) & 0x10) != 0)
        EFlags |= AdjustFlag;

    if(((Left ^ Right) & (Left ^ Result) & SignFlagMask) != 0)
        EFlags |= OverflowFlag;

    ContextRecord->EFlags = EFlags;
}

static bool EmuDecodeModRmAddress(const CONTEXT *ContextRecord, const BYTE *Instruction, ULONG *Address, ULONG *Length)
{
    BYTE ModRm = Instruction[1];
    ULONG Mod = (ModRm >> 6) & 0x03;
    ULONG Rm = ModRm & 0x07;
    ULONG Offset = 2;
    ULONG Result = 0;

    if(Mod == 3)
        return false;

    if(Rm == 4)
    {
        BYTE Sib = Instruction[Offset++];
        ULONG Scale = 1 << ((Sib >> 6) & 0x03);
        ULONG Index = (Sib >> 3) & 0x07;
        ULONG Base = Sib & 0x07;

        if(Mod == 0 && Base == 5)
        {
            Result = *(ULONG*)&Instruction[Offset];
            Offset += 4;
        }
        else
        {
            Result = EmuContextRegister(ContextRecord, Base);
        }

        if(Index != 4)
            Result += EmuContextRegister(ContextRecord, Index) * Scale;
    }
    else if(Mod == 0 && Rm == 5)
    {
        Result = *(ULONG*)&Instruction[Offset];
        Offset += 4;
    }
    else
    {
        Result = EmuContextRegister(ContextRecord, Rm);
    }

    if(Mod == 1)
        Result += (LONG)(CHAR)Instruction[Offset++];
    else if(Mod == 2)
    {
        Result += *(ULONG*)&Instruction[Offset];
        Offset += 4;
    }

    *Address = Result;
    *Length = Offset;
    return true;
}

static const ULONG EmuMmioRegisterSlotCount = 65536;

struct EmuMmioRegister
{
    ULONG Address;
    ULONG Value;
    bool Used;
};

static EmuMmioRegister g_EmuMmioRegisters[EmuMmioRegisterSlotCount] = {};

static bool EmuLookupMmioRegister(ULONG Address, ULONG *Value)
{
    ULONG Slot = (Address >> 2) & (EmuMmioRegisterSlotCount - 1);

    for(ULONG i = 0; i < EmuMmioRegisterSlotCount; i++)
    {
        ULONG Index = (Slot + i) & (EmuMmioRegisterSlotCount - 1);

        if(!g_EmuMmioRegisters[Index].Used)
            return false;

        if(g_EmuMmioRegisters[Index].Address == Address)
        {
            if(Value != NULL)
                *Value = g_EmuMmioRegisters[Index].Value;

            return true;
        }
    }

    return false;
}

static void EmuStoreMmioRegister(ULONG Address, ULONG Value)
{
    ULONG Slot = (Address >> 2) & (EmuMmioRegisterSlotCount - 1);

    for(ULONG i = 0; i < EmuMmioRegisterSlotCount; i++)
    {
        ULONG Index = (Slot + i) & (EmuMmioRegisterSlotCount - 1);

        if(g_EmuMmioRegisters[Index].Used && g_EmuMmioRegisters[Index].Address == Address)
        {
            g_EmuMmioRegisters[Index].Value = Value;
            return;
        }

        if(!g_EmuMmioRegisters[Index].Used)
        {
            g_EmuMmioRegisters[Index].Address = Address;
            g_EmuMmioRegisters[Index].Value = Value;
            g_EmuMmioRegisters[Index].Used = true;
            return;
        }
    }

    printf("Emu (0x%lX): MMIO register cache exhausted at 0x%.08lX.\n",
           GetCurrentThreadId(), Address);
    fflush(stdout);
}

static const ULONG EmuNv2aMmioBase = NV2A_XBOX_MMIO_BASE;
static const ULONG EmuNv2aMmioEnd = NV2A_XBOX_MMIO_BASE + NV2A_MMIO_SIZE - 1;
static const ULONG EmuNv2aRaminBase = NV_PRAMIN;
static const ULONG EmuNv2aRaminSize = 0x00100000;
static const ULONG EmuNv2aRaminDwordCount = EmuNv2aRaminSize / sizeof(ULONG);
static const ULONG EmuNv2aPmcIntrPfifo = 0x00000100;
static const ULONG EmuNv2aPmcIntrPgraph = 0x00001000;
static const ULONG EmuNv2aPmcIntrPcrtc = 0x01000000;
static const ULONG EmuNv2aPmcEnablePfifo = 0x00000100;
static const ULONG EmuNv2aPmcEnablePgraph = 0x00001000;
static const ULONG NV_PCRTC_INTR_EN_0 = 0x600140;
static const ULONG EmuNv2aPcrtcStart = 0x600800;   // CRTC scanout base (display flip)
static const ULONG EmuNv2aPcrtcIntrVblank = 0x00000001;
static const ULONG EmuNv2aPgraphFifoAccess = 0x00000001;
static const ULONG EmuNv2aPfifoRunoutStatus = 0x002400;
static const ULONG EmuNv2aPfifoCache1Push1 = 0x003204;
static const ULONG EmuNv2aPfifoCache1Status = 0x003214;
static const ULONG EmuNv2aPfbWbc = 0x100410;
static const ULONG EmuNv2aUserChannel0Put = 0x800040;
static const ULONG EmuNv2aUserChannel0Get = 0x800044;
static const ULONG EmuUsb0MmioBase = PCI_USB0_REGISTER_BASE;
static const ULONG EmuUsb0MmioEnd = PCI_USB0_REGISTER_BASE + 0x00000FFF;
static const ULONG EmuUsbOhciRevision = 0x00000010;
static const ULONG EmuApuMmioBase = 0xFE800000;
static const ULONG EmuApuMmioEnd = EmuApuMmioBase + 0x0007FFFF;
static const ULONG EmuApuXgscnt = 0x0000200C;
static const ULONG EmuApuVpBase = 0x00020000;
static const ULONG EmuApuVpPioFree = EmuApuVpBase + 0x00000010;
static const ULONG EmuApuVpPioFreeQueueEmpty = 0x00000080;
static const ULONG EmuAciMmioBase = 0xFEC00000;
static const ULONG EmuAciMmioEnd = EmuAciMmioBase + 0x00000FFF;
static const ULONG EmuAciBusMasterBase = 0x00000100;
static const ULONG EmuAciGlobCnt = EmuAciBusMasterBase + 0x0000002C;
static const ULONG EmuAciGlobSta = EmuAciBusMasterBase + 0x00000030;
static const ULONG EmuAciCas = EmuAciBusMasterBase + 0x00000034;
static const ULONG EmuAciGlobStaCodec0Ready = 0x00000100;
static const ULONG EmuAciStatusDmaHalted = 0x00000001;
static const ULONG EmuAciBusMasterStatusOffset = 0x00000006;
static const ULONG EmuAciBusMasterControlOffset = 0x0000000B;
static const ULONG EmuAciBusMasterControlRun = 0x00000001;
static const ULONG EmuAciBusMasterControlReset = 0x00000002;

static ULONG g_EmuNv2aRamin[EmuNv2aRaminDwordCount] = {};
static ULONG g_EmuNv2aSubchannelClass[8] = {};

static bool EmuNv2aIsMmioAddress(ULONG Address)
{
    return Address >= EmuNv2aMmioBase && Address <= EmuNv2aMmioEnd;
}

static bool EmuUsb0IsMmioAddress(ULONG Address)
{
    return Address >= EmuUsb0MmioBase && Address <= EmuUsb0MmioEnd;
}

static bool EmuApuIsMmioAddress(ULONG Address)
{
    return Address >= EmuApuMmioBase && Address <= EmuApuMmioEnd;
}

static bool EmuAciIsMmioAddress(ULONG Address)
{
    return Address >= EmuAciMmioBase && Address <= EmuAciMmioEnd;
}

static bool EmuIsMmioAddress(ULONG Address)
{
    return EmuNv2aIsMmioAddress(Address) || EmuUsb0IsMmioAddress(Address) ||
           EmuApuIsMmioAddress(Address) || EmuAciIsMmioAddress(Address);
}

static ULONG EmuNv2aOffset(ULONG Address)
{
    return Address - EmuNv2aMmioBase;
}

static bool EmuNv2aIsRaminOffset(ULONG Offset)
{
    return Offset >= EmuNv2aRaminBase && Offset < EmuNv2aRaminBase + EmuNv2aRaminSize;
}

static ULONG EmuNv2aRegisterAddress(ULONG Offset)
{
    return EmuNv2aMmioBase + Offset;
}

static ULONG EmuNv2aCachedRegister(ULONG Offset, ULONG DefaultValue)
{
    ULONG Value = DefaultValue;
    EmuLookupMmioRegister(EmuNv2aRegisterAddress(Offset), &Value);
    return Value;
}

static void EmuNv2aStoreRegister(ULONG Offset, ULONG Value)
{
    EmuStoreMmioRegister(EmuNv2aRegisterAddress(Offset), Value);
}

static ULONG EmuUsb0Offset(ULONG Address)
{
    return Address - EmuUsb0MmioBase;
}

static ULONG EmuUsb0CachedRegister(ULONG Address, ULONG DefaultValue)
{
    ULONG Value = DefaultValue;
    EmuLookupMmioRegister(Address, &Value);
    return Value;
}

static ULONG EmuApuOffset(ULONG Address)
{
    return Address - EmuApuMmioBase;
}

static ULONG EmuApuCachedRegister(ULONG Address, ULONG DefaultValue)
{
    ULONG Value = DefaultValue;
    EmuLookupMmioRegister(Address, &Value);
    return Value;
}

static ULONG EmuAciOffset(ULONG Address)
{
    return Address - EmuAciMmioBase;
}

static ULONG EmuAciCachedRegister(ULONG Address, ULONG DefaultValue)
{
    ULONG Value = DefaultValue;
    EmuLookupMmioRegister(Address, &Value);
    return Value;
}

static bool EmuAciIsBusMasterStatus(ULONG Offset)
{
    if(Offset < EmuAciBusMasterBase)
        return false;

    ULONG BusMasterOffset = Offset - EmuAciBusMasterBase;
    ULONG RegisterOffset = BusMasterOffset & 0x0F;

    return RegisterOffset == EmuAciBusMasterStatusOffset;
}

static void EmuAciClearCas()
{
    EmuStoreMmioRegister(EmuAciMmioBase + EmuAciCas, 0);
}

static void EmuAciStorePartialRegister(ULONG Address, ULONG Size, ULONG Value)
{
    ULONG AlignedAddress = Address & ~3;
    ULONG OldValue = EmuAciCachedRegister(AlignedAddress, 0);
    ULONG Shift = (Address & 3) * 8;
    ULONG Mask = (Size == 1 ? 0xFF : 0xFFFF) << Shift;

    EmuStoreMmioRegister(AlignedAddress,
                         (OldValue & ~Mask) | ((Value & (Size == 1 ? 0xFF : 0xFFFF)) << Shift));
}

static void EmuAciSetBusMasterStatus(ULONG StreamBase, ULONG Status)
{
    EmuAciStorePartialRegister(EmuAciMmioBase + StreamBase + EmuAciBusMasterStatusOffset, 2, Status);
}

static ULONG EmuUsb0ReadRegister32(ULONG Address)
{
    ULONG Offset = EmuUsb0Offset(Address);
    ULONG Value = Offset == 0 ? EmuUsbOhciRevision : EmuUsb0CachedRegister(Address, 0);

    printf("Emu (0x%lX): USB0 MMIO read 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);

    return Value;
}

static void EmuUsb0WriteRegister32(ULONG Address, ULONG Value)
{
    EmuStoreMmioRegister(Address, Value);

    printf("Emu (0x%lX): USB0 MMIO write 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);
}

static ULONG EmuApuReadRegister32(ULONG Address)
{
    ULONG Offset = EmuApuOffset(Address);
    ULONG Value = 0;

    switch(Offset)
    {
        case EmuApuXgscnt:
            Value = GetTickCount() * 10000;
            break;

        case EmuApuVpPioFree:
            Value = EmuApuVpPioFreeQueueEmpty;
            break;

        default:
            Value = EmuApuCachedRegister(Address, 0);
            break;
    }

    printf("Emu (0x%lX): APU MMIO read 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);

    return Value;
}

static void EmuApuWriteRegister32(ULONG Address, ULONG Value)
{
    EmuStoreMmioRegister(Address, Value);

    printf("Emu (0x%lX): APU MMIO write 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);
}

static ULONG EmuAciReadRegister32(ULONG Address)
{
    ULONG Offset = EmuAciOffset(Address);
    ULONG Value = 0;

    switch(Offset)
    {
        case EmuAciGlobSta:
            Value = EmuAciCachedRegister(Address, 0) | EmuAciGlobStaCodec0Ready;
            break;

        case EmuAciCas:
            // Codec Access Semaphore: bit 0 = "codec access in progress"; on real
            // hardware it is set for the (microsecond-scale) duration of a codec
            // register access and cleared by hardware the moment it completes. Model
            // that completion as immediate -- return the current value then auto-clear
            // -- so a guest that spin-polls the semaphore waiting for it to read free
            // (e.g. the FCEUltra DSOUND ISR looping on `test [0xFEC00134],1`) sees it
            // free instead of latched busy forever.
            Value = EmuAciCachedRegister(Address, 0);
            EmuStoreMmioRegister(Address, 0);
            break;

        default:
            if(Offset < EmuAciBusMasterBase)
                EmuAciClearCas();

            Value = EmuAciCachedRegister(Address,
                                         EmuAciIsBusMasterStatus(Offset) ? EmuAciStatusDmaHalted : 0);
            break;
    }

    printf("Emu (0x%lX): ACI MMIO read 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);

    return Value;
}

static void EmuAciWriteRegister32(ULONG Address, ULONG Value)
{
    ULONG Offset = EmuAciOffset(Address);

    if(Offset == EmuAciGlobCnt)
    {
        if((Value & 0x00000006) == 0)
            EmuStoreMmioRegister(Address, Value & 0x0000003F);
    }
    else if(Offset == EmuAciGlobSta)
    {
        ULONG Current = EmuAciReadRegister32(Address);
        EmuStoreMmioRegister(Address, (Current & ~Value) | EmuAciGlobStaCodec0Ready);
    }
    else if(Offset == EmuAciCas)
    {
        EmuStoreMmioRegister(Address, Value);
    }
    else if(Offset < EmuAciBusMasterBase)
    {
        EmuAciClearCas();
        EmuStoreMmioRegister(Address, Value);
    }
    else
    {
        EmuStoreMmioRegister(Address, Value);
    }

    printf("Emu (0x%lX): ACI MMIO write 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);
}

// Raise an AC97 PCM-out buffer-completion status in GLOB_STA. The audio ISR reads
// GLOB_STA & 0x51 to decide whether a real interrupt is pending; with only the
// Codec0Ready bit set it treats every synthesized interrupt as spurious and never
// services audio. Set bit 0x40 (a masked status bit) so the ISR processes; the
// guest clears it write-1-to-clear afterwards. Called from the audio delivery
// thread (EmuKrnl.cpp) just before it fires the ISR.
extern "C" void EmuAciSignalAudioInterrupt()
{
    ULONG Address = EmuAciMmioBase + EmuAciGlobSta;
    EmuStoreMmioRegister(Address, EmuAciCachedRegister(Address, 0) | 0x40);
}

static void EmuAciWriteRegister(ULONG Address, ULONG Size, ULONG Value)
{
    if(Size == 4)
    {
        EmuAciWriteRegister32(Address, Value);
        return;
    }

    ULONG Offset = EmuAciOffset(Address);
    if(Offset < EmuAciBusMasterBase)
    {
        EmuAciClearCas();
        EmuAciStorePartialRegister(Address, Size, Value);
    }
    else if(Size == 1 && ((Offset - EmuAciBusMasterBase) & 0x0F) == EmuAciBusMasterControlOffset)
    {
        ULONG StreamBase = Offset & ~0x0F;
        ULONG Control = Value & 0x1F;

        if((Control & EmuAciBusMasterControlReset) != 0)
            Control = 0;

        EmuAciStorePartialRegister(Address, 1, Control);

        if((Control & EmuAciBusMasterControlRun) == 0)
            EmuAciSetBusMasterStatus(StreamBase, EmuAciStatusDmaHalted);
    }
    else
    {
        EmuAciStorePartialRegister(Address, Size, Value);
    }

    printf("Emu (0x%lX): ACI MMIO %s write 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Size == 1 ? "byte" : "word", Address, Value);
    fflush(stdout);
}

static ULONG EmuNv2aReadRamin32(ULONG Offset)
{
    ULONG RaminOffset = Offset - EmuNv2aRaminBase;

    if(RaminOffset + sizeof(ULONG) > EmuNv2aRaminSize)
        return 0;

    return g_EmuNv2aRamin[RaminOffset / sizeof(ULONG)];
}

static void EmuNv2aWriteRamin32(ULONG Offset, ULONG Value)
{
    ULONG RaminOffset = Offset - EmuNv2aRaminBase;

    if(RaminOffset + sizeof(ULONG) > EmuNv2aRaminSize)
        return;

    g_EmuNv2aRamin[RaminOffset / sizeof(ULONG)] = Value;
}

static ULONG EmuNv2aPendingPmcInterrupts()
{
    ULONG Pending = 0;
    ULONG PfifoIntr = EmuNv2aCachedRegister(NV_PFIFO_INTR_0, 0);
    ULONG PfifoIntrEn = EmuNv2aCachedRegister(NV_PFIFO_INTR_EN_0, 0);
    ULONG PgraphIntr = EmuNv2aCachedRegister(NV_PGRAPH_INTR, 0);
    ULONG PgraphIntrEn = EmuNv2aCachedRegister(NV_PGRAPH_INTR_EN, 0);
    ULONG PcrtcIntr = EmuNv2aCachedRegister(NV_PCRTC_INTR_0, 0);
    ULONG PcrtcIntrEn = EmuNv2aCachedRegister(NV_PCRTC_INTR_EN_0, 0);

    if((PfifoIntr & PfifoIntrEn) != 0)
        Pending |= EmuNv2aPmcIntrPfifo;

    if((PgraphIntr & PgraphIntrEn) != 0)
        Pending |= EmuNv2aPmcIntrPgraph;

    if((PcrtcIntr & PcrtcIntrEn) != 0)
        Pending |= EmuNv2aPmcIntrPcrtc;

    return Pending;
}

// Raise the CRTC vertical-blank interrupt in the NV2A model. The vblank thread
// (EmuKrnl.cpp) calls this ~60x/second, then invokes the guest's connected
// display ISR, which reads these pending bits, acks them, and queues the DPC
// that unblocks D3D's BlockUntilVerticalBlank -- letting a natively-running XDK
// title (e.g. NestopiaX 1.3) advance from render-state init into its frame loop.
extern "C" void EmuNv2aRaiseVblank()
{
    ULONG Intr = EmuNv2aCachedRegister(NV_PCRTC_INTR_0, 0);
    EmuNv2aStoreRegister(NV_PCRTC_INTR_0, Intr | EmuNv2aPcrtcIntrVblank);
}

// Report whether the guest has enabled CRTC vblank interrupts yet, so the vblank
// thread can hold off firing the ISR until the title is actually listening.
extern "C" int EmuNv2aVblankEnabled()
{
    return (EmuNv2aCachedRegister(NV_PCRTC_INTR_EN_0, 0) & EmuNv2aPcrtcIntrVblank) != 0 ? 1 : 0;
}

// Clear the CRTC vblank pending bit. The XDK display ISR acknowledges vblank via
// legacy port I/O (in al, 0x80C0), not the PCRTC MMIO register, so nothing in the
// guest ever clears the bit this model raises. Left set, PMC_INTR_0 reports an
// unserviceable PCRTC interrupt forever and the title's kernel jams with
// interrupts masked. The vblank thread calls this right after the ISR returns to
// emulate the acknowledge the guest performed out-of-band.
extern "C" void EmuNv2aAckVblank()
{
    ULONG Intr = EmuNv2aCachedRegister(NV_PCRTC_INTR_0, 0);
    EmuNv2aStoreRegister(NV_PCRTC_INTR_0, Intr & ~EmuNv2aPcrtcIntrVblank);
}

// Re-enable master PMC interrupts. The XDK vblank path masks them (writes 0 to
// PMC_INTR_EN) once a vblank is serviced and relies on its vblank callback to
// re-enable them; that callback slot is null here (see the near-null recovery),
// so without this the enable bit stays 0 and the title's vblank-wait -- which
// only advances while PMC_INTR_EN != 0 -- deadlocks. The vblank thread restores
// it each tick so the next synthesized vblank can be observed.
extern "C" void EmuNv2aEnableGpuInterrupts()
{
    ULONG En = EmuNv2aCachedRegister(NV_PMC_INTR_EN_0, 0);
    EmuNv2aStoreRegister(NV_PMC_INTR_EN_0, En | 0x00000001);
}

static bool EmuNv2aRamhtLookup(ULONG Handle, ULONG *Instance, ULONG *Class)
{
    ULONG Ramht = EmuNv2aCachedRegister(NV_PFIFO_RAMHT, 0);
    ULONG RamhtBase = ((Ramht & 0x000001F0) >> 4) << 12;
    ULONG RamhtSize = 1 << (((Ramht & 0x00030000) >> 16) + 12);
    ULONG Bits = 11;
    ULONG Hash = 0;
    ULONG TempHandle = Handle;

    while((1ul << (Bits + 1)) < RamhtSize && Bits < 15)
        Bits++;

    while(TempHandle != 0)
    {
        Hash ^= TempHandle & ((1ul << Bits) - 1);
        TempHandle >>= Bits;
    }

    if(Bits > 4)
        Hash ^= (EmuNv2aCachedRegister(EmuNv2aPfifoCache1Push1, 0) & 0x1F) << (Bits - 4);

    Hash &= (RamhtSize / 8) - 1;

    for(ULONG Probe = 0; Probe < 16 && (Probe * 8) < RamhtSize; Probe++)
    {
        ULONG EntryOffset = EmuNv2aRaminBase + RamhtBase + (((Hash + Probe) & ((RamhtSize / 8) - 1)) * 8);
        ULONG EntryHandle = EmuNv2aReadRamin32(EntryOffset);
        ULONG EntryContext = EmuNv2aReadRamin32(EntryOffset + 4);

        if(EntryHandle != Handle || (EntryContext & 0x80000000) == 0)
            continue;

        ULONG ObjectInstance = (EntryContext & 0x0000FFFF) << 4;
        ULONG ObjectClass = EmuNv2aReadRamin32(EmuNv2aRaminBase + ObjectInstance) & 0xFF;

        if(ObjectClass == 0)
            ObjectClass = NV_CLASS_KELVIN;

        if(Instance != NULL)
            *Instance = ObjectInstance;

        if(Class != NULL)
            *Class = ObjectClass;

        NV2A_TRACE_RAMHT(Handle, ObjectInstance, ObjectClass);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// NV2A source-texture interception (path 1)
//
// The register-level model does not rasterize, so titles that draw through the
// KELVIN 3D pipeline (e.g. NestopiaX) never leave a finished frame in a CPU
// framebuffer. But the *source* image a title uploads -- the NES picture, a UI
// atlas -- is a plain texture in guest memory before it enters that pipeline.
// By decoding the KELVIN SET_TEXTURE_* methods we recover the texture offset +
// format and dump the bytes directly, giving "the picture the title drew"
// without a GPU. Triggered on SET_BEGIN_END (a primitive batch) when a stage-0
// texture is bound.
// ---------------------------------------------------------------------------
#define NV097_SET_CONTEXT_DMA_A        0x1A60u
#define NV097_SET_BEGIN_END            0x17FCu
#define NV097_SET_TEXTURE_OFFSET_0     0x1B00u
#define NV097_SET_TEXTURE_FORMAT_0     0x1B04u
#define NV097_SET_TEXTURE_IMAGE_RECT_0 0x1B1Cu
#define EmuNv2aTextureStageStride      0x40u
#define EmuNv2aTextureStageCount       4u

struct EmuNv2aTextureState
{
    ULONG Offset;
    ULONG Format;
    ULONG ImageRect;
};

static EmuNv2aTextureState g_EmuNv2aTexture[EmuNv2aTextureStageCount] = {};
static ULONG g_EmuNv2aContextDmaAHandle = 0;
static ULONG g_EmuNv2aTextureDumpIndex = 0;
static ULONG g_EmuNv2aTextureMethodLogCount = 0;
static ULONG g_EmuNv2aScanoutDumpIndex = 0;

// Bytes/scanline of the displayed surface, published by the video HAL's
// AvSetDisplayMode (EmuKrnl.cpp). Lets the scanout capture recover the width
// (pitch/4 at 32bpp); 0 until a mode is set, in which case a 640-wide default
// is assumed. See EmuNv2aDumpScanout.
extern "C" ULONG g_EmuDisplayPitch = 0;

static void EmuNv2aDumpSourceTexture(ULONG Stage);
static void EmuNv2aDumpScanout(ULONG PhysicalAddress);

static void EmuNv2aHandlePgraphMethod(ULONG Subchannel, ULONG Method, ULONG Data)
{
    ULONG Class = g_EmuNv2aSubchannelClass[Subchannel & 0x07];

    if(Method == 0)
    {
        ULONG Instance = 0;

        if(EmuNv2aRamhtLookup(Data, &Instance, &Class))
            g_EmuNv2aSubchannelClass[Subchannel & 0x07] = Class;
        else
            Class = NV_CLASS_KELVIN;

        NV2A_TRACE_METHOD(Class, Method, Data);
        return;
    }

    if(Class == 0)
        Class = NV_CLASS_KELVIN;

    if(Method >= 0x180 && Method < 0x200)
    {
        ULONG Instance = 0;
        ULONG ObjectClass = 0;

        if(EmuNv2aRamhtLookup(Data, &Instance, &ObjectClass))
            Data = Instance;
    }

    if(g_EmuNv2aTextureMethodLogCount < 64 && Method != 0)
    {
        printf("Emu (0x%lX): PGRAPH method class=0x%.04lX m=0x%.04lX data=0x%.08lX\n",
               GetCurrentThreadId(), Class, Method, Data);
        fflush(stdout);
        g_EmuNv2aTextureMethodLogCount++;
    }

    // Source-texture interception: capture the KELVIN texture descriptors as
    // they stream past, and snapshot the bound stage-0 texture whenever a
    // primitive batch begins.
    if(Class == NV_CLASS_KELVIN)
    {
        if(Method == NV097_SET_CONTEXT_DMA_A)
        {
            g_EmuNv2aContextDmaAHandle = Data;
        }
        else if(Method >= NV097_SET_TEXTURE_OFFSET_0 &&
                Method < NV097_SET_TEXTURE_OFFSET_0 + EmuNv2aTextureStageStride * EmuNv2aTextureStageCount)
        {
            ULONG Stage = (Method - NV097_SET_TEXTURE_OFFSET_0) / EmuNv2aTextureStageStride;
            ULONG StageMethod = NV097_SET_TEXTURE_OFFSET_0 + Stage * EmuNv2aTextureStageStride;

            if(Method == StageMethod)
                g_EmuNv2aTexture[Stage].Offset = Data;
            else if(Method == StageMethod + 4)
                g_EmuNv2aTexture[Stage].Format = Data;
            else if(Method == StageMethod + (NV097_SET_TEXTURE_IMAGE_RECT_0 - NV097_SET_TEXTURE_OFFSET_0))
                g_EmuNv2aTexture[Stage].ImageRect = Data;

            if(g_EmuNv2aTextureMethodLogCount < 32)
            {
                printf("Emu (0x%lX): KELVIN texture[%lu] method 0x%.04lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), Stage, Method, Data);
                fflush(stdout);
                g_EmuNv2aTextureMethodLogCount++;
            }
        }
        else if(Method == NV097_SET_BEGIN_END && Data != 0)
        {
            if(g_EmuNv2aTexture[0].Offset != 0 && g_EmuNv2aTexture[0].Format != 0)
                EmuNv2aDumpSourceTexture(0);
        }
    }

    EmuNv2aStoreRegister(NV_PGRAPH + (Method & 0x1FFC), Data);
    NV2A_TRACE_METHOD(Class, Method, Data);
}

static void EmuNv2aRunPusher();

static ULONG EmuReadMmioRegister32(ULONG Address)
{
    ULONG Offset = EmuNv2aOffset(Address);
    ULONG Value = 0;

    if(EmuNv2aIsRaminOffset(Offset))
    {
        Value = EmuNv2aReadRamin32(Offset);
        NV2A_TRACE_REG("rd", Offset, Value);
        return Value;
    }

    switch(Offset)
    {
        case NV_PMC_BOOT_0:
            Value = 0x02A000A1;
            break;

        case NV_PMC_INTR_0:
            Value = EmuNv2aPendingPmcInterrupts();
            break;

        case NV_PMC_ENABLE:
            Value = EmuNv2aCachedRegister(Offset, EmuNv2aPmcEnablePfifo | EmuNv2aPmcEnablePgraph);
            break;

        case NV_PFIFO_INTR_0:
        case NV_PFIFO_INTR_EN_0:
        case NV_PFIFO_RAMHT:
        case NV_PFIFO_RAMFC:
        case NV_PFIFO_RAMRO:
        case NV_PFIFO_CACHE1_DMA_STATE:
        case NV_PFIFO_CACHE1_DMA_INSTANCE:
        case NV_PFIFO_CACHE1_DMA_PUT:
        case NV_PFIFO_CACHE1_DMA_GET:
        case NV_PGRAPH_INTR:
        case NV_PGRAPH_INTR_EN:
        case NV_PGRAPH_CTX_CONTROL:
            Value = EmuNv2aCachedRegister(Offset, 0);
            break;

        case NV_PFIFO_CACHE1_PUSH0:
        case NV_PFIFO_CACHE1_DMA_PUSH:
            Value = EmuNv2aCachedRegister(Offset, 1);
            break;

        case EmuNv2aPfifoRunoutStatus:
            Value = 0x10;
            break;

        case EmuNv2aPfifoCache1Status:
            Value = EmuNv2aCachedRegister(Offset, 0) | 0x10;
            break;

        case NV_PGRAPH_STATUS:
            Value = 0;
            break;

        case NV_PGRAPH_FIFO:
            Value = EmuNv2aCachedRegister(Offset, EmuNv2aPgraphFifoAccess) | EmuNv2aPgraphFifoAccess;
            break;

        case NV_PFB_CFG0:
            // Framebuffer memory config. The low two bits report the RAM type/
            // config; nxdk pbkit requires them set (CFG0 & 3 == 3) to proceed with
            // channel setup and otherwise bails into a cleanup path that crashes.
            Value = EmuNv2aCachedRegister(Offset, 0) | 0x00000003;
            break;

        case 0x100240:
            Value = EmuNv2aCachedRegister(Offset, 0);
            break;

        case EmuNv2aPfbWbc:
            Value = 0;
            break;

        case EmuNv2aUserChannel0Get:
            Value = EmuNv2aCachedRegister(EmuNv2aUserChannel0Put, 0);
            break;

        // NV2A PRAMDAC clock-PLL coefficients. Software (e.g. nxdk pbkit) reads
        // these to derive the GPU/memory/video clocks and DIVIDES by the M field
        // (bits 0-7); a zero coefficient means M==0, so pbkit bails out of channel
        // setup, leaves its DMA-context pointer null, and later crashes. Report
        // realistic Xbox values: clock = N * 16.66MHz / (M << P).
        case 0x680500:   // NVPLL_COEFF: GPU core ~233 MHz  (M=1, N=28, P=1)
            Value = 0x00011C01;
            break;
        case 0x680504:   // MPLL_COEFF:  memory  ~200 MHz  (M=1, N=12, P=0)
            Value = 0x00000C01;
            break;
        case 0x680508:   // VPLL_COEFF:  video   ~33 MHz   (M=1, N=2,  P=0)
            Value = 0x00000201;
            break;

        default:
            Value = EmuNv2aCachedRegister(Offset, 0);
            break;
    }

    NV2A_TRACE_REG("rd", Offset, Value);
    return Value;
}

// Unimplemented device-MMIO catch-all. A faulting access to hardware-MMIO space
// that no device model (NV2A, USB0, ...) backs is logged and stubbed (reads -> 0,
// writes ignored) instead of crashing the title. This mirrors the kernel warned
// trap: the title survives to reveal the *next* device it needs, and the log
// names the exact address (e.g. the NIC at 0xFEF00000, USB1 at 0xFED08000).
// Set CXBX_STUB_UNHANDLED_MMIO=0 to restore crash-on-unhandled-MMIO when
// isolating a specific device.
#ifndef CXBX_STUB_UNHANDLED_MMIO
#define CXBX_STUB_UNHANDLED_MMIO 1
#endif

// Xbox hardware device / flash MMIO region, above the NV2A aperture (0xFD...).
// Kept high so genuine null/wild-pointer bugs (low addresses) still crash.
static const ULONG EmuStubMmioBase = 0xFE000000;
static const ULONG EmuStubMmioEnd  = 0xFEFFFFFF;

static bool EmuIsStubMmioAddress(ULONG Address)
{
#if CXBX_STUB_UNHANDLED_MMIO
    return Address >= EmuStubMmioBase && Address <= EmuStubMmioEnd;
#else
    (void)Address;
    return false;
#endif
}

static ULONG EmuStubMmioRead(ULONG Address)
{
    printf("Emu (0x%lX): *UNIMPLEMENTED MMIO* read 0x%.08lX -> 0\n",
           GetCurrentThreadId(), Address);
    fflush(stdout);
    return 0;
}

static void EmuStubMmioWrite(ULONG Address, ULONG Value)
{
    printf("Emu (0x%lX): *UNIMPLEMENTED MMIO* write 0x%.08lX = 0x%.08lX (ignored)\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);
}

static ULONG EmuReadMmio(ULONG Address, ULONG Size)
{
    ULONG AlignedAddress = Address & ~3;

    ULONG Value;
    if(EmuUsb0IsMmioAddress(AlignedAddress))
        Value = EmuUsb0ReadRegister32(AlignedAddress);
    else if(EmuApuIsMmioAddress(AlignedAddress))
        Value = EmuApuReadRegister32(AlignedAddress);
    else if(EmuAciIsMmioAddress(AlignedAddress))
        Value = EmuAciReadRegister32(AlignedAddress);
    else if(EmuNv2aIsMmioAddress(AlignedAddress))
        Value = EmuReadMmioRegister32(AlignedAddress);
    else
        Value = EmuStubMmioRead(AlignedAddress);

    ULONG Shift = (Address & 3) * 8;

    if(Size == 1)
        return (Value >> Shift) & 0xFF;

    if(Size == 2)
        return (Value >> Shift) & 0xFFFF;

    return Value;
}

static void EmuWriteMmio(ULONG Address, ULONG Size, ULONG Value)
{
    ULONG AlignedAddress = Address & ~3;
    ULONG Offset = EmuNv2aOffset(AlignedAddress);

    if(EmuAciIsMmioAddress(Address))
    {
        EmuAciWriteRegister(Address, Size, Value);
        return;
    }

    if(Size == 1 || Size == 2)
    {
        ULONG OldValue = EmuReadMmio(AlignedAddress, 4);
        ULONG Shift = (Address & 3) * 8;
        ULONG Mask = (Size == 1 ? 0xFF : 0xFFFF) << Shift;
        Value = (OldValue & ~Mask) | ((Value & (Size == 1 ? 0xFF : 0xFFFF)) << Shift);
        Address = AlignedAddress;
    }

    if(EmuUsb0IsMmioAddress(Address))
    {
        EmuUsb0WriteRegister32(Address, Value);
        return;
    }

    if(EmuApuIsMmioAddress(Address))
    {
        EmuApuWriteRegister32(Address, Value);
        return;
    }

    if(!EmuNv2aIsMmioAddress(Address))
    {
        EmuStubMmioWrite(Address, Value);
        return;
    }

    if(EmuNv2aIsRaminOffset(Offset))
    {
        EmuNv2aWriteRamin32(Offset, Value);
        NV2A_TRACE_REG("wr", Offset, Value);
        return;
    }

    if(Offset == NV_PMC_INTR_0)
        Value = EmuNv2aPendingPmcInterrupts() & ~Value;
    else if(Offset == NV_PFIFO_INTR_0 || Offset == NV_PGRAPH_INTR || Offset == NV_PCRTC_INTR_0)
        Value = EmuReadMmioRegister32(Address) & ~Value;
    else if(Offset == EmuNv2aPfbWbc)
    {
        NV2A_TRACE_REG("wr", Offset, Value);
        return;
    }

    EmuNv2aStoreRegister(Offset, Value);
    NV2A_TRACE_REG("wr", Offset, Value);

    // A write to the CRTC scanout base flips the display to that surface; capture
    // what the guest just put on screen (path 2). The programmed value is a
    // masked physical address (low 28 bits).
    if(Offset == EmuNv2aPcrtcStart)
        EmuNv2aDumpScanout(Value & 0x0FFFFFFF);

    // The Xbox pushbuffer kickoff is a write to the per-channel USER PUT register
    // (NV_USER + 0x40). Hardware mirrors that into the PFIFO CACHE1 DMA_PUT that
    // the pusher actually advances against, so mirror it here before running the
    // pusher; otherwise DMA_GET==DMA_PUT stays stale and no methods dispatch.
    if(Offset == EmuNv2aUserChannel0Put)
        EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_PUT, Value);

    if(Offset == NV_PFIFO_CACHE1_DMA_PUT || Offset == NV_PFIFO_CACHE1_DMA_PUSH ||
       Offset == NV_PFIFO_CACHE1_PUSH0 || Offset == EmuNv2aUserChannel0Put)
    {
        EmuNv2aRunPusher();
    }
}

static const ULONG EmuPhysicalMapBase = 0x80000000;
static const ULONG EmuPhysicalMapEnd = 0x8FFFFFFF;
static const ULONG EmuPhysicalRamMirrorSize = 0x04000000;
static const ULONG EmuPhysicalRamMirrorMask = EmuPhysicalRamMirrorSize - 1;
static const ULONG EmuPhysicalShadowBase = 0xF0000000;
static const ULONG EmuPhysicalShadowEnd = EmuPhysicalShadowBase + EmuPhysicalRamMirrorSize - 1;
static const ULONG EmuPhysicalHighApertureBase = 0xF8000000;
static const ULONG EmuPhysicalHighApertureEnd = 0xFCFFFFFF;
static const ULONG EmuPhysicalPageSize = 0x1000;
static const ULONG EmuPhysicalPageSlotCount = 4096;

struct EmuPhysicalPage
{
    ULONG Address;
    BYTE *Data;
};

static EmuPhysicalPage g_EmuPhysicalPages[EmuPhysicalPageSlotCount] = {};
static ULONG g_EmuPhysicalMovntpsLogCount = 0;

static bool EmuIsPhysicalMapAddress(ULONG Address)
{
    return (Address >= EmuPhysicalMapBase && Address <= EmuPhysicalMapEnd) ||
           (Address >= EmuPhysicalShadowBase && Address <= EmuPhysicalShadowEnd) ||
           (Address >= EmuPhysicalHighApertureBase && Address <= EmuPhysicalHighApertureEnd);
}

static ULONG EmuPhysicalMapPageAddress(ULONG Address)
{
    if(Address >= EmuPhysicalMapBase && Address <= EmuPhysicalMapEnd)
        return Address;

    return EmuPhysicalMapBase + (Address & EmuPhysicalRamMirrorMask);
}

static BYTE *EmuGetPhysicalPage(ULONG Address, bool Create)
{
    if(!EmuIsPhysicalMapAddress(Address))
        return NULL;

    ULONG PageAddress = EmuPhysicalMapPageAddress(Address) & ~(EmuPhysicalPageSize - 1);

    for(ULONG i = 0; i < EmuPhysicalPageSlotCount; i++)
    {
        if(g_EmuPhysicalPages[i].Data != NULL && g_EmuPhysicalPages[i].Address == PageAddress)
            return g_EmuPhysicalPages[i].Data;
    }

    if(!Create)
        return NULL;

    for(ULONG i = 0; i < EmuPhysicalPageSlotCount; i++)
    {
        if(g_EmuPhysicalPages[i].Data == NULL)
        {
            BYTE *Page = new BYTE[EmuPhysicalPageSize];
            ZeroMemory(Page, EmuPhysicalPageSize);
            g_EmuPhysicalPages[i].Address = PageAddress;
            g_EmuPhysicalPages[i].Data = Page;
            return Page;
        }
    }

    printf("Emu (0x%lX): Physical map backing exhausted at 0x%.08lX.\n",
           GetCurrentThreadId(), Address);
    fflush(stdout);

    return NULL;
}

static bool EmuReadPhysicalMap(ULONG Address, ULONG Size, ULONG *Value)
{
    ULONG End = Address + Size - 1;
    if(Size == 0 || Value == NULL || End < Address || !EmuIsPhysicalMapAddress(Address) ||
       !EmuIsPhysicalMapAddress(End))
    {
        return false;
    }

    ULONG Result = 0;
    for(ULONG i = 0; i < Size; i++)
    {
        BYTE *Page = EmuGetPhysicalPage(Address + i, false);
        BYTE ByteValue = 0;
        if(Page != NULL)
            ByteValue = Page[(Address + i) & (EmuPhysicalPageSize - 1)];

        Result |= (ULONG)ByteValue << (i * 8);
    }

    *Value = Result;
    return true;
}

static bool EmuWritePhysicalMap(ULONG Address, ULONG Size, ULONG Value)
{
    ULONG End = Address + Size - 1;
    if(Size == 0 || End < Address || !EmuIsPhysicalMapAddress(Address) ||
       !EmuIsPhysicalMapAddress(End))
    {
        return false;
    }

    for(ULONG i = 0; i < Size; i++)
    {
        BYTE *Page = EmuGetPhysicalPage(Address + i, true);
        if(Page == NULL)
            return false;

        Page[(Address + i) & (EmuPhysicalPageSize - 1)] = (BYTE)(Value >> (i * 8));
    }

    return true;
}

static bool EmuWritePhysicalMapBytes(ULONG Address, const BYTE *Data, ULONG Size)
{
    ULONG End = Address + Size - 1;
    if(Size == 0 || Data == NULL || End < Address || !EmuIsPhysicalMapAddress(Address) ||
       !EmuIsPhysicalMapAddress(End))
    {
        return false;
    }

    for(ULONG i = 0; i < Size; i++)
    {
        BYTE *Page = EmuGetPhysicalPage(Address + i, true);
        if(Page == NULL)
            return false;

        Page[(Address + i) & (EmuPhysicalPageSize - 1)] = Data[i];
    }

    return true;
}

static bool EmuWritePhysicalMapRepeated(ULONG Address, ULONG Count, ULONG Size, ULONG Value, bool DirectionDown)
{
    if(Count == 0 || Size == 0)
        return false;

    ULONGLONG TotalSize = (ULONGLONG)Count * Size;
    if(TotalSize > 0x100000000ULL)
        return false;

    ULONG FirstAddress = Address;
    ULONG LastAddress = Address;

    if(DirectionDown)
    {
        ULONGLONG Span = TotalSize - Size;
        if(Span > Address)
            return false;

        FirstAddress = Address - (ULONG)Span;
    }
    else
    {
        ULONGLONG End = (ULONGLONG)Address + TotalSize - 1;
        if(End > 0xFFFFFFFFULL)
            return false;

        LastAddress = (ULONG)End;
    }

    if(!EmuIsPhysicalMapAddress(FirstAddress) || !EmuIsPhysicalMapAddress(LastAddress))
        return false;

    ULONG CurrentAddress = Address;
    for(ULONG i = 0; i < Count; i++)
    {
        if(!EmuWritePhysicalMap(CurrentAddress, Size, Value))
            return false;

        if(DirectionDown)
            CurrentAddress -= Size;
        else
            CurrentAddress += Size;
    }

    return true;
}

static bool EmuReadMemoryValue(ULONG Address, ULONG Size, ULONG *Value)
{
    if(Value == NULL)
        return false;

    if(EmuIsPhysicalMapAddress(Address))
        return EmuReadPhysicalMap(Address, Size, Value);

    __try
    {
        if(Size == 1)
            *Value = *(BYTE*)Address;
        else if(Size == 4)
            *Value = *(ULONG*)Address;
        else
            return false;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return true;
}

static bool EmuCopyToPhysicalMapRepeated(ULONG DestinationAddress, ULONG SourceAddress, ULONG Count, ULONG Size, bool DirectionDown)
{
    if(Count == 0 || Size == 0)
        return false;

    ULONGLONG TotalSize = (ULONGLONG)Count * Size;
    if(TotalSize > 0x100000000ULL)
        return false;

    ULONG FirstAddress = DestinationAddress;
    ULONG LastAddress = DestinationAddress;

    if(DirectionDown)
    {
        ULONGLONG Span = TotalSize - Size;
        if(Span > DestinationAddress)
            return false;

        FirstAddress = DestinationAddress - (ULONG)Span;
    }
    else
    {
        ULONGLONG End = (ULONGLONG)DestinationAddress + TotalSize - 1;
        if(End > 0xFFFFFFFFULL)
            return false;

        LastAddress = (ULONG)End;
    }

    if(!EmuIsPhysicalMapAddress(FirstAddress) || !EmuIsPhysicalMapAddress(LastAddress))
        return false;

    ULONG CurrentDestination = DestinationAddress;
    ULONG CurrentSource = SourceAddress;
    for(ULONG i = 0; i < Count; i++)
    {
        ULONG Value = 0;
        if(!EmuReadMemoryValue(CurrentSource, Size, &Value))
            return false;

        if(!EmuWritePhysicalMap(CurrentDestination, Size, Value))
            return false;

        if(DirectionDown)
        {
            CurrentDestination -= Size;
            CurrentSource -= Size;
        }
        else
        {
            CurrentDestination += Size;
            CurrentSource += Size;
        }
    }

    return true;
}

// Bridges into the kernel's contiguous-memory tracker (defined in EmuKrnl.cpp).
// Used to reach guest data the NV2A references by raw host pointer.
extern "C" ULONG EmuContiguousBlockBase(ULONG HostAddress, ULONG *BlockSize);
extern "C" ULONG EmuContiguousHostFromPhysical(ULONG PhysicalAddress);

// SEH-guarded read of guest data straight from host memory. In this HLE model
// the guest hands the NV2A real host pointers (pushbuffer PUT, texture offsets),
// so the payload is directly addressable -- but a bad pointer must not crash the
// emulator, hence the guard.
static bool EmuTryReadHost(ULONG Address, void *Dst, ULONG Size)
{
    if(Address == 0 || Dst == NULL || Size == 0)
        return false;

    __try
    {
        memcpy(Dst, (const void *)Address, Size);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Resolve a value the guest programmed into the NV2A (pushbuffer/texture pointer)
// to a directly-readable host address. It is usually already a host pointer into
// a contiguous block; titles that route through MmGetPhysicalAddress instead
// program a fake physical address, which reverse-maps back to its host block.
static ULONG EmuNv2aHostPointer(ULONG GuestAddress)
{
    ULONG BlockSize = 0;
    if(EmuContiguousBlockBase(GuestAddress, &BlockSize) != 0)
        return GuestAddress;

    ULONG Host = EmuContiguousHostFromPhysical(GuestAddress);
    if(Host != 0)
        return Host;

    return 0;
}

// Bulk-read a contiguous span from the physical-map shadow, walking one page at
// a time (byte-at-a-time via EmuReadPhysicalMap would be ~1e9 ops for a 256 KiB
// texture). Unbacked pages read as zero.
static bool EmuReadPhysicalMapBlock(ULONG Address, BYTE *Dst, ULONG Size)
{
    ULONG End = Address + Size - 1;
    if(Size == 0 || Dst == NULL || End < Address ||
       !EmuIsPhysicalMapAddress(Address) || !EmuIsPhysicalMapAddress(End))
    {
        return false;
    }

    ULONG Copied = 0;
    while(Copied < Size)
    {
        ULONG Cur = Address + Copied;
        ULONG PageOffset = Cur & (EmuPhysicalPageSize - 1);
        ULONG Chunk = EmuPhysicalPageSize - PageOffset;
        if(Chunk > Size - Copied)
            Chunk = Size - Copied;

        BYTE *Page = EmuGetPhysicalPage(Cur, false);
        if(Page != NULL)
            memcpy(Dst + Copied, Page + PageOffset, Chunk);
        else
            ZeroMemory(Dst + Copied, Chunk);

        Copied += Chunk;
    }

    return true;
}

// Resolve a texture's context-DMA object handle to its physical base frame, so
// SET_TEXTURE_OFFSET (which is relative to that DMA object) becomes an absolute
// guest physical address. Returns 0 when the handle is unbound/unknown, which is
// the common Xbox case where the texture offset is already a physical address.
static ULONG EmuNv2aResolveDmaBase(ULONG Handle)
{
    if(Handle == 0)
        return 0;

    ULONG Instance = 0;
    ULONG Class = 0;
    if(!EmuNv2aRamhtLookup(Handle, &Instance, &Class))
        return 0;

    if(Instance + 12 > EmuNv2aRaminSize)
        return 0;

    ULONG Flags = EmuNv2aReadRamin32(EmuNv2aRaminBase + Instance);
    ULONG Frame = EmuNv2aReadRamin32(EmuNv2aRaminBase + Instance + 8);
    return (Frame & 0x07FFFFFF) | (Flags & 0x00000FFF);
}

// Interleave the low bits of x and y (Morton / Z-order) to locate a texel inside
// an NV2A swizzled texture. Handles non-square by consuming each axis until it
// runs out of bits, then appending the remainder of the longer axis.
static ULONG EmuNv2aSwizzleTexelIndex(ULONG X, ULONG Y, ULONG LogW, ULONG LogH)
{
    ULONG Result = 0, Out = 0, Bx = 0, By = 0;

    while(Bx < LogW || By < LogH)
    {
        if(Bx < LogW) { Result |= ((X >> Bx) & 1u) << Out; Out++; Bx++; }
        if(By < LogH) { Result |= ((Y >> By) & 1u) << Out; Out++; By++; }
    }

    return Result;
}

static ULONG EmuNv2aLog2(ULONG Value)
{
    ULONG Log = 0;
    while((1ul << Log) < Value && Log < 31)
        Log++;
    return Log;
}

// Decode a bound KELVIN texture from guest memory and write it to a BMP. This is
// the payload of path 1: it reproduces the source image the title uploaded,
// independent of any rasterization.
static void EmuNv2aDumpSourceTexture(ULONG Stage)
{
    if(Stage >= EmuNv2aTextureStageCount || g_EmuNv2aTextureDumpIndex >= 16)
        return;

    ULONG Format = g_EmuNv2aTexture[Stage].Format;
    ULONG Color = (Format >> 8) & 0xFF;
    ULONG SizeU = (Format >> 20) & 0xF;
    ULONG SizeV = (Format >> 24) & 0xF;
    ULONG ImageRect = g_EmuNv2aTexture[Stage].ImageRect;

    // Byte width and how each texel maps to RGB. kind: 0=A8R8G8B8/X8R8G8B8,
    // 1=R5G6B5, 2=A1R5G5B5/X1R5G5B5, 3=A4R4G4B4, 4=Y8. Swizzled formats are the
    // low color codes; the LU_IMAGE_* (>=0x10) formats are linear.
    ULONG Bpp = 0, Kind = 0;
    bool Swizzled = false;
    switch(Color)
    {
        case 0x00: Bpp = 1; Kind = 4; Swizzled = true;  break; // SZ_Y8
        case 0x13: Bpp = 1; Kind = 4; Swizzled = false; break; // LU_Y8
        case 0x02: Bpp = 2; Kind = 2; Swizzled = true;  break; // SZ_A1R5G5B5
        case 0x03: Bpp = 2; Kind = 2; Swizzled = true;  break; // SZ_X1R5G5B5
        case 0x10: Bpp = 2; Kind = 2; Swizzled = false; break; // LU_A1R5G5B5
        case 0x04: Bpp = 2; Kind = 3; Swizzled = true;  break; // SZ_A4R4G4B4
        case 0x19: Bpp = 2; Kind = 3; Swizzled = false; break; // LU_A4R4G4B4
        case 0x05: Bpp = 2; Kind = 1; Swizzled = true;  break; // SZ_R5G6B5
        case 0x11: Bpp = 2; Kind = 1; Swizzled = false; break; // LU_R5G6B5
        case 0x06: Bpp = 4; Kind = 0; Swizzled = true;  break; // SZ_A8R8G8B8
        case 0x07: Bpp = 4; Kind = 0; Swizzled = true;  break; // SZ_X8R8G8B8
        case 0x12: Bpp = 4; Kind = 0; Swizzled = false; break; // LU_A8R8G8B8
        case 0x1A: Bpp = 4; Kind = 0; Swizzled = false; break; // LU_A8R8G8B8 (rect)
        case 0x1C: Bpp = 4; Kind = 0; Swizzled = false; break; // LU_X8R8G8B8
        case 0x1E: Bpp = 4; Kind = 0; Swizzled = false; break; // LU_A8B8G8R8 approx
        default:
            printf("Emu (0x%lX): KELVIN texture[%lu] unsupported color format 0x%.02lX (offset=0x%.08lX); skipping dump.\n",
                   GetCurrentThreadId(), Stage, Color, g_EmuNv2aTexture[Stage].Offset);
            fflush(stdout);
            return;
    }

    // Dimensions: swizzled textures carry log2 sizes in the format word; linear
    // (rect) textures carry pixel dimensions in SET_TEXTURE_IMAGE_RECT.
    ULONG Width = 0, Height = 0;
    if(Swizzled || ImageRect == 0)
    {
        Width = 1ul << SizeU;
        Height = 1ul << SizeV;
    }
    else
    {
        Width = (ImageRect >> 16) & 0xFFFF;
        Height = ImageRect & 0xFFFF;
    }

    if(Width == 0 || Height == 0 || Width > 4096 || Height > 4096)
    {
        printf("Emu (0x%lX): KELVIN texture[%lu] implausible size %lux%lu; skipping dump.\n",
               GetCurrentThreadId(), Stage, Width, Height);
        fflush(stdout);
        return;
    }

    ULONG Base = EmuNv2aResolveDmaBase(g_EmuNv2aContextDmaAHandle);
    ULONG TextureAddress = Base + g_EmuNv2aTexture[Stage].Offset;
    ULONG SourceSize = Width * Height * Bpp;
    BYTE *Source = new BYTE[SourceSize];
    const char *SourceKind = NULL;

    // The texture is normally a raw host pointer into a contiguous block (or a
    // fake-physical that reverse-maps to one); read it straight from host memory.
    // Fall back to the physical-map shadow for the probe path.
    ULONG HostAddress = EmuNv2aHostPointer(TextureAddress);
    if(HostAddress != 0 && EmuTryReadHost(HostAddress, Source, SourceSize))
    {
        SourceKind = "host";
    }
    else
    {
        ULONG PhysicalAddress = TextureAddress;
        if(!EmuIsPhysicalMapAddress(PhysicalAddress))
            PhysicalAddress = EmuPhysicalMapBase + (PhysicalAddress & EmuPhysicalRamMirrorMask);

        if(EmuReadPhysicalMapBlock(PhysicalAddress, Source, SourceSize))
            SourceKind = "shadow";
    }

    if(SourceKind == NULL)
    {
        printf("Emu (0x%lX): KELVIN texture[%lu] source 0x%.08lX unreadable (host=0x%.08lX); skipping dump.\n",
               GetCurrentThreadId(), Stage, TextureAddress, HostAddress);
        fflush(stdout);
        delete[] Source;
        return;
    }

    ULONG LogW = EmuNv2aLog2(Width);
    ULONG LogH = EmuNv2aLog2(Height);
    ULONG *Pixels = new ULONG[Width * Height]; // BGRA, top-down

    ULONG DistinctSample = 0;
    ULONG FirstColor = 0;
    for(ULONG Y = 0; Y < Height; Y++)
    {
        for(ULONG X = 0; X < Width; X++)
        {
            ULONG TexelIndex = Swizzled ? EmuNv2aSwizzleTexelIndex(X, Y, LogW, LogH)
                                        : (Y * Width + X);
            ULONG ByteOffset = TexelIndex * Bpp;
            ULONG Raw = 0;
            for(ULONG b = 0; b < Bpp && ByteOffset + b < SourceSize; b++)
                Raw |= (ULONG)Source[ByteOffset + b] << (b * 8);

            ULONG R = 0, G = 0, B = 0;
            switch(Kind)
            {
                case 0: B = Raw & 0xFF; G = (Raw >> 8) & 0xFF; R = (Raw >> 16) & 0xFF; break;
                case 1: R = ((Raw >> 11) & 0x1F) << 3; G = ((Raw >> 5) & 0x3F) << 2; B = (Raw & 0x1F) << 3; break;
                case 2: R = ((Raw >> 10) & 0x1F) << 3; G = ((Raw >> 5) & 0x1F) << 3; B = (Raw & 0x1F) << 3; break;
                case 3: R = ((Raw >> 8) & 0xF) << 4; G = ((Raw >> 4) & 0xF) << 4; B = (Raw & 0xF) << 4; break;
                case 4: R = G = B = Raw & 0xFF; break;
            }

            ULONG Bgra = B | (G << 8) | (R << 16) | 0xFF000000;
            Pixels[Y * Width + X] = Bgra;

            if(X == 0 && Y == 0)
                FirstColor = Bgra;
            else if(Bgra != FirstColor && DistinctSample < 2)
                DistinctSample = 2;
        }
    }

    char dir[MAX_PATH] = {0};
    GetTempPathA(sizeof(dir), dir);
    char path[MAX_PATH];
    sprintf(path, "%scxbx_tex%lu.bmp", dir, g_EmuNv2aTextureDumpIndex);

    FILE *f = fopen(path, "wb");
    if(f != NULL)
    {
        ULONG DataSize = Width * Height * 4;
        unsigned char fh[14] = {0}, ih[40] = {0};
        ULONG fileSize = 54 + DataSize;
        fh[0] = 'B'; fh[1] = 'M';
        fh[2] = (unsigned char)fileSize;         fh[3] = (unsigned char)(fileSize >> 8);
        fh[4] = (unsigned char)(fileSize >> 16); fh[5] = (unsigned char)(fileSize >> 24);
        fh[10] = 54;
        ih[0] = 40;
        ih[4] = (unsigned char)Width; ih[5] = (unsigned char)(Width >> 8);
        LONG nh = -(LONG)Height; // negative height => top-down
        ih[8]  = (unsigned char)nh;         ih[9]  = (unsigned char)(nh >> 8);
        ih[10] = (unsigned char)(nh >> 16); ih[11] = (unsigned char)(nh >> 24);
        ih[12] = 1;
        ih[14] = 32;
        fwrite(fh, 1, 14, f);
        fwrite(ih, 1, 40, f);
        fwrite(Pixels, 1, DataSize, f);
        fclose(f);

        printf("Emu (0x%lX): KELVIN texture[%lu] dumped %lux%lu color=0x%.02lX %s src=%s offset=0x%.08lX first=0x%.08lX distinct>=2:%s -> %s\n",
               GetCurrentThreadId(), Stage, Width, Height, Color,
               Swizzled ? "swizzled" : "linear", SourceKind,
               g_EmuNv2aTexture[Stage].Offset, FirstColor,
               DistinctSample >= 2 ? "yes" : "no", path);
        fflush(stdout);
        g_EmuNv2aTextureDumpIndex++;
    }

    delete[] Pixels;
    delete[] Source;
}

// Capture the displayed framebuffer ("path 2"). The CRTC scanout base register
// (NV_PCRTC_START) is programmed with the physical address of the surface to put
// on screen -- exactly what a title flips to at frame end (nxdk pbkit's
// pb_show_front_screen / VBL swap, the XDK's D3D present). Snapshotting the
// surface at that address reproduces the actual on-screen image, independent of
// any rasterization: whatever the guest drew into that buffer (CPU or GPU) is
// what we write to %TEMP%\cxbx_fbN.bmp.
static void EmuNv2aDumpScanout(ULONG PhysicalAddress)
{
    if(g_EmuNv2aScanoutDumpIndex >= 16 || PhysicalAddress == 0)
        return;

    // Geometry: pitch from the video HAL (else a 640-wide default); height from
    // the contiguous surface's tracked size (a scanout buffer is width*height*4).
    ULONG Pitch = g_EmuDisplayPitch != 0 ? g_EmuDisplayPitch : (640u * 4u);
    ULONG Width = Pitch / 4u;
    if(Width == 0 || Width > 4096)
        return;

    // Resolve the scanout physical address to a readable host pointer. With the
    // framebuffer in the low Xbox-RAM window, host == physical in the low bits,
    // so the masked PCRTC_START value maps straight back to the host surface.
    ULONG BlockSize = 0;
    ULONG Host = EmuNv2aHostPointer(PhysicalAddress);
    if(Host != 0)
        EmuContiguousBlockBase(Host, &BlockSize);

    ULONG Height = (BlockSize != 0) ? (BlockSize / Pitch) : 480u;
    if(Height == 0)
        Height = 480u;
    if(Height > 2048)
        Height = 2048u;

    ULONG SurfaceSize = Pitch * Height;
    BYTE *Surface = new BYTE[SurfaceSize];
    const char *SourceKind = NULL;

    if(Host != 0 && EmuTryReadHost(Host, Surface, SurfaceSize))
    {
        SourceKind = "host";
    }
    else
    {
        ULONG Phys = PhysicalAddress;
        if(!EmuIsPhysicalMapAddress(Phys))
            Phys = EmuPhysicalMapBase + (Phys & EmuPhysicalRamMirrorMask);
        if(EmuReadPhysicalMapBlock(Phys, Surface, SurfaceSize))
            SourceKind = "shadow";
    }

    if(SourceKind == NULL)
    {
        printf("Emu (0x%lX): scanout 0x%.08lX unreadable (host=0x%.08lX); skipping dump.\n",
               GetCurrentThreadId(), PhysicalAddress, Host);
        fflush(stdout);
        delete[] Surface;
        return;
    }

    // Repack each X8R8G8B8 scanline to BGRA (drop any pitch padding beyond width)
    // and force opaque alpha so the BMP renders solid.
    ULONG *Pixels = new ULONG[Width * Height];
    ULONG DistinctSample = 0, FirstColor = 0;
    for(ULONG Y = 0; Y < Height; Y++)
    {
        const ULONG *Row = (const ULONG *)(Surface + (size_t)Y * Pitch);
        for(ULONG X = 0; X < Width; X++)
        {
            ULONG Bgra = (Row[X] & 0x00FFFFFF) | 0xFF000000;
            Pixels[Y * Width + X] = Bgra;
            if(X == 0 && Y == 0)
                FirstColor = Bgra;
            else if(Bgra != FirstColor && DistinctSample < 2)
                DistinctSample = 2;
        }
    }

    char dir[MAX_PATH] = {0};
    GetTempPathA(sizeof(dir), dir);
    char path[MAX_PATH];
    sprintf(path, "%scxbx_fb%lu.bmp", dir, g_EmuNv2aScanoutDumpIndex);

    FILE *f = fopen(path, "wb");
    if(f != NULL)
    {
        ULONG DataSize = Width * Height * 4;
        unsigned char fh[14] = {0}, ih[40] = {0};
        ULONG fileSize = 54 + DataSize;
        fh[0] = 'B'; fh[1] = 'M';
        fh[2] = (unsigned char)fileSize;         fh[3] = (unsigned char)(fileSize >> 8);
        fh[4] = (unsigned char)(fileSize >> 16); fh[5] = (unsigned char)(fileSize >> 24);
        fh[10] = 54;
        ih[0] = 40;
        ih[4] = (unsigned char)Width; ih[5] = (unsigned char)(Width >> 8);
        ih[6] = (unsigned char)(Width >> 16); ih[7] = (unsigned char)(Width >> 24);
        LONG nh = -(LONG)Height; // negative height => top-down
        ih[8]  = (unsigned char)nh;         ih[9]  = (unsigned char)(nh >> 8);
        ih[10] = (unsigned char)(nh >> 16); ih[11] = (unsigned char)(nh >> 24);
        ih[12] = 1;
        ih[14] = 32;
        fwrite(fh, 1, 14, f);
        fwrite(ih, 1, 40, f);
        fwrite(Pixels, 1, DataSize, f);
        fclose(f);

        printf("Emu (0x%lX): NV2A scanout dumped %lux%lu pitch=%lu %s phys=0x%.08lX host=0x%.08lX first=0x%.08lX distinct>=2:%s -> %s\n",
               GetCurrentThreadId(), Width, Height, Pitch, SourceKind,
               PhysicalAddress, Host, FirstColor,
               DistinctSample >= 2 ? "yes" : "no", path);
        fflush(stdout);
        g_EmuNv2aScanoutDumpIndex++;
    }

    delete[] Pixels;
    delete[] Surface;
}

static bool EmuNv2aLoadDmaObject(ULONG *BaseAddress, ULONG *Limit)
{
    ULONG Instance = (EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_INSTANCE, 0) & 0x0000FFFF) << 4;

    if(Instance + 12 > EmuNv2aRaminSize)
        return false;

    ULONG Flags = EmuNv2aReadRamin32(EmuNv2aRaminBase + Instance);
    ULONG DmaLimit = EmuNv2aReadRamin32(EmuNv2aRaminBase + Instance + 4);
    ULONG Frame = EmuNv2aReadRamin32(EmuNv2aRaminBase + Instance + 8);
    ULONG Address = (Frame & 0x07FFFFFF) | (Flags & 0x00000FFF);

    if(BaseAddress != NULL)
        *BaseAddress = Address;

    if(Limit != NULL)
        *Limit = DmaLimit;

    return true;
}

static bool EmuNv2aReadDmaWord(ULONG BaseAddress, ULONG Offset, ULONG *Value)
{
    ULONG PhysicalAddress = BaseAddress + Offset;

    if(!EmuIsPhysicalMapAddress(PhysicalAddress))
        PhysicalAddress = EmuPhysicalMapBase + (PhysicalAddress & (EmuPhysicalMapEnd - EmuPhysicalMapBase));

    return EmuReadPhysicalMap(PhysicalAddress, 4, Value);
}

static ULONG g_EmuNv2aPusherRunCount = 0;
// True once the guest has submitted at least one pushbuffer, i.e. it finished
// render-state init and reached its frame loop. The vblank thread waits for this
// before firing the display ISR, so synthetic vblanks never race early GPU init.
extern "C" int EmuNv2aRenderStarted()
{
    return g_EmuNv2aPusherRunCount > 0 ? 1 : 0;
}

// Fetch a pushbuffer word. In host mode the pushbuffer lives in a host
// contiguous block and Address is an absolute host pointer; otherwise it is an
// offset into the DMA object read from the physical-map shadow (probe path).
static bool EmuNv2aFetchPushWord(bool HostMode, ULONG BaseAddress, ULONG Address, ULONG *Value)
{
    if(HostMode)
        return EmuTryReadHost(Address, Value, 4);

    return EmuNv2aReadDmaWord(BaseAddress, Address, Value);
}

static void EmuNv2aRunPusher()
{
    ULONG Push0 = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_PUSH0, 1);
    ULONG DmaPush = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_PUSH, 1);
    ULONG Get = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_GET, 0);
    ULONG Put = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_PUT, 0);

    if((Push0 & 1) == 0 || (DmaPush & 1) == 0)
        return;

    if(Get == Put)
        return;

    // If PUT points into a host contiguous block, the guest built the pushbuffer
    // in host memory and programmed the NV2A with raw host pointers. Read it
    // straight from host memory, anchoring GET at the block base (channel init is
    // stubbed, so GET arrives uninitialized). The physical-map path is preserved
    // for the self-checking probes, which submit through the shadow aperture.
    ULONG BlockBase = EmuContiguousBlockBase(Put, NULL);
    bool HostMode = BlockBase != 0;
    ULONG BaseAddress = 0;
    ULONG Limit = 0xFFFFFFFF;

    if(HostMode)
    {
        if(Get < BlockBase || Get >= Put)
            Get = BlockBase;
    }
    else if(!EmuNv2aLoadDmaObject(&BaseAddress, &Limit))
    {
        EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_GET, Put);
        return;
    }

    if(g_EmuNv2aPusherRunCount < 4 || (g_EmuNv2aPusherRunCount % 500) == 0)
    {
        printf("Emu (0x%lX): NV2A pusher run #%lu %s base=0x%.08lX GET=0x%.08lX PUT=0x%.08lX.\n",
               GetCurrentThreadId(), g_EmuNv2aPusherRunCount, HostMode ? "host" : "shadow",
               HostMode ? BlockBase : BaseAddress, Get, Put);
        fflush(stdout);
    }
    g_EmuNv2aPusherRunCount++;

    ULONG GuardLimit = HostMode ? 0x100000 : 4096;
    for(ULONG Guard = 0; Guard < GuardLimit && Get != Put; Guard++)
    {
        ULONG Word = 0;

        if(Get > Limit || !EmuNv2aFetchPushWord(HostMode, BaseAddress, Get, &Word))
        {
            Get = Put;
            break;
        }

        Get += 4;
        NV2A_TRACE_PB(Word);

        if((Word & 0xE0000003) == 0x20000000)
        {
            Get = Word & 0x1FFFFFFC;
            continue;
        }

        if((Word & 3) == 1 || (Word & 3) == 2)
        {
            Get = Word & 0xFFFFFFFC;
            continue;
        }

        if(Word == 0x00020000)
            continue;

        if((Word & 0xE0030003) == 0 || (Word & 0xE0030003) == 0x40000000)
        {
            bool Incrementing = (Word & 0xE0030003) == 0;
            ULONG Method = Word & 0x1FFC;
            ULONG Subchannel = (Word >> 13) & 0x07;
            ULONG Count = (Word >> 18) & 0x07FF;

            for(ULONG i = 0; i < Count && Get != Put; i++)
            {
                ULONG Data = 0;

                if(Get > Limit || !EmuNv2aFetchPushWord(HostMode, BaseAddress, Get, &Data))
                {
                    Get = Put;
                    break;
                }

                Get += 4;
                EmuNv2aHandlePgraphMethod(Subchannel, Method, Data);

                if(Incrementing)
                    Method += 4;
            }
        }
    }

    EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_GET, Get);

    if(Get == Put)
        EmuNv2aStoreRegister(EmuNv2aPfifoCache1Status,
                             EmuNv2aCachedRegister(EmuNv2aPfifoCache1Status, 0) | 0x10);
}

static bool EmuTryEmulatePhysicalMapAccess(LPEXCEPTION_POINTERS e)
{
    if(e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
       e->ExceptionRecord->NumberParameters < 2)
    {
        return false;
    }

    ULONG AccessType = (ULONG)e->ExceptionRecord->ExceptionInformation[0];
    ULONG FaultAddress = (ULONG)e->ExceptionRecord->ExceptionInformation[1];
    bool FaultIsPhysicalMap = EmuIsPhysicalMapAddress(FaultAddress);
    bool MayBePhysicalStoreFault = AccessType == 0 && FaultAddress == 0xFFFFFFFF;

    if(!FaultIsPhysicalMap && !MayBePhysicalStoreFault)
        return false;

    __try
    {
        BYTE *Instruction = (BYTE*)e->ContextRecord->Eip;

        if(AccessType == 1 && Instruction[0] == 0xF3 &&
           (Instruction[1] == 0xAB || Instruction[1] == 0xAA) &&
           FaultAddress == e->ContextRecord->Edi)
        {
            ULONG Count = e->ContextRecord->Ecx;
            ULONG Size = Instruction[1] == 0xAB ? 4 : 1;
            bool DirectionDown = (e->ContextRecord->EFlags & 0x400) != 0;

            if(!EmuWritePhysicalMapRepeated(FaultAddress, Count, Size, e->ContextRecord->Eax, DirectionDown))
                return false;

            ULONG TotalSize = Count * Size;
            e->ContextRecord->Ecx = 0;
            if(DirectionDown)
                e->ContextRecord->Edi -= TotalSize;
            else
                e->ContextRecord->Edi += TotalSize;
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated physical rep stos%c 0x%.08lX count 0x%.08lX value 0x%.08lX.\n",
                   GetCurrentThreadId(), Size == 4 ? 'd' : 'b', FaultAddress, Count, e->ContextRecord->Eax);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0xF3 &&
           (Instruction[1] == 0xA5 || Instruction[1] == 0xA4) &&
           FaultAddress == e->ContextRecord->Edi)
        {
            ULONG Count = e->ContextRecord->Ecx;
            ULONG Size = Instruction[1] == 0xA5 ? 4 : 1;
            ULONG SourceAddress = e->ContextRecord->Esi;
            bool DirectionDown = (e->ContextRecord->EFlags & 0x400) != 0;

            if(!EmuCopyToPhysicalMapRepeated(FaultAddress, SourceAddress, Count, Size, DirectionDown))
                return false;

            ULONG TotalSize = Count * Size;
            e->ContextRecord->Ecx = 0;
            if(DirectionDown)
            {
                e->ContextRecord->Edi -= TotalSize;
                e->ContextRecord->Esi -= TotalSize;
            }
            else
            {
                e->ContextRecord->Edi += TotalSize;
                e->ContextRecord->Esi += TotalSize;
            }
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated physical rep movs%c 0x%.08lX <- 0x%.08lX count 0x%.08lX.\n",
                   GetCurrentThreadId(), Size == 4 ? 'd' : 'b', FaultAddress, SourceAddress, Count);
            fflush(stdout);

            return true;
        }

        if((AccessType == 1 || MayBePhysicalStoreFault) && Instruction[0] == 0x0F && Instruction[1] == 0x2B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               ((FaultIsPhysicalMap && Address == FaultAddress) ||
                (MayBePhysicalStoreFault && EmuIsPhysicalMapAddress(Address))))
            {
                BYTE Value[16];
                ULONG RegisterIndex = (Instruction[2] >> 3) & 0x07;

                if(!EmuReadContextXmmRegister(e->ContextRecord, RegisterIndex, Value) ||
                   !EmuWritePhysicalMapBytes(Address, Value, sizeof(Value)))
                {
                    return false;
                }

                e->ContextRecord->Eip += 1 + OperandLength;

                if(g_EmuPhysicalMovntpsLogCount < 16)
                {
                    printf("Emu (0x%lX): Emulated physical movntps 0x%.08lX <- xmm%lu.\n",
                           GetCurrentThreadId(), Address, RegisterIndex);
                    fflush(stdout);
                }
                else if(g_EmuPhysicalMovntpsLogCount == 16)
                {
                    printf("Emu (0x%lX): Emulated physical movntps logging suppressed.\n",
                           GetCurrentThreadId());
                    fflush(stdout);
                }

                g_EmuPhysicalMovntpsLogCount++;

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0xA1 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            ULONG Value = 0;
            if(!EmuReadPhysicalMap(FaultAddress, 4, &Value))
                return false;

            e->ContextRecord->Eax = Value;
            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated physical read 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, Value);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = 0;
                if(!EmuReadPhysicalMap(FaultAddress, 4, &Value))
                    return false;

                EmuSetContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07, Value);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical read 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x8A)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = 0;
                if(!EmuReadPhysicalMap(FaultAddress, 1, &Value))
                    return false;

                EmuSetContextByteRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07, (BYTE)Value);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical byte read 0x%.08lX = 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x0F &&
           (Instruction[1] == 0xB6 || Instruction[1] == 0xB7))
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Size = Instruction[1] == 0xB6 ? 1 : 2;
                ULONG Value = 0;
                if(!EmuReadPhysicalMap(FaultAddress, Size, &Value))
                    return false;

                EmuSetContextRegister(e->ContextRecord, (Instruction[2] >> 3) & 0x07, Value);
                e->ContextRecord->Eip += 1 + OperandLength;

                printf("Emu (0x%lX): Emulated physical movzx %s read 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), Size == 1 ? "byte" : "word", FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0xF7 && (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = 0;
                ULONG Immediate = *(ULONG*)&Instruction[OperandLength];
                if(!EmuReadPhysicalMap(FaultAddress, 4, &Value))
                    return false;

                EmuSetTestFlags(e->ContextRecord, Value & Immediate, 0x80000000);
                e->ContextRecord->Eip += OperandLength + 4;

                printf("Emu (0x%lX): Emulated physical test 0x%.08lX & 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Immediate);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0xF6 && (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = 0;
                ULONG Immediate = Instruction[OperandLength];
                if(!EmuReadPhysicalMap(FaultAddress, 1, &Value))
                    return false;

                EmuSetTestFlags(e->ContextRecord, (Value & Immediate) & 0xFF, 0x80);
                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated physical byte test 0x%.08lX & 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Immediate);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0x80 &&
           ((Instruction[1] & 0x38) == 0x08 || (Instruction[1] & 0x38) == 0x20 ||
            (Instruction[1] & 0x38) == 0x30))
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = 0;
                ULONG Immediate = Instruction[OperandLength];
                ULONG Operation = Instruction[1] & 0x38;
                const char *OperationName = "or";

                if(!EmuReadPhysicalMap(FaultAddress, 1, &Value))
                    return false;

                if(Operation == 0x20)
                {
                    Value = (Value & Immediate) & 0xFF;
                    OperationName = "and";
                }
                else if(Operation == 0x30)
                {
                    Value = (Value ^ Immediate) & 0xFF;
                    OperationName = "xor";
                }
                else
                {
                    Value = (Value | Immediate) & 0xFF;
                }

                if(!EmuWritePhysicalMap(FaultAddress, 1, Value))
                    return false;

                EmuSetTestFlags(e->ContextRecord, Value, 0x80);
                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated physical byte %s 0x%.08lX with 0x%.02lX = 0x%.02lX.\n",
                       GetCurrentThreadId(), OperationName, FaultAddress, Immediate, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && (Instruction[0] == 0x83 || Instruction[0] == 0x81) &&
           ((Instruction[1] & 0x38) == 0x08 || (Instruction[1] & 0x38) == 0x20 ||
            (Instruction[1] & 0x38) == 0x30))
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = 0;
                ULONG Immediate = 0;
                ULONG ImmediateLength = 0;
                ULONG Operation = Instruction[1] & 0x38;
                const char *OperationName = "or";

                if(!EmuReadPhysicalMap(FaultAddress, 4, &Value))
                    return false;

                if(Instruction[0] == 0x83)
                {
                    Immediate = (ULONG)(LONG)(CHAR)Instruction[OperandLength];
                    ImmediateLength = 1;
                }
                else
                {
                    Immediate = *(ULONG*)&Instruction[OperandLength];
                    ImmediateLength = 4;
                }

                if(Operation == 0x20)
                {
                    Value &= Immediate;
                    OperationName = "and";
                }
                else if(Operation == 0x30)
                {
                    Value ^= Immediate;
                    OperationName = "xor";
                }
                else
                {
                    Value |= Immediate;
                }

                if(!EmuWritePhysicalMap(FaultAddress, 4, Value))
                    return false;

                EmuSetTestFlags(e->ContextRecord, Value, 0x80000000);
                e->ContextRecord->Eip += OperandLength + ImmediateLength;

                printf("Emu (0x%lX): Emulated physical %s 0x%.08lX with 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), OperationName, FaultAddress, Immediate, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x80 && (Instruction[1] & 0x38) == 0x38)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = 0;
                ULONG Right = Instruction[OperandLength];
                if(!EmuReadPhysicalMap(FaultAddress, 1, &Left))
                    return false;

                Left &= 0xFF;
                EmuSetSubtractFlags(e->ContextRecord, Left, Right, (Left - Right) & 0xFF, 0x80);
                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated physical byte compare 0x%.08lX with 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x38)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = 0;
                ULONG Right = EmuContextByteRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                if(!EmuReadPhysicalMap(FaultAddress, 1, &Left))
                    return false;

                Left &= 0xFF;
                EmuSetSubtractFlags(e->ContextRecord, Left, Right, (Left - Right) & 0xFF, 0x80);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical byte compare 0x%.08lX with 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x3A)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuContextByteRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                ULONG Right = 0;
                if(!EmuReadPhysicalMap(FaultAddress, 1, &Right))
                    return false;

                Right &= 0xFF;
                EmuSetSubtractFlags(e->ContextRecord, Left, Right, (Left - Right) & 0xFF, 0x80);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical byte compare 0x%.02lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), Left, FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x85)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuReadMmio(FaultAddress, 4);
                ULONG Right = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                EmuSetTestFlags(e->ContextRecord, Left & Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO test 0x%.08lX & 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x39)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = 0;
                ULONG Right = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                if(!EmuReadPhysicalMap(FaultAddress, 4, &Left))
                    return false;

                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x3B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                ULONG Right = 0;
                if(!EmuReadPhysicalMap(FaultAddress, 4, &Right))
                    return false;

                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), Left, FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        // 0x2B /r = sub r32, r/m32 : like the register compare above but the
        // difference is written back (ordinal - export->Base in the EvolutionX
        // kernel-export resolver).
        if(AccessType == 0 && Instruction[0] == 0x2B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                ULONG Right = 0;
                if(!EmuReadPhysicalMap(FaultAddress, 4, &Right))
                    return false;

                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                EmuSetContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07, Left - Right);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical subtract 0x%.08lX - [0x%.08lX].\n",
                       GetCurrentThreadId(), Left, FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x3B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                ULONG Right = EmuReadMmio(FaultAddress, 4);
                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), Left, FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x83 && (Instruction[1] & 0x38) == 0x38)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = 0;
                ULONG Right = (ULONG)(LONG)(CHAR)Instruction[OperandLength];
                if(!EmuReadPhysicalMap(FaultAddress, 4, &Left))
                    return false;

                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated physical compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        // 0x81 /7 = cmp r/m32, imm32 : same as 0x83 /7 but a full 32-bit immediate.
        // Titles scan low physical memory for a dword signature (e.g. `cmp [eax],
        // 'INIT'`) this way; without it the read faults unemulated.
        if(AccessType == 0 && Instruction[0] == 0x81 && (Instruction[1] & 0x38) == 0x38)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = 0;
                ULONG Right = *(ULONG*)&Instruction[OperandLength];
                if(!EmuReadPhysicalMap(FaultAddress, 4, &Left))
                    return false;

                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength + 4;

                printf("Emu (0x%lX): Emulated physical compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        // 0x66 0x81 /7 = cmp r/m16, imm16 : the word form of the compare above.
        // The EvolutionX dashboard checks the kernel image's DOS magic this way
        // (`cmp word ptr [ecx], 'MZ'` at 0x80010000).
        if(AccessType == 0 && Instruction[0] == 0x66 && Instruction[1] == 0x81 &&
           (Instruction[2] & 0x38) == 0x38)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = 0;
                ULONG Right = *(WORD*)&Instruction[1 + OperandLength];
                if(!EmuReadPhysicalMap(FaultAddress, 2, &Left))
                    return false;

                EmuSetSubtractFlags(e->ContextRecord, Left & 0xFFFF, Right, (Left & 0xFFFF) - Right, 0x8000);
                e->ContextRecord->Eip += 1 + OperandLength + 2;

                printf("Emu (0x%lX): Emulated physical word compare 0x%.08lX with 0x%.04lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0xA3 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            ULONG Value = e->ContextRecord->Eax;
            if(!EmuWritePhysicalMap(FaultAddress, 4, Value))
                return false;

            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated physical write 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, Value);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0x89)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                if(!EmuWritePhysicalMap(FaultAddress, 4, Value))
                    return false;

                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical write 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0x88)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuContextByteRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                if(!EmuWritePhysicalMap(FaultAddress, 1, Value))
                    return false;

                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated physical byte write 0x%.08lX = 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0xC7 && (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = *(ULONG*)&Instruction[OperandLength];
                if(!EmuWritePhysicalMap(FaultAddress, 4, Value))
                    return false;

                e->ContextRecord->Eip += OperandLength + 4;

                printf("Emu (0x%lX): Emulated physical write 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0x66 && Instruction[1] == 0xC7 &&
           (Instruction[2] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = *(USHORT*)&Instruction[1 + OperandLength];
                if(!EmuWritePhysicalMap(FaultAddress, 2, Value))
                    return false;

                e->ContextRecord->Eip += 1 + OperandLength + 2;

                printf("Emu (0x%lX): Emulated physical word write 0x%.08lX = 0x%.04lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0xC6 && (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = Instruction[OperandLength];
                if(!EmuWritePhysicalMap(FaultAddress, 1, Value))
                    return false;

                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated physical byte write 0x%.08lX = 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return false;
}

static bool EmuTryEmulatePortIo(LPEXCEPTION_POINTERS e)
{
    if(e->ExceptionRecord->ExceptionCode != EXCEPTION_PRIV_INSTRUCTION)
        return false;

    __try
    {
        BYTE *Instruction = (BYTE*)e->ContextRecord->Eip;
        ULONG Port = e->ContextRecord->Edx & 0xFFFF;

        switch(Instruction[0])
        {
            case 0xEC:
                EmuSetContextByteRegister(e->ContextRecord, 0, 0);
                e->ContextRecord->Eip += 1;
                printf("Emu (0x%lX): Emulated IN AL, DX port=0x%.04lX.\n", GetCurrentThreadId(), Port);
                fflush(stdout);
                return true;

            case 0xED:
                e->ContextRecord->Eax = 0;
                e->ContextRecord->Eip += 1;
                printf("Emu (0x%lX): Emulated IN EAX, DX port=0x%.04lX.\n", GetCurrentThreadId(), Port);
                fflush(stdout);
                return true;

            case 0xEE:
                e->ContextRecord->Eip += 1;
                printf("Emu (0x%lX): Emulated OUT DX, AL port=0x%.04lX value=0x%.02X.\n",
                       GetCurrentThreadId(), Port, EmuContextByteRegister(e->ContextRecord, 0));
                fflush(stdout);
                return true;

            case 0xEF:
                e->ContextRecord->Eip += 1;
                printf("Emu (0x%lX): Emulated OUT DX, EAX port=0x%.04lX value=0x%.08lX.\n",
                       GetCurrentThreadId(), Port, e->ContextRecord->Eax);
                fflush(stdout);
                return true;

            case 0xE4:
                EmuSetContextByteRegister(e->ContextRecord, 0, 0);
                e->ContextRecord->Eip += 2;
                printf("Emu (0x%lX): Emulated IN AL, 0x%.02X.\n", GetCurrentThreadId(), Instruction[1]);
                fflush(stdout);
                return true;

            case 0xE5:
                e->ContextRecord->Eax = 0;
                e->ContextRecord->Eip += 2;
                printf("Emu (0x%lX): Emulated IN EAX, 0x%.02X.\n", GetCurrentThreadId(), Instruction[1]);
                fflush(stdout);
                return true;

            case 0xE6:
                e->ContextRecord->Eip += 2;
                printf("Emu (0x%lX): Emulated OUT 0x%.02X, AL value=0x%.02X.\n",
                       GetCurrentThreadId(), Instruction[1], EmuContextByteRegister(e->ContextRecord, 0));
                fflush(stdout);
                return true;

            case 0xE7:
                e->ContextRecord->Eip += 2;
                printf("Emu (0x%lX): Emulated OUT 0x%.02X, EAX value=0x%.08lX.\n",
                       GetCurrentThreadId(), Instruction[1], e->ContextRecord->Eax);
                fflush(stdout);
                return true;

            // CLI / STI: the guest toggles the interrupt flag around kernel
            // patches (e.g. the soft-mod launcher's atomic xchg into kernel code).
            // The HLE has no real interrupt flag, so treat both as no-ops.
            case 0xFA:
                e->ContextRecord->Eip += 1;
                return true;

            case 0xFB:
                e->ContextRecord->Eip += 1;
                return true;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return false;
}

// The soft-mod launcher acquires ring 0 by patching a GDT code-segment descriptor
// (sgdt to find the GDT base, then `xchg` the 8-byte descriptor for selector 8)
// and far-jumping to it. A user-mode process can neither write the real GDT nor
// load a ring-0 selector -- and does not need to: this HLE services kernel calls
// at the export level, so the patch changes nothing. Emulate the faulting steps
// so the title proceeds: the `xchg` into the protected descriptor returns its
// current value and drops the write; the far jump keeps the flat user CS and just
// moves EIP to the target offset.
static bool EmuTryEmulateGdtPatch(LPEXCEPTION_POINTERS e)
{
    if(e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return false;

    __try
    {
        BYTE *Instruction = (BYTE*)e->ContextRecord->Eip;

        if(Instruction[0] == 0x87)   // xchg r/m32, r32
        {
            ULONG Address = 0, Length = 0;
            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &Length))
            {
                // The descriptor lives in the real (no-access) GDT, so neither read
                // nor write it: hand back 0 as the "old" descriptor and step over.
                ULONG Reg = (Instruction[1] >> 3) & 0x07;
                EmuSetContextRegister(e->ContextRecord, Reg, 0);
                e->ContextRecord->Eip += Length;
                printf("Emu (0x%lX): Emulated GDT-patch xchg [0x%.08lX] (skipped).\n",
                       GetCurrentThreadId(), Address);
                fflush(stdout);
                return true;
            }
        }

        if(Instruction[0] == 0xEA)   // ljmp ptr16:32
        {
            ULONG Target = *(ULONG*)&Instruction[1];
            e->ContextRecord->Eip = Target;
            printf("Emu (0x%lX): Emulated far jump to 0x%.08lX (flat CS).\n",
                   GetCurrentThreadId(), Target);
            fflush(stdout);
            return true;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return false;
}

static bool EmuTryEmulateMmioAccess(LPEXCEPTION_POINTERS e)
{
    if(e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
       e->ExceptionRecord->NumberParameters < 2)
    {
        return false;
    }

    ULONG AccessType = (ULONG)e->ExceptionRecord->ExceptionInformation[0];
    ULONG FaultAddress = (ULONG)e->ExceptionRecord->ExceptionInformation[1];
    if(!EmuIsMmioAddress(FaultAddress) && !EmuIsStubMmioAddress(FaultAddress))
        return false;

    __try
    {
        BYTE *Instruction = (BYTE*)e->ContextRecord->Eip;

        if(AccessType == 0 && Instruction[0] == 0xA1 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            e->ContextRecord->Eax = EmuReadMmio(FaultAddress, 4);
            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        // 0xA0 = mov AL, moffs8 : 8-bit read from an absolute address (used by the
        // VGA/CRTC access in nxdk pbkit, e.g. reading PRMCIO 0xFD6013D5).
        if(AccessType == 0 && Instruction[0] == 0xA0 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            ULONG Value = EmuReadMmio(FaultAddress, 1);
            e->ContextRecord->Eax = (e->ContextRecord->Eax & ~0xFFul) | (Value & 0xFF);
            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated MMIO byte read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        // 0x66 0xA1 = mov AX, moffs16 : 16-bit read from an absolute address (what
        // a C `uint16_t` volatile load of a fixed MMIO address compiles to, e.g.
        // reading an AC97 channel status register at 0xFEC00116).
        if(AccessType == 0 && Instruction[0] == 0x66 && Instruction[1] == 0xA1 &&
           *(ULONG*)&Instruction[2] == FaultAddress)
        {
            ULONG Value = EmuReadMmio(FaultAddress, 2);
            e->ContextRecord->Eax = (e->ContextRecord->Eax & ~0xFFFFul) | (Value & 0xFFFF);
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO word read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuReadMmio(FaultAddress, 4);
                EmuSetContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07, Value);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x66 && Instruction[1] == 0x8B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuReadMmio(FaultAddress, 2);
                EmuSetContextWordRegister(e->ContextRecord, (Instruction[2] >> 3) & 0x07, (WORD)Value);
                e->ContextRecord->Eip += 1 + OperandLength;

                printf("Emu (0x%lX): Emulated MMIO word read 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x01 &&
           e->ContextRecord->Ecx == FaultAddress)
        {
            e->ContextRecord->Eax = EmuReadMmio(FaultAddress, 4);
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x08 &&
           e->ContextRecord->Eax == FaultAddress)
        {
            e->ContextRecord->Ecx = EmuReadMmio(FaultAddress, 4);
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x00 &&
           e->ContextRecord->Eax == FaultAddress)
        {
            e->ContextRecord->Eax = EmuReadMmio(FaultAddress, 4);
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8A)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuReadMmio(FaultAddress, 1);
                EmuSetContextByteRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07, (BYTE)Value);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO byte read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x0F && Instruction[1] == 0xB6 &&
           (Instruction[2] & 0xC7) == 0x40 &&
           e->ContextRecord->Eax + (LONG)(CHAR)Instruction[3] == FaultAddress)
        {
            EmuSetContextRegister(e->ContextRecord, (Instruction[2] >> 3) & 0x07,
                                  EmuReadMmio(FaultAddress, 1));
            e->ContextRecord->Eip += 4;

            printf("Emu (0x%lX): Emulated MMIO byte read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x82 &&
           e->ContextRecord->Edx + *(ULONG*)&Instruction[2] == FaultAddress)
        {
            e->ContextRecord->Eax = EmuReadMmio(FaultAddress, 4);
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x92 &&
           e->ContextRecord->Edx + *(ULONG*)&Instruction[2] == FaultAddress)
        {
            e->ContextRecord->Edx = EmuReadMmio(FaultAddress, 4);
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x87 &&
           e->ContextRecord->Edi + *(ULONG*)&Instruction[2] == FaultAddress)
        {
            e->ContextRecord->Eax = EmuReadMmio(FaultAddress, 4);
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0xF7 && (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Immediate = *(ULONG*)&Instruction[OperandLength];
                ULONG Value = EmuReadMmio(FaultAddress, 4);
                EmuSetTestFlags(e->ContextRecord, Value & Immediate, 0x80000000);
                e->ContextRecord->Eip += OperandLength + 4;

                printf("Emu (0x%lX): Emulated MMIO test 0x%.08lX & 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Immediate);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0xF6 && (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Immediate = Instruction[OperandLength];
                ULONG Value = EmuReadMmio(FaultAddress, 1);
                EmuSetTestFlags(e->ContextRecord, (Value & Immediate) & 0xFF, 0x80);
                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated MMIO byte test 0x%.08lX & 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Immediate);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x85)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuReadMmio(FaultAddress, 4);
                ULONG Right = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                EmuSetTestFlags(e->ContextRecord, Left & Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO test 0x%.08lX & 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x39)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuReadMmio(FaultAddress, 4);
                ULONG Right = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x3B)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                ULONG Right = EmuReadMmio(FaultAddress, 4);
                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), Left, FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x83 && (Instruction[1] & 0x38) == 0x38)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Left = EmuReadMmio(FaultAddress, 4);
                ULONG Right = (ULONG)(LONG)(CHAR)Instruction[OperandLength];
                EmuSetSubtractFlags(e->ContextRecord, Left, Right, Left - Right, 0x80000000);
                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated MMIO compare 0x%.08lX with 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Right);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0xA3 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            EmuWriteMmio(FaultAddress, 4, e->ContextRecord->Eax);
            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, e->ContextRecord->Eax);
            fflush(stdout);

            return true;
        }

        // 0xA2 = mov moffs8, AL : 8-bit store to an absolute address (used by the
        // video-DAC / gamma-ramp writes, e.g. XVideoSetGammaRamp to PRMDIO).
        if(AccessType == 1 && Instruction[0] == 0xA2 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            EmuWriteMmio(FaultAddress, 1, e->ContextRecord->Eax & 0xFF);
            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated MMIO byte write 0x%.08lX = 0x%.02lX.\n",
                   GetCurrentThreadId(), FaultAddress, e->ContextRecord->Eax & 0xFF);
            fflush(stdout);

            return true;
        }

        // 0x66 0xA3 = mov moffs16, AX : 16-bit store to an absolute address (what a
        // C `uint16_t` volatile store of a register value to a fixed MMIO address
        // compiles to; the write counterpart of the 0x66 0xA1 read above).
        if(AccessType == 1 && Instruction[0] == 0x66 && Instruction[1] == 0xA3 &&
           *(ULONG*)&Instruction[2] == FaultAddress)
        {
            ULONG Value = e->ContextRecord->Eax & 0xFFFF;
            EmuWriteMmio(FaultAddress, 2, Value);
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO word write 0x%.08lX = 0x%.04lX.\n",
                   GetCurrentThreadId(), FaultAddress, Value);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0x89 && Instruction[1] == 0x01 &&
           e->ContextRecord->Ecx == FaultAddress)
        {
            EmuWriteMmio(FaultAddress, 4, e->ContextRecord->Eax);
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, e->ContextRecord->Eax);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0xC7 && Instruction[1] == 0x01 &&
           e->ContextRecord->Ecx == FaultAddress)
        {
            ULONG Value = *(ULONG*)&Instruction[2];
            EmuWriteMmio(FaultAddress, 4, Value);
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, Value);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0x89)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                EmuWriteMmio(FaultAddress, 4, Value);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0x66 && Instruction[1] == 0x89)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuContextWordRegister(e->ContextRecord, (Instruction[2] >> 3) & 0x07);
                EmuWriteMmio(FaultAddress, 2, Value);
                e->ContextRecord->Eip += 1 + OperandLength;

                printf("Emu (0x%lX): Emulated MMIO word write 0x%.08lX = 0x%.04lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0x88)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = EmuContextByteRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07);
                EmuWriteMmio(FaultAddress, 1, Value);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO byte write 0x%.08lX = 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0x66 && Instruction[1] == 0xC7 &&
           (Instruction[2] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = *(WORD*)&Instruction[1 + OperandLength];
                EmuWriteMmio(FaultAddress, 2, Value);
                e->ContextRecord->Eip += 1 + OperandLength + 2;

                printf("Emu (0x%lX): Emulated MMIO word write 0x%.08lX = 0x%.04lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0xC7 &&
           (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = *(ULONG*)&Instruction[OperandLength];
                EmuWriteMmio(FaultAddress, 4, Value);
                e->ContextRecord->Eip += OperandLength + 4;

                printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0xC6 &&
           (Instruction[1] & 0x38) == 0)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Value = Instruction[OperandLength];
                EmuWriteMmio(FaultAddress, 1, Value);
                e->ContextRecord->Eip += OperandLength + 1;

                printf("Emu (0x%lX): Emulated MMIO byte write 0x%.08lX = 0x%.02lX.\n",
                       GetCurrentThreadId(), FaultAddress, Value);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 1 && Instruction[0] == 0xC7 && Instruction[1] == 0x05 &&
           *(ULONG*)&Instruction[2] == FaultAddress)
        {
            ULONG Value = *(ULONG*)&Instruction[6];
            EmuWriteMmio(FaultAddress, 4, Value);
            e->ContextRecord->Eip += 10;

            printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, Value);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0xC7 && Instruction[1] == 0x87 &&
           e->ContextRecord->Edi + *(ULONG*)&Instruction[2] == FaultAddress)
        {
            ULONG Value = *(ULONG*)&Instruction[6];
            EmuWriteMmio(FaultAddress, 4, Value);
            e->ContextRecord->Eip += 10;

            printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, Value);
            fflush(stdout);

            return true;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return false;
}

// Heuristic: does Address look like a real return address, i.e. is it preceded
// by a CALL instruction? Used to recover from calls/tail-jumps through NULL
// function pointers by finding where control should resume. Recognises call
// rel32 (E8) and the call r/m32 forms (FF /2), where the ModR/M reg field == 2.
static bool EmuLooksLikeReturnAddress(ULONG Address)
{
    if(Address < 0x00010000 || Address >= 0x10000000)
        return false;

    __try
    {
        const BYTE *p = (const BYTE*)Address;
        if(p[-5] == 0xE8)                                  return true; // call rel32
        if(p[-6] == 0xFF && (p[-5] & 0x38) == 0x10)        return true; // call r/m32, disp32
        if(p[-3] == 0xFF && (p[-2] & 0x38) == 0x10)        return true; // call r/m32, disp8
        if(p[-2] == 0xFF && (p[-1] & 0x38) == 0x10)        return true; // call r/m32 / call reg
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return false;
}

static LONG WINAPI EmuVectoredExceptionHandler(LPEXCEPTION_POINTERS e)
{
    bool WasXboxFS = EmuIsXboxFS();

    if(WasXboxFS)
        EmuSwapFS();

    if(EmuTryEmulateRdmsr(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(EmuTryEmulatePortIo(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(EmuTryEmulatePhysicalMapAccess(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(EmuTryEmulateMmioAccess(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(EmuTryEmulateGdtPatch(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    printf("Emu (0x%lX): Vectored exception [0x%.08lX]@0x%.08lX\n",
           GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);
    printf("Emu (0x%lX): Vectored ESP=0x%.08lX EBP=0x%.08lX\n",
           GetCurrentThreadId(), e->ContextRecord->Esp, e->ContextRecord->Ebp);
    if(e->ExceptionRecord->NumberParameters >= 2)
    {
        printf("Emu (0x%lX): Vectored access type=%lu address=0x%.08lX\n",
               GetCurrentThreadId(), e->ExceptionRecord->ExceptionInformation[0],
               e->ExceptionRecord->ExceptionInformation[1]);
    }

    // A near-null EIP means the guest executed a call/jump through a NULL or
    // uninitialised function pointer (e.g. an un-HLE'd XDK callback slot). If the
    // top of stack holds a plausible guest return address, this was a `call
    // [null]`; simulate the missing callee as a no-op that returns (pop the
    // return address, resume there) so a stubbed callback doesn't kill the title.
    // This is what lets the synthesized display ISR survive callback slots the
    // native XDK left for a driver we don't fully model.
    if(e->ContextRecord->Eip < 0x00010000)
    {
        // Scan the top of stack for the first genuine return address (validated
        // by a preceding CALL). A `call [null]` leaves it at [ESP]; a `jmp [null]`
        // tail call leaves callee args at [ESP..] with the return address a few
        // slots deeper. Resuming there (and discarding the intervening args)
        // makes the missing callee behave like a no-op that returned 0.
        ULONG ResumeAddress = 0;
        ULONG ResumeEsp = 0;
        for(ULONG Slot = 0; Slot < 8; Slot++)
        {
            ULONG SlotAddr = e->ContextRecord->Esp + Slot * 4;
            ULONG Candidate = 0;
            __try
            {
                Candidate = *(ULONG*)SlotAddr;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                break;
            }

            if(EmuLooksLikeReturnAddress(Candidate))
            {
                ResumeAddress = Candidate;
                ResumeEsp = SlotAddr + 4;
                break;
            }
        }

        if(ResumeAddress != 0)
        {
            printf("Emu (0x%lX): near-null call recovered -- resuming at 0x%.08lX, ESP 0x%.08lX->0x%.08lX (stubbed callback).\n",
                   GetCurrentThreadId(), ResumeAddress, e->ContextRecord->Esp, ResumeEsp);
            fflush(stdout);
            e->ContextRecord->Eip = ResumeAddress;
            e->ContextRecord->Esp = ResumeEsp;
            e->ContextRecord->Eax = 0;   // callback "returned" 0/void
            if(WasXboxFS)
                EmuSwapFS();
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        printf("Emu (0x%lX): Vectored EIP is near-null (0x%.08lX) -- guest called/jumped "
               "through a NULL or uninitialised function pointer.\n",
               GetCurrentThreadId(), e->ContextRecord->Eip);
    }
    else __try
    {
        BYTE *Instruction = (BYTE*)e->ContextRecord->Eip;
        printf("Emu (0x%lX): Vectored bytes: 0x%.02X 0x%.02X 0x%.02X 0x%.02X 0x%.02X 0x%.02X 0x%.02X 0x%.02X\n",
               GetCurrentThreadId(), Instruction[0], Instruction[1], Instruction[2], Instruction[3],
               Instruction[4], Instruction[5], Instruction[6], Instruction[7]);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        printf("Emu (0x%lX): Vectored bytes unavailable.\n", GetCurrentThreadId());
    }

    __try
    {
        DWORD *Stack = (DWORD*)e->ContextRecord->Esp;

        printf("Emu (0x%lX): Vectored stack: 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX\n",
               GetCurrentThreadId(), Stack[0], Stack[1], Stack[2], Stack[3], Stack[4], Stack[5], Stack[6], Stack[7]);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        printf("Emu (0x%lX): Vectored stack unavailable.\n", GetCurrentThreadId());
    }

    fflush(stdout);

    if(WasXboxFS)
        EmuSwapFS();

    return EXCEPTION_CONTINUE_SEARCH;
}

static PVOID g_hEmuVectoredExceptionHandler = NULL;

// ******************************************************************
// * func: DllMain
// ******************************************************************
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if(fdwReason == DLL_PROCESS_ATTACH)
    {
#ifdef _DEBUG
        _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
        EmuConfigureLogFile();
        printf("--- cxbx runtime attach ---\n");
        EmuShared::Init();
    }
    
    if(fdwReason == DLL_PROCESS_DETACH)
    {
        if(g_hEmuVectoredExceptionHandler != NULL)
        {
            RemoveVectoredExceptionHandler(g_hEmuVectoredExceptionHandler);
            g_hEmuVectoredExceptionHandler = NULL;
        }

        EmuShared::Cleanup();
    }

    return TRUE;
}

// ******************************************************************
// * func: EmuNoFunc
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuNoFunc()
{
    EmuConfigureLogFile();

    EmuSwapFS();   // Win2k/XP FS

    printf("Emu (0x%X): EmuNoFunc()\n", GetCurrentThreadId());

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuVerifyVersion
// ******************************************************************
extern "C" CXBXKRNL_API bool NTAPI EmuVerifyVersion(const char *szVersion)
{
    if(strcmp(szVersion, _CXBX_VERSION) != 0)
        return false;

    return true;
}

// ******************************************************************
// * func: EmuCleanThread
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuCleanThread()
{
    if(EmuIsXboxFS())
        EmuSwapFS();    // Win2k/XP FS

    EmuCleanupFS();

    TerminateThread(GetCurrentThread(), 0);
}

// ******************************************************************
// * func: EmuInit
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuInit
(
    void                   *pTLSData, 
    Xbe::TLS               *pTLS,
    Xbe::LibraryVersion    *pLibraryVersion,
    DebugMode               DbgMode,
    char                   *szDebugFilename,
    Xbe::Header            *pXbeHeader,
    uint32                  dwXbeHeaderSize,
    void                  (*Entry)())
{
    g_pTLS       = pTLS;
    g_pTLSData   = pTLSData;
	g_pXbeHeader = pXbeHeader;

	// For Unicode Conversions
	setlocale(LC_ALL, "English");

    // ******************************************************************
    // * debug console allocation (if configured)
    // ******************************************************************
    if(DbgMode == DM_CONSOLE)
    {
        if(AllocConsole())
        {
            freopen("CONOUT$", "wt", stdout);

            SetConsoleTitle("cxbx : Kernel Debug Console");

            SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
            
            printf("Emu (0x%X): Debug console allocated (DM_CONSOLE).\n", GetCurrentThreadId());
        }
    }
    else if(DbgMode == DM_FILE)
    {
        FreeConsole();

        freopen(szDebugFilename, "a", stdout);

        printf("Emu (0x%X): Debug console allocated (DM_FILE).\n", GetCurrentThreadId());
    }
    else
    {
        FreeConsole();

        char buffer[16];

        if(GetConsoleTitle(buffer, 16) != NULL)
            freopen("nul", "w", stdout);
    }

    EmuConfigureLogFile();
    printf("--- cxbx runtime start ---\n");

    if(g_hEmuVectoredExceptionHandler == NULL)
        g_hEmuVectoredExceptionHandler = AddVectoredExceptionHandler(1, EmuVectoredExceptionHandler);

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    {
        #ifdef _DEBUG_TRACE
        printf("Emu (0x%X): Debug Trace Enabled.\n", GetCurrentThreadId());

        printf("Emu (0x%X): EmuInit\n"
               "(\n"
               "   pTLSData            : 0x%.08X\n"
               "   pTLS                : 0x%.08X\n"
               "   pLibraryVersion     : 0x%.08X\n"
               "   DebugConsole        : 0x%.08X\n"
               "   DebugFilename       : \"%s\"\n"
               "   pXBEHeader          : 0x%.08X\n"
               "   pXBEHeaderSize      : 0x%.08X\n"
               "   Entry               : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pTLSData, pTLS, pLibraryVersion, DbgMode, szDebugFilename, pXbeHeader, dwXbeHeaderSize, Entry);

        #else
        printf("Emu (0x%X): Debug Trace Disabled.\n", GetCurrentThreadId());
        #endif
    }

    // ******************************************************************
    // * Load the necessary pieces of XBEHeader
    // ******************************************************************
    {
        Xbe::Header *MemXbeHeader = (Xbe::Header*)0x00010000;

        uint32 old_protection = 0;

        VirtualProtect(MemXbeHeader, 0x1000, PAGE_READWRITE, &old_protection);

        // we sure hope we aren't corrupting anything necessary for an .exe to survive :]
        MemXbeHeader->dwSizeofHeaders   = pXbeHeader->dwSizeofHeaders;
        MemXbeHeader->dwCertificateAddr = pXbeHeader->dwCertificateAddr;
        MemXbeHeader->dwPeHeapReserve   = pXbeHeader->dwPeHeapReserve;
        MemXbeHeader->dwPeHeapCommit    = pXbeHeader->dwPeHeapCommit;

        memcpy(&MemXbeHeader->dwInitFlags, &pXbeHeader->dwInitFlags, sizeof(pXbeHeader->dwInitFlags));

        memcpy((void*)pXbeHeader->dwCertificateAddr, &((uint08*)pXbeHeader)[pXbeHeader->dwCertificateAddr - 0x00010000], sizeof(Xbe::Certificate));
    }

    // ******************************************************************
	// * Initialize current directory
    // ******************************************************************
	{
		char szBuffer[260];

        g_EmuShared->GetXbePath(szBuffer);

        SetCurrentDirectory(szBuffer);

		g_hCurDir = CreateFile(szBuffer, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

        if(g_hCurDir == INVALID_HANDLE_VALUE)
			EmuCleanup("Could not map D:\\\n");
	}

    // ******************************************************************
	// * Initialize T:\ and U:\ directories
    // ******************************************************************
    {
		char szBuffer[260];

        #ifdef _DEBUG
        GetModuleFileName(GetModuleHandle("cxbxkrnl.dll"), szBuffer, 260);
        #else
        GetModuleFileName(GetModuleHandle("cxbx.dll"), szBuffer, 260);
        #endif

        sint32 spot=-1;
        for(int v=0;v<260;v++)
        {
            if(szBuffer[v] == '\\')
                spot = v;
            else if(szBuffer[v] == '\0')
                break;
        }

        if(spot != -1)
            szBuffer[spot] = '\0';

        Xbe::Certificate *pCertificate = (Xbe::Certificate*)pXbeHeader->dwCertificateAddr;

        // Create TData Directory
        {
            strcpy(&szBuffer[spot], "\\TDATA");

            CreateDirectory(szBuffer, NULL);

            sprintf(&szBuffer[spot+6], "\\%08x", pCertificate->dwTitleId);

            CreateDirectory(szBuffer, NULL);

            g_hTDrive = CreateFile(szBuffer, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

            if(g_hTDrive == INVALID_HANDLE_VALUE)
                EmuCleanup("Could not map T:\\\n");
        }

        // Create UData Directory
        {
            strcpy(&szBuffer[spot], "\\UDATA");

            CreateDirectory(szBuffer, NULL);

            sprintf(&szBuffer[spot+6], "\\%08x", pCertificate->dwTitleId);

            CreateDirectory(szBuffer, NULL);

            g_hUDrive = CreateFile(szBuffer, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

            if(g_hUDrive == INVALID_HANDLE_VALUE)
                EmuCleanup("Could not map U:\\\n");
        }

        // Create ZData Directory
        {
            strcpy(&szBuffer[spot], "\\CxbxCache");

            CreateDirectory(szBuffer, NULL);

            //* is it necessary to make this directory title unique?
            sprintf(&szBuffer[spot+10], "\\%08x", pCertificate->dwTitleId);

            CreateDirectory(szBuffer, NULL);
            //*/

            g_hZDrive = CreateFile(szBuffer, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

            if(g_hZDrive == INVALID_HANDLE_VALUE)
                EmuCleanup("Could not map Z:\\\n");
        }
    }

    // ******************************************************************
    // * Initialize OpenXDK emulation
    // ******************************************************************
    if(pLibraryVersion == 0)
        printf("Emu (0x%X): Detected OpenXDK application...\n", GetCurrentThreadId());

    // ******************************************************************
    // * Initialize Microsoft XDK emulation
    // ******************************************************************
    if(pLibraryVersion != 0)
    {
        printf("Emu (0x%X): Detected Microsoft XDK application...\n", GetCurrentThreadId());

        uint32 dwLibraryVersions = pXbeHeader->dwLibraryVersions;
        uint32 dwHLEEntries      = HLEDataBaseSize/sizeof(HLEData);

        uint32 LastUnResolvedXRefs = UnResolvedXRefs+1;
        uint32 OrigUnResolvedXRefs = UnResolvedXRefs;

        for(int p=0;UnResolvedXRefs < LastUnResolvedXRefs;p++)
        {
            printf("Emu (0x%X): Beginning HLE Pass %d...\n", GetCurrentThreadId(), p);

            LastUnResolvedXRefs = UnResolvedXRefs;

            bool bFoundD3D = false;
            for(uint32 v=0;v<dwLibraryVersions;v++)
            {
                uint16 MajorVersion = pLibraryVersion[v].wMajorVersion;
                uint16 MinorVersion = pLibraryVersion[v].wMinorVersion;
                uint16 BuildVersion = pLibraryVersion[v].wBuildVersion;

                char szLibraryName[9] = {0};

                for(uint32 c=0;c<8;c++)
                    szLibraryName[c] = pLibraryVersion[v].szName[c];

                printf("Emu (0x%X): Locating HLE Information for %s %d.%d.%d...", GetCurrentThreadId(), szLibraryName, MajorVersion, MinorVersion, BuildVersion);

                // TODO: HACK: These libraries are packed into one database
                if(strcmp(szLibraryName, "D3DX8") == 0)
                    strcpy(szLibraryName, "D3D8");

                if(strcmp(szLibraryName, "D3D8") == 0)
                {
                    if(bFoundD3D)
                    {
                        printf("Redundant\n");
                        continue;
                    }

                    bFoundD3D = true;
                }

                bool found=false;

                for(uint32 d=0;d<dwHLEEntries;d++)
                {
                    if(BuildVersion != HLEDataBase[d].BuildVersion || MinorVersion != HLEDataBase[d].MinorVersion || MajorVersion != HLEDataBase[d].MajorVersion || strcmp(szLibraryName, HLEDataBase[d].Library) != 0)
                        continue;

                    found = true;

                    printf("Found\n");

                    EmuInstallWrappers(HLEDataBase[d].OovpaTable, HLEDataBase[d].OovpaTableSize, Entry, pXbeHeader);
                }

                if(!found)
                    printf("Skipped\n");

                if(bXRefFirstPass)
                {
                    if(strcmp("XAPILIB", szLibraryName) == 0 && MajorVersion == 1 && MinorVersion == 0 && (BuildVersion == 3911 || BuildVersion == 4034 || BuildVersion == 4134 || BuildVersion == 4361 || BuildVersion == 4627))
                    {
                        uint32 lower = pXbeHeader->dwBaseAddr;
                        uint32 upper = pXbeHeader->dwBaseAddr + pXbeHeader->dwSizeofImage;

				        // ******************************************************************
				        // * Locate XapiProcessHeap
				        // ******************************************************************
                        {
                            void *pFunc = 0;

                            if(BuildVersion >= 4361)
					            pFunc = EmuLocateFunction((OOVPA*)&XapiInitProcess_1_0_4361, lower, upper);
                            else // 3911, 4034, 4134
                                pFunc = EmuLocateFunction((OOVPA*)&XapiInitProcess_1_0_3911, lower, upper);

					        if(pFunc != 0)
					        {
						        XTL::EmuXapiProcessHeap = *(PVOID**)((uint32)pFunc + 0x3E);

						        XTL::g_pRtlCreateHeap = *(XTL::pfRtlCreateHeap*)((uint32)pFunc + 0x37);
						        XTL::g_pRtlCreateHeap = (XTL::pfRtlCreateHeap)((uint32)pFunc + (uint32)XTL::g_pRtlCreateHeap + 0x37 + 0x04);

						        printf("Emu (0x%X): 0x%.08X -> EmuXapiProcessHeap\n", GetCurrentThreadId(), XTL::EmuXapiProcessHeap);
						        printf("Emu (0x%X): 0x%.08X -> RtlCreateHeap\n", GetCurrentThreadId(), XTL::g_pRtlCreateHeap);
					        }
				        }
                    }
			        else if(strcmp("D3D8", szLibraryName) == 0 && MajorVersion == 1 && MinorVersion == 0 && (BuildVersion == 4134 || BuildVersion == 4361 || BuildVersion == 4627))
			        {
                        uint32 lower = pXbeHeader->dwBaseAddr;
                        uint32 upper = pXbeHeader->dwBaseAddr + pXbeHeader->dwSizeofImage;

				        void *pFunc = EmuLocateFunction((OOVPA*)&IDirect3DDevice8_SetRenderState_CullMode_1_0_4134, lower, upper);

                        // ******************************************************************
				        // * Locate D3DDeferredRenderState
				        // ******************************************************************
                        if(pFunc != 0 && (BuildVersion == 4134 || BuildVersion == 4361 || BuildVersion == 4627))
                        {
                            if(BuildVersion == 4134)
                                XTL::EmuD3DDeferredRenderState = (DWORD*)(*(DWORD*)((uint32)pFunc + 0x2B) - 0x248 + 82*4);  // TODO: Verify
                            else if(BuildVersion == 4361)
						        XTL::EmuD3DDeferredRenderState = (DWORD*)(*(DWORD*)((uint32)pFunc + 0x2B) - 0x200 + 82*4);
                            else if(BuildVersion == 4627)
						        XTL::EmuD3DDeferredRenderState = (DWORD*)(*(DWORD*)((uint32)pFunc + 0x2B) - 0x24C + 92*4);

                            for(int v=0;v<146;v++)
                                XTL::EmuD3DDeferredRenderState[v] = X_D3DRS_UNK;

                            printf("Emu (0x%X): 0x%.08X -> EmuD3DDeferredRenderState\n", GetCurrentThreadId(), XTL::EmuD3DDeferredRenderState);
                        }
                        else
                        {
                            XTL::EmuD3DDeferredRenderState = 0;
                            EmuWarning("EmuD3DDeferredRenderState was not found!");
                        }

                        // ******************************************************************
				        // * Locate D3DDeferredTextureState
				        // ******************************************************************
                        {
                            if(BuildVersion == 4134)
                                pFunc = EmuLocateFunction((OOVPA*)&IDirect3DDevice8_SetTextureState_TexCoordIndex_1_0_4134, lower, upper);
                            else if(BuildVersion == 4361)
                                pFunc = EmuLocateFunction((OOVPA*)&IDirect3DDevice8_SetTextureState_TexCoordIndex_1_0_4361, lower, upper);
                            else if(BuildVersion == 4627)
                                pFunc = EmuLocateFunction((OOVPA*)&IDirect3DDevice8_SetTextureState_TexCoordIndex_1_0_4627, lower, upper);

                            if(pFunc != 0)
                            {
                                if(BuildVersion == 4134)
					                XTL::EmuD3DDeferredTextureState = (DWORD*)(*(DWORD*)((uint32)pFunc + 0x18) - 0x70);
                                else
					                XTL::EmuD3DDeferredTextureState = (DWORD*)(*(DWORD*)((uint32)pFunc + 0x19) - 0x70);

                                for(int v=0;v<32*4;v++)
                                    XTL::EmuD3DDeferredTextureState[v] = X_D3DTSS_UNK;

                                printf("Emu (0x%X): 0x%.08X -> EmuD3DDeferredTextureState\n", GetCurrentThreadId(), XTL::EmuD3DDeferredTextureState);
                            }
                            else
                            {
                                XTL::EmuD3DDeferredTextureState = 0;
                                EmuWarning("EmuD3DDeferredTextureState was not found!");
                            }
                        }
			        }
                }
            }

            bXRefFirstPass = false;
        }

        // ******************************************************************
        // * Display XRef Summary
        // ******************************************************************
        printf("Emu (0x%X): Resolved %d cross reference(s)\n", GetCurrentThreadId(), OrigUnResolvedXRefs - UnResolvedXRefs);

        EmuInstallNestopiaX13Bootstrap(pXbeHeader);
        EmuInstallFceultraBootstrap(pXbeHeader);
        EmuInstallAutoBootLaunchData();
    }

    // The fake kernel PE image at 0x80010000 is title-agnostic: OpenXDK-style
    // titles that link no XDK libraries (e.g. the EvolutionX dashboard) parse
    // the kernel image at startup just like XDK soft-mod titles do.
    EmuInstallFakeKernelImage();

	// ******************************************************************
    // * Initialize FS Emulation
    // ******************************************************************
    {
        printf("Emu (0x%X): Initializing FS emulation.\n", GetCurrentThreadId());

        EmuInitFS();

        printf("Emu (0x%X): Generating initial FS state.\n", GetCurrentThreadId());

        EmuGenerateFS(pTLS, pTLSData);

        printf("Emu (0x%X): FS emulation initialized.\n", GetCurrentThreadId());
    }

    printf("Emu (0x%X): Initializing Direct3D.\n", GetCurrentThreadId());

    XTL::EmuD3DInit(pXbeHeader, dwXbeHeaderSize);

    printf("Emu (0x%X): Initial thread starting.\n", GetCurrentThreadId());

    // ******************************************************************
    // * Entry Point
    // ******************************************************************
    __try
    {
        EmuSwapFS();   // XBox FS

        // _USE_XGMATH Disabled in mesh :[
        // halo : dword_0_2E2D18
        //_asm int 3

        Entry();

        EmuSwapFS();   // Win2k/XP FS
    }
    __except(EmuException(GetExceptionInformation()))
    {
        printf("Emu: WARNING!! Problem with ExceptionFilter\n");
    }

    printf("Emu (0x%X): Initial thread ended.\n", GetCurrentThreadId());

    fflush(stdout);

    EmuCleanThread();

    return;
}

// ******************************************************************
// * func: EmuWarning
// ******************************************************************
#ifdef _DEBUG_WARNINGS
extern "C" CXBXKRNL_API void NTAPI EmuWarning(const char *szWarningMessage, ...)
{
    if(szWarningMessage == NULL)
        return;

    char szBuffer1[255];
    char szBuffer2[255];

    va_list argp;

    sprintf(szBuffer1, "Emu (0x%X): *WARNING* -> ", GetCurrentThreadId());

    va_start(argp, szWarningMessage);

    vsprintf(szBuffer2, szWarningMessage, argp);

    va_end(argp);

    strcat(szBuffer1, szBuffer2);

    printf("%s\n", szBuffer1);

    fflush(stdout);

    return;
}
#endif

// ******************************************************************
// * func: EmuCleanup
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuCleanup(const char *szErrorMessage, ...)
{
    // ******************************************************************
    // * Print out ErrorMessage (if exists)
    // ******************************************************************
    if(szErrorMessage != NULL)
    {
        char szBuffer1[255];
        char szBuffer2[255];

        va_list argp;

        sprintf(szBuffer1, "Emu (0x%X): Recieved Fatal Message -> \n\n", GetCurrentThreadId());

        va_start(argp, szErrorMessage);

        vsprintf(szBuffer2, szErrorMessage, argp);

        va_end(argp);

        strcat(szBuffer1, szBuffer2);

        printf("%s\n", szBuffer1);

        char szLogFile[260];

        if(!EmuGetLogFile(szLogFile, sizeof(szLogFile)))
            MessageBox(NULL, szBuffer1, "cxbxkrnl", MB_OK | MB_ICONEXCLAMATION);
    }

    printf("cxbxkrnl: Terminating Process\n");
    fflush(stdout);

    // ******************************************************************
    // * Cleanup debug output
    // ******************************************************************
    {
        FreeConsole();

        char buffer[16];

        if(GetConsoleTitle(buffer, 16) != NULL)
            freopen("nul", "w", stdout);
    }

    TerminateProcess(GetCurrentProcess(), 0);

    return;
}

// ******************************************************************
// * func: EmuPanic
// ******************************************************************
extern "C" CXBXKRNL_API void NTAPI EmuPanic()
{
    if(EmuIsXboxFS())
        EmuSwapFS();   // Win2k/XP FS

    printf("Emu (0x%X): EmuPanic()\n", GetCurrentThreadId());

    EmuCleanup("Kernel Panic!");

    EmuSwapFS();   // XBox FS
}

// ******************************************************************
// * func: EmuInstallWrapper
// ******************************************************************
inline void EmuInstallWrapper(void *FunctionAddr, void *WrapperAddr)
{
    uint08 *FuncBytes = (uint08*)FunctionAddr;

    *(uint08*)&FuncBytes[0] = 0xE9;
    *(uint32*)&FuncBytes[1] = (uint32)WrapperAddr - (uint32)FunctionAddr - 5;
}

static const uint32 EmuNestopiaX13DSoundApuHeapUsedPointer = 0x00197048;
static const uint32 EmuNestopiaX13DSoundApuHeapCommittedPointer = 0x0019704C;
static const uint32 EmuNestopiaX13DSoundApuHeapUsedCounter = 0x001968DC;
static const uint32 EmuNestopiaX13DSoundApuHeapCommittedCounter = 0x001968D8;

static void EmuInstallNestopiaX13DSoundCounters(bool ResetCounters)
{
    if(ResetCounters)
    {
        *(uint32*)EmuNestopiaX13DSoundApuHeapUsedCounter = 0;
        *(uint32*)EmuNestopiaX13DSoundApuHeapCommittedCounter = 0;
    }

    *(uint32*)EmuNestopiaX13DSoundApuHeapUsedPointer = EmuNestopiaX13DSoundApuHeapUsedCounter;
    *(uint32*)EmuNestopiaX13DSoundApuHeapCommittedPointer = EmuNestopiaX13DSoundApuHeapCommittedCounter;
}

static VOID WINAPI EmuNestopiaX13XapiInitProcess()
{
    EmuSwapFS();   // Win2k/XP FS

    printf("Emu (0x%lX): NestopiaX 1.3 XapiInitProcess skipped.\n", GetCurrentThreadId());

    EmuInstallNestopiaX13DSoundCounters(true);

    EmuSwapFS();   // XBox FS
}

// Optionally seed the kernel LaunchDataPage so a homebrew emulator auto-boots a
// ROM instead of stalling at its menu for controller input we can't supply
// headless. The guest ROM path (e.g. "d:\\nesroms\\Battle.nes", relative to the
// title's D: = its own directory) comes from the CXBX_AUTOBOOT_ROM environment
// variable. FCEUltra/z26x/etc. read this in XGetCustomLaunchData via XGetLaunchInfo,
// which wants launch type LDT_TITLE (0) plus a CUSTOM_LAUNCH_DATA carrying magic
// 0xEE456777 and szFilename. Page layout is the XDK's LAUNCH_DATA_PAGE:
// Header.dwLaunchDataType at +0x000, the 3 KiB LaunchData blob at +0x400;
// CUSTOM_LAUNCH_DATA is { DWORD magic; char szFilename[300]; ... }.
static void EmuInstallAutoBootLaunchData()
{
    char rom[300] = {0};
    if(GetEnvironmentVariableA("CXBX_AUTOBOOT_ROM", rom, sizeof(rom)) == 0 || rom[0] == '\0')
        return;

    unsigned char *Page = (unsigned char*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if(Page == NULL)
        return;
    memset(Page, 0, 0x1000);

    *(uint32*)(Page + 0x000) = 0;                    // Header.dwLaunchDataType = LDT_TITLE

    unsigned char *LaunchData = Page + 0x400;
    *(uint32*)(LaunchData + 0x000) = 0xEE456777;     // CUSTOM_LAUNCH_DATA.magic
    strncpy((char*)(LaunchData + 0x004), rom, 299);  // CUSTOM_LAUNCH_DATA.szFilename[300]

    xboxkrnl::LaunchDataPage = (xboxkrnl::DWORD)(uintptr_t)Page;

    printf("Emu (0x%lX): auto-boot launch data installed rom=\"%s\" page=0x%.08lX.\n",
           GetCurrentThreadId(), rom, (unsigned long)(uintptr_t)Page);
    fflush(stdout);
}

// Populate a minimal Xbox kernel PE image in the physical-map shadow at guest
// 0x80010000 (the real kernel's fixed base). The soft-mod launcher framework
// shared by FCEUltra/z26x/NestopiaX 2.2 parses the running kernel as a PE --
// e_lfanew at +0x3C, IMAGE_FILE_HEADER at pe+4 (NumberOfSections at pe+6,
// SizeOfOptionalHeader at pe+0x14), section table after the optional header --
// and scans it for the 'INIT' section. Against this HLE's all-zero kernel the
// scan finds nothing and the title QuickReboots before it ever runs; a
// well-formed header + section table (including INIT) lets the parse succeed.
static void EmuInstallFakeKernelImage()
{
    BYTE Image[0x400];
    ZeroMemory(Image, sizeof(Image));

    // IMAGE_DOS_HEADER
    Image[0x00] = 'M'; Image[0x01] = 'Z';
    *(ULONG*)(Image + 0x3C) = 0xF8;                 // e_lfanew -> IMAGE_NT_HEADERS

    // IMAGE_NT_HEADERS at 0xF8
    BYTE *Pe = Image + 0xF8;
    *(ULONG*)(Pe + 0x00) = 0x00004550;              // "PE\0\0"
    *(USHORT*)(Pe + 0x04) = 0x014C;                 // FileHeader.Machine = i386
    *(USHORT*)(Pe + 0x06) = 6;                      // FileHeader.NumberOfSections
    *(USHORT*)(Pe + 0x14) = 0xE0;                   // FileHeader.SizeOfOptionalHeader
    *(USHORT*)(Pe + 0x18) = 0x010B;                 // OptionalHeader.Magic = PE32
    *(ULONG*)(Pe + 0x18 + 0x1C) = 0x80010000;       // OptionalHeader.ImageBase

    // IMAGE_SECTION_HEADERs after the optional header. The soft-mod scan only
    // checks the LAST section's name == 'INIT' (real xboxkrnl keeps INIT last, as
    // it is discarded after boot), so INIT must be the final entry.
    BYTE *Sec = Pe + 0x18 + 0xE0;
    struct { char Name[8]; ULONG VSize, VAddr; } Sects[6] =
    {
        { { '.','t','e','x','t', 0,0,0 }, 0x5D000, 0x00001000 },
        { { '.','d','a','t','a', 0,0,0 }, 0x03000, 0x0005E000 },
        { { 'P','A','G','E',  0, 0,0,0 }, 0x10000, 0x00061000 },
        { { '.','r','d','a','t','a',0,0 }, 0x05000, 0x00071000 },
        { { '.','e','d','a','t','a',0,0 }, 0x02000, 0x00076000 },
        { { 'I','N','I','T',  0, 0,0,0 }, 0x0A000, 0x00078000 },
    };
    for(int i = 0; i < 6; i++)
    {
        BYTE *S = Sec + i * 0x28;
        memcpy(S, Sects[i].Name, 8);
        *(ULONG*)(S + 0x08) = Sects[i].VSize;       // VirtualSize
        *(ULONG*)(S + 0x0C) = Sects[i].VAddr;       // VirtualAddress
        *(ULONG*)(S + 0x10) = Sects[i].VSize;       // SizeOfRawData
        *(ULONG*)(S + 0x14) = Sects[i].VAddr;       // PointerToRawData
    }

    if(EmuWritePhysicalMapBytes(EmuPhysicalMapBase + 0x00010000, Image, sizeof(Image)))
    {
        printf("Emu (0x%lX): fake Xbox kernel PE installed at 0x80010000 (INIT @ RVA 0x61000).\n",
               GetCurrentThreadId());
        fflush(stdout);
    }
}

// FCEUltra v17 (title id 0x10152007) is built with the XDK 5344 XAPI startup,
// which acquires ring 0 (GDT descriptor 8 + far-jump to CS 8), verifies it, and
// -- because a user-mode HLE can never satisfy the CS==8 check -- reboot-launches
// itself forever via XLaunchNewImage (guest 0x000AFCC4). Neutralise that function
// to a no-op return so the title stops rebooting and proceeds into its emulator.
// This is the same targeted-patch approach as EmuInstallNestopiaX13Bootstrap.
static bool EmuIsFceultra(Xbe::Header *pXbeHeader)
{
    if(pXbeHeader->dwBaseAddr != 0x00010000)
        return false;

    Xbe::Certificate *pCertificate = (Xbe::Certificate*)pXbeHeader->dwCertificateAddr;
    return pCertificate->dwTitleId == 0x10152007;
}

static void EmuInstallFceultraBootstrap(Xbe::Header *pXbeHeader)
{
    if(!EmuIsFceultra(pXbeHeader))
        return;

    // XLaunchNewImage prologue: push ebp / mov ebp,esp / sub esp,0xC00 / mov eax,[0x10118]
    const uint08 XLaunchNewImageSig[] =
    {
        0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x00, 0x0C, 0x00,
        0x00, 0xA1, 0x18, 0x01, 0x01, 0x00
    };
    // xor eax,eax ; ret 0x0C  (matches the function's own __stdcall ret 0xC)
    const uint08 XLaunchNewImagePatch[] = { 0x33, 0xC0, 0xC2, 0x0C, 0x00 };

    if(EmuBytesMatch(0x000AFCC4, XLaunchNewImageSig, sizeof(XLaunchNewImageSig), pXbeHeader))
    {
        EmuWriteBytes(0x000AFCC4, XLaunchNewImagePatch, sizeof(XLaunchNewImagePatch));
        printf("Emu (0x%lX): FCEUltra XLaunchNewImage (0x000AFCC4) neutralised (no reboot-to-self).\n",
               GetCurrentThreadId());
    }
    else
    {
        printf("Emu (0x%lX): FCEUltra XLaunchNewImage signature NOT matched at 0x000AFCC4.\n",
               GetCurrentThreadId());
    }

    // DSOUND tracks APU-heap usage through a pointer global at 0x00110E98 that the
    // kernel/APU init would set; on this HLE DSOUND stores NULL there at runtime, so
    // `mov eax,[0x110E98] ; mov ecx,[ebx+8] ; add [eax],ecx` (0x0010C1D7) faults.
    // Rewrite that site to update a scratch counter directly (skipping the null
    // pointer), leaving the accounting a harmless no-op.
    static uint32 s_FceultraApuCounter = 0;
    const uint08 ApuAccountSig[] = { 0xA1, 0x98, 0x0E, 0x11, 0x00, 0x8B, 0x4B, 0x08, 0x01, 0x08 };
    if(EmuBytesMatch(0x0010C1D7, ApuAccountSig, sizeof(ApuAccountSig), pXbeHeader))
    {
        // mov ecx,[ebx+8] ; add [&scratch],ecx ; nop
        uint08 ApuAccountPatch[10] = { 0x8B, 0x4B, 0x08, 0x01, 0x0D, 0, 0, 0, 0, 0x90 };
        *(uint32*)&ApuAccountPatch[5] = (uint32)(uintptr_t)&s_FceultraApuCounter;
        EmuWriteBytes(0x0010C1D7, ApuAccountPatch, sizeof(ApuAccountPatch));
        printf("Emu (0x%lX): FCEUltra DSOUND APU accounting (0x0010C1D7) redirected to scratch.\n",
               GetCurrentThreadId());
    }
    else
    {
        printf("Emu (0x%lX): FCEUltra DSOUND APU accounting signature NOT matched at 0x0010C1D7.\n",
               GetCurrentThreadId());
    }

    fflush(stdout);
}

static DWORD WINAPI EmuNestopiaX13XLaunchNewImageA(LPCSTR lpTitlePath, PVOID pLaunchData)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("Emu (0x%lX): NestopiaX 1.3 XLaunchNewImageA title=\"%s\" launchData=0x%.08lX.\n",
           GetCurrentThreadId(), lpTitlePath != NULL ? lpTitlePath : "(null)", (uint32)pLaunchData);

    EmuSwapFS();   // XBox FS

    return ERROR_GEN_FAILURE;
}

static PVOID WINAPI EmuNestopiaX13GetXapiProcess()
{
    static uint08 XapiProcess[0x200];
    static bool Logged = false;

    EmuSwapFS();   // Win2k/XP FS

    if(!Logged)
    {
        printf("Emu (0x%lX): NestopiaX 1.3 returning fallback XAPI process 0x%.08lX.\n",
               GetCurrentThreadId(), (uint32)XapiProcess);
        Logged = true;
    }

    EmuSwapFS();   // XBox FS

    return XapiProcess;
}

static bool EmuBytesMatch(uint32 Address, const uint08 *Bytes, uint32 Count, Xbe::Header *pXbeHeader)
{
    if(Address < pXbeHeader->dwBaseAddr)
        return false;

    if(Address + Count > pXbeHeader->dwBaseAddr + pXbeHeader->dwSizeofImage)
        return false;

    return memcmp((void*)Address, Bytes, Count) == 0;
}

static void EmuWriteBytes(uint32 Address, const uint08 *Bytes, uint32 Count)
{
    memcpy((void*)Address, Bytes, Count);
}

static bool EmuIsNestopiaX13(Xbe::Header *pXbeHeader)
{
    if(pXbeHeader->dwBaseAddr != 0x00010000 || pXbeHeader->dwSizeofImage != 0x00314240)
        return false;

    Xbe::Certificate *pCertificate = (Xbe::Certificate*)pXbeHeader->dwCertificateAddr;

    return pCertificate->dwTitleId == 0xFFFF0780;
}

static void EmuInstallNestopiaX13Bootstrap(Xbe::Header *pXbeHeader)
{
    if(!EmuIsNestopiaX13(pXbeHeader))
        return;

    const uint08 XapiThreadStartupBytes[] =
    {
        0x6A, 0x18, 0x68, 0xC0, 0x57, 0x1D, 0x00, 0xE8,
        0x9F, 0x74, 0x00, 0x00
    };
    const uint08 XapiInitProcessBytes[] =
    {
        0xA1, 0x04, 0x50, 0x1C, 0x00, 0x8B, 0x00, 0x8B,
        0x0D, 0x0C, 0x50, 0x1C
    };
    const uint08 XLaunchNewImageABytes[] =
    {
        0xB8, 0x80, 0xFD, 0x1D, 0x00, 0x56, 0x8B, 0xF0,
        0x8B, 0xC8, 0xB8, 0x84
    };
    const uint08 XapiProcessGetterBytes[] =
    {
        0x64, 0xA1, 0x28, 0x00, 0x00, 0x00, 0x8B, 0x80,
        0x2C, 0x01, 0x00, 0x00, 0xC3
    };
    const uint08 XapiBugCheckGuardBytes[] =
    {
        0x64, 0x0F, 0xB6, 0x05, 0x24, 0x00, 0x00, 0x00,
        0x3C, 0x02, 0x72, 0x08, 0x6A, 0x0A, 0xFF, 0x15,
        0x9C, 0x50, 0x1C, 0x00
    };
    const uint08 XapiPerThreadDataBytes[] =
    {
        0x64, 0xA1, 0x28, 0x00, 0x00, 0x00, 0x83, 0x78,
        0x28, 0x00, 0x74, 0x17
    };
    const uint08 XapiFsCallbackBytes[] =
    {
        0x64, 0xA1, 0x20, 0x00, 0x00, 0x00, 0x8B, 0x80,
        0x50, 0x02, 0x00, 0x00
    };
    const uint08 DSoundApuHeapAllocateAccountingBytes[] =
    {
        0xA1, 0x48, 0x70, 0x19, 0x00, 0x8B, 0x4B, 0x08,
        0x01, 0x08
    };
    const uint08 DSoundApuHeapAllocateAccountingPatch[] =
    {
        0x8B, 0x4B, 0x08, 0x01, 0x0D, 0xDC, 0x68, 0x19,
        0x00, 0x90
    };
    const uint08 DSoundApuHeapFreeAccountingBytes[] =
    {
        0xA1, 0x48, 0x70, 0x19, 0x00, 0x8B, 0x4E, 0x08,
        0x29, 0x08
    };
    const uint08 DSoundApuHeapFreeAccountingPatch[] =
    {
        0x8B, 0x4E, 0x08, 0x29, 0x0D, 0xDC, 0x68, 0x19,
        0x00, 0x90
    };
    const uint08 DSoundApuHeapCommitAccountingBytes[] =
    {
        0xA1, 0x4C, 0x70, 0x19, 0x00, 0x8B, 0x4D, 0x0C,
        0x01, 0x08
    };
    const uint08 DSoundApuHeapCommitAccountingPatch[] =
    {
        0x8B, 0x4D, 0x0C, 0x01, 0x0D, 0xD8, 0x68, 0x19,
        0x00, 0x90
    };
    const uint08 DSoundApuHeapResetAccountingBytes[] =
    {
        0xA1, 0x4C, 0x70, 0x19, 0x00, 0x83, 0x20, 0x00,
        0xA1, 0x48, 0x70, 0x19, 0x00, 0x83, 0x20, 0x00
    };
    const uint08 DSoundApuHeapResetAccountingPatch[] =
    {
        0x83, 0x25, 0xD8, 0x68, 0x19, 0x00, 0x00, 0x90,
        0x83, 0x25, 0xDC, 0x68, 0x19, 0x00, 0x00, 0x90
    };

    struct NestopiaFsCallbackPatch
    {
        const char *Name;
        uint32 From;
        uint32 To;
    };

    const NestopiaFsCallbackPatch XapiFsCallbackPatches[] =
    {
        { "Xapi thread start FS callback", 0x00132750, 0x00132767 },
        { "Xapi shutdown FS callback", 0x001331D9, 0x001331F2 },
        { "XapiProcessStartup FS notify", 0x001336AE, 0x001336F0 },
        { "Xapi handle FS callback", 0x00133CE9, 0x00133CFF },
        { "Xapi FS callback A", 0x00136B98, 0x00136BB1 },
        { "Xapi FS callback B", 0x00136C0C, 0x00136C25 },
        { "Xapi unwind FS callback A", 0x0013748B, 0x001374AA },
        { "Xapi unwind FS callback B", 0x001374BA, 0x001374D9 },
        { "D3D Present FS callback", 0x00151F25, 0x00151F79 }
    };

    const uint32 XapiThreadStartup = 0x00133215;
    const uint32 XapiInitProcess = 0x001346E6;
    const uint32 XLaunchNewImageA = 0x001325AC;
    const uint32 XapiProcessGetter = 0x0013331B;
    const uint32 XapiBugCheckGuard = 0x0013BB39;
    const uint32 XapiAfterBugCheckGuard = 0x0013BB4D;
    const uint32 XapiPerThreadData = 0x0013BB4F;
    const uint32 XapiGlobalDataFallback = 0x0013BB72;
    const uint32 DSoundApuHeapAllocateAccounting = 0x0017FB26;
    const uint32 DSoundApuHeapFreeAccounting = 0x0017FCA8;
    const uint32 DSoundApuHeapCommitAccounting = 0x0017FA55;
    const uint32 DSoundApuHeapResetAccounting = 0x0017F9C9;

    if(!EmuBytesMatch(XapiThreadStartup, XapiThreadStartupBytes, sizeof(XapiThreadStartupBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; XapiThreadStartup bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(XapiInitProcess, XapiInitProcessBytes, sizeof(XapiInitProcessBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; XapiInitProcess bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(XLaunchNewImageA, XLaunchNewImageABytes, sizeof(XLaunchNewImageABytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; XLaunchNewImageA bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(XapiProcessGetter, XapiProcessGetterBytes, sizeof(XapiProcessGetterBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; XapiProcess getter bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(DSoundApuHeapAllocateAccounting, DSoundApuHeapAllocateAccountingBytes,
                      sizeof(DSoundApuHeapAllocateAccountingBytes), pXbeHeader) ||
       !EmuBytesMatch(DSoundApuHeapFreeAccounting, DSoundApuHeapFreeAccountingBytes,
                      sizeof(DSoundApuHeapFreeAccountingBytes), pXbeHeader) ||
       !EmuBytesMatch(DSoundApuHeapCommitAccounting, DSoundApuHeapCommitAccountingBytes,
                      sizeof(DSoundApuHeapCommitAccountingBytes), pXbeHeader) ||
       !EmuBytesMatch(DSoundApuHeapResetAccounting, DSoundApuHeapResetAccountingBytes,
                      sizeof(DSoundApuHeapResetAccountingBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; DSOUND APU heap accounting bytes did not match.\n",
               GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(XapiBugCheckGuard, XapiBugCheckGuardBytes, sizeof(XapiBugCheckGuardBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; Xapi bugcheck guard bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(XapiPerThreadData, XapiPerThreadDataBytes, sizeof(XapiPerThreadDataBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; Xapi per-thread data bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    for(uint32 i = 0; i < sizeof(XapiFsCallbackPatches) / sizeof(XapiFsCallbackPatches[0]); i++)
    {
        if(!EmuBytesMatch(XapiFsCallbackPatches[i].From, XapiFsCallbackBytes, sizeof(XapiFsCallbackBytes), pXbeHeader))
        {
            printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; %s bytes did not match.\n",
                   GetCurrentThreadId(), XapiFsCallbackPatches[i].Name);
            return;
        }
    }

    printf("Emu (0x%lX): Installing NestopiaX 1.3 bootstrap HLE patches.\n", GetCurrentThreadId());
    printf("Emu (0x%lX): 0x%.08lX -> EmuXapiThreadStartup\n", GetCurrentThreadId(), XapiThreadStartup);
    printf("Emu (0x%lX): 0x%.08lX -> EmuNestopiaX13XapiInitProcess\n", GetCurrentThreadId(), XapiInitProcess);
    printf("Emu (0x%lX): 0x%.08lX -> EmuNestopiaX13XLaunchNewImageA\n", GetCurrentThreadId(), XLaunchNewImageA);
    printf("Emu (0x%lX): 0x%.08lX -> EmuNestopiaX13GetXapiProcess\n", GetCurrentThreadId(), XapiProcessGetter);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (skip FS bugcheck guard)\n", GetCurrentThreadId(), XapiBugCheckGuard, XapiAfterBugCheckGuard);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (use XAPI global data fallback)\n", GetCurrentThreadId(), XapiPerThreadData, XapiGlobalDataFallback);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (DSOUND APU heap used counter)\n",
           GetCurrentThreadId(), EmuNestopiaX13DSoundApuHeapUsedPointer, EmuNestopiaX13DSoundApuHeapUsedCounter);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (DSOUND APU heap committed counter)\n",
           GetCurrentThreadId(), EmuNestopiaX13DSoundApuHeapCommittedPointer, EmuNestopiaX13DSoundApuHeapCommittedCounter);
    printf("Emu (0x%lX): Patching NestopiaX 1.3 DSOUND APU heap accounting.\n", GetCurrentThreadId());
    for(uint32 i = 0; i < sizeof(XapiFsCallbackPatches) / sizeof(XapiFsCallbackPatches[0]); i++)
    {
        printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (skip %s)\n",
               GetCurrentThreadId(), XapiFsCallbackPatches[i].From, XapiFsCallbackPatches[i].To,
               XapiFsCallbackPatches[i].Name);
    }

    EmuInstallWrapper((void*)XapiThreadStartup, XTL::EmuXapiThreadStartup);
    EmuInstallWrapper((void*)XapiInitProcess, EmuNestopiaX13XapiInitProcess);
    EmuInstallWrapper((void*)XLaunchNewImageA, EmuNestopiaX13XLaunchNewImageA);
    EmuInstallWrapper((void*)XapiProcessGetter, EmuNestopiaX13GetXapiProcess);
    EmuInstallWrapper((void*)XapiBugCheckGuard, (void*)XapiAfterBugCheckGuard);
    EmuInstallWrapper((void*)XapiPerThreadData, (void*)XapiGlobalDataFallback);
    for(uint32 i = 0; i < sizeof(XapiFsCallbackPatches) / sizeof(XapiFsCallbackPatches[0]); i++)
        EmuInstallWrapper((void*)XapiFsCallbackPatches[i].From, (void*)XapiFsCallbackPatches[i].To);

    EmuInstallNestopiaX13DSoundCounters(true);
    EmuWriteBytes(DSoundApuHeapAllocateAccounting, DSoundApuHeapAllocateAccountingPatch,
                  sizeof(DSoundApuHeapAllocateAccountingPatch));
    EmuWriteBytes(DSoundApuHeapFreeAccounting, DSoundApuHeapFreeAccountingPatch,
                  sizeof(DSoundApuHeapFreeAccountingPatch));
    EmuWriteBytes(DSoundApuHeapCommitAccounting, DSoundApuHeapCommitAccountingPatch,
                  sizeof(DSoundApuHeapCommitAccountingPatch));
    EmuWriteBytes(DSoundApuHeapResetAccounting, DSoundApuHeapResetAccountingPatch,
                  sizeof(DSoundApuHeapResetAccountingPatch));
}

// ******************************************************************
// * func: EmuLocateFunction
// ******************************************************************
void *EmuLocateFunction(OOVPA *Oovpa, uint32 lower, uint32 upper)
{
    uint32 count = Oovpa->Count;

    // Skip out if this is an unnecessary search
    if(!bXRefFirstPass && Oovpa->XRefCount == 0 && Oovpa->XRefSaveIndex == (uint08)-1)
        return 0;

    // ******************************************************************
    // * Large
    // ******************************************************************
    if(Oovpa->Large == 1)
    {
        LOOVPA<1> *Loovpa = (LOOVPA<1>*)Oovpa;

        upper -= Loovpa->Lovp[count-1].Offset;

        // ******************************************************************
        // * Search all of the image memory
        // ******************************************************************
        for(uint32 cur=lower;cur<upper;cur++)
        {
            uint32 v;

            // ******************************************************************
            // * check all cross references
            // ******************************************************************
            for(v=0;v<Loovpa->XRefCount;v++)
            {
                uint32 Offset = Loovpa->Lovp[v].Offset;
                uint32 Value  = Loovpa->Lovp[v].Value;

                uint32 RealValue = *(uint32*)(cur + Offset);

                if(XRefDataBase[Value] == -1)
                    goto skipout_L;   // Unsatisfied XRef is not acceptable

                if(RealValue + cur + Offset+4 != XRefDataBase[Value])
                    break;
            }

            // ******************************************************************
            // * check all pairs, moving on if any do not match
            // ******************************************************************
            for(v=0;v<count;v++)
            {
                uint32 Offset = Loovpa->Lovp[v].Offset;
                uint32 Value  = Loovpa->Lovp[v].Value;

                uint08 RealValue = *(uint08*)(cur + Offset);

                if(RealValue != Value)
                    break;
            }

            // ******************************************************************
            // * success if we found all pairs
            // ******************************************************************
            if(v == count)
            {
                if(Loovpa->XRefSaveIndex != (uint08)-1)
                {
                    if(XRefDataBase[Loovpa->XRefSaveIndex] == -1)
                    {
                        UnResolvedXRefs--;
                        XRefDataBase[Loovpa->XRefSaveIndex] = cur;

                        return (void*)cur;
                    }
                    else
                        return 0;   // Already Found, no bother patching again
                }

                return (void*)cur;
            }

            skipout_L:;
        }
    }
    // ******************************************************************
    // * Small
    // ******************************************************************
    else
    {
        SOOVPA<1> *Soovpa = (SOOVPA<1>*)Oovpa;

        upper -= Soovpa->Sovp[count-1].Offset;

        // ******************************************************************
        // * Search all of the image memory
        // ******************************************************************
        for(uint32 cur=lower;cur<upper;cur++)
        {
            uint32 v;

//            if( (cur == 0x0006A6C6) && (Soovpa->Sovp[v].Value == XREF_SETCURRENTPOSITION2) )
//                _asm int 3
            // ******************************************************************
            // * check all cross references
            // ******************************************************************
            for(v=0;v<Soovpa->XRefCount;v++)
            {
                uint32 Offset = Soovpa->Sovp[v].Offset;
                uint32 Value  = Soovpa->Sovp[v].Value;

                uint32 RealValue = *(uint32*)(cur + Offset);

                if(XRefDataBase[Value] == -1)
                    goto skipout_S;   // Unsatisfied XRef is not acceptable

                if( (RealValue + cur + Offset + 4 != XRefDataBase[Value]))
                    break;
            }

            // check OV pairs if all xrefs matched
            if(v == Soovpa->XRefCount)
            {
                // ******************************************************************
                // * check all pairs, moving on if any do not match
                // ******************************************************************
                for(;v<count;v++)
                {
                    uint32 Offset = Soovpa->Sovp[v].Offset;
                    uint32 Value  = Soovpa->Sovp[v].Value;

                    uint08 RealValue = *(uint08*)(cur + Offset);

                    if(RealValue != Value)
                        break;
                }
            }

            // ******************************************************************
            // * success if we found all pairs
            // ******************************************************************
            if(v == count)
            {
                if(Soovpa->XRefSaveIndex != (uint08)-1)
                {
                    if(XRefDataBase[Soovpa->XRefSaveIndex] == -1)
                    {
                        UnResolvedXRefs--;
                        XRefDataBase[Soovpa->XRefSaveIndex] = cur;

                        return (void*)cur;
                    }
                    else
                        return 0;   // Already Found, no bother patching again
                }

                return (void*)cur;
            }

            skipout_S:;
        }
    }

    return 0;
}

// ******************************************************************
// * func: EmuInstallWrappers
// ******************************************************************
void EmuInstallWrappers(OOVPATable *OovpaTable, uint32 OovpaTableSize, void (*Entry)(), Xbe::Header *pXbeHeader)
{
    uint32 lower = pXbeHeader->dwBaseAddr;
    uint32 upper = pXbeHeader->dwBaseAddr + pXbeHeader->dwSizeofImage;

    // ******************************************************************
    // * traverse the full OOVPA table
    // ******************************************************************
    for(uint32 a=0;a<OovpaTableSize/sizeof(OOVPATable);a++)
    {
        OOVPA *Oovpa = OovpaTable[a].Oovpa;

        void *pFunc = EmuLocateFunction(Oovpa, lower, upper);

        if(pFunc != 0)
        {
            #ifdef _DEBUG_TRACE
            printf("Emu (0x%X): 0x%.08X -> %s\n", GetCurrentThreadId(), pFunc, OovpaTable[a].szFuncName);
            #endif

            if(OovpaTable[a].lpRedirect == 0)
                EmuInstallWrapper(pFunc, EmuXRefFailure);
            else
                EmuInstallWrapper(pFunc, OovpaTable[a].lpRedirect);
        }
    }
}

// ******************************************************************
// * func: EmuXRefFailure
// ******************************************************************
void EmuXRefFailure()
{
    EmuSwapFS();    // Win2k/XP FS
    
    EmuCleanup("XRef-only function body reached. Fatal Error.");
}

// ******************************************************************
// * func: EmuException
// ******************************************************************
int EmuException(LPEXCEPTION_POINTERS e)
{
    bool WasXboxFS = EmuIsXboxFS();

    if(WasXboxFS)
        EmuSwapFS();

    if(EmuTryEmulateRdmsr(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(EmuTryEmulatePortIo(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(EmuTryEmulatePhysicalMapAccess(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(EmuTryEmulateMmioAccess(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // ******************************************************************
	// * Debugging Information
	// ******************************************************************
	{
		printf("Emu (0x%X): * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());
		printf("Emu (0x%X): Recieved Exception [0x%.08X]@0x%.08X\n", GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);
        printf("Emu (0x%X): ESP=0x%.08X EBP=0x%.08X\n", GetCurrentThreadId(), e->ContextRecord->Esp, e->ContextRecord->Ebp);
        __try
        {
            DWORD *Stack = (DWORD*)e->ContextRecord->Esp;

            printf("Emu (0x%X): Stack: 0x%.08X 0x%.08X 0x%.08X 0x%.08X 0x%.08X 0x%.08X 0x%.08X 0x%.08X\n",
                   GetCurrentThreadId(), Stack[0], Stack[1], Stack[2], Stack[3], Stack[4], Stack[5], Stack[6], Stack[7]);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            printf("Emu (0x%X): Stack unavailable.\n", GetCurrentThreadId());
        }
		printf("Emu (0x%X): * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());
	}

    fflush(stdout);

	// ******************************************************************
	// * Notify User
	// ******************************************************************
	{
		char buffer[256];

		sprintf(buffer, "Recieved Exception [0x%.08X]@0x%.08X\n\nPress 'OK' to terminate emulation.\nPress 'Cancel' to debug.", e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);

        char szLogFile[260];

        if(EmuGetLogFile(szLogFile, sizeof(szLogFile)))
            ExitProcess(e->ExceptionRecord->ExceptionCode);

        if(MessageBox(XTL::g_hEmuWindow, buffer, "cxbx", MB_ICONSTOP | MB_OKCANCEL) == IDOK)
			ExitProcess(1);
	}

    return EXCEPTION_CONTINUE_SEARCH;
}


// ******************************************************************
// * func: ExitException
// ******************************************************************
int ExitException(LPEXCEPTION_POINTERS e)
{
    if(EmuIsXboxFS())
        EmuSwapFS();

    static int count = 0;

    // ******************************************************************
	// * Debugging Information
	// ******************************************************************
	{
		printf("Emu (0x%X): * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());
		printf("Emu (0x%X): Recieved Exception [0x%.08X]@0x%.08X\n", GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);
		printf("Emu (0x%X): * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());
	}

    fflush(stdout);

    MessageBox(XTL::g_hEmuWindow, "Warning: Could not safely terminate process!", "cxbx", MB_OK);

    count++;

    if(count > 1)
    {
        MessageBox(XTL::g_hEmuWindow, "Warning: Multiple Problems!", "cxbx", MB_OK);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    ExitProcess(1);

    return EXCEPTION_CONTINUE_SEARCH;
}
