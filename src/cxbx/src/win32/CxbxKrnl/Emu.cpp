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
#include <tlhelp32.h>
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
static void  EmuInstallCdxLaunchBootstrap(Xbe::Header *pXbeHeader);
static void  EmuInstallDsoundApuAccountingPatch(Xbe::Header *pXbeHeader);
extern "C" void EmuAciStartDmaThread();   // EmuKrnl.cpp: AC97 DMA delivery thread
static bool  EmuBytesMatch(uint32 Address, const uint08 *Bytes, uint32 Count, Xbe::Header *pXbeHeader);
static void  EmuWriteBytes(uint32 Address, const uint08 *Bytes, uint32 Count);
static bool  EmuLooksLikeReturnAddress(ULONG Address);
static const char *EmuHostAddressToModuleOffset(ULONG Address, char *Buffer, size_t BufferSize);
static bool  EmuIsReadableRange(ULONG Address, ULONG Bytes);
// Optional D3D wrapper entry trace (EmuD3D8.cpp); NULL when the trace is off.
extern "C" const char *EmuGetLastD3DCall(void);
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
static const ULONG EmuNv2aPfifoIntrDmaPusher = 0x00001000;
static const ULONG EmuNv2aPfifoDmaPushSuspended = 0x00001000;
static const ULONG EmuNv2aPfifoDmaStateMethodType = 0x00000001;
static const ULONG EmuNv2aPfifoDmaStateMethod = 0x00001FFC;
static const ULONG EmuNv2aPfifoDmaStateSubchannel = 0x0000E000;
static const ULONG EmuNv2aPfifoDmaStateMethodCount = 0x1FFC0000;
static const ULONG EmuNv2aPfifoDmaStateError = 0xE0000000;
static const ULONG EmuNv2aPfifoDmaErrorCall = 0x20000000;
static const ULONG EmuNv2aPfifoDmaErrorReturn = 0x60000000;
static const ULONG EmuNv2aPfifoDmaErrorReserved = 0x80000000;
static const ULONG EmuNv2aPfifoDmaErrorProtection = 0xC0000000;
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
// OHCI host-controller registers the root-hub model reacts to (offsets from the
// USB0 MMIO base). HcCommandStatus's reset bit self-clears; the four
// HcRhPortStatus registers implement real read/write-to-clear semantics so the
// guest's port poll terminates instead of spinning on stale change bits.
static const ULONG EmuUsbHcCommandStatus = 0x00000008;
static const ULONG EmuUsbHcCommandStatusReset = 0x00000001; // HCR (self-clearing)
static const ULONG EmuUsbHcRhPortStatus0 = 0x00000054;
static const ULONG EmuUsbPortCount = 4;
// HcRhPortStatus bit fields.
static const ULONG EmuUsbPortCCS  = 1u << 0;   // CurrentConnectStatus
static const ULONG EmuUsbPortPES  = 1u << 1;   // PortEnableStatus
static const ULONG EmuUsbPortPRS  = 1u << 4;   // PortResetStatus
static const ULONG EmuUsbPortPPS  = 1u << 8;   // PortPowerStatus
static const ULONG EmuUsbPortCSC  = 1u << 16;  // ConnectStatusChange
static const ULONG EmuUsbPortPESC = 1u << 17;  // PortEnableStatusChange
static const ULONG EmuUsbPortPSSC = 1u << 18;  // PortSuspendStatusChange
static const ULONG EmuUsbPortOCIC = 1u << 19;  // OverCurrentIndicatorChange
static const ULONG EmuUsbPortPRSC = 1u << 20;  // PortResetStatusChange
static const ULONG EmuUsbPortChangeMask =
    EmuUsbPortCSC | EmuUsbPortPESC | EmuUsbPortPSSC | EmuUsbPortOCIC | EmuUsbPortPRSC;
static ULONG g_EmuUsb0PortStatus[EmuUsbPortCount] = { 0, 0, 0, 0 };
// HcInterruptStatus (write-1-to-clear) + the frame counter, so a synthesized SOF
// interrupt can be raised for the guest USB ISR and acknowledged by it.
static const ULONG EmuUsbHcInterruptStatus = 0x0000000C;
static const ULONG EmuUsbHcFmNumber = 0x0000003C;
static const ULONG EmuUsbIntStatusSF = 1u << 2;   // StartOfFrame
static ULONG g_EmuUsb0IntStatus = 0;
static ULONG g_EmuUsb0FmNumber = 0;

// Raise a USB start-of-frame interrupt source (called from the USB delivery
// thread just before it invokes the connected level-1 ISR).
extern "C" void EmuUsb0SignalInterrupt()
{
    g_EmuUsb0IntStatus |= EmuUsbIntStatusSF;
    g_EmuUsb0FmNumber = (g_EmuUsb0FmNumber + 1) & 0xFFFF;
}
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

static bool EmuUsb0IsPortStatus(ULONG Offset, ULONG *PortIndex)
{
    if(Offset < EmuUsbHcRhPortStatus0 ||
       Offset >= EmuUsbHcRhPortStatus0 + EmuUsbPortCount * 4 || (Offset & 3) != 0)
        return false;
    *PortIndex = (Offset - EmuUsbHcRhPortStatus0) / 4;
    return true;
}

static bool EmuMmioTraceEnabled()
{
    static bool Enabled = getenv("CXBX_MMIO_TRACE") != NULL;
    return Enabled;
}

static ULONG EmuUsb0ReadRegister32(ULONG Address)
{
    ULONG Offset = EmuUsb0Offset(Address);
    ULONG PortIndex = 0;
    ULONG Value;

    if(Offset == 0)
    {
        Value = EmuUsbOhciRevision;
    }
    else if(EmuUsb0IsPortStatus(Offset, &PortIndex))
    {
        Value = g_EmuUsb0PortStatus[PortIndex];
    }
    else if(Offset == EmuUsbHcCommandStatus)
    {
        // The controller reset bit self-clears once the reset completes.
        Value = EmuUsb0CachedRegister(Address, 0) & ~EmuUsbHcCommandStatusReset;
    }
    else if(Offset == EmuUsbHcInterruptStatus)
    {
        Value = g_EmuUsb0IntStatus;
    }
    else if(Offset == EmuUsbHcFmNumber)
    {
        Value = g_EmuUsb0FmNumber;
    }
    else
    {
        Value = EmuUsb0CachedRegister(Address, 0);
    }

    printf("Emu (0x%lX): USB0 MMIO read 0x%.08lX = 0x%.08lX.\n",
           GetCurrentThreadId(), Address, Value);
    fflush(stdout);

    return Value;
}

// Apply OHCI HcRhPortStatus write semantics: set/clear enable, power and reset,
// and write-1-to-clear the change bits. With no device attached (CCS clear) the
// enable/reset writes are no-ops and the change bits simply clear, so the
// guest's "clear change, re-read" hub poll terminates.
static void EmuUsb0WritePortStatus(ULONG PortIndex, ULONG Value)
{
    ULONG Status = g_EmuUsb0PortStatus[PortIndex];

    if(Value & (1u << 0))                       // ClearPortEnable
        Status &= ~EmuUsbPortPES;
    if((Value & (1u << 1)) && (Status & EmuUsbPortCCS)) // SetPortEnable
        Status |= EmuUsbPortPES;
    if((Value & (1u << 4)) && (Status & EmuUsbPortCCS)) // SetPortReset -> completes
        Status = (Status | EmuUsbPortPES | EmuUsbPortPRSC) & ~EmuUsbPortPRS;
    if(Value & (1u << 8))                       // SetPortPower
        Status |= EmuUsbPortPPS;
    if(Value & (1u << 9))                       // ClearPortPower
        Status &= ~EmuUsbPortPPS;

    Status &= ~(Value & EmuUsbPortChangeMask);  // write-1-to-clear change bits
    g_EmuUsb0PortStatus[PortIndex] = Status;
}

static void EmuUsb0WriteRegister32(ULONG Address, ULONG Value)
{
    ULONG Offset = EmuUsb0Offset(Address);
    ULONG PortIndex = 0;

    if(EmuUsb0IsPortStatus(Offset, &PortIndex))
        EmuUsb0WritePortStatus(PortIndex, Value);
    else if(Offset == EmuUsbHcInterruptStatus)
        g_EmuUsb0IntStatus &= ~Value;   // write-1-to-clear
    else
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

    if(EmuMmioTraceEnabled())
    {
        printf("Emu (0x%lX): APU MMIO read 0x%.08lX = 0x%.08lX.\n",
               GetCurrentThreadId(), Address, Value);
        fflush(stdout);
    }

    return Value;
}

static void EmuApuWriteRegister32(ULONG Address, ULONG Value)
{
    EmuStoreMmioRegister(Address, Value);

    if(EmuMmioTraceEnabled())
    {
        printf("Emu (0x%lX): APU MMIO write 0x%.08lX = 0x%.08lX.\n",
               GetCurrentThreadId(), Address, Value);
        fflush(stdout);
    }
}

static void EmuAciDmaSync();   // time-based DMA state-machine catch-up (defined below)

static ULONG EmuAciReadRegister32(ULONG Address)
{
    ULONG Offset = EmuAciOffset(Address);
    ULONG Value = 0;

    // Bring the bus-master DMA state machine up to date before the guest reads a
    // channel/status register. Advancement is otherwise driven by a background
    // thread; making a read observe current state removes the dependency on that
    // thread being scheduled within the guest's poll window (which starves under
    // heavy host load and left CIV/SR/GLOB_STA reading stale-zero).
    if(Offset >= EmuAciBusMasterBase)
        EmuAciDmaSync();

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

// ---------------------------------------------------------------------------
// AC97 bus-master DMA engine
//
// A title programs a channel by writing a buffer-descriptor-list base (BDBAR),
// a last-valid index (LVI) and then setting the channel Control run bit; real
// hardware then consumes descriptors, advancing the current index (CIV) and
// raising buffer-completion statuses/interrupts the title's audio path blocks
// on. Model that state machine: each tick consumes the current buffer of every
// running channel -- PICB drains, CIV advances, SR latches BCIS (and
// CELV/LVBCI + halt on the last valid buffer), GLOB_STA latches the channel's
// interrupt status. Buffer CONTENT is not consumed (no host audio device);
// every completion is treated as interrupt-on-completion since the descriptor
// entries hold guest-physical addresses this HLE cannot translate back.
// ---------------------------------------------------------------------------
static const ULONG EmuAciDmaChannels[3] = { 0x100, 0x110, 0x170 };   // PCM-in, PCM-out, SPDIF
static const ULONG EmuAciDmaGlobStaBits[3] = { 0x10, 0x40, 0x01 };   // ISR mask GLOB_STA & 0x51
static const ULONG EmuAciStatusCelv = 0x02;    // current equals last valid
static const ULONG EmuAciStatusLvbci = 0x04;   // last valid buffer completed
static const ULONG EmuAciStatusBcis = 0x08;    // buffer completion status
static const ULONG EmuAciControlIoce = 0x10;   // interrupt-on-completion enable

// The DMA state machine advances at a fixed buffer cadence. It is stepped from
// two places -- the background delivery thread and, lazily, a guest register
// read -- so both share one monotonic time base guarded by a lock, and whoever
// runs first consumes the elapsed buffer periods (the other then sees none).
// This makes the observable register state (CIV/SR/GLOB_STA) a pure function of
// elapsed wall time, independent of host thread scheduling.
static const DWORD EmuAciDmaPeriodMs = 5;       // ~200 Hz buffer cadence
static const DWORD EmuAciDmaMaxCatchup = 64;    // cap a backlog burst after a long stall

static CRITICAL_SECTION g_EmuAciDmaLock;
static DWORD g_EmuAciDmaLastMs = 0;
static volatile LONG g_EmuAciDmaPendingIrq = 0;
static volatile LONG g_EmuAciDmaInitState = 0;   // 0=uninit, 1=initializing, 2=ready

// Race-safe one-time init of the lock + time base without depending on Vista's
// InitOnce (this tree targets the XP-era SDK). The winner initializes; any
// concurrent caller briefly spins until the lock is constructed.
static void EmuAciDmaEnsureInit()
{
    if(g_EmuAciDmaInitState == 2)
        return;

    if(InterlockedCompareExchange(&g_EmuAciDmaInitState, 1, 0) == 0)
    {
        InitializeCriticalSection(&g_EmuAciDmaLock);
        g_EmuAciDmaLastMs = GetTickCount();
        InterlockedExchange(&g_EmuAciDmaInitState, 2);
    }
    else
    {
        while(g_EmuAciDmaInitState != 2)
            Sleep(0);
    }
}

// Advance every running channel by one buffer. Returns the number of
// completions that want an interrupt delivered (caller fires the audio ISR).
static int EmuAciDmaAdvanceOnce()
{
    int Fired = 0;

    for(int i = 0; i < 3; i++)
    {
        ULONG Channel = EmuAciMmioBase + EmuAciDmaChannels[i];

        ULONG Control = (EmuAciCachedRegister(Channel + 0x08, 0) >> 24) & 0xFF;   // CR at +0x0B
        if((Control & EmuAciBusMasterControlRun) == 0)
            continue;

        ULONG IndexWord = EmuAciCachedRegister(Channel + 0x04, 0);
        ULONG Civ = IndexWord & 0x1F;                                             // CIV at +0x04
        ULONG Lvi = (IndexWord >> 8) & 0x1F;                                      // LVI at +0x05
        ULONG Status = (IndexWord >> 16) & 0xFFFF;                                // SR at +0x06

        if((Status & EmuAciStatusDmaHalted) != 0)
        {
            // Halted on the last valid buffer; resume only once the title
            // moves LVI ahead again.
            if(Civ == Lvi)
                continue;
            Status &= ~(EmuAciStatusDmaHalted | EmuAciStatusCelv);
        }

        // Current buffer consumed: drain PICB.
        EmuAciStorePartialRegister(Channel + 0x08, 2, 0);

        if(Civ == Lvi)
        {
            // Last valid buffer completed: latch and halt until LVI advances.
            Status |= EmuAciStatusLvbci | EmuAciStatusCelv | EmuAciStatusDmaHalted;
        }
        else
        {
            Civ = (Civ + 1) & 0x1F;
            EmuAciStorePartialRegister(Channel + 0x04, 1, Civ);
            EmuAciStorePartialRegister(Channel + 0x0A, 1, (Civ + 1) & 0x1F);   // prefetch index
        }

        Status |= EmuAciStatusBcis;
        EmuAciStorePartialRegister(Channel + 0x06, 2, Status);

        ULONG GlobStaAddress = EmuAciMmioBase + EmuAciGlobSta;
        EmuStoreMmioRegister(GlobStaAddress,
                             EmuAciCachedRegister(GlobStaAddress, 0) | EmuAciDmaGlobStaBits[i]);

        if((Control & EmuAciControlIoce) != 0)
            Fired++;
    }

    return Fired;
}

// Step the DMA model forward by however many buffer periods have elapsed since
// the last catch-up. Idempotent and safe to call from either the delivery
// thread or a guest read: the lock serializes the two, and a caller that finds
// no whole period elapsed does nothing. Completions that want an interrupt are
// accumulated for the delivery thread to fire (a read never delivers an ISR).
static void EmuAciDmaSync()
{
    EmuAciDmaEnsureInit();
    EnterCriticalSection(&g_EmuAciDmaLock);

    DWORD Now = GetTickCount();
    DWORD Elapsed = Now - g_EmuAciDmaLastMs;   // unsigned: correct across a single wrap
    DWORD Steps = Elapsed / EmuAciDmaPeriodMs;

    if(Steps > 0)
    {
        if(Steps > EmuAciDmaMaxCatchup)
        {
            Steps = EmuAciDmaMaxCatchup;
            g_EmuAciDmaLastMs = Now;                        // collapse a large backlog
        }
        else
        {
            g_EmuAciDmaLastMs += Steps * EmuAciDmaPeriodMs;  // keep the sub-period remainder
        }

        for(DWORD k = 0; k < Steps; k++)
        {
            int Fired = EmuAciDmaAdvanceOnce();
            if(Fired > 0)
                InterlockedExchangeAdd(&g_EmuAciDmaPendingIrq, Fired);
        }
    }

    LeaveCriticalSection(&g_EmuAciDmaLock);
}

// Background delivery-thread entry point: bring the model up to date and report
// whether a buffer completion is pending an interrupt (delivered by the caller).
extern "C" int EmuAciDmaAdvance()
{
    EmuAciDmaSync();
    return (int)InterlockedExchange(&g_EmuAciDmaPendingIrq, 0);
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
#define NV097_NO_OPERATION             0x0100u
#define NV097_SET_BEGIN_END            0x17FCu
#define NV097_SET_TEXTURE_OFFSET_0     0x1B00u
#define NV097_SET_TEXTURE_FORMAT_0     0x1B04u
#define NV097_SET_TEXTURE_IMAGE_RECT_0 0x1B1Cu
#define EmuNv2aTextureStageStride      0x40u
#define EmuNv2aTextureStageCount       4u

#define NV097_SET_TEXTURE_FILTER_0     0x1B14u

struct EmuNv2aTextureState
{
    ULONG Offset;
    ULONG Format;
    ULONG ImageRect;
    ULONG Filter;
};

static EmuNv2aTextureState g_EmuNv2aTexture[EmuNv2aTextureStageCount] = {};
static ULONG g_EmuNv2aContextDmaAHandle = 0;
static ULONG g_EmuNv2aTextureDumpIndex = 0;
static ULONG g_EmuNv2aTextureMethodLogCount = 0;
static ULONG g_EmuNv2aScanoutDumpIndex = 0;
// Last physical address the guest programmed into the CRTC scanout base
// (NV_PCRTC_START). Used as the render-target fallback for raw-NV2A titles whose
// color-surface DMA object this HLE does not model (the kernel-created
// framebuffer DMA object is absent, so SET_SURFACE_COLOR_OFFSET's DMA cannot be
// resolved); the displayed surface is the best available target.
static ULONG g_EmuNv2aScanoutAddress = 0;

// Bytes/scanline of the displayed surface, published by the video HAL's
// AvSetDisplayMode (EmuKrnl.cpp). Lets the scanout capture recover the width
// (pitch/4 at 32bpp); 0 until a mode is set, in which case a 640-wide default
// is assumed. See EmuNv2aDumpScanout.
extern "C" ULONG g_EmuDisplayPitch = 0;

// ---------------------------------------------------------------------------
// NV2A software rasterizer (Phase 0): the register model above captures the
// method stream but never turns a triangle into pixels, so raw-NV2A titles
// (nxdk / non-XDK homebrew that render through the pushbuffer instead of the
// HLE D3D8 path) present a black frame. Phase 0 takes the simplest slice that
// proves the pipeline end to end: a bound color surface + pre-transformed
// (screen-space) vertices fetched from the vertex arrays + a flat-shaded
// edge-function triangle raster writing straight into the surface. No z-buffer,
// no texturing, no vertex program yet. Gated behind CXBX_NV2A_RASTER so it
// cannot perturb the working HLE-D3D8 titles or the conformance suite.
#define NV097_SET_CONTEXT_DMA_COLOR         0x0194u
#define NV097_SET_CONTEXT_DMA_VERTEX_A      0x019Cu
#define NV097_SET_CONTEXT_DMA_ZETA          0x0198u
#define NV097_SET_SURFACE_CLIP_HORIZONTAL   0x0200u
#define NV097_SET_SURFACE_CLIP_VERTICAL     0x0204u
#define NV097_SET_SURFACE_FORMAT            0x0208u
#define NV097_SET_SURFACE_PITCH             0x020Cu
#define NV097_SET_SURFACE_COLOR_OFFSET      0x0210u
#define NV097_SET_SURFACE_ZETA_OFFSET       0x0214u
#define NV097_SET_BLEND_ENABLE              0x0304u
#define NV097_SET_DEPTH_TEST_ENABLE         0x030Cu
#define NV097_SET_STENCIL_TEST_ENABLE       0x032Cu
#define NV097_SET_STENCIL_MASK              0x0360u
#define NV097_SET_STENCIL_FUNC              0x0364u
#define NV097_SET_STENCIL_FUNC_REF          0x0368u
#define NV097_SET_STENCIL_FUNC_MASK         0x036Cu
#define NV097_SET_STENCIL_OP_FAIL           0x0370u
#define NV097_SET_STENCIL_OP_ZFAIL          0x0374u
#define NV097_SET_STENCIL_OP_ZPASS          0x0378u
#define NV097_SET_BLEND_FUNC_SFACTOR        0x0344u
#define NV097_SET_BLEND_FUNC_DFACTOR        0x0348u
#define NV097_SET_BLEND_EQUATION            0x0350u
#define NV097_SET_DEPTH_FUNC                0x0354u
#define NV097_SET_DEPTH_MASK                0x035Cu
#define NV097_SET_PROJECTION_MATRIX         0x0440u
#define NV097_SET_COMPOSITE_MATRIX          0x0680u
#define NV097_SET_VIEWPORT_OFFSET           0x0A20u
#define NV097_SET_VIEWPORT_SCALE            0x0AF0u
#define NV097_SET_TRANSFORM_PROGRAM         0x0B00u
#define NV097_SET_TRANSFORM_CONSTANT        0x0B80u
#define NV097_SET_TRANSFORM_EXECUTION_MODE  0x1E94u
#define NV097_SET_TRANSFORM_PROGRAM_LOAD    0x1E9Cu
#define NV097_SET_TRANSFORM_PROGRAM_START   0x1EA0u
#define NV097_SET_TRANSFORM_CONSTANT_LOAD   0x1EA4u
#define NV097_SET_VERTEX_DATA_ARRAY_OFFSET  0x1720u
#define NV097_SET_VERTEX_DATA_ARRAY_FORMAT  0x1760u
#define NV097_DRAW_ARRAYS                   0x1810u
#define NV097_SET_CONTEXT_DMA_SEMAPHORE     0x01A4u
#define NV097_SET_SEMAPHORE_OFFSET          0x1D6Cu
#define NV097_BACK_END_WRITE_SEMAPHORE_RELEASE 0x1D70u
#define EmuNv2aVertexAttrCount              16u
#define EmuNv2aAttrPosition                 0u
#define EmuNv2aAttrDiffuse                  3u
#define EmuNv2aAttrTexcoord0                9u
#define EmuNv2aVpMaxInstr                   136u

// CPU vertex-program interpreter (implemented in EmuVshDecoder.cpp): transform
// one vertex through the loaded NV2A microcode. Inputs are the 16 attribute
// registers (16*4 floats), constants the 192-entry constant memory (192*4
// floats), outputs clip-space oPos + oD0 diffuse.
extern "C" bool EmuVshExecuteProgram(const DWORD *Program, int InstrCount, int Start,
                                     const float *Const, const float *Input,
                                     float *OutPos, float *OutCol0, float *OutTex0);

struct EmuNv2aVertexArrayState
{
    ULONG Offset;
    ULONG Format;
};

static EmuNv2aVertexArrayState g_EmuNv2aVertexArray[EmuNv2aVertexAttrCount] = {};
static ULONG g_EmuNv2aContextDmaColor = 0;
static ULONG g_EmuNv2aContextDmaVertex = 0;
static ULONG g_EmuNv2aContextDmaSemaphore = 0;
static ULONG g_EmuNv2aSemaphoreOffset = 0;
static ULONG g_EmuNv2aSurfaceColorOffset = 0;
static ULONG g_EmuNv2aSurfacePitchColor = 0;
static ULONG g_EmuNv2aSurfaceClipW = 0;
static ULONG g_EmuNv2aSurfaceClipH = 0;
static ULONG g_EmuNv2aBeginOp = 0;
static bool  g_bEmuNv2aRaster = false;
static bool  g_bEmuNv2aRasterChecked = false;
// Viewport transform (clip/NDC -> screen). Defaults are identity so the Phase 0
// pre-transformed case (w==1, no viewport programmed) maps position straight to
// screen coordinates; a title that programs a real viewport gets it applied.
static float g_EmuNv2aViewportOffset[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float g_EmuNv2aViewportScale[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };
// Fixed-function composite matrix (object -> clip), applied in the non-program
// path. Row-major, D3D row-vector convention (clip.j = sum_i pos.i * M[i][j]).
// Identity by default so a title that supplies clip/screen coordinates directly
// (and never programs the matrix) is untransformed.
static float g_EmuNv2aCompositeMatrix[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};
// Vertex-program state (Phase 2): uploaded microcode + constant memory. The
// interpreter runs when execution mode is PROGRAM and a program is loaded;
// otherwise the raw position/diffuse arrays feed the transform back-end.
static DWORD g_EmuNv2aVpProgram[EmuNv2aVpMaxInstr * 4] = {};
static ULONG g_EmuNv2aVpWriteDword = 0;   // SET_TRANSFORM_PROGRAM upload cursor
static ULONG g_EmuNv2aVpInstrCount = 0;   // highest loaded instruction slot + 1
static ULONG g_EmuNv2aVpStart = 0;        // SET_TRANSFORM_PROGRAM_START
static ULONG g_EmuNv2aVpExecMode = 0;     // SET_TRANSFORM_EXECUTION_MODE & 3
static float g_EmuNv2aTransformConstant[192 * 4] = {};
static ULONG g_EmuNv2aConstWriteFloat = 0; // SET_TRANSFORM_CONSTANT upload cursor
// Depth buffer (Phase 3): bound zeta surface + the depth-test state. Testing is
// off by default so Phase 0-2 titles (which never enable it) are unaffected.
static ULONG g_EmuNv2aContextDmaZeta = 0;
static ULONG g_EmuNv2aSurfaceZetaOffset = 0;
static ULONG g_EmuNv2aSurfacePitchZeta = 0;
static ULONG g_EmuNv2aSurfaceZetaFormat = 0; // 1=Z16, 2=Z24S8
static bool  g_EmuNv2aDepthTest = false;
static bool  g_EmuNv2aDepthWrite = true;
static ULONG g_EmuNv2aDepthFunc = 0x0201;    // LESS
// Alpha blending. Disabled by default (source overwrites), so titles that never
// enable it are unaffected. Defaults mirror the NV2A reset state (ONE/ZERO/ADD).
static bool  g_EmuNv2aBlendEnable = false;
static ULONG g_EmuNv2aBlendSFactor = 0x0001; // ONE
static ULONG g_EmuNv2aBlendDFactor = 0x0000; // ZERO
static ULONG g_EmuNv2aBlendEquation = 0x8006; // FUNC_ADD
// Stencil (in the Z24S8 zeta buffer's low byte). Off by default.
static bool  g_EmuNv2aStencilTest = false;
static ULONG g_EmuNv2aStencilFunc = 0x0207;      // ALWAYS
static ULONG g_EmuNv2aStencilRef = 0;
static ULONG g_EmuNv2aStencilFuncMask = 0xFF;
static ULONG g_EmuNv2aStencilMask = 0xFF;        // write mask
static ULONG g_EmuNv2aStencilOpFail = 0x1E00;    // KEEP
static ULONG g_EmuNv2aStencilOpZFail = 0x1E00;
static ULONG g_EmuNv2aStencilOpZPass = 0x1E00;

static void EmuNv2aDumpSourceTexture(ULONG Stage);
static void EmuNv2aDumpScanout(ULONG PhysicalAddress);
static void EmuNv2aRasterizeDrawArrays(ULONG Start, ULONG Count);
static void EmuNv2aWriteBackendSemaphore(ULONG Data);

// Opt-in PGRAPH method histogram (CXBX_NV2A_METHOD_STATS=1): count every
// dispatched method per object class so a title's method vocabulary — in
// particular which draw/clear methods it uses — can be enumerated from one run
// without drowning in per-method trace lines. Methods are 4-byte-aligned
// offsets < 0x2000, so 0x800 slots per class; the begin-op distribution of
// SET_BEGIN_END is tracked separately (the primitive types matter, not just
// the count). Dumped every 64k recorded methods and again from EmuCleanup.
#define EmuNv2aStatsClassMax  16u
#define EmuNv2aStatsSlotCount 0x800u

struct EmuNv2aMethodStats
{
    ULONG Class;
    ULONG Binds;
    ULONG Counts[EmuNv2aStatsSlotCount];
};

static EmuNv2aMethodStats g_EmuNv2aMethodStats[EmuNv2aStatsClassMax] = {};
static ULONG g_EmuNv2aStatsClassCount = 0;
static ULONG g_EmuNv2aStatsBeginOps[32] = {};
static ULONG g_EmuNv2aStatsTotal = 0;
static int   g_EmuNv2aStatsEnabled = -1;

static bool EmuNv2aMethodStatsEnabled()
{
    if(g_EmuNv2aStatsEnabled < 0)
    {
        char Buffer[8] = {};
        DWORD Length = GetEnvironmentVariableA("CXBX_NV2A_METHOD_STATS", Buffer, sizeof(Buffer));
        g_EmuNv2aStatsEnabled = (Length > 0 && Buffer[0] == '1') ? 1 : 0;
    }
    return g_EmuNv2aStatsEnabled == 1;
}

extern "C" void EmuNv2aDumpMethodStats(const char *Reason)
{
    if(g_EmuNv2aStatsEnabled != 1 || g_EmuNv2aStatsTotal == 0)
        return;

    printf("NV2A| stats dump (%s) total=%lu\n", Reason, g_EmuNv2aStatsTotal);

    for(ULONG i = 0; i < g_EmuNv2aStatsClassCount; i++)
    {
        EmuNv2aMethodStats *Stats = &g_EmuNv2aMethodStats[i];

        if(Stats->Binds != 0)
            printf("NV2A| stats class=0x%.02lX bind count=%lu\n", Stats->Class, Stats->Binds);

        for(ULONG Slot = 0; Slot < EmuNv2aStatsSlotCount; Slot++)
        {
            if(Stats->Counts[Slot] != 0)
                printf("NV2A| stats class=0x%.02lX method=0x%.04lX count=%lu\n",
                       Stats->Class, Slot * 4, Stats->Counts[Slot]);
        }
    }

    for(ULONG Op = 0; Op < 32; Op++)
    {
        if(g_EmuNv2aStatsBeginOps[Op] != 0)
            printf("NV2A| stats begin_op=0x%.02lX count=%lu\n", Op, g_EmuNv2aStatsBeginOps[Op]);
    }

    fflush(stdout);
}

static EmuNv2aMethodStats *EmuNv2aStatsForClass(ULONG Class)
{
    for(ULONG i = 0; i < g_EmuNv2aStatsClassCount; i++)
    {
        if(g_EmuNv2aMethodStats[i].Class == Class)
            return &g_EmuNv2aMethodStats[i];
    }

    if(g_EmuNv2aStatsClassCount >= EmuNv2aStatsClassMax)
        return NULL;

    EmuNv2aMethodStats *Stats = &g_EmuNv2aMethodStats[g_EmuNv2aStatsClassCount++];
    Stats->Class = Class;
    return Stats;
}

static void EmuNv2aRecordMethodStat(ULONG Class, ULONG Method, ULONG Data)
{
    if(!EmuNv2aMethodStatsEnabled())
        return;

    EmuNv2aMethodStats *Stats = EmuNv2aStatsForClass(Class);
    if(Stats == NULL)
        return;

    if(Method == 0)
        Stats->Binds++;
    else if((Method / 4) < EmuNv2aStatsSlotCount)
        Stats->Counts[Method / 4]++;

    if(Method == NV097_SET_BEGIN_END && Data != 0 && Data < 32)
        g_EmuNv2aStatsBeginOps[Data]++;

    if((++g_EmuNv2aStatsTotal % 65536) == 0)
        EmuNv2aDumpMethodStats("periodic");
}

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

        EmuNv2aRecordMethodStat(Class, Method, Data);
        NV2A_TRACE_METHOD(Class, Method, Data);
        return;
    }

    if(Class == 0)
        Class = NV_CLASS_KELVIN;

    EmuNv2aRecordMethodStat(Class, Method, Data);

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
            else if(Method == StageMethod + (NV097_SET_TEXTURE_FILTER_0 - NV097_SET_TEXTURE_OFFSET_0))
                g_EmuNv2aTexture[Stage].Filter = Data;

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

        // Phase 0 rasterizer state: track the color surface, the vertex arrays,
        // and the active primitive so a DRAW_ARRAYS can be turned into pixels.
        switch(Method)
        {
            case NV097_SET_CONTEXT_DMA_COLOR:    g_EmuNv2aContextDmaColor = Data; break;
            case NV097_SET_CONTEXT_DMA_VERTEX_A: g_EmuNv2aContextDmaVertex = Data; break;
            case NV097_SET_CONTEXT_DMA_ZETA:     g_EmuNv2aContextDmaZeta = Data; break;
            case NV097_SET_CONTEXT_DMA_SEMAPHORE: g_EmuNv2aContextDmaSemaphore = Data; break;
            case NV097_SET_SEMAPHORE_OFFSET:     g_EmuNv2aSemaphoreOffset = Data; break;
            case NV097_BACK_END_WRITE_SEMAPHORE_RELEASE:
                EmuNv2aWriteBackendSemaphore(Data);
                break;
            case NV097_SET_SURFACE_CLIP_HORIZONTAL: g_EmuNv2aSurfaceClipW = (Data >> 16) & 0xFFFF; break;
            case NV097_SET_SURFACE_CLIP_VERTICAL:   g_EmuNv2aSurfaceClipH = (Data >> 16) & 0xFFFF; break;
            case NV097_SET_SURFACE_FORMAT:       g_EmuNv2aSurfaceZetaFormat = (Data >> 4) & 0xF; break;
            case NV097_SET_SURFACE_PITCH:
                g_EmuNv2aSurfacePitchColor = Data & 0xFFFF;
                g_EmuNv2aSurfacePitchZeta = (Data >> 16) & 0xFFFF;
                break;
            case NV097_SET_SURFACE_COLOR_OFFSET: g_EmuNv2aSurfaceColorOffset = Data; break;
            case NV097_SET_SURFACE_ZETA_OFFSET:  g_EmuNv2aSurfaceZetaOffset = Data; break;
            case NV097_SET_DEPTH_TEST_ENABLE:    g_EmuNv2aDepthTest = (Data != 0); break;
            case NV097_SET_DEPTH_FUNC:           g_EmuNv2aDepthFunc = Data; break;
            case NV097_SET_DEPTH_MASK:           g_EmuNv2aDepthWrite = (Data != 0); break;
            case NV097_SET_BLEND_ENABLE:         g_EmuNv2aBlendEnable = (Data != 0); break;
            case NV097_SET_BLEND_FUNC_SFACTOR:   g_EmuNv2aBlendSFactor = Data; break;
            case NV097_SET_BLEND_FUNC_DFACTOR:   g_EmuNv2aBlendDFactor = Data; break;
            case NV097_SET_BLEND_EQUATION:       g_EmuNv2aBlendEquation = Data; break;
            case NV097_SET_STENCIL_TEST_ENABLE:  g_EmuNv2aStencilTest = (Data != 0); break;
            case NV097_SET_STENCIL_MASK:         g_EmuNv2aStencilMask = Data & 0xFF; break;
            case NV097_SET_STENCIL_FUNC:         g_EmuNv2aStencilFunc = Data; break;
            case NV097_SET_STENCIL_FUNC_REF:     g_EmuNv2aStencilRef = Data & 0xFF; break;
            case NV097_SET_STENCIL_FUNC_MASK:    g_EmuNv2aStencilFuncMask = Data & 0xFF; break;
            case NV097_SET_STENCIL_OP_FAIL:      g_EmuNv2aStencilOpFail = Data; break;
            case NV097_SET_STENCIL_OP_ZFAIL:     g_EmuNv2aStencilOpZFail = Data; break;
            case NV097_SET_STENCIL_OP_ZPASS:     g_EmuNv2aStencilOpZPass = Data; break;
            case NV097_SET_BEGIN_END:            g_EmuNv2aBeginOp = Data; break;
            case NV097_DRAW_ARRAYS:
                EmuNv2aRasterizeDrawArrays(Data & 0xFFFFFF, ((Data >> 24) & 0xFF) + 1);
                break;
            case NV097_SET_TRANSFORM_EXECUTION_MODE: g_EmuNv2aVpExecMode = Data & 0x3; break;
            case NV097_SET_TRANSFORM_PROGRAM_LOAD:   g_EmuNv2aVpWriteDword = Data * 4; break;
            case NV097_SET_TRANSFORM_PROGRAM_START:  g_EmuNv2aVpStart = Data; break;
            case NV097_SET_TRANSFORM_CONSTANT_LOAD:  g_EmuNv2aConstWriteFloat = Data * 4; break;
            default:
                if(Method >= NV097_SET_TRANSFORM_PROGRAM &&
                   Method < NV097_SET_TRANSFORM_PROGRAM + 0x80)
                {
                    // Microcode upload: append one 32-bit word at the load cursor.
                    if(g_EmuNv2aVpWriteDword < EmuNv2aVpMaxInstr * 4)
                    {
                        g_EmuNv2aVpProgram[g_EmuNv2aVpWriteDword++] = Data;
                        ULONG Instrs = (g_EmuNv2aVpWriteDword + 3) / 4;
                        if(Instrs > g_EmuNv2aVpInstrCount)
                            g_EmuNv2aVpInstrCount = Instrs;
                    }
                }
                else if(Method >= NV097_SET_TRANSFORM_CONSTANT &&
                        Method < NV097_SET_TRANSFORM_CONSTANT + 0x80)
                {
                    // Constant memory upload: one float per word at the load cursor.
                    if(g_EmuNv2aConstWriteFloat < 192 * 4)
                        memcpy(&g_EmuNv2aTransformConstant[g_EmuNv2aConstWriteFloat++], &Data, 4);
                }
                else if(Method >= NV097_SET_COMPOSITE_MATRIX && Method < NV097_SET_COMPOSITE_MATRIX + 64)
                {
                    memcpy(&g_EmuNv2aCompositeMatrix[(Method - NV097_SET_COMPOSITE_MATRIX) / 4], &Data, 4);
                }
                else if(Method >= NV097_SET_VERTEX_DATA_ARRAY_OFFSET &&
                   Method < NV097_SET_VERTEX_DATA_ARRAY_OFFSET + EmuNv2aVertexAttrCount * 4)
                {
                    g_EmuNv2aVertexArray[(Method - NV097_SET_VERTEX_DATA_ARRAY_OFFSET) / 4].Offset = Data;
                }
                else if(Method >= NV097_SET_VERTEX_DATA_ARRAY_FORMAT &&
                        Method < NV097_SET_VERTEX_DATA_ARRAY_FORMAT + EmuNv2aVertexAttrCount * 4)
                {
                    g_EmuNv2aVertexArray[(Method - NV097_SET_VERTEX_DATA_ARRAY_FORMAT) / 4].Format = Data;
                }
                else if(Method >= NV097_SET_VIEWPORT_OFFSET && Method < NV097_SET_VIEWPORT_OFFSET + 16)
                {
                    memcpy(&g_EmuNv2aViewportOffset[(Method - NV097_SET_VIEWPORT_OFFSET) / 4], &Data, 4);
                }
                else if(Method >= NV097_SET_VIEWPORT_SCALE && Method < NV097_SET_VIEWPORT_SCALE + 16)
                {
                    memcpy(&g_EmuNv2aViewportScale[(Method - NV097_SET_VIEWPORT_SCALE) / 4], &Data, 4);
                }
                break;
        }
    }

    EmuNv2aStoreRegister(NV_PGRAPH + (Method & 0x1FFC), Data);

    if(Class == NV_CLASS_KELVIN && Method == NV097_NO_OPERATION && Data != 0)
    {
        ULONG Intr = EmuNv2aCachedRegister(NV_PGRAPH_INTR, 0);
        EmuNv2aStoreRegister(NV_PGRAPH_INTR, Intr | 0x00100000);
    }

    NV2A_TRACE_METHOD(Class, Method, Data);
}

static void EmuNv2aRunPusher();

// Replay an XDK CPU-copy push buffer through the same method decoder used by
// the register-level NV2A pusher. In-place GPU buffers use physical addresses
// and are intentionally left to the MMIO path.
extern "C" bool EmuNv2aExecutePushBuffer(const DWORD *Buffer, DWORD Size)
{
    if(Buffer == NULL || Size < sizeof(DWORD) || Size > 16 * 1024 * 1024)
    {
        return false;
    }

    DWORD Offset = 0;
    DWORD Guard = 0;
    DWORD ReturnOffset = 0;
    bool InSubroutine = false;

    __try
    {
        while(Offset + sizeof(DWORD) <= Size && Guard++ < Size / sizeof(DWORD) * 2)
        {
            DWORD Word = Buffer[Offset / sizeof(DWORD)];
            Offset += sizeof(DWORD);
            NV2A_TRACE_PB(Word);

            if((Word & 0xE0000003) == 0x20000000 || (Word & 3) == 1)
            {
                DWORD Target = Word & ((Word & 0xE0000003) == 0x20000000 ? 0x1FFFFFFC : 0xFFFFFFFC);
                if(Target >= Size)
                {
                    return false;
                }
                Offset = Target;
                continue;
            }

            if((Word & 3) == 2)
            {
                DWORD Target = Word & 0xFFFFFFFC;
                if(InSubroutine || Target >= Size)
                {
                    return false;
                }

                ReturnOffset = Offset;
                InSubroutine = true;
                Offset = Target;
                continue;
            }

            if(Word == 0x00020000)
            {
                if(!InSubroutine)
                {
                    return false;
                }

                Offset = ReturnOffset;
                InSubroutine = false;
                continue;
            }

            if((Word & 0xE0030003) != 0 && (Word & 0xE0030003) != 0x40000000)
            {
                return false;
            }

            bool Incrementing = (Word & 0xE0030003) == 0;
            DWORD Method = Word & 0x1FFC;
            DWORD Subchannel = (Word >> 13) & 0x07;
            DWORD Count = (Word >> 18) & 0x07FF;

            if(Count > (Size - Offset) / sizeof(DWORD))
            {
                return false;
            }

            for(DWORD i = 0; i < Count; i++)
            {
                DWORD Data = Buffer[Offset / sizeof(DWORD)];
                Offset += sizeof(DWORD);
                EmuNv2aHandlePgraphMethod(Subchannel, Method, Data);
                if(Incrementing)
                {
                    Method += sizeof(DWORD);
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return Offset == Size;
}

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
        case NV_PFIFO_CACHE1_DMA_SUBROUTINE:
        case NV_PFIFO_CACHE1_DMA_DCOUNT:
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
        Value = (Value >> Shift) & 0xFF;
    else if(Size == 2)
        Value = (Value >> Shift) & 0xFFFF;

    // One value-carrying line per read regardless of which opcode decoder
    // serviced it — the per-site traces name the access shape but omit the
    // value, which is exactly what's needed to spot a failed guest poll.
    if(EmuMmioTraceEnabled())
    {
        printf("Emu (0x%lX): MMIO rd%lu 0x%.08lX -> 0x%.08lX\n",
               GetCurrentThreadId(), Size, Address, Value);
        fflush(stdout);
    }

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
    {
        g_EmuNv2aScanoutAddress = Value & 0x0FFFFFFF;
        EmuNv2aDumpScanout(Value & 0x0FFFFFFF);
    }

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

// Physical-map aliases in the 0x80000000 identity view frequently target real
// host contiguous allocations: the guest takes the physical address D3D stored
// in a resource, adds the identity-map base, and streams data through it
// (XGRAPHCL swizzle copies do this with MMX moves). Those bytes must land in
// the real allocation -- the NV2A model reads textures and pushbuffers through
// raw host pointers -- so resolve the alias to its host backing first; the
// shadow pool stays the fallback for unbacked physical ranges (kernel-image
// decode, the probe apertures at 0xF0000000+).
extern "C" ULONG EmuContiguousBlockBase(ULONG HostAddress, ULONG *BlockSize);
extern "C" ULONG EmuContiguousHostFromPhysical(ULONG PhysicalAddress);

static BYTE *EmuPhysicalHostSpan(ULONG Address, ULONG Size)
{
    if(Address < EmuPhysicalMapBase || Address > EmuPhysicalMapEnd || Size == 0)
        return NULL;

    ULONG Physical = Address - EmuPhysicalMapBase;
    ULONG BlockSize = 0;
    ULONG Base = EmuContiguousBlockBase(Physical, &BlockSize);

    if(Base != 0 && Physical + Size <= Base + BlockSize)
        return (BYTE *)(uintptr_t)Physical;

    ULONG Host = EmuContiguousHostFromPhysical(Physical);
    if(Host != 0)
    {
        Base = EmuContiguousBlockBase(Host, &BlockSize);
        if(Base != 0 && Host + Size <= Base + BlockSize)
            return (BYTE *)(uintptr_t)Host;
    }

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

    BYTE *Host = EmuPhysicalHostSpan(Address, Size);
    if(Host != NULL)
    {
        ULONG Result = 0;
        for(ULONG i = 0; i < Size; i++)
            Result |= (ULONG)Host[i] << (i * 8);
        *Value = Result;
        return true;
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

    BYTE *Host = EmuPhysicalHostSpan(Address, Size);
    if(Host != NULL)
    {
        for(ULONG i = 0; i < Size; i++)
            Host[i] = (BYTE)(Value >> (i * 8));
        return true;
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

// ******************************************************************
// * NV2A push-buffer completion notifiers (D3D-HLE mode)
// ******************************************************************
// A title's raw NV2A push-buffer ends a batch by having the GPU write a
// completion sentinel (0x8000BEEF) into a notifier in physical memory, then
// spins reading that notifier until it appears. In High-Level-Emulation mode
// Cxbx executes the equivalent work synchronously through the host Direct3D
// device and never runs the raw push-buffer, so the notifier is never written
// and the guest spins forever (NestopiaX 1.3's menu/video path does exactly
// this). Since the host work the notifier stands for is already complete by the
// time the guest polls, signalling completion is legitimate: when a title
// compares a physical-map location against the sentinel, write it through so the
// poll (and any later read of the same notifier) terminates.
static const ULONG EmuNv2aNotifierSentinel = 0x8000BEEF;

static bool EmuNv2aNotifySatisfyEnabled()
{
    // On by default: in D3D-HLE mode the host has already performed the batch's
    // work synchronously by the time the guest polls, so reporting the NV2A
    // completion sentinel is legitimate. Titles that spin on it -- NestopiaX
    // 1.3's XMV menu/video path -- otherwise hang or, worse, present torn frames
    // (the decoder blocks mid-frame, so the YUY2 buffer the overlay samples is
    // half-written and shows as garbage). Opt out with CXBX_NV2A_NO_NOTIFY when
    // diagnosing the raw push-buffer path.
    static int s_Enabled = -1;
    if(s_Enabled < 0)
        s_Enabled = getenv("CXBX_NV2A_NO_NOTIFY") != NULL ? 0 : 1;

    return s_Enabled != 0;
}

// If the guest is polling a physical notifier for the NV2A completion sentinel,
// write it through and report the satisfied value back so the emulated compare
// reflects equality on this very iteration. Returns true when it acted.
static bool EmuMaybeSatisfyNv2aNotifier(ULONG Address, ULONG ComparedValue, ULONG *MemoryValue)
{
    if(!EmuNv2aNotifySatisfyEnabled() || ComparedValue != EmuNv2aNotifierSentinel)
        return false;

    if(MemoryValue != NULL && *MemoryValue == EmuNv2aNotifierSentinel)
        return false;

    if(!EmuWritePhysicalMap(Address, 4, EmuNv2aNotifierSentinel))
        return false;

    if(MemoryValue != NULL)
        *MemoryValue = EmuNv2aNotifierSentinel;

    static ULONG s_Logged = 0;
    if(s_Logged < 16)
    {
        s_Logged++;
        printf("Emu (0x%lX): Satisfied NV2A completion notifier 0x%.08lX.\n",
               GetCurrentThreadId(), Address);
        fflush(stdout);
    }

    return true;
}

// Opt-in thread-EIP watchdog: after a delay, suspend every other thread in the
// process and log its EIP/ESP, so a stalled title's thread landscape is visible.
static DWORD WINAPI EmuThreadEipWatchdog(LPVOID)
{
    // CXBX_FENCE_DUMP holds the snapshot interval in seconds; anything
    // unparsable keeps the legacy one-shot-ish default of 14s. Snapshots
    // repeat so a stall that develops minutes in (e.g. after a title's asset
    // load) is still captured.
    ULONG IntervalMs = 14000;
    const char *Value = getenv("CXBX_FENCE_DUMP");
    if(Value != NULL)
    {
        ULONG Seconds = strtoul(Value, NULL, 10);
        if(Seconds >= 1 && Seconds <= 3600)
            IntervalMs = Seconds * 1000;
    }

    printf("WATCHDOG: armed, snapshot interval %lums.\n", IntervalMs);
    fflush(stdout);

    for(;;)
    {
        Sleep(IntervalMs);

        DWORD Pid = GetCurrentProcessId();
        DWORD Self = GetCurrentThreadId();
        HANDLE Snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if(Snap == INVALID_HANDLE_VALUE)
            continue; // transient failure must not end the watchdog

        THREADENTRY32 Te;
        Te.dwSize = sizeof(Te);
        printf("WATCHDOG: --- thread EIP snapshot ---\n");
        if(Thread32First(Snap, &Te))
        {
            do
            {
                if(Te.th32OwnerProcessID != Pid || Te.th32ThreadID == Self)
                    continue;

                HANDLE Th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, Te.th32ThreadID);
                if(Th == NULL)
                    continue;

                // Capture context and stack words while suspended, but do NOT
                // call printf (or anything taking a CRT lock) until the thread
                // is resumed -- the target may be mid-printf holding the stdio
                // lock, and blocking on it here would leave the target frozen
                // forever (watchdog deadlock).
                CONTEXT Ctx;
                Ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                ULONG StackCopy[96];
                ULONG StackWords = 0;
                bool GotContext = false;

                SuspendThread(Th);
                if(GetThreadContext(Th, &Ctx))
                {
                    GotContext = true;
                    while(StackWords < 96 &&
                          EmuIsReadableRange(Ctx.Esp + StackWords * 4, 4))
                    {
                        StackCopy[StackWords] = *(ULONG*)(Ctx.Esp + StackWords * 4);
                        StackWords++;
                    }
                }
                ResumeThread(Th);
                CloseHandle(Th);

                if(GotContext)
                {
                    printf("WATCHDOG: tid 0x%lX eip=0x%08X esp=0x%08X eax=0x%08X ecx=0x%08X edx=0x%08X esi=0x%08X edi=0x%08X\n",
                           Te.th32ThreadID, (unsigned)Ctx.Eip, (unsigned)Ctx.Esp,
                           (unsigned)Ctx.Eax, (unsigned)Ctx.Ecx, (unsigned)Ctx.Edx,
                           (unsigned)Ctx.Esi, (unsigned)Ctx.Edi);

                    // Attribute the wait: return addresses into loaded modules
                    // or guest code name who is waiting, not just that the
                    // thread waits in ntdll.
                    char Where[MAX_PATH + 16];
                    ULONG Shown = 0;
                    for(ULONG Slot = 0; Slot < StackWords && Shown < 8; Slot++)
                    {
                        ULONG SlotValue = StackCopy[Slot];
                        if(EmuHostAddressToModuleOffset(SlotValue, Where, sizeof(Where)) != NULL)
                        {
                            printf("WATCHDOG:   [%02lu] 0x%08lX = %s\n", Slot, SlotValue, Where);
                            Shown++;
                        }
                        else if(SlotValue >= 0x00010000 && SlotValue < 0x10000000 &&
                                EmuLooksLikeReturnAddress(SlotValue))
                        {
                            printf("WATCHDOG:   [%02lu] 0x%08lX = guest return\n", Slot, SlotValue);
                            Shown++;
                        }
                    }
                }
            }
            while(Thread32Next(Snap, &Te));
        }
        CloseHandle(Snap);
        printf("WATCHDOG: --- end ---\n");
        fflush(stdout);
    }
}

static bool EmuWritePhysicalMapBytes(ULONG Address, const BYTE *Data, ULONG Size)
{
    ULONG End = Address + Size - 1;
    if(Size == 0 || Data == NULL || End < Address || !EmuIsPhysicalMapAddress(Address) ||
       !EmuIsPhysicalMapAddress(End))
    {
        return false;
    }

    BYTE *Host = EmuPhysicalHostSpan(Address, Size);
    if(Host != NULL)
    {
        memcpy(Host, Data, Size);
        return true;
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

    BYTE *Host = EmuPhysicalHostSpan(Address, Size);
    if(Host != NULL)
    {
        memcpy(Dst, Host, Size);
        return true;
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

// Execute NV097_BACK_END_WRITE_SEMAPHORE_RELEASE: real hardware writes the
// release value into the bound semaphore DMA object's memory once the back end
// drains; D3D's fence spins on that dword, so a model that ignores the method
// stalls the title forever after its first pushbuffer (KOF2002's exact hang).
// The bound handle has usually already been RAMHT-resolved to an instance by
// the 0x180-0x200 rewrite in EmuNv2aHandlePgraphMethod, so try the handle path
// first and fall back to reading the DMA object straight from RAMIN.
static void EmuNv2aWriteBackendSemaphore(ULONG Data)
{
    ULONG DmaInstance = g_EmuNv2aContextDmaSemaphore;
    ULONG ResolvedInstance = 0;
    ULONG ObjectClass = 0;
    ULONG Base = 0;
    ULONG Flags = 0, Limit = 0, Frame = 0;

    if(EmuNv2aRamhtLookup(g_EmuNv2aContextDmaSemaphore,
                          &ResolvedInstance, &ObjectClass))
    {
        DmaInstance = ResolvedInstance;
    }

    if(DmaInstance <= EmuNv2aRaminSize - 12)
    {
        Flags = EmuNv2aReadRamin32(EmuNv2aRaminBase + DmaInstance);
        Limit = EmuNv2aReadRamin32(EmuNv2aRaminBase + DmaInstance + 4);
        Frame = EmuNv2aReadRamin32(EmuNv2aRaminBase + DmaInstance + 8);
        // NV_DMA_IN_MEMORY object: word0 = class | adjust<<20, word2 = page
        // frame | target bits. The XDK D3D semaphore object carries the
        // sub-page offset in adjust, so honor it (frame|class-bits is wrong).
        Base = (Frame & 0xFFFFF000) + ((Flags >> 20) & 0x00000FFF);
    }

    if(g_EmuNv2aSemaphoreOffset > Limit ||
       Limit - g_EmuNv2aSemaphoreOffset < sizeof(ULONG) - 1)
    {
        printf("Emu (0x%lX): NV2A semaphore release out of range (limit=0x%.08lX off=0x%.08lX val=0x%.08lX).\n",
               GetCurrentThreadId(), Limit, g_EmuNv2aSemaphoreOffset, Data);
        fflush(stdout);
        return;
    }

    ULONG Host = EmuNv2aHostPointer(Base + g_EmuNv2aSemaphoreOffset);

    if(Host == 0)
    {
        printf("Emu (0x%lX): NV2A semaphore release unresolved (dma=0x%.08lX obj={0x%.08lX,0x%.08lX,0x%.08lX} base=0x%.08lX off=0x%.08lX val=0x%.08lX).\n",
               GetCurrentThreadId(), g_EmuNv2aContextDmaSemaphore, Flags, Limit, Frame,
               Base, g_EmuNv2aSemaphoreOffset, Data);
        fflush(stdout);
        return;
    }

    *(volatile ULONG *)(uintptr_t)Host = Data;

    static ULONG s_ReleaseCount = 0;
    if(s_ReleaseCount < 8 || (s_ReleaseCount % 1000) == 0)
    {
        printf("Emu (0x%lX): NV2A semaphore release #%lu: [0x%.08lX+0x%lX] <- 0x%.08lX.\n",
               GetCurrentThreadId(), s_ReleaseCount, Base, g_EmuNv2aSemaphoreOffset, Data);
        fflush(stdout);
    }
    s_ReleaseCount++;
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

// Decode a KELVIN texture color format code to bytes-per-texel, an unpack kind
// (0=A8R8G8B8/X8R8G8B8, 1=R5G6B5, 2=A1R5G5B5, 3=A4R4G4B4, 4=Y8), and whether it
// is swizzled (Morton) or linear. Returns false for unsupported codes.
static bool EmuNv2aTextureFormatInfo(ULONG Color, ULONG *Bpp, ULONG *Kind, bool *Swizzled)
{
    switch(Color)
    {
        case 0x00: *Bpp = 1; *Kind = 4; *Swizzled = true;  return true; // SZ_Y8
        case 0x13: *Bpp = 1; *Kind = 4; *Swizzled = false; return true; // LU_Y8
        case 0x02: *Bpp = 2; *Kind = 2; *Swizzled = true;  return true; // SZ_A1R5G5B5
        case 0x03: *Bpp = 2; *Kind = 2; *Swizzled = true;  return true; // SZ_X1R5G5B5
        case 0x10: *Bpp = 2; *Kind = 2; *Swizzled = false; return true; // LU_A1R5G5B5
        case 0x04: *Bpp = 2; *Kind = 3; *Swizzled = true;  return true; // SZ_A4R4G4B4
        case 0x19: *Bpp = 2; *Kind = 3; *Swizzled = false; return true; // LU_A4R4G4B4
        case 0x05: *Bpp = 2; *Kind = 1; *Swizzled = true;  return true; // SZ_R5G6B5
        case 0x11: *Bpp = 2; *Kind = 1; *Swizzled = false; return true; // LU_R5G6B5
        case 0x06: *Bpp = 4; *Kind = 0; *Swizzled = true;  return true; // SZ_A8R8G8B8
        case 0x07: *Bpp = 4; *Kind = 0; *Swizzled = true;  return true; // SZ_X8R8G8B8
        case 0x12: *Bpp = 4; *Kind = 0; *Swizzled = false; return true; // LU_A8R8G8B8
        case 0x1A: *Bpp = 4; *Kind = 0; *Swizzled = false; return true; // LU_A8R8G8B8 (rect)
        case 0x1C: *Bpp = 4; *Kind = 0; *Swizzled = false; return true; // LU_X8R8G8B8
        case 0x1E: *Bpp = 4; *Kind = 0; *Swizzled = false; return true; // LU_A8B8G8R8 approx
        default:   return false;
    }
}

// Unpack one raw texel (per the format Kind) to 0xAARRGGBB. Alpha is opaque
// except for the A8R8G8B8 kind, which carries it in the high byte.
static ULONG EmuNv2aUnpackTexel(ULONG Raw, ULONG Kind)
{
    ULONG R = 0, G = 0, B = 0, A = 0xFF;
    switch(Kind)
    {
        case 0: B = Raw & 0xFF; G = (Raw >> 8) & 0xFF; R = (Raw >> 16) & 0xFF; A = (Raw >> 24) & 0xFF; break;
        case 1: R = ((Raw >> 11) & 0x1F) << 3; G = ((Raw >> 5) & 0x3F) << 2; B = (Raw & 0x1F) << 3; break;
        case 2: R = ((Raw >> 10) & 0x1F) << 3; G = ((Raw >> 5) & 0x1F) << 3; B = (Raw & 0x1F) << 3;
                A = (Raw & 0x8000) ? 0xFF : 0xFF; break;
        case 3: A = ((Raw >> 12) & 0xF) << 4; R = ((Raw >> 8) & 0xF) << 4; G = ((Raw >> 4) & 0xF) << 4; B = (Raw & 0xF) << 4; break;
        case 4: R = G = B = Raw & 0xFF; break;
    }
    return (A << 24) | (R << 16) | (G << 8) | B;
}

// A bound texture prepared for point sampling: the decoded texel block plus the
// geometry needed to address it. Set up once per DRAW_ARRAYS, freed after.
struct EmuNv2aSampler
{
    BYTE  *Data;
    ULONG  Size, Width, Height, Bpp, Kind, LogW, LogH;
    bool   Swizzled;
    bool   Bilinear;   // mag filter == LINEAR
};

static bool EmuNv2aSetupSampler(ULONG Stage, EmuNv2aSampler *S)
{
    ZeroMemory(S, sizeof(*S));
    if(Stage >= EmuNv2aTextureStageCount)
        return false;

    ULONG Format = g_EmuNv2aTexture[Stage].Format;
    ULONG Color = (Format >> 8) & 0xFF;
    ULONG SizeU = (Format >> 20) & 0xF;
    ULONG SizeV = (Format >> 24) & 0xF;
    ULONG ImageRect = g_EmuNv2aTexture[Stage].ImageRect;
    if(!EmuNv2aTextureFormatInfo(Color, &S->Bpp, &S->Kind, &S->Swizzled))
        return false;

    if(S->Swizzled || ImageRect == 0)
    {
        S->Width = 1ul << SizeU;
        S->Height = 1ul << SizeV;
    }
    else
    {
        S->Width = (ImageRect >> 16) & 0xFFFF;
        S->Height = ImageRect & 0xFFFF;
    }
    if(S->Width == 0 || S->Height == 0 || S->Width > 4096 || S->Height > 4096)
        return false;

    S->LogW = EmuNv2aLog2(S->Width);
    S->LogH = EmuNv2aLog2(S->Height);
    S->Bilinear = (((g_EmuNv2aTexture[Stage].Filter >> 24) & 0xF) == 2); // MAG == LINEAR

    ULONG Base = EmuNv2aResolveDmaBase(g_EmuNv2aContextDmaAHandle);
    ULONG Address = Base + g_EmuNv2aTexture[Stage].Offset;
    S->Size = S->Width * S->Height * S->Bpp;
    S->Data = new BYTE[S->Size];

    ULONG Host = EmuNv2aHostPointer(Address);
    if(Host != 0 && EmuTryReadHost(Host, S->Data, S->Size))
        return true;

    ULONG Phys = Address;
    if(!EmuIsPhysicalMapAddress(Phys))
        Phys = EmuPhysicalMapBase + (Phys & EmuPhysicalRamMirrorMask);
    if(EmuReadPhysicalMapBlock(Phys, S->Data, S->Size))
        return true;

    delete[] S->Data;
    S->Data = NULL;
    return false;
}

static void EmuNv2aFreeSampler(EmuNv2aSampler *S)
{
    if(S->Data != NULL)
    {
        delete[] S->Data;
        S->Data = NULL;
    }
}

// Fetch one texel at integer coordinates (clamped to the edge, swizzle-aware).
static ULONG EmuNv2aFetchTexel(const EmuNv2aSampler *S, int X, int Y)
{
    if(X < 0) X = 0; if(X >= (int)S->Width)  X = (int)S->Width - 1;
    if(Y < 0) Y = 0; if(Y >= (int)S->Height) Y = (int)S->Height - 1;

    ULONG TexelIndex = S->Swizzled ? EmuNv2aSwizzleTexelIndex(X, Y, S->LogW, S->LogH)
                                   : ((ULONG)Y * S->Width + (ULONG)X);
    ULONG ByteOffset = TexelIndex * S->Bpp;
    ULONG Raw = 0;
    for(ULONG b = 0; b < S->Bpp && ByteOffset + b < S->Size; b++)
        Raw |= (ULONG)S->Data[ByteOffset + b] << (b * 8);
    return EmuNv2aUnpackTexel(Raw, S->Kind);
}

// Sample at normalized (u,v): nearest, or bilinear (4-texel blend) when the mag
// filter is LINEAR.
static ULONG EmuNv2aSampleTexel(const EmuNv2aSampler *S, float u, float v)
{
    if(!S->Bilinear)
        return EmuNv2aFetchTexel(S, (int)(u * (float)S->Width), (int)(v * (float)S->Height));

    // Sample the 2x2 texel neighborhood around the point (texel centers at +0.5).
    float fx = u * (float)S->Width  - 0.5f;
    float fy = v * (float)S->Height - 0.5f;
    int x0 = (int)fx; if((float)x0 > fx) x0--;   // floor without libm
    int y0 = (int)fy; if((float)y0 > fy) y0--;
    float wx = fx - (float)x0, wy = fy - (float)y0;

    ULONG c00 = EmuNv2aFetchTexel(S, x0,     y0);
    ULONG c10 = EmuNv2aFetchTexel(S, x0 + 1, y0);
    ULONG c01 = EmuNv2aFetchTexel(S, x0,     y0 + 1);
    ULONG c11 = EmuNv2aFetchTexel(S, x0 + 1, y0 + 1);

    ULONG Out = 0;
    for(int sh = 0; sh < 32; sh += 8)
    {
        float a = (float)((c00 >> sh) & 0xFF), b = (float)((c10 >> sh) & 0xFF);
        float c = (float)((c01 >> sh) & 0xFF), d = (float)((c11 >> sh) & 0xFF);
        float top = a + (b - a) * wx;
        float bot = c + (d - c) * wx;
        float val = top + (bot - top) * wy;
        Out |= ((ULONG)(val + 0.5f) & 0xFF) << sh;
    }
    return Out;
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

    // Byte width and how each texel maps to RGB (see EmuNv2aTextureFormatInfo).
    ULONG Bpp = 0, Kind = 0;
    bool Swizzled = false;
    if(!EmuNv2aTextureFormatInfo(Color, &Bpp, &Kind, &Swizzled))
    {
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

            ULONG Argb = EmuNv2aUnpackTexel(Raw, Kind);
            ULONG Bgra = (Argb & 0x00FFFFFF) | 0xFF000000;
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

// Optional live window for the raw-NV2A scanout. A raw-NV2A / pbkit title never
// creates a host D3D8 window (that only exists on the HLE-D3D8 path), so its
// software-rasterized frames were visible only as BMP dumps. When
// CXBX_NV2A_WINDOW=1 this opens a window and blits the displayed framebuffer to
// it on each present -- the same on-screen path the D3D8 titles get. Off by
// default, so the conformance suite (which never sets it) is untouched.
static HWND g_hEmuNv2aWindow = NULL;
static volatile LONG g_EmuNv2aWindowChecked = 0;
static bool g_bEmuNv2aWindow = false;

static bool EmuNv2aWindowEnabled()
{
    if(InterlockedCompareExchange(&g_EmuNv2aWindowChecked, 1, 0) == 0)
    {
        char Buffer[8] = {0};
        DWORD Length = GetEnvironmentVariableA("CXBX_NV2A_WINDOW", Buffer, sizeof(Buffer));
        g_bEmuNv2aWindow = (Length > 0 && Buffer[0] == '1');
    }
    return g_bEmuNv2aWindow;
}

static LRESULT CALLBACK EmuNv2aWndProc(HWND Hwnd, UINT Msg, WPARAM W, LPARAM L)
{
    if(Msg == WM_DESTROY)
    {
        g_hEmuNv2aWindow = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(Hwnd, Msg, W, L);
}

static DWORD WINAPI EmuNv2aWindowThread(LPVOID Param)
{
    ULONG Packed = (ULONG)(uintptr_t)Param;
    int W = (int)(Packed >> 16), H = (int)(Packed & 0xFFFF);

    WNDCLASSA Wc = {};
    Wc.lpfnWndProc = EmuNv2aWndProc;
    Wc.hInstance = GetModuleHandle(NULL);
    Wc.lpszClassName = "CxbxNv2aRender";
    Wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&Wc);

    RECT R = { 0, 0, W, H };
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, FALSE);
    g_hEmuNv2aWindow = CreateWindowA("CxbxNv2aRender", "cxbx : NV2A software rasterizer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, R.right - R.left, R.bottom - R.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);
    ShowWindow(g_hEmuNv2aWindow, SW_SHOW);
    UpdateWindow(g_hEmuNv2aWindow);

    MSG Msg;
    while(GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
    return 0;
}

// Blit a top-down BGRA framebuffer to the live window, creating it (on its own
// message-pump thread) on first use.
static void EmuNv2aBlitToWindow(const ULONG *Pixels, ULONG Width, ULONG Height)
{
    if(g_hEmuNv2aWindow == NULL)
    {
        ULONG WinH = Height > 480 ? 480 : Height;   // pbkit hands over-tall buffers
        ULONG Packed = (Width << 16) | (WinH & 0xFFFF);
        CreateThread(NULL, 0, EmuNv2aWindowThread, (LPVOID)(uintptr_t)Packed, 0, NULL);
        for(int i = 0; i < 200 && g_hEmuNv2aWindow == NULL; i++)
            Sleep(2);
    }
    if(g_hEmuNv2aWindow == NULL)
        return;

    HDC Hdc = GetDC(g_hEmuNv2aWindow);
    if(Hdc == NULL)
        return;

    BITMAPINFO Bmi = {};
    Bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Bmi.bmiHeader.biWidth = (LONG)Width;
    Bmi.bmiHeader.biHeight = -(LONG)Height;   // top-down
    Bmi.bmiHeader.biPlanes = 1;
    Bmi.bmiHeader.biBitCount = 32;
    Bmi.bmiHeader.biCompression = BI_RGB;

    RECT Rc;
    GetClientRect(g_hEmuNv2aWindow, &Rc);
    if(Rc.right > 0 && Rc.bottom > 0)
        StretchDIBits(Hdc, 0, 0, Rc.right, Rc.bottom, 0, 0, (int)Width, (int)Height,
                      Pixels, &Bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hEmuNv2aWindow, Hdc);
}

// Exposed for the D3D8-HLE present path (EmuD3D8.cpp): mirror the host back
// buffer to the same GDI live window the software rasterizer uses. Some hosts'
// D3D8 *windowed* Present does not composite to the visible window (it blits
// below the desktop compositor), leaving a black window even though rendering
// succeeded; a GDI StretchDIBits of the presented pixels always shows.
extern "C" void EmuHostBlitToWindow(const void *Pixels, unsigned Width, unsigned Height)
{
    EmuNv2aBlitToWindow((const ULONG *)Pixels, (ULONG)Width, (ULONG)Height);
}

// Capture the displayed framebuffer ("path 2"). The CRTC scanout base register
// (NV_PCRTC_START) is programmed with the physical address of the surface to put
// on screen -- exactly what a title flips to at frame end (nxdk pbkit's
// pb_show_front_screen / VBL swap, the XDK's D3D present). Snapshotting the
// surface at that address reproduces the actual on-screen image, independent of
// any rasterization: whatever the guest drew into that buffer (CPU or GPU) is
// what we write to %TEMP%\cxbx_fbN.bmp and (with CXBX_NV2A_WINDOW=1) the window.
static void EmuNv2aDumpScanout(ULONG PhysicalAddress)
{
    bool WantWindow = EmuNv2aWindowEnabled();
    bool WantBmp = (g_EmuNv2aScanoutDumpIndex < 16);
    if(PhysicalAddress == 0 || (!WantBmp && !WantWindow))
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

    // Live window (every present, uncapped) then the BMP dump (first 16 only).
    if(WantWindow)
        EmuNv2aBlitToWindow(Pixels, Width, Height);

    char dir[MAX_PATH] = {0};
    GetTempPathA(sizeof(dir), dir);
    char path[MAX_PATH];
    sprintf(path, "%scxbx_fb%lu.bmp", dir, g_EmuNv2aScanoutDumpIndex);

    FILE *f = WantBmp ? fopen(path, "wb") : NULL;
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

// Read the Phase 0 raster gate once. CXBX_NV2A_RASTER=1 turns on the pixel path;
// off by default so the HLE-D3D8 titles and the conformance suite are untouched.
static bool EmuNv2aRasterEnabled()
{
    if(!g_bEmuNv2aRasterChecked)
    {
        char Buffer[8] = {0};
        DWORD Length = GetEnvironmentVariableA("CXBX_NV2A_RASTER", Buffer, sizeof(Buffer));
        g_bEmuNv2aRaster = (Length > 0 && Buffer[0] == '1');
        g_bEmuNv2aRasterChecked = true;
    }
    return g_bEmuNv2aRaster;
}

static float EmuNv2aReadHostFloat(ULONG HostAddress)
{
    float Value = 0.0f;
    if(HostAddress != 0)
        EmuTryReadHost(HostAddress, &Value, sizeof(Value));
    return Value;
}

static ULONG EmuNv2aClampByte(float v)
{
    if(v <= 0.0f) return 0;
    if(v >= 255.0f) return 255;
    return (ULONG)(v + 0.5f);
}

// Where the current draw's pixels land: color surface + optional bound depth
// (zeta) surface and the depth-test state. Populated once per DRAW_ARRAYS.
struct EmuNv2aRasterTarget
{
    ULONG *Color;
    int    PitchPx;
    int    Width;
    int    Height;
    void  *Depth;        // NULL when neither depth nor stencil is active this draw
    int    DepthPitchB;
    ULONG  DepthFormat;  // 1=Z16, 2=Z24S8
    ULONG  DepthFunc;    // NV097_SET_DEPTH_FUNC value (0x200..0x207)
    bool   DepthTest;
    bool   DepthWrite;
    const EmuNv2aSampler *Sampler; // NULL when no texture this draw
    bool   BlendEnable;
    ULONG  BlendSFactor, BlendDFactor, BlendEquation;
    bool   StencilTest;
    ULONG  StencilFunc, StencilRef, StencilFuncMask, StencilMask;
    ULONG  StencilOpFail, StencilOpZFail, StencilOpZPass;
};

// The blend factor for one channel (all normalized 0..1). SRC/DST_COLOR use the
// channel's own value; SRC/DST_ALPHA use the alpha; the CONSTANT_* factors are
// approximated as ONE (blend color is rarely used by titles).
static float EmuNv2aBlendFactor(ULONG Factor, float ChanSrc, float ChanDst, float Sa, float Da)
{
    switch(Factor)
    {
        case 0x0000: return 0.0f;                 // ZERO
        case 0x0001: return 1.0f;                 // ONE
        case 0x0300: return ChanSrc;              // SRC_COLOR
        case 0x0301: return 1.0f - ChanSrc;       // ONE_MINUS_SRC_COLOR
        case 0x0302: return Sa;                   // SRC_ALPHA
        case 0x0303: return 1.0f - Sa;            // ONE_MINUS_SRC_ALPHA
        case 0x0304: return Da;                   // DST_ALPHA
        case 0x0305: return 1.0f - Da;            // ONE_MINUS_DST_ALPHA
        case 0x0306: return ChanDst;              // DST_COLOR
        case 0x0307: return 1.0f - ChanDst;       // ONE_MINUS_DST_COLOR
        case 0x0308: { float m = Sa < 1.0f - Da ? Sa : 1.0f - Da; return m; } // SRC_ALPHA_SATURATE
        default:     return 1.0f;
    }
}

static ULONG EmuNv2aBlendChannel(ULONG Eq, float S, float D)
{
    float R;
    switch(Eq)
    {
        case 0x800A: R = S - D; break;   // FUNC_SUBTRACT
        case 0x800B: R = D - S; break;   // FUNC_REVERSE_SUBTRACT
        case 0x8007: R = S < D ? S : D; break; // MIN (of the pre-factored terms)
        case 0x8008: R = S > D ? S : D; break; // MAX
        default:     R = S + D; break;   // FUNC_ADD
    }
    if(R <= 0.0f) return 0;
    if(R >= 1.0f) return 255;
    return (ULONG)(R * 255.0f + 0.5f);
}

// Blend a source pixel over the destination per the programmed factors/equation.
static ULONG EmuNv2aBlend(ULONG Src, ULONG Dst, ULONG Sf, ULONG Df, ULONG Eq)
{
    float sa = ((Src >> 24) & 0xFF) / 255.0f, sr = ((Src >> 16) & 0xFF) / 255.0f;
    float sg = ((Src >> 8) & 0xFF) / 255.0f,  sb = (Src & 0xFF) / 255.0f;
    float da = ((Dst >> 24) & 0xFF) / 255.0f, dr = ((Dst >> 16) & 0xFF) / 255.0f;
    float dg = ((Dst >> 8) & 0xFF) / 255.0f,  db = (Dst & 0xFF) / 255.0f;

    ULONG A = EmuNv2aBlendChannel(Eq, sa * EmuNv2aBlendFactor(Sf, sa, da, sa, da),
                                      da * EmuNv2aBlendFactor(Df, sa, da, sa, da));
    ULONG R = EmuNv2aBlendChannel(Eq, sr * EmuNv2aBlendFactor(Sf, sr, dr, sa, da),
                                      dr * EmuNv2aBlendFactor(Df, sr, dr, sa, da));
    ULONG G = EmuNv2aBlendChannel(Eq, sg * EmuNv2aBlendFactor(Sf, sg, dg, sa, da),
                                      dg * EmuNv2aBlendFactor(Df, sg, dg, sa, da));
    ULONG B = EmuNv2aBlendChannel(Eq, sb * EmuNv2aBlendFactor(Sf, sb, db, sa, da),
                                      db * EmuNv2aBlendFactor(Df, sb, db, sa, da));
    return (A << 24) | (R << 16) | (G << 8) | B;
}

static bool EmuNv2aDepthPass(ULONG Func, ULONG Src, ULONG Dst)
{
    switch(Func)
    {
        case 0x200: return false;        // NEVER
        case 0x201: return Src < Dst;    // LESS
        case 0x202: return Src == Dst;   // EQUAL
        case 0x203: return Src <= Dst;   // LEQUAL
        case 0x204: return Src > Dst;    // GREATER
        case 0x205: return Src != Dst;   // NOTEQUAL
        case 0x206: return Src >= Dst;   // GEQUAL
        default:    return true;         // ALWAYS
    }
}

// Apply a stencil operation to the current 8-bit stencil value.
static ULONG EmuNv2aStencilOp(ULONG Op, ULONG Value, ULONG Ref)
{
    switch(Op)
    {
        case 0x0000: return 0;                                   // ZERO
        case 0x1E01: return Ref & 0xFF;                          // REPLACE
        case 0x1E02: return Value < 0xFF ? Value + 1 : 0xFF;     // INCR (saturate)
        case 0x1E03: return Value > 0 ? Value - 1 : 0;           // DECR (saturate)
        case 0x150A: return (~Value) & 0xFF;                     // INVERT
        case 0x8507: return (Value + 1) & 0xFF;                  // INCR_WRAP
        case 0x8508: return (Value - 1) & 0xFF;                  // DECR_WRAP
        default:     return Value;                               // KEEP
    }
}

// Gouraud-fill one screen-space triangle (vertices i0,i1,i2 in the transformed
// arrays) into a 32bpp surface with the edge-function (half-plane) test.
// Barycentric weights (normalized by the signed area, so winding is handled
// either way) cover the triangle and blend the three vertex diffuse colors per
// pixel; uniform-colored vertices collapse to a flat fill. When a depth surface
// is bound the screen z is interpolated and tested/written; when a sampler is
// bound the texcoords are perspective-correctly interpolated (via per-vertex
// 1/w), point-sampled, and modulated with the diffuse. Clipped to the surface.
static void EmuNv2aFillTriangle(const EmuNv2aRasterTarget *T,
                                const float *VX, const float *VY, const float *VZ,
                                const float *VU, const float *VV, const float *VIW,
                                const ULONG *VC, ULONG i0, ULONG i1, ULONG i2)
{
    float ax = VX[i0], ay = VY[i0], az = VZ[i0];
    float bx = VX[i1], by = VY[i1], bz = VZ[i1];
    float cx = VX[i2], cy = VY[i2], cz = VZ[i2];
    ULONG Ca = VC[i0], Cb = VC[i1], Cc = VC[i2];

    float Area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if(Area > -1e-3f && Area < 1e-3f)
        return; // degenerate / zero-area
    float InvArea = 1.0f / Area;

    float LoXf = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    float HiXf = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    float LoYf = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    float HiYf = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);

    int MinX = (int)LoXf;       int MinY = (int)LoYf;
    int MaxX = (int)HiXf + 1;   int MaxY = (int)HiYf + 1;
    if(MinX < 0) MinX = 0;
    if(MinY < 0) MinY = 0;
    if(MaxX > T->Width)  MaxX = T->Width;
    if(MaxY > T->Height) MaxY = T->Height;

    bool Uniform = (Ca == Cb && Cb == Cc);
    float Aa = (float)((Ca >> 24) & 0xFF), Ra = (float)((Ca >> 16) & 0xFF);
    float Ga = (float)((Ca >>  8) & 0xFF), Bva = (float)(Ca & 0xFF);
    float Ab = (float)((Cb >> 24) & 0xFF), Rb = (float)((Cb >> 16) & 0xFF);
    float Gb = (float)((Cb >>  8) & 0xFF), Bvb = (float)(Cb & 0xFF);
    float Ac = (float)((Cc >> 24) & 0xFF), Rc = (float)((Cc >> 16) & 0xFF);
    float Gc = (float)((Cc >>  8) & 0xFF), Bvc = (float)(Cc & 0xFF);

    bool  UseDepth   = (T->Depth != NULL);
    bool  Depth24    = (T->DepthFormat == 2);
    float DepthMaxF  = Depth24 ? 16777215.0f : 65535.0f;
    int   DepthPitchElem = Depth24 ? (T->DepthPitchB / 4) : (T->DepthPitchB / 2);

    bool  UseTex = (T->Sampler != NULL);
    float au = VU[i0], av = VV[i0], aiw = VIW[i0];
    float bu = VU[i1], bv = VV[i1], biw = VIW[i1];
    float cu = VU[i2], cv = VV[i2], ciw = VIW[i2];

    for(int Y = MinY; Y < MaxY; Y++)
    {
        for(int X = MinX; X < MaxX; X++)
        {
            float px = (float)X + 0.5f, py = (float)Y + 0.5f;
            // Barycentric weights of a, b, c (sum to 1 inside the triangle).
            float la = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) * InvArea;
            float lb = ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) * InvArea;
            float lc = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * InvArea;
            if(la < -1e-4f || lb < -1e-4f || lc < -1e-4f)
                continue;

            if(UseDepth)
            {
                float zf = la * az + lb * bz + lc * cz;
                if(zf < 0.0f) zf = 0.0f;
                if(zf > DepthMaxF) zf = DepthMaxF;
                ULONG Src = (ULONG)(zf + 0.5f);

                if(Depth24)
                {
                    // Z24S8: 24-bit depth in the high bits, 8-bit stencil in the
                    // low byte. Stencil test first, then depth; the stencil op
                    // (fail / zfail / zpass) updates the stored stencil.
                    ULONG *Slot = (ULONG *)T->Depth + Y * DepthPitchElem + X;
                    ULONG Stored = *Slot;
                    ULONG Dst = Stored >> 8;
                    ULONG Sten = Stored & 0xFF;

                    bool SPass = !T->StencilTest ||
                        EmuNv2aDepthPass(T->StencilFunc, T->StencilRef & T->StencilFuncMask,
                                         Sten & T->StencilFuncMask);
                    bool DPass = !T->DepthTest || EmuNv2aDepthPass(T->DepthFunc, Src, Dst);

                    if(T->StencilTest)
                    {
                        ULONG Op = !SPass ? T->StencilOpFail
                                          : (!DPass ? T->StencilOpZFail : T->StencilOpZPass);
                        ULONG NewSt = EmuNv2aStencilOp(Op, Sten, T->StencilRef);
                        Sten = (NewSt & T->StencilMask) | (Sten & ~T->StencilMask);
                    }
                    ULONG NewDepth = (T->DepthTest && SPass && DPass && T->DepthWrite) ? Src : Dst;
                    *Slot = (NewDepth << 8) | (Sten & 0xFF);
                    if(!SPass || !DPass)
                        continue;   // fragment killed -> no color
                }
                else
                {
                    unsigned short *Slot = (unsigned short *)T->Depth + Y * DepthPitchElem + X;
                    ULONG Dst = *Slot;
                    if(T->DepthTest && !EmuNv2aDepthPass(T->DepthFunc, Src, Dst))
                        continue;
                    if(T->DepthTest && T->DepthWrite)
                        *Slot = (unsigned short)Src;
                }
            }

            ULONG Color;
            if(Uniform)
            {
                Color = Ca;
            }
            else
            {
                ULONG A = EmuNv2aClampByte(la * Aa + lb * Ab + lc * Ac);
                ULONG R = EmuNv2aClampByte(la * Ra + lb * Rb + lc * Rc);
                ULONG G = EmuNv2aClampByte(la * Ga + lb * Gb + lc * Gc);
                ULONG B = EmuNv2aClampByte(la * Bva + lb * Bvb + lc * Bvc);
                Color = (A << 24) | (R << 16) | (G << 8) | B;
            }

            if(UseTex)
            {
                // Perspective-correct texcoords: interpolate u/w, v/w and 1/w.
                float iw = la * aiw + lb * biw + lc * ciw;
                float inv = (iw > 1e-9f || iw < -1e-9f) ? (1.0f / iw) : 0.0f;
                float u = (la * au * aiw + lb * bu * biw + lc * cu * ciw) * inv;
                float v = (la * av * aiw + lb * bv * biw + lc * cv * ciw) * inv;
                ULONG Tex = EmuNv2aSampleTexel(T->Sampler, u, v);
                // MODULATE: (texel * diffuse) / 255 per channel.
                ULONG ca = (Color >> 24) & 0xFF, cr = (Color >> 16) & 0xFF;
                ULONG cg = (Color >> 8) & 0xFF,  cb = Color & 0xFF;
                ULONG ta = (Tex >> 24) & 0xFF, tr = (Tex >> 16) & 0xFF;
                ULONG tg = (Tex >> 8) & 0xFF,  tb = Tex & 0xFF;
                Color = (((ca * ta) / 255) << 24) | (((cr * tr) / 255) << 16) |
                        (((cg * tg) / 255) << 8)  |  ((cb * tb) / 255);
            }

            if(T->BlendEnable)
                Color = EmuNv2aBlend(Color, T->Color[Y * T->PitchPx + X],
                                     T->BlendSFactor, T->BlendDFactor, T->BlendEquation);

            T->Color[Y * T->PitchPx + X] = Color;
        }
    }
}

// Phase 0 draw: fetch pre-transformed vertices from the position/diffuse arrays,
// assemble the active primitive, and flat-shade each triangle into the bound
// color surface. Proves the pushbuffer -> pixels path without a vertex program,
// z-buffer, or texturing.
static ULONG g_EmuNv2aRasterLogCount = 0;

static void EmuNv2aRasterizeDrawArrays(ULONG Start, ULONG Count)
{
    if(!EmuNv2aRasterEnabled() || g_EmuNv2aBeginOp == 0 || Count < 3)
        return;

    ULONG SurfaceBase = EmuNv2aResolveDmaBase(g_EmuNv2aContextDmaColor);
    ULONG SurfaceHost = EmuNv2aHostPointer(SurfaceBase + g_EmuNv2aSurfaceColorOffset);
    // Fall back to treating the offset as a raw guest pointer (base-0 DMA, the
    // common Xbox case) when the color DMA base did not land on mapped memory.
    if(SurfaceHost == 0 && SurfaceBase != 0 && g_EmuNv2aSurfaceColorOffset != 0)
        SurfaceHost = EmuNv2aHostPointer(g_EmuNv2aSurfaceColorOffset);
    // Last resort for a real pbkit title whose color-surface DMA object this HLE
    // does not model: render into the displayed framebuffer (the surface the
    // guest last flipped to via NV_PCRTC_START).
    if(SurfaceHost == 0 && g_EmuNv2aScanoutAddress != 0)
        SurfaceHost = EmuNv2aHostPointer(g_EmuNv2aScanoutAddress);
    if(SurfaceHost == 0)
    {
        if(g_EmuNv2aRasterLogCount < 16)
        {
            printf("Emu (0x%lX): NV2A raster: surface unresolved (dma=0x%.08lX off=0x%.08lX).\n",
                   GetCurrentThreadId(), g_EmuNv2aContextDmaColor, g_EmuNv2aSurfaceColorOffset);
            fflush(stdout);
            g_EmuNv2aRasterLogCount++;
        }
        return;
    }

    int PitchB = (int)g_EmuNv2aSurfacePitchColor;
    if(PitchB <= 0) PitchB = 640 * 4;
    int PitchPx = PitchB / 4;
    int Width = (int)g_EmuNv2aSurfaceClipW;
    if(Width <= 0 || Width > PitchPx) Width = PitchPx;
    int Height = (int)g_EmuNv2aSurfaceClipH;
    if(Height <= 0 || Height > 4096) Height = 480;

    EmuNv2aRasterTarget Target = {};
    Target.Color = (ULONG *)(uintptr_t)SurfaceHost;
    Target.PitchPx = PitchPx;
    Target.Width = Width;
    Target.Height = Height;
    Target.BlendEnable = g_EmuNv2aBlendEnable;
    Target.BlendSFactor = g_EmuNv2aBlendSFactor;
    Target.BlendDFactor = g_EmuNv2aBlendDFactor;
    Target.BlendEquation = g_EmuNv2aBlendEquation;

    // Resolve the depth (zeta) surface when depth OR stencil testing is enabled
    // and a zeta surface is bound (same base-0 raw-pointer fallback as color).
    if((g_EmuNv2aDepthTest || g_EmuNv2aStencilTest) &&
       g_EmuNv2aSurfaceZetaFormat != 0 && g_EmuNv2aSurfaceZetaOffset != 0)
    {
        ULONG ZetaBase = EmuNv2aResolveDmaBase(g_EmuNv2aContextDmaZeta);
        ULONG ZetaHost = EmuNv2aHostPointer(ZetaBase + g_EmuNv2aSurfaceZetaOffset);
        if(ZetaHost == 0 && ZetaBase != 0)
            ZetaHost = EmuNv2aHostPointer(g_EmuNv2aSurfaceZetaOffset);
        if(ZetaHost != 0)
        {
            Target.Depth = (void *)(uintptr_t)ZetaHost;
            Target.DepthFormat = g_EmuNv2aSurfaceZetaFormat;
            Target.DepthFunc = g_EmuNv2aDepthFunc;
            Target.DepthTest = g_EmuNv2aDepthTest;
            Target.DepthWrite = g_EmuNv2aDepthWrite;
            Target.DepthPitchB = (int)g_EmuNv2aSurfacePitchZeta;
            if(Target.DepthPitchB <= 0)
                Target.DepthPitchB = Width * (g_EmuNv2aSurfaceZetaFormat == 2 ? 4 : 2);
            Target.StencilTest = g_EmuNv2aStencilTest && (g_EmuNv2aSurfaceZetaFormat == 2);
            Target.StencilFunc = g_EmuNv2aStencilFunc;
            Target.StencilRef = g_EmuNv2aStencilRef;
            Target.StencilFuncMask = g_EmuNv2aStencilFuncMask;
            Target.StencilMask = g_EmuNv2aStencilMask;
            Target.StencilOpFail = g_EmuNv2aStencilOpFail;
            Target.StencilOpZFail = g_EmuNv2aStencilOpZFail;
            Target.StencilOpZPass = g_EmuNv2aStencilOpZPass;
        }
    }

    EmuNv2aVertexArrayState *Pos = &g_EmuNv2aVertexArray[EmuNv2aAttrPosition];
    EmuNv2aVertexArrayState *Dif = &g_EmuNv2aVertexArray[EmuNv2aAttrDiffuse];
    EmuNv2aVertexArrayState *Tex = &g_EmuNv2aVertexArray[EmuNv2aAttrTexcoord0];
    ULONG VertexBase = EmuNv2aResolveDmaBase(g_EmuNv2aContextDmaVertex);
    ULONG PosStride = (Pos->Format >> 8) & 0xFF;
    ULONG PosType   = Pos->Format & 0x0F;
    ULONG DifStride = (Dif->Format >> 8) & 0xFF;
    ULONG DifType   = Dif->Format & 0x0F;
    ULONG TexStride = (Tex->Format >> 8) & 0xFF;
    // Phase 2: run the loaded vertex program when execution mode is PROGRAM.
    // Otherwise the position/diffuse arrays are consumed directly (Phase 0/1),
    // which needs a float position array.
    bool VpActive = (g_EmuNv2aVpExecMode == 2 /* PROGRAM */) && (g_EmuNv2aVpInstrCount > 0);
    if(!VpActive && (PosStride == 0 || PosType != 2 /* TYPE_F */))
    {
        if(g_EmuNv2aRasterLogCount < 16)
        {
            printf("Emu (0x%lX): NV2A raster: position array not float (fmt=0x%.08lX); skipping.\n",
                   GetCurrentThreadId(), Pos->Format);
            fflush(stdout);
            g_EmuNv2aRasterLogCount++;
        }
        return;
    }

    ULONG PosSize = (Pos->Format >> 4) & 0x0F;

    // Texture stage 0: sample when a texture is bound and a texcoord source is
    // available (the attr-9 array, or the vertex program's oT0).
    EmuNv2aSampler Sampler;
    bool SamplerReady = false;
    bool TexBound = (g_EmuNv2aTexture[0].Offset != 0 && g_EmuNv2aTexture[0].Format != 0);
    if(TexBound && (VpActive || TexStride != 0))
        SamplerReady = EmuNv2aSetupSampler(0, &Sampler);
    if(SamplerReady)
        Target.Sampler = &Sampler;

    static float VX[4096];
    static float VY[4096];
    static float VZ[4096];
    static float VU[4096];
    static float VV[4096];
    static float VIW[4096];
    static ULONG VC[4096];
    if(Count > 4096) Count = 4096;

    for(ULONG i = 0; i < Count; i++)
    {
        ULONG Index = Start + i;
        float Xc, Yc, Zc = 0.0f, W;
        float U = 0.0f, V = 0.0f;
        ULONG Color = 0xFFFFFFFF;

        if(VpActive)
        {
            // Gather all bound attribute arrays into the 16 vertex-program input
            // registers (x,y,z default 0, w default 1), then transform on the CPU.
            float Input[16 * 4];
            for(int a = 0; a < 16; a++)
            {
                Input[a * 4 + 0] = 0.0f; Input[a * 4 + 1] = 0.0f;
                Input[a * 4 + 2] = 0.0f; Input[a * 4 + 3] = 1.0f;
                EmuNv2aVertexArrayState *Arr = &g_EmuNv2aVertexArray[a];
                ULONG Stride = (Arr->Format >> 8) & 0xFF;
                ULONG Size   = (Arr->Format >> 4) & 0x0F;
                ULONG Type   = Arr->Format & 0x0F;
                if(Stride == 0 || Size == 0)
                    continue;
                ULONG Host = EmuNv2aHostPointer(VertexBase + Arr->Offset + Index * Stride);
                if(Host == 0)
                    continue;
                if(Type == 2 /* TYPE_F */)
                {
                    for(ULONG c = 0; c < Size && c < 4; c++)
                        Input[a * 4 + c] = EmuNv2aReadHostFloat(Host + c * 4);
                }
                else /* UB_D3D packed color -> normalized RGBA */
                {
                    ULONG Raw = 0;
                    EmuTryReadHost(Host, &Raw, sizeof(Raw));
                    Input[a * 4 + 0] = (float)((Raw >> 16) & 0xFF) / 255.0f;
                    Input[a * 4 + 1] = (float)((Raw >>  8) & 0xFF) / 255.0f;
                    Input[a * 4 + 2] = (float)( Raw        & 0xFF) / 255.0f;
                    Input[a * 4 + 3] = (float)((Raw >> 24) & 0xFF) / 255.0f;
                }
            }

            float OutPos[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            float OutCol[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            float OutTex[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            EmuVshExecuteProgram(g_EmuNv2aVpProgram, (int)g_EmuNv2aVpInstrCount,
                                 (int)g_EmuNv2aVpStart, g_EmuNv2aTransformConstant,
                                 Input, OutPos, OutCol, OutTex);
            Xc = OutPos[0]; Yc = OutPos[1]; Zc = OutPos[2]; W = OutPos[3];
            U = OutTex[0]; V = OutTex[1];
            ULONG R = EmuNv2aClampByte(OutCol[0] * 255.0f);
            ULONG G = EmuNv2aClampByte(OutCol[1] * 255.0f);
            ULONG B = EmuNv2aClampByte(OutCol[2] * 255.0f);
            ULONG A = EmuNv2aClampByte(OutCol[3] * 255.0f);
            Color = (A << 24) | (R << 16) | (G << 8) | B;
        }
        else
        {
            ULONG PosHost = EmuNv2aHostPointer(VertexBase + Pos->Offset + Index * PosStride);
            Xc = EmuNv2aReadHostFloat(PosHost);
            Yc = EmuNv2aReadHostFloat(PosHost + 4);
            Zc = (PosSize >= 3) ? EmuNv2aReadHostFloat(PosHost + 8) : 0.0f;
            W  = (PosSize >= 4) ? EmuNv2aReadHostFloat(PosHost + 12) : 1.0f;

            if(DifStride != 0)
            {
                ULONG DifHost = EmuNv2aHostPointer(VertexBase + Dif->Offset + Index * DifStride);
                if(DifHost != 0)
                {
                    if(DifType == 2 /* TYPE_F, float4 RGBA */)
                    {
                        ULONG R = (ULONG)(EmuNv2aReadHostFloat(DifHost + 0) * 255.0f + 0.5f) & 0xFF;
                        ULONG G = (ULONG)(EmuNv2aReadHostFloat(DifHost + 4) * 255.0f + 0.5f) & 0xFF;
                        ULONG B = (ULONG)(EmuNv2aReadHostFloat(DifHost + 8) * 255.0f + 0.5f) & 0xFF;
                        ULONG A = (ULONG)(EmuNv2aReadHostFloat(DifHost + 12) * 255.0f + 0.5f) & 0xFF;
                        Color = (A << 24) | (R << 16) | (G << 8) | B;
                    }
                    else
                    {
                        ULONG Raw = 0xFFFFFFFF;
                        EmuTryReadHost(DifHost, &Raw, sizeof(Raw)); // D3DCOLOR 0xAARRGGBB
                        Color = Raw;
                    }
                }
            }

            if(TexStride != 0)
            {
                ULONG TexHost = EmuNv2aHostPointer(VertexBase + Tex->Offset + Index * TexStride);
                if(TexHost != 0)
                {
                    U = EmuNv2aReadHostFloat(TexHost);
                    V = EmuNv2aReadHostFloat(TexHost + 4);
                }
            }

            // Fixed-function transform: object position * composite matrix ->
            // clip. Identity by default, so a title that supplies clip/screen
            // coordinates directly is unaffected; a title that programs the
            // matrix (fixed-function pipeline) gets its object geometry mapped.
            const float *M = g_EmuNv2aCompositeMatrix;
            float ox = Xc, oy = Yc, oz = Zc, ow = W;
            Xc = ox * M[0] + oy * M[4] + oz * M[8]  + ow * M[12];
            Yc = ox * M[1] + oy * M[5] + oz * M[9]  + ow * M[13];
            Zc = ox * M[2] + oy * M[6] + oz * M[10] + ow * M[14];
            W  = ox * M[3] + oy * M[7] + oz * M[11] + ow * M[15];
        }

        // Homogeneous clip -> NDC (perspective divide) -> screen (viewport). With
        // w==1 and the identity viewport default this reproduces Phase 0/1.
        float InvW = (W > 1e-6f || W < -1e-6f) ? (1.0f / W) : 1.0f;
        VX[i] = (Xc * InvW) * g_EmuNv2aViewportScale[0] + g_EmuNv2aViewportOffset[0];
        VY[i] = (Yc * InvW) * g_EmuNv2aViewportScale[1] + g_EmuNv2aViewportOffset[1];
        VZ[i] = (Zc * InvW) * g_EmuNv2aViewportScale[2] + g_EmuNv2aViewportOffset[2];
        VU[i] = U;
        VV[i] = V;
        VIW[i] = InvW;
        VC[i] = Color;
    }

    ULONG Triangles = 0;
    __try
    {
        switch(g_EmuNv2aBeginOp)
        {
            case 5: // TRIANGLES
                for(ULONG i = 0; i + 2 < Count; i += 3)
                {
                    EmuNv2aFillTriangle(&Target, VX, VY, VZ, VU, VV, VIW, VC, i, i+1, i+2);
                    Triangles++;
                }
                break;
            case 6: // TRIANGLE_STRIP
                for(ULONG i = 0; i + 2 < Count; i++)
                {
                    EmuNv2aFillTriangle(&Target, VX, VY, VZ, VU, VV, VIW, VC, i, i+1, i+2);
                    Triangles++;
                }
                break;
            case 7:  // TRIANGLE_FAN
            case 10: // POLYGON
                for(ULONG i = 1; i + 1 < Count; i++)
                {
                    EmuNv2aFillTriangle(&Target, VX, VY, VZ, VU, VV, VIW, VC, 0, i, i+1);
                    Triangles++;
                }
                break;
            case 8: // QUADS
                for(ULONG i = 0; i + 3 < Count; i += 4)
                {
                    EmuNv2aFillTriangle(&Target, VX, VY, VZ, VU, VV, VIW, VC, i, i+1, i+2);
                    EmuNv2aFillTriangle(&Target, VX, VY, VZ, VU, VV, VIW, VC, i, i+2, i+3);
                    Triangles += 2;
                }
                break;
            case 9: // QUAD_STRIP
                for(ULONG i = 0; i + 3 < Count; i += 2)
                {
                    EmuNv2aFillTriangle(&Target, VX, VY, VZ, VU, VV, VIW, VC, i, i+1, i+3);
                    EmuNv2aFillTriangle(&Target, VX, VY, VZ, VU, VV, VIW, VC, i, i+3, i+2);
                    Triangles += 2;
                }
                break;
            default:
                break;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        printf("Emu (0x%lX): NV2A raster: fault while filling (surf=0x%.08lX).\n",
               GetCurrentThreadId(), SurfaceHost);
        fflush(stdout);
    }

    if(SamplerReady)
        EmuNv2aFreeSampler(&Sampler);

    if(g_EmuNv2aRasterLogCount < 32)
    {
        printf("Emu (0x%lX): NV2A raster op=%lu start=%lu verts=%lu tris=%lu surf=0x%.08lX %dx%d pitch=%d vp=%s(%lu) depth=%s(fmt=%lu func=0x%lX) tex=%s(%lux%lu) vp_scale=(%.1f,%.1f) vp_off=(%.1f,%.1f) v0=(%.1f,%.1f) c0=0x%.08lX\n",
               GetCurrentThreadId(), g_EmuNv2aBeginOp, Start, Count, Triangles,
               SurfaceHost, Width, Height, PitchB,
               VpActive ? "prog" : "raw", g_EmuNv2aVpInstrCount,
               Target.Depth ? "on" : "off", g_EmuNv2aSurfaceZetaFormat, g_EmuNv2aDepthFunc,
               SamplerReady ? "on" : "off", SamplerReady ? Sampler.Width : 0, SamplerReady ? Sampler.Height : 0,
               g_EmuNv2aViewportScale[0], g_EmuNv2aViewportScale[1],
               g_EmuNv2aViewportOffset[0], g_EmuNv2aViewportOffset[1],
               VX[0], VY[0], VC[0]);
        fflush(stdout);
        g_EmuNv2aRasterLogCount++;
    }
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

static bool EmuNv2aDmaWordInRange(ULONG Offset, ULONG Limit)
{
    return Offset <= Limit && Limit - Offset >= sizeof(ULONG) - 1;
}

static void EmuNv2aSetPusherError(ULONG Error)
{
    ULONG State = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_STATE, 0);
    ULONG Push = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_PUSH, 1);
    ULONG Intr = EmuNv2aCachedRegister(NV_PFIFO_INTR_0, 0);

    EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_STATE,
                         (State & ~EmuNv2aPfifoDmaStateError) | Error);
    EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_PUSH,
                         Push | EmuNv2aPfifoDmaPushSuspended);
    EmuNv2aStoreRegister(NV_PFIFO_INTR_0, Intr | EmuNv2aPfifoIntrDmaPusher);
}

static void EmuNv2aRunPusher()
{
    ULONG Push0 = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_PUSH0, 1);
    ULONG DmaPush = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_PUSH, 1);
    ULONG Get = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_GET, 0);
    ULONG Put = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_PUT, 0);

    if((Push0 & 1) == 0 || (DmaPush & 1) == 0 ||
       (DmaPush & EmuNv2aPfifoDmaPushSuspended) != 0)
    {
        return;
    }

    if(Get == Put)
    {
        return;
    }

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
        {
            Get = BlockBase;
        }
    }
    else if(!EmuNv2aLoadDmaObject(&BaseAddress, &Limit))
    {
        EmuNv2aSetPusherError(EmuNv2aPfifoDmaErrorProtection);
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
    ULONG State = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_STATE, 0);
    ULONG Dcount = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_DCOUNT, 0);
    for(ULONG Guard = 0; Guard < GuardLimit && Get != Put; Guard++)
    {
        ULONG Word = 0;

        if(!EmuNv2aDmaWordInRange(Get, Limit) ||
           !EmuNv2aFetchPushWord(HostMode, BaseAddress, Get, &Word))
        {
            EmuNv2aSetPusherError(EmuNv2aPfifoDmaErrorProtection);
            break;
        }

        Get += 4;
        NV2A_TRACE_PB(Word);

        ULONG Count = (State & EmuNv2aPfifoDmaStateMethodCount) >> 18;
        if(Count != 0)
        {
            ULONG Method = State & EmuNv2aPfifoDmaStateMethod;
            ULONG Subchannel = (State & EmuNv2aPfifoDmaStateSubchannel) >> 13;
            EmuNv2aHandlePgraphMethod(Subchannel, Method, Word);

            if((State & EmuNv2aPfifoDmaStateMethodType) == 0)
            {
                Method += sizeof(ULONG);
            }

            Count--;
            State &= ~(EmuNv2aPfifoDmaStateMethod | EmuNv2aPfifoDmaStateMethodCount);
            State |= Method & EmuNv2aPfifoDmaStateMethod;
            State |= (Count << 18) & EmuNv2aPfifoDmaStateMethodCount;
            Dcount++;
            EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_STATE, State);
            EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_DCOUNT, Dcount);
            continue;
        }

        if((Word & 0xE0000003) == 0x20000000)
        {
            Get = Word & 0x1FFFFFFC;
            continue;
        }

        if((Word & 3) == 1)
        {
            Get = Word & 0xFFFFFFFC;
            continue;
        }

        if((Word & 3) == 2)
        {
            ULONG Subroutine = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_SUBROUTINE, 0);
            if((Subroutine & 1) != 0)
            {
                EmuNv2aSetPusherError(EmuNv2aPfifoDmaErrorCall);
                break;
            }

            EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_SUBROUTINE, (Get & 0xFFFFFFFC) | 1);
            Get = Word & 0xFFFFFFFC;
            continue;
        }

        if(Word == 0x00020000)
        {
            ULONG Subroutine = EmuNv2aCachedRegister(NV_PFIFO_CACHE1_DMA_SUBROUTINE, 0);
            if((Subroutine & 1) == 0)
            {
                EmuNv2aSetPusherError(EmuNv2aPfifoDmaErrorReturn);
                break;
            }

            Get = Subroutine & 0xFFFFFFFC;
            EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_SUBROUTINE, 0);
            continue;
        }

        if((Word & 0xE0030003) == 0 || (Word & 0xE0030003) == 0x40000000)
        {
            bool Incrementing = (Word & 0xE0030003) == 0;
            State &= EmuNv2aPfifoDmaStateError;
            State |= Word & (EmuNv2aPfifoDmaStateMethod |
                             EmuNv2aPfifoDmaStateSubchannel |
                             EmuNv2aPfifoDmaStateMethodCount);
            if(!Incrementing)
            {
                State |= EmuNv2aPfifoDmaStateMethodType;
            }

            Dcount = 0;
            EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_STATE, State);
            EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_DCOUNT, Dcount);
            continue;
        }

        EmuNv2aSetPusherError(EmuNv2aPfifoDmaErrorReserved);
        break;
    }

    EmuNv2aStoreRegister(NV_PFIFO_CACHE1_DMA_GET, Get);

    if(Get == Put)
    {
        EmuNv2aStoreRegister(EmuNv2aPfifoCache1Status,
                             EmuNv2aCachedRegister(EmuNv2aPfifoCache1Status, 0) | 0x10);
    }

    // Titles that stall waiting on GPU write-back never reach the EmuCleanup
    // dump, so flush the opt-in histogram after early batches (and periodically
    // once the title is submitting at frame rate) — the interesting stream is
    // often a single early pushbuffer.
    if(g_EmuNv2aPusherRunCount <= 8 || (g_EmuNv2aPusherRunCount % 500) == 0)
    {
        EmuNv2aDumpMethodStats("pusher");
    }
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

        // Reboot-decision trace (opt-in via CXBX_REBOOT_TRACE): log the guest EIP
        // that performed each kernel/physical read so the read sequence can be
        // correlated back to the exact guest instructions and their branch.
        static bool s_RebootTrace = getenv("CXBX_REBOOT_TRACE") != NULL;
        if(s_RebootTrace)
        {
            printf("Emu (0x%lX): PHYSFAULT eip=0x%.08lX access=%lu addr=0x%.08lX bytes=%02X %02X %02X %02X %02X %02X %02X\n",
                   GetCurrentThreadId(), (ULONG)Instruction, AccessType, FaultAddress,
                   Instruction[0], Instruction[1], Instruction[2], Instruction[3],
                   Instruction[4], Instruction[5], Instruction[6]);
            fflush(stdout);
        }

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

        // 0x0F 0x7F /r = movq m64, mmN : MMX 8-byte store (XGRAPHCL's swizzle
        // copies stream texture data through the 0x80000000 write-combined
        // alias with MMX moves). The MMX registers alias the x87 register
        // file: after any MMX instruction the stack top is 0, so mmN is the
        // low 8 bytes of FloatSave.RegisterArea slot N (10 bytes per slot).
        if((AccessType == 1 || MayBePhysicalStoreFault) && Instruction[0] == 0x0F && Instruction[1] == 0x7F)
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               ((FaultIsPhysicalMap && Address == FaultAddress) ||
                (MayBePhysicalStoreFault && EmuIsPhysicalMapAddress(Address))))
            {
                ULONG RegisterIndex = (Instruction[2] >> 3) & 0x07;
                const BYTE *Mm = (const BYTE *)&e->ContextRecord->FloatSave.RegisterArea[RegisterIndex * 10];

                if(!EmuWritePhysicalMapBytes(Address, Mm, 8))
                    return false;

                e->ContextRecord->Eip += 1 + OperandLength;

                static ULONG s_MovqLogCount = 0;
                if(s_MovqLogCount < 8)
                {
                    printf("Emu (0x%lX): Emulated physical movq store 0x%.08lX <- mm%lu.\n",
                           GetCurrentThreadId(), Address, RegisterIndex);
                    fflush(stdout);
                    s_MovqLogCount++;
                }

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

                EmuMaybeSatisfyNv2aNotifier(FaultAddress, Right, &Left);

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

                EmuMaybeSatisfyNv2aNotifier(FaultAddress, Right, &Left);

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

            if(EmuMmioTraceEnabled())
            {
                printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
                fflush(stdout);
            }

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

        // 0x0F 0xB6/0xB7 /r = movzx r32, r/m8|r/m16 : zero-extended narrow read
        // (what a C `uint16_t` volatile load into a wider variable compiles to,
        // e.g. the DSOUND ISR reading an AC97 channel status word at 0xFEC00116).
        if(AccessType == 0 && Instruction[0] == 0x0F &&
           (Instruction[1] == 0xB6 || Instruction[1] == 0xB7))
        {
            ULONG Address = 0;
            ULONG OperandLength = 0;

            if(EmuDecodeModRmAddress(e->ContextRecord, Instruction + 1, &Address, &OperandLength) &&
               Address == FaultAddress)
            {
                ULONG Size = Instruction[1] == 0xB6 ? 1 : 2;
                ULONG Value = EmuReadMmio(FaultAddress, Size);
                EmuSetContextRegister(e->ContextRecord, (Instruction[2] >> 3) & 0x07, Value);
                e->ContextRecord->Eip += 1 + OperandLength;

                printf("Emu (0x%lX): Emulated MMIO movzx %s read 0x%.08lX.\n",
                       GetCurrentThreadId(), Size == 1 ? "byte" : "word", FaultAddress);
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

            if(EmuMmioTraceEnabled())
            {
                printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                       GetCurrentThreadId(), FaultAddress, e->ContextRecord->Eax);
                fflush(stdout);
            }

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

// The initial (main/render) thread; its unrecoverable faults stay fatal, but a
// worker thread's can be survived (CXBX_SURVIVE_THREAD_FAULT) so an unrelated
// subsystem crash -- e.g. a DSOUND mixing thread NULL-dereferencing an
// uninitialised voice array -- does not tear down a title that is otherwise
// reaching its render loop.
static DWORD g_EmuInitialThreadId = 0;

// CRC32 trace breakpoint state (Turok .tre VFS investigation). Armed only
// when CXBX_CRC_TRACE is set (its value is the guest VA in hex; anything
// unparsable selects the Turok default 0x000267B0). g_Crc32SingleStep is a
// counter, not a flag, so two threads mid-resume cannot strand one another's
// single-step exception on the fatal path.
static uint32_t g_Crc32BpAddr = 0;
static uint8_t g_Crc32OrigByte = 0;
static volatile LONG g_Crc32SingleStep = 0;
static const uint32_t crc32_table[256] = {
    0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
    0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
    0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
    0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
    0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
    0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
    0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
    0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
    0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
    0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
    0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
    0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
    0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
    0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
    0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
    0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
    0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
    0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
    0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
    0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
    0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
    0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
    0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
    0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
    0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
    0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
    0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
    0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
    0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
    0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
    0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
    0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D,
};

// Log the hash function's arguments on a CRC32 trace-breakpoint hit. ESP is
// the guest stack at function entry: [esp+4] is the string pointer and
// [esp+8] is believed to be the length -- when it does not look like one,
// fall back to the NUL terminator. Guarded because the string can cross into
// an unmapped page even when the pointer itself passes the range check.
static void EmuCrc32TraceLog(ULONG Esp)
{
    __try
    {
        uint32_t str_ptr = *(uint32_t*)(Esp + 4);
        uint32_t str_len = *(uint32_t*)(Esp + 8);

        if(str_ptr < 0x00010000 || str_ptr >= 0x10000000)
        {
            // Not the CRC-string shape: dump the first six stack args raw so
            // the hook doubles as a generic argument probe for cdecl/stdcall
            // guest functions.
            printf("CRC32| hit at 0x%.08lX args=[0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X]\n",
                   (ULONG)g_Crc32BpAddr, str_ptr, str_len,
                   *(uint32_t*)(Esp + 12), *(uint32_t*)(Esp + 16),
                   *(uint32_t*)(Esp + 20), *(uint32_t*)(Esp + 24));
            fflush(stdout);
            return;
        }

        const char *s = (const char*)(uintptr_t)str_ptr;
        char buf[512];
        uint32_t limit = (str_len > 0 && str_len < sizeof(buf)) ? str_len : sizeof(buf) - 1;
        uint32_t n;
        for(n = 0; n < limit && s[n] != '\0'; n++)
            buf[n] = s[n];
        buf[n] = '\0';

        // Hash exactly the bytes printed, so the logged pair is self-consistent.
        uint32_t crc = 0xFFFFFFFF;
        for(uint32_t i = 0; i < n; i++)
            crc = (crc >> 8) ^ crc32_table[(crc ^ (uint8_t)buf[i]) & 0xFF];
        crc ^= 0xFFFFFFFF;

        printf("CRC32| input=\"%s\" len_arg=%u used=%u hash=0x%08X\n", buf, str_len, n, crc);
        fflush(stdout);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        printf("CRC32| hit at 0x%.08lX but the arguments were unreadable.\n",
               (ULONG)g_Crc32BpAddr);
        fflush(stdout);
    }
}

// Resolve a host-side address to "module.dll+0xOFFSET" for the vectored dump.
// Guest addresses (below Cxbx.dll's 0x10000000 base) and values that are not
// inside a mapped PE image resolve to NULL so callers can skip them.
// VirtualQuery's AllocationBase doubles as the module handle for any address
// inside a loaded image.
static const char *EmuHostAddressToModuleOffset(ULONG Address, char *Buffer, size_t BufferSize)
{
    MEMORY_BASIC_INFORMATION mbi;

    if(Address < 0x10000000)
        return NULL;
    if(VirtualQuery((LPCVOID)Address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return NULL;
    if(mbi.State != MEM_COMMIT || mbi.AllocationBase == NULL)
        return NULL;

    char Path[MAX_PATH];
    if(GetModuleFileNameA((HMODULE)mbi.AllocationBase, Path, sizeof(Path)) == 0)
        return NULL; // committed but not a PE image (heap, stack, file mapping)

    const char *Name = strrchr(Path, '\\');
    Name = (Name != NULL) ? Name + 1 : Path;
    _snprintf(Buffer, BufferSize - 1, "%s+0x%X", Name, Address - (ULONG)mbi.AllocationBase);
    Buffer[BufferSize - 1] = '\0';
    return Buffer;
}

// Fault-free readability probe for the vectored dump: dereferencing a bad
// pointer inside the handler re-enters it, so pointer chains harvested from a
// crashed context must be validated with VirtualQuery instead of __try.
static bool EmuIsReadableRange(ULONG Address, ULONG Bytes)
{
    MEMORY_BASIC_INFORMATION mbi;

    if(Address == 0)
        return false;
    if(VirtualQuery((LPCVOID)Address, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if(mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        return false;
    return (ULONG)mbi.BaseAddress + mbi.RegionSize >= Address + Bytes;
}

// Nested-fault guard for the vectored dump: a fault while dumping (bad
// pointer chain, unreadable stack) must not cascade into a second full dump
// and take down the process before the original dump finishes.
static volatile LONG g_VectoredDumpDepth = 0;

// Repeat collapser: once a title reaches its render loop, a recoverable
// exception at one site (e.g. d3d8's internal per-frame D3DERR throw) fires
// thousands of times; after a few full dumps of the same (code, EIP) pair,
// collapse further repeats to one line each.
static ULONG g_VectoredLastCode = 0;
static ULONG g_VectoredLastEip = 0;
static volatile LONG g_VectoredRepeatCount = 0;

static LONG WINAPI EmuVectoredExceptionHandler(LPEXCEPTION_POINTERS e)
{
    // Under the LDT-less content-swap an SEH unwind (e.g. a guest __except
    // catching a fault raised inside a host-side HLE call) skips the balancing
    // EmuSwapFS calls, leaving the shared FS slots in the wrong role for the
    // code that then runs. The faulting EIP says definitively which side was
    // executing (guest image below Cxbx.dll's 0x10000000 base, host above), so
    // re-anchor the role to it: the handler's own swaps below then start from
    // truth, and the eventual resume returns the interrupted side its values.
    EmuFsSwapEnsureRole((ULONG)e->ContextRecord->Eip < 0x10000000);

    // SEH self-heal (host-side exceptions, content-swap mode only):
    // RtlDispatchException walks the FS:[0] chain AFTER this handler returns
    // and rejects it wholesale if a registration record lies outside the
    // TIB's [StackLimit, StackBase], turning any recoverable host C++ throw
    // -- e.g. d3d8's internal D3DERR_INVALIDCALL -- into process death. The
    // chain itself is intact, so recompute the real stack extent from ESP and
    // repair the bounds before dispatch continues.
    //
    // MUST stay gated on the content-swap: in legacy (non-swap) mode
    // NtTib.StackBase permanently holds the guest TLS pointer (fs:[0x04], see
    // EmuGenerateFS), and overwriting it here destroys the thread's XAPI TLS
    // -- the guest's SetLastError then dereferences stack garbage and dies
    // (seen on Turok Evolution and NestopiaX 1.3). Under the swap the slots
    // are re-applied from per-role storage at every boundary, so a backstop
    // repair of the live TIB is safe.
    if(g_bEmuFSContentSwap && (ULONG)e->ContextRecord->Eip >= 0x10000000)
    {
        NT_TIB *Tib = (NT_TIB*)NtCurrentTeb();
        ULONG Esp = e->ContextRecord->Esp;

        if(Esp < (ULONG)Tib->StackLimit || Esp >= (ULONG)Tib->StackBase)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if(VirtualQuery((LPCVOID)Esp, &mbi, sizeof(mbi)) == sizeof(mbi) &&
               mbi.State == MEM_COMMIT)
            {
                // Top of the stack reservation = end of the last region that
                // shares ESP's AllocationBase.
                ULONG AllocBase = (ULONG)mbi.AllocationBase;
                ULONG Top = (ULONG)mbi.BaseAddress + mbi.RegionSize;
                MEMORY_BASIC_INFORMATION next;
                for(ULONG i = 0; i < 64; i++)
                {
                    if(VirtualQuery((LPCVOID)Top, &next, sizeof(next)) != sizeof(next) ||
                       (ULONG)next.AllocationBase != AllocBase || next.State == MEM_FREE)
                        break;
                    Top = (ULONG)next.BaseAddress + next.RegionSize;
                }

                static volatile LONG RepairCount = 0;
                if(InterlockedIncrement(&RepairCount) <= 5)
                {
                    printf("Emu (0x%lX): TIB stack bounds repaired for host dispatch: "
                           "StackBase 0x%p->0x%.08lX StackLimit 0x%p->0x%.08lX (ESP 0x%.08lX)\n",
                           GetCurrentThreadId(), Tib->StackBase, Top,
                           Tib->StackLimit, AllocBase, Esp);
                    fflush(stdout);
                }

                // StackLimit must be permissive: the committed-region base is
                // too tight -- RtlUnwind runs deeper than the throw point via
                // normal guard-page growth, and validating against a tight
                // limit raises STATUS_BAD_STACK mid-unwind. The bottom of the
                // reservation can never falsely reject a live frame.
                Tib->StackLimit = (PVOID)AllocBase;
                Tib->StackBase = (PVOID)Top;
            }
        }

        // Prune stale SEH head records: the content-swap can restore a chain
        // head captured when the stack was deeper. Those functions have long
        // returned, and unwinding through their dead records raises
        // STATUS_BAD_STACK (0xC0000028) mid-unwind. The stack grows down, so
        // at dispatch time every live registration sits above ESP -- records
        // below it are provably stale.
        {
            struct SehRecord { SehRecord *Next; void *Handler; };
            SehRecord *Head = (SehRecord*)Tib->ExceptionList;
            ULONG Pruned = 0;

            while(Pruned < 64 && Head != (SehRecord*)0xFFFFFFFF && (ULONG)Head < Esp)
            {
                if(!EmuIsReadableRange((ULONG)Head, sizeof(SehRecord)))
                    break; // chain unusable past this point; leave it alone
                Head = Head->Next;
                Pruned++;
            }

            if(Pruned != 0 &&
               (Head == (SehRecord*)0xFFFFFFFF || (ULONG)Head >= Esp))
            {
                static volatile LONG PruneCount = 0;
                if(InterlockedIncrement(&PruneCount) <= 5)
                {
                    printf("Emu (0x%lX): SEH chain head repaired for host dispatch: "
                           "0x%p -> 0x%p (%lu stale records below ESP 0x%.08lX pruned)\n",
                           GetCurrentThreadId(), Tib->ExceptionList, Head, Pruned, Esp);
                    fflush(stdout);
                }
                Tib->ExceptionList = (struct _EXCEPTION_REGISTRATION_RECORD*)(void*)Head;
            }
        }
    }

    // CRC32 trace resume, step 2 of 2: the trap-flag single-step armed below
    // has executed one original instruction past the hook address; put the
    // int3 back. TF was cleared by the exception delivery itself.
    if(e->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
       g_Crc32SingleStep > 0)
    {
        InterlockedDecrement(&g_Crc32SingleStep);
        *(uint8_t*)g_Crc32BpAddr = 0xCC;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Guest breakpoints: debug-build XDK titles trace through a print-then-int3
    // debug service (e.g. the CDX XBApp's "XBApp: Creating Direct3D...") that a
    // devkit's attached kernel debugger swallows and continues. Emulate the
    // debugger being present: step over the int3 and resume. Host-side
    // breakpoints (real debugger / CRT asserts) are left alone.
    if(e->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT &&
       (ULONG)e->ContextRecord->Eip >= 0x00010000 &&
       (ULONG)e->ContextRecord->Eip < 0x10000000)
    {
        // CRC32 trace hook, step 1 of 2: our int3 sits on the hash function's
        // first instruction byte, and Windows reports EIP rewound to the int3
        // itself (the same convention the devkit skip below relies on). Log
        // the arguments, restore the original byte, and single-step so the
        // check above re-arms the breakpoint after one instruction.
        if(g_Crc32BpAddr != 0 && (ULONG)e->ContextRecord->Eip == g_Crc32BpAddr)
        {
            EmuCrc32TraceLog(e->ContextRecord->Esp);

            *(uint8_t*)g_Crc32BpAddr = g_Crc32OrigByte;
            e->ContextRecord->EFlags |= 0x100; // Trap Flag
            InterlockedIncrement(&g_Crc32SingleStep);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        printf("Emu (0x%lX): guest breakpoint (int3) at 0x%.08lX skipped (devkit debugger emulation).\n",
               GetCurrentThreadId(), e->ContextRecord->Eip);
        fflush(stdout);
        e->ContextRecord->Eip += 1;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

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

    if(InterlockedIncrement(&g_VectoredDumpDepth) > 1)
    {
        InterlockedDecrement(&g_VectoredDumpDepth);
        printf("Emu (0x%lX): nested exception [0x%.08lX]@0x%.08lX during vectored dump -- suppressed.\n",
               GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);
        fflush(stdout);
        if(WasXboxFS)
            EmuSwapFS();
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if(e->ExceptionRecord->ExceptionCode == g_VectoredLastCode &&
       (ULONG)e->ContextRecord->Eip == g_VectoredLastEip)
    {
        if(InterlockedIncrement(&g_VectoredRepeatCount) > 3)
        {
            printf("Emu (0x%lX): Vectored exception [0x%.08lX]@0x%.08lX repeat x%ld -- dump suppressed.\n",
                   GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip,
                   g_VectoredRepeatCount);
            fflush(stdout);
            InterlockedDecrement(&g_VectoredDumpDepth);
            if(WasXboxFS)
                EmuSwapFS();
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }
    else
    {
        g_VectoredLastCode = e->ExceptionRecord->ExceptionCode;
        g_VectoredLastEip = (ULONG)e->ContextRecord->Eip;
        g_VectoredRepeatCount = 1;
    }

    printf("Emu (0x%lX): Vectored exception [0x%.08lX]@0x%.08lX\n",
           GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);
    printf("Emu (0x%lX): Vectored ESP=0x%.08lX EBP=0x%.08lX\n",
           GetCurrentThreadId(), e->ContextRecord->Esp, e->ContextRecord->Ebp);
    {
        const char *last = EmuGetLastD3DCall();
        if(last != NULL)
            printf("Emu (0x%lX): Vectored last D3D call: %s\n",
                   GetCurrentThreadId(), last);
    }

    // TIB sanity: RtlDispatchException rejects the whole SEH chain (and the
    // exception goes unhandled) when a registration record lies outside
    // [StackLimit, StackBase]. Under the FS content-swap on guest-created
    // threads those bounds may not describe the stack actually in use, so
    // print the verdict and walk the chain with module attribution.
    {
        NT_TIB *Tib = (NT_TIB*)NtCurrentTeb();
        ULONG Esp = e->ContextRecord->Esp;
        bool EspInBounds = (Esp >= (ULONG)Tib->StackLimit && Esp < (ULONG)Tib->StackBase);

        printf("Emu (0x%lX): Vectored TIB: StackBase=0x%p StackLimit=0x%p ExceptionList=0x%p (ESP %s bounds)\n",
               GetCurrentThreadId(), Tib->StackBase, Tib->StackLimit, Tib->ExceptionList,
               EspInBounds ? "inside" : "OUTSIDE");

        struct SehRecord { SehRecord *Next; void *Handler; };
        SehRecord *Rec = (SehRecord*)Tib->ExceptionList;
        char Where[MAX_PATH + 16];
        for(ULONG i = 0; i < 8 && Rec != (SehRecord*)0xFFFFFFFF; i++)
        {
            if(!EmuIsReadableRange((ULONG)Rec, sizeof(SehRecord)))
            {
                printf("Emu (0x%lX): Vectored SEH[%lu] 0x%p UNREADABLE -- chain broken.\n",
                       GetCurrentThreadId(), i, Rec);
                break;
            }

            bool RecInBounds = ((ULONG)Rec >= (ULONG)Tib->StackLimit &&
                                (ULONG)Rec < (ULONG)Tib->StackBase);
            const char *HandlerName =
                EmuHostAddressToModuleOffset((ULONG)Rec->Handler, Where, sizeof(Where));
            printf("Emu (0x%lX): Vectored SEH[%lu] rec=0x%p%s handler=0x%p%s%s\n",
                   GetCurrentThreadId(), i, Rec, RecInBounds ? "" : " (OUTSIDE stack bounds)",
                   Rec->Handler, HandlerName != NULL ? " = " : "",
                   HandlerName != NULL ? HandlerName : "");
            Rec = Rec->Next;
        }
    }
    if(e->ExceptionRecord->NumberParameters >= 2)
    {
        printf("Emu (0x%lX): Vectored access type=%lu address=0x%.08lX\n",
               GetCurrentThreadId(), e->ExceptionRecord->ExceptionInformation[0],
               e->ExceptionRecord->ExceptionInformation[1]);
    }

    // MSVC C++ throw (0xE06D7363): ExceptionInformation = {0x19930520 magic,
    // thrown object, ThrowInfo}. On x86 the ThrowInfo chain holds absolute
    // pointers, so the thrown type's decorated name is reachable directly:
    // ThrowInfo+12 -> CatchableTypeArray, +4 -> CatchableType, +4 ->
    // TypeDescriptor, +8 -> name. Turns an anonymous host abort into e.g.
    // ".?AVbad_alloc@std@@" -- or ".J" for a bare `throw <long>`, where the
    // payload is the object's first dword. Every pointer hop is probed with
    // VirtualQuery: this chain comes from a crashed context, and faulting on
    // it here would nest another exception inside the dump.
    if(e->ExceptionRecord->ExceptionCode == 0xE06D7363 &&
       e->ExceptionRecord->NumberParameters >= 3)
    {
        ULONG ThrowInfo = (ULONG)e->ExceptionRecord->ExceptionInformation[2];
        ULONG Object = (ULONG)e->ExceptionRecord->ExceptionInformation[1];
        const char *TypeName = "(unreadable)";

        if(EmuIsReadableRange(ThrowInfo + 12, 4))
        {
            ULONG CatchableArray = *(ULONG*)(ThrowInfo + 12);
            if(EmuIsReadableRange(CatchableArray + 4, 4))
            {
                ULONG FirstCatchable = *(ULONG*)(CatchableArray + 4);
                if(EmuIsReadableRange(FirstCatchable + 4, 4))
                {
                    ULONG TypeDescriptor = *(ULONG*)(FirstCatchable + 4);
                    if(EmuIsReadableRange(TypeDescriptor + 8, 64))
                        TypeName = (const char*)(TypeDescriptor + 8);
                }
            }
        }

        printf("Emu (0x%lX): Vectored C++ throw type='%.64s' object=0x%.08lX\n",
               GetCurrentThreadId(), TypeName, Object);

        if(EmuIsReadableRange(Object, 16))
        {
            ULONG *Data = (ULONG*)Object;
            printf("Emu (0x%lX): Vectored C++ throw object data: 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX\n",
                   GetCurrentThreadId(), Data[0], Data[1], Data[2], Data[3]);
        }
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

        printf("Emu (0x%lX): Vectored stack: 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX\n",
               GetCurrentThreadId(), Stack[0], Stack[1], Stack[2], Stack[3], Stack[4], Stack[5], Stack[6], Stack[7],
               Stack[8], Stack[9], Stack[10], Stack[11]);
        printf("Emu (0x%lX): Vectored stack+12: 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX 0x%.08lX\n",
               GetCurrentThreadId(), Stack[12], Stack[13], Stack[14], Stack[15], Stack[16], Stack[17], Stack[18], Stack[19]);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        printf("Emu (0x%lX): Vectored stack unavailable.\n", GetCurrentThreadId());
    }

    // Attribute host frames: the faulting EIP plus every stack slot that
    // resolves into a loaded module, so a host-side fault names the DLL and
    // offset that raised it instead of bare addresses. Data pointers into a
    // module's rdata resolve too -- noise, but cheap to ignore when reading.
    __try
    {
        char Where[MAX_PATH + 16];

        if(EmuHostAddressToModuleOffset(e->ContextRecord->Eip, Where, sizeof(Where)) != NULL)
            printf("Emu (0x%lX): Vectored EIP = %s\n", GetCurrentThreadId(), Where);

        DWORD *Stack = (DWORD*)e->ContextRecord->Esp;
        for(ULONG Slot = 0; Slot < 256; Slot++)
        {
            if(!EmuIsReadableRange((ULONG)&Stack[Slot], 4))
                break;
            if(EmuHostAddressToModuleOffset(Stack[Slot], Where, sizeof(Where)) != NULL)
                printf("Emu (0x%lX): Vectored stack[%03lu] 0x%.08lX = %s\n",
                       GetCurrentThreadId(), Slot, Stack[Slot], Where);
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    fflush(stdout);

    InterlockedDecrement(&g_VectoredDumpDepth);

    // Survive a worker thread's unrecoverable fault (opt-in): a fault in HLE
    // code mid-guest-call does not reliably unwind to the PCSTProxy __except
    // (the SEH chain / FS state at the fault is unreliable), so terminating in
    // EmuException is not enough. Redirect the faulting thread's execution to a
    // clean ExitThread instead -- an unrelated subsystem crash (e.g. a DSOUND
    // mixer thread, or a partial-HLE resource helper) then loses only its own
    // thread rather than tearing down a title reaching its render loop. The
    // FS role was re-anchored to the faulting EIP at entry, so ExitThread runs
    // in the correct role.
    if(g_EmuInitialThreadId != 0 && GetCurrentThreadId() != g_EmuInitialThreadId &&
       getenv("CXBX_SURVIVE_THREAD_FAULT") != NULL)
    {
        extern void EmuThreadFaultExit();
        printf("Emu (0x%lX): worker thread fault survived -- exiting this thread only.\n",
               GetCurrentThreadId());
        fflush(stdout);
        e->ContextRecord->Eip = (ULONG)(uintptr_t)&EmuThreadFaultExit;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    if(WasXboxFS)
        EmuSwapFS();

    return EXCEPTION_CONTINUE_SEARCH;
}

// Redirect target for a survived worker-thread fault: end the thread cleanly.
void EmuThreadFaultExit()
{
    ExitThread(0);
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

    // Thread-EIP watchdog (opt-in via CXBX_FENCE_DUMP=<interval seconds>):
    // periodic snapshots of every thread's EIP so a stalled title's thread
    // landscape can be read. Started here, before FS emulation touches this
    // thread -- a bare CreateThread after the FS content-swap is armed hangs
    // the boot (the loader reads TEB fields the swap has repurposed).
    if(getenv("CXBX_FENCE_DUMP") != NULL)
        CreateThread(NULL, 0, EmuThreadEipWatchdog, NULL, 0, NULL);

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

        // Install the pattern-scanned bootstraps BEFORE the HLE pass: the OOVPA
        // engine overwrites located functions' prologues (e.g. D3DDevice_Swap,
        // whose first bytes carry the D3D__pDevice address the CDX bootstrap
        // extracts), which destroys the signatures these scans look for.
        EmuInstallNestopiaX13Bootstrap(pXbeHeader);
        EmuInstallFceultraBootstrap(pXbeHeader);
        EmuInstallCdxLaunchBootstrap(pXbeHeader);
        EmuInstallDsoundApuAccountingPatch(pXbeHeader);
        EmuInstallAutoBootLaunchData();

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

                // CXBX_HLE_SKIP holds a comma-separated library list (e.g.
                // "DSOUND" or "DSOUND,XGRAPHC") whose HLE tables are NOT
                // installed, leaving the guest library to run natively.
                // A partially-hooked library is often worse than an unhooked
                // one: an HLE'd create hands out an emulator-owned object and
                // the first un-hooked guest method that receives it faults.
                bool skipped_by_env = false;
                {
                    const char *skip = getenv("CXBX_HLE_SKIP");
                    if(skip != NULL && strstr(skip, szLibraryName) != NULL)
                        skipped_by_env = true;
                }

                for(uint32 d=0;d<dwHLEEntries && !skipped_by_env;d++)
                {
                    if(BuildVersion != HLEDataBase[d].BuildVersion || MinorVersion != HLEDataBase[d].MinorVersion || MajorVersion != HLEDataBase[d].MajorVersion || strcmp(szLibraryName, HLEDataBase[d].Library) != 0)
                        continue;

                    found = true;

                    printf("Found\n");

                    EmuInstallWrappers(HLEDataBase[d].OovpaTable, HLEDataBase[d].OovpaTableSize, Entry, pXbeHeader);
                }

                if(skipped_by_env)
                    printf("Skipped (CXBX_HLE_SKIP)\n");
                else if(!found)
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

    // CRC32 trace breakpoint (opt-in): install an int3 at a guest hash
    // function to capture the exact strings the title feeds to CRC32. The
    // exception handler logs each call and re-installs the breakpoint.
    // CXBX_CRC_TRACE holds the guest VA in hex; a value that is not a guest
    // address selects the Turok .tre hash function at 0x000267B0.
    if(getenv("CXBX_CRC_TRACE") != NULL)
    {
        uint32 Addr = strtoul(getenv("CXBX_CRC_TRACE"), NULL, 16);
        if(Addr < 0x00010000 || Addr >= 0x10000000)
            Addr = 0x000267B0;

        g_Crc32OrigByte = *(uint8_t*)Addr;
        g_Crc32BpAddr = Addr;
        *(uint8_t*)Addr = 0xCC;
        printf("Emu (0x%X): CRC32 trace breakpoint installed at 0x%.08X (orig byte 0x%02X).\n",
               GetCurrentThreadId(), Addr, g_Crc32OrigByte);
    }

    // Start the AC97 bus-master DMA engine (EmuKrnl.cpp). It idles until a
    // title sets a channel run bit; started here (not from the MMIO trap
    // handler) because CreateThread inside the vectored-exception path wedges
    // the faulting thread.
    EmuAciStartDmaThread();

    printf("Emu (0x%X): Initializing Direct3D.\n", GetCurrentThreadId());

    XTL::EmuD3DInit(pXbeHeader, dwXbeHeaderSize);

    printf("Emu (0x%X): Initial thread starting.\n", GetCurrentThreadId());

    g_EmuInitialThreadId = GetCurrentThreadId();

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

    EmuNv2aDumpMethodStats("cleanup");

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
    *(ULONG*)(Pe + 0x18 + 0x38) = 0x00082000;       // OptionalHeader.SizeOfImage
    *(ULONG*)(Pe + 0x18 + 0x5C) = 0x10;             // OptionalHeader.NumberOfRvaAndSizes

    // Data directory 0: the export directory (lives in .edata, RVA 0x76000).
    const ULONG ExportDirRva = 0x00076000;
    const ULONG ExportOrdinalBase = 1;
    const ULONG ExportFunctionCount = 366;          // real xboxkrnl exports ordinals 1..366
    *(ULONG*)(Pe + 0x18 + 0x60) = ExportDirRva;
    *(ULONG*)(Pe + 0x18 + 0x64) = 0x28 + ExportFunctionCount * 4;

    // IMAGE_SECTION_HEADERs after the optional header. The XDK XAPI startup scans
    // for the LAST section named 'INIT' (real xboxkrnl keeps INIT last, as it is
    // discarded after boot). Finding it, the title builds a ring-0 code descriptor
    // over INIT, far-jumps to CS 8, verifies CS==8 -- which a user-mode HLE can
    // never satisfy -- and QuickReboots forever. Presenting a kernel whose last
    // section is NOT 'INIT' models the *post-patch* kernel (INIT already discarded):
    // the title's `cmpl [last],'INIT'` fails, it skips the whole ring-0 acquisition,
    // and proceeds straight into the app -- no reboot, valid state, no per-title
    // bootstrap patch needed. Opt-in via CXBX_KERNEL_SKIP_INIT while it is proven
    // out against the title set.
    const bool SkipInit = getenv("CXBX_KERNEL_SKIP_INIT") != NULL;
    BYTE* Sec = Pe + 0x18 + 0xE0;
    struct
    {
        char Name[8];
        ULONG VSize, VAddr;
    } Sects[6] = {
        { { '.', 't', 'e', 'x', 't', 0, 0, 0 }, 0x5D000, 0x00001000 },
        { { '.', 'd', 'a', 't', 'a', 0, 0, 0 }, 0x03000, 0x0005E000 },
        { { 'P', 'A', 'G', 'E', 0, 0, 0, 0 }, 0x10000, 0x00061000 },
        { { '.', 'r', 'd', 'a', 't', 'a', 0, 0 }, 0x05000, 0x00071000 },
        { { '.', 'e', 'd', 'a', 't', 'a', 0, 0 }, 0x02000, 0x00076000 },
        { { 'I', 'N', 'I', 'T', 0, 0, 0, 0 }, 0x0A000, 0x00078000 },
    };
    if(SkipInit)
        memcpy(Sects[5].Name, ".init\0\0\0", 8);   // not 'INIT' -> title skips the ring-0 patch+reboot
    for(int i = 0; i < 6; i++)
    {
        BYTE *S = Sec + i * 0x28;
        memcpy(S, Sects[i].Name, 8);
        *(ULONG*)(S + 0x08) = Sects[i].VSize;       // VirtualSize
        *(ULONG*)(S + 0x0C) = Sects[i].VAddr;       // VirtualAddress
        *(ULONG*)(S + 0x10) = Sects[i].VSize;       // SizeOfRawData
        *(ULONG*)(S + 0x14) = Sects[i].VAddr;       // PointerToRawData
    }

    // IMAGE_EXPORT_DIRECTORY + AddressOfFunctions in .edata. The Xbox kernel
    // exports by ordinal only (no name table). Each function "RVA" is chosen so
    // that ImageBase + RVA wraps (mod 2^32) to the host EmuKrnl implementation
    // in KernelThunkTable -- an ordinal-resolving guest loader (the EvolutionX
    // dashboard patches its own kernel-thunk markers this way) then calls our
    // kernel functions directly, exactly like the launcher-patched XBE thunks.
    BYTE Edata[0x28 + ExportFunctionCount * 4];
    ZeroMemory(Edata, sizeof(Edata));
    *(ULONG*)(Edata + 0x10) = ExportOrdinalBase;                // Base
    *(ULONG*)(Edata + 0x14) = ExportFunctionCount;              // NumberOfFunctions
    *(ULONG*)(Edata + 0x1C) = ExportDirRva + 0x28;              // AddressOfFunctions

    ULONG *FunctionRvas = (ULONG*)(Edata + 0x28);
    for(ULONG Ordinal = ExportOrdinalBase; Ordinal < ExportOrdinalBase + ExportFunctionCount; Ordinal++)
        FunctionRvas[Ordinal - ExportOrdinalBase] = KernelThunkTable[Ordinal] - 0x80010000;

    if(EmuWritePhysicalMapBytes(EmuPhysicalMapBase + 0x00010000, Image, sizeof(Image)) &&
       EmuWritePhysicalMapBytes(EmuPhysicalMapBase + 0x00010000 + ExportDirRva, Edata, sizeof(Edata)))
    {
        printf("Emu (0x%lX): fake Xbox kernel PE installed at 0x80010000 (INIT @ RVA 0x61000, %lu exports).\n",
               GetCurrentThreadId(), ExportFunctionCount);
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

// Find a unique occurrence of a byte pattern inside the mapped XBE image.
// Returns the guest address of the single match, or 0 when the pattern is
// absent or ambiguous (multiple hits).
static uint32 EmuFindUniquePattern(Xbe::Header *pXbeHeader, const uint08 *Bytes, uint32 Count)
{
    uint32 Base = pXbeHeader->dwBaseAddr;
    uint32 End = Base + pXbeHeader->dwSizeofImage;
    uint32 Found = 0;

    for(uint32 Address = Base; Address + Count <= End; Address++)
    {
        if(IsBadReadPtr((void*)Address, Count))
        {
            Address += 0xFFF;   // skip toward the next page
            continue;
        }

        if(memcmp((void*)Address, Bytes, Count) == 0)
        {
            if(Found != 0)
                return 0;       // ambiguous
            Found = Address;
        }
    }

    return Found;
}

// The XDK DSOUND library accounts APU-heap usage through a pointer global that
// the audio-processor init would set on real hardware; in this HLE it stays
// NULL, so DSOUND's `mov eax,[global] / mov ecx,[ebx+8] / add [eax],ecx`
// accounting faults on the first sound-buffer allocation. Three titles have
// now hit the identical compiler-generated site (FCEUltra 0x0010C1D7, z26x
// 0x0017502F, NestopiaX 1.3): find every occurrence of the pattern in the
// image and rewrite it to bump a scratch counter instead, leaving the
// accounting a harmless no-op.
static void EmuInstallDsoundApuAccountingPatch(Xbe::Header *pXbeHeader)
{
    if(pXbeHeader->dwBaseAddr != 0x00010000)
        return;

    // A1 ?? ?? ?? ?? 8B (4B|4E) 08 (01|29) 08 : mov eax,[global] /
    // mov ecx,[ebx+8 or esi+8] / add-or-sub [eax],ecx. The alloc side uses
    // add (seen via ebx on FCEUltra/z26x/NestopiaX 1.3); the FREE side uses
    // sub via esi (seen on NestopiaX 1.0's CMcpxGPDspManager teardown after
    // GP-DSP init fails against the APU model). The ?? dword is the
    // per-title accounting global; it stays NULL because the DSP-heap init
    // that would set it never succeeds under the HLE.
    const uint08 Head = 0xA1;
    static uint32 s_ApuScratchCounters[4];
    int Patched = 0;

    uint32 Base = pXbeHeader->dwBaseAddr;
    uint32 End = Base + pXbeHeader->dwSizeofImage;
    for(uint32 Address = Base; Address + 10 <= End && Patched < 4; Address++)
    {
        if(IsBadReadPtr((void*)Address, 10))
        {
            Address += 0xFFF;   // skip toward the next page
            continue;
        }

        const uint08 *p = (const uint08*)Address;
        if(p[0] != Head ||
           p[5] != 0x8B || (p[6] != 0x4B && p[6] != 0x4E) || p[7] != 0x08 ||
           (p[8] != 0x01 && p[8] != 0x29) || p[9] != 0x08)
            continue;

        // The loaded global must live inside the image and still be NULL
        // (never initialised) -- both true only for the DSOUND counter.
        uint32 GlobalAddr = *(uint32*)(Address + 1);
        if(GlobalAddr < Base || GlobalAddr + 4 > End || *(uint32*)GlobalAddr != 0)
            continue;

        // mov ecx,[reg+8] ; add-or-sub [&scratch],ecx ; nop
        // (preserves the original register and add/sub opcode)
        uint08 Patch[10] = { 0x8B, p[6], 0x08, p[8], 0x0D, 0, 0, 0, 0, 0x90 };
        *(uint32*)&Patch[5] = (uint32)(uintptr_t)&s_ApuScratchCounters[Patched];
        EmuWriteBytes(Address, Patch, sizeof(Patch));
        printf("Emu (0x%lX): DSOUND APU accounting (0x%.08lX, %s, global 0x%.08lX) redirected to scratch.\n",
               GetCurrentThreadId(), Address, (p[8] == 0x01) ? "add" : "sub", GlobalAddr);
        Patched++;
    }

    if(Patched != 0)
        fflush(stdout);
}

// XDK 5849 titles built on the CDX demo framework (the dolphin demo, the CDX
// player, z26x-era launchers) relaunch at startup via an XLaunchNewImage
// wrapper whose success path QuickReboots (HalReturnToFirmware(2)) -- on real
// hardware the reboot re-runs the image with the launch applied. A user-mode
// HLE can't persist that, and letting the reboot return sends the framework
// down an error path that dereferences uninitialised globals. Locate the
// wrapper by its (position-independent) prologue signature anywhere in the
// image and neutralise it to a no-op return -- the same targeted-patch
// approach as EmuInstallFceultraBootstrap, made title-agnostic.
static void EmuInstallCdxLaunchBootstrap(Xbe::Header *pXbeHeader)
{
    if(pXbeHeader->dwBaseAddr != 0x00010000)
        return;

    // XLaunchNewImage wrapper prologue: push ebp / mov ebp,esp / push args / push 1
    const uint08 LaunchWrapperSig[] =
    {
        0x55, 0x8B, 0xEC, 0xFF, 0x75, 0x18, 0xFF, 0x75, 0x14,
        0xFF, 0x75, 0x10, 0x6A, 0x01, 0xFF, 0x75, 0x0C, 0xFF, 0x75, 0x08
    };
    // xor eax,eax ; ret 0x14  (matches the wrapper's own __stdcall ret 0x14)
    const uint08 LaunchWrapperPatch[] = { 0x33, 0xC0, 0xC2, 0x14, 0x00 };

    uint32 WrapperAddr = EmuFindUniquePattern(pXbeHeader, LaunchWrapperSig, sizeof(LaunchWrapperSig));
    if(WrapperAddr == 0)
        return;

    EmuWriteBytes(WrapperAddr, LaunchWrapperPatch, sizeof(LaunchWrapperPatch));
    printf("Emu (0x%lX): CDX XLaunchNewImage wrapper (0x%.08lX) neutralised (no reboot-to-self).\n",
           GetCurrentThreadId(), WrapperAddr);

    // The HLE replaces the D3D8 library code that would set its internal device
    // global D3D__pDevice, so un-patched library internals (e.g. present.obj
    // helpers) dereference NULL. The global's per-title address is embedded in
    // D3DDevice_Swap's prologue (`push esi / mov esi,[D3D__pDevice] /
    // mov eax,[esi+8D4h]`); extract it and point it at a zeroed scratch device
    // so those internals read benign state and skip their raw-hardware work,
    // while the patched API surface (Clear/Swap/...) renders through the host
    // device.
    // The device-field offset in the prologue differs per XDK: 5849 reads
    // [esi+8D4h], 5558 reads [esi+8D0h] -- accept either.
    const uint08 SwapPrologueHead[] = { 0x56, 0x8B, 0x35 };
    const uint08 SwapPrologueTail5849[] = { 0x8B, 0x86, 0xD4, 0x08, 0x00, 0x00, 0x85, 0xC0 };
    const uint08 SwapPrologueTail5558[] = { 0x8B, 0x86, 0xD0, 0x08, 0x00, 0x00, 0x85, 0xC0 };

    uint32 Base = pXbeHeader->dwBaseAddr;
    uint32 End = Base + pXbeHeader->dwSizeofImage;
    bool DeviceGlobalFound = false;
    for(uint32 Address = Base; Address + 15 <= End; Address++)
    {
        if(IsBadReadPtr((void*)Address, 15))
        {
            Address += 0xFFF;
            continue;
        }

        if(memcmp((void*)Address, SwapPrologueHead, sizeof(SwapPrologueHead)) == 0 &&
           (memcmp((void*)(Address + 7), SwapPrologueTail5849, sizeof(SwapPrologueTail5849)) == 0 ||
            memcmp((void*)(Address + 7), SwapPrologueTail5558, sizeof(SwapPrologueTail5558)) == 0))
        {
            uint32 DeviceGlobal = *(uint32*)(Address + 3);
            if(DeviceGlobal >= Base && DeviceGlobal < End)
            {
                static uint08 s_CdxFakeD3DDevice[0x4000];
                *(uint32*)DeviceGlobal = (uint32)(uintptr_t)s_CdxFakeD3DDevice;
                DeviceGlobalFound = true;
                printf("Emu (0x%lX): CDX D3D__pDevice (0x%.08lX) pointed at a scratch device.\n",
                       GetCurrentThreadId(), DeviceGlobal);
            }
            else
            {
                printf("Emu (0x%lX): CDX D3D__pDevice candidate at 0x%.08lX rejected (global 0x%.08lX out of image).\n",
                       GetCurrentThreadId(), Address, DeviceGlobal);
            }
            break;
        }
    }

    if(!DeviceGlobalFound)
        printf("Emu (0x%lX): CDX D3D__pDevice prologue not located in the image.\n",
               GetCurrentThreadId());

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
    const uint08 QueryPerformanceCounterBytes[] =
    {
        0x8B, 0x4C, 0x24, 0x04, 0x0F, 0x31, 0x89, 0x01,
        0x89, 0x51, 0x04, 0x33, 0xC0, 0x40, 0xC2, 0x04,
        0x00
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
    const uint32 QueryPerformanceCounter = 0x00132E0B;
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

    if(!EmuBytesMatch(QueryPerformanceCounter, QueryPerformanceCounterBytes,
                      sizeof(QueryPerformanceCounterBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; QueryPerformanceCounter bytes did not match.\n",
               GetCurrentThreadId());
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
    printf("Emu (0x%lX): 0x%.08lX -> EmuQueryPerformanceCounter\n", GetCurrentThreadId(), QueryPerformanceCounter);
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
    EmuInstallWrapper((void*)QueryPerformanceCounter, XTL::EmuQueryPerformanceCounter);
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

        uint32 scan = lower;

        do
        {
            void *pFunc = EmuLocateFunction(Oovpa, scan, upper);

            if(pFunc == 0)
                break;

            #ifdef _DEBUG_TRACE
            printf("Emu (0x%X): 0x%.08X -> %s\n", GetCurrentThreadId(), pFunc, OovpaTable[a].szFuncName);
            #endif

            // Opt-in install trace (CXBX_HLE_TRACE): one line per patch with
            // the table entry index, so hook installation can be audited in
            // builds without _DEBUG_TRACE (which carries the entry names).
            {
                static int HleTrace = -1;

                if(HleTrace < 0)
                    HleTrace = (getenv("CXBX_HLE_TRACE") != NULL) ? 1 : 0;

                if(HleTrace)
                    printf("HLE| patch entry=%u addr=0x%.08X redirect=0x%.08X flags=%u\n",
                           a, (uint32)pFunc, (uint32)OovpaTable[a].lpRedirect, OovpaTable[a].Flags);
            }

            if(OovpaTable[a].lpRedirect == 0)
                EmuInstallWrapper(pFunc, EmuXRefFailure);
            else
                EmuInstallWrapper(pFunc, OovpaTable[a].lpRedirect);

            // A PATCH_ALL entry keeps scanning past the match; the E9 jmp
            // just written makes re-matching the same address impossible.
            scan = (uint32)pFunc + 1;
        }
        while(OovpaTable[a].Flags & OOVPA_FLAG_PATCH_ALL);
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
    // Re-anchor the content-swap role to the faulting EIP (see
    // EmuVectoredExceptionHandler) -- this filter can run after an unwind
    // already skipped balancing EmuSwapFS calls.
    EmuFsSwapEnsureRole((ULONG)e->ContextRecord->Eip < 0x10000000);

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

        // Survive a worker thread's unrecoverable fault (opt-in): let the
        // faulting thread unwind to its PCSTProxy __except and die alone,
        // instead of terminating the whole title. The initial/render thread
        // stays fatal so a broken render path is still surfaced.
        if(g_EmuInitialThreadId != 0 && GetCurrentThreadId() != g_EmuInitialThreadId &&
           getenv("CXBX_SURVIVE_THREAD_FAULT") != NULL)
        {
            printf("Emu (0x%X): worker thread fault survived -- terminating this thread only.\n",
                   GetCurrentThreadId());
            fflush(stdout);
            if(WasXboxFS)
                EmuSwapFS();
            return EXCEPTION_EXECUTE_HANDLER;
        }

        if(EmuGetLogFile(szLogFile, sizeof(szLogFile)))
            ExitProcess(e->ExceptionRecord->ExceptionCode);

        if(MessageBox(XTL::g_hEmuWindow, buffer, "cxbx", MB_ICONSTOP | MB_OKCANCEL) == IDOK)
			ExitProcess(1);
	}

    // Restore the role the faulting code held (the entry swap above moved to the
    // host role); without this, declining the search in the Xbox role leaves the
    // slots inverted for whatever handler runs next.
    if(WasXboxFS)
        EmuSwapFS();

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
