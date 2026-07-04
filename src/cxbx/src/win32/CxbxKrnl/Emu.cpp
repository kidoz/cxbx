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
// *  along with this program; see the file LICENSE.md.
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
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return false;
}

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
    if((FaultAddress & 0xFF000000) != 0xFD000000)
        return false;

    __try
    {
        BYTE *Instruction = (BYTE*)e->ContextRecord->Eip;

        if(AccessType == 0 && Instruction[0] == 0xA1 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            e->ContextRecord->Eax = 0;
            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
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
                EmuSetContextRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07, 0);
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
                fflush(stdout);

                return true;
            }
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x01 &&
           e->ContextRecord->Ecx == FaultAddress)
        {
            e->ContextRecord->Eax = 0;
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x08 &&
           e->ContextRecord->Eax == FaultAddress)
        {
            e->ContextRecord->Ecx = 0;
            e->ContextRecord->Eip += 2;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x00 &&
           e->ContextRecord->Eax == FaultAddress)
        {
            e->ContextRecord->Eax = 0;
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
                EmuSetContextByteRegister(e->ContextRecord, (Instruction[1] >> 3) & 0x07, 0);
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
            switch((Instruction[2] >> 3) & 0x07)
            {
                case 0: e->ContextRecord->Eax = 0; break;
                case 1: e->ContextRecord->Ecx = 0; break;
                case 2: e->ContextRecord->Edx = 0; break;
                case 3: e->ContextRecord->Ebx = 0; break;
                case 4: e->ContextRecord->Esp = 0; break;
                case 5: e->ContextRecord->Ebp = 0; break;
                case 6: e->ContextRecord->Esi = 0; break;
                case 7: e->ContextRecord->Edi = 0; break;
            }
            e->ContextRecord->Eip += 4;

            printf("Emu (0x%lX): Emulated MMIO byte read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x82 &&
           e->ContextRecord->Edx + *(ULONG*)&Instruction[2] == FaultAddress)
        {
            e->ContextRecord->Eax = 0;
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x92 &&
           e->ContextRecord->Edx + *(ULONG*)&Instruction[2] == FaultAddress)
        {
            e->ContextRecord->Edx = 0;
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 0 && Instruction[0] == 0x8B && Instruction[1] == 0x87 &&
           e->ContextRecord->Edi + *(ULONG*)&Instruction[2] == FaultAddress)
        {
            e->ContextRecord->Eax = 0;
            e->ContextRecord->Eip += 6;

            printf("Emu (0x%lX): Emulated MMIO read 0x%.08lX.\n", GetCurrentThreadId(), FaultAddress);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0xA3 && *(ULONG*)&Instruction[1] == FaultAddress)
        {
            e->ContextRecord->Eip += 5;

            printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
                   GetCurrentThreadId(), FaultAddress, e->ContextRecord->Eax);
            fflush(stdout);

            return true;
        }

        if(AccessType == 1 && Instruction[0] == 0x89 && Instruction[1] == 0x01 &&
           e->ContextRecord->Ecx == FaultAddress)
        {
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
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO write 0x%.08lX = 0x%.08lX.\n",
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
                e->ContextRecord->Eip += OperandLength;

                printf("Emu (0x%lX): Emulated MMIO byte write 0x%.08lX = 0x%.02lX.\n",
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

    if(EmuTryEmulateMmioAccess(e))
    {
        if(WasXboxFS)
            EmuSwapFS();

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    printf("Emu (0x%lX): Vectored exception [0x%.08lX]@0x%.08lX\n",
           GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);
    printf("Emu (0x%lX): Vectored ESP=0x%.08lX EBP=0x%.08lX\n",
           GetCurrentThreadId(), e->ContextRecord->Esp, e->ContextRecord->Ebp);

    __try
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
    }

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

static VOID WINAPI EmuNestopiaX13XapiInitProcess()
{
    EmuSwapFS();   // Win2k/XP FS

    printf("Emu (0x%lX): NestopiaX 1.3 XapiInitProcess skipped.\n", GetCurrentThreadId());

    EmuSwapFS();   // XBox FS
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
    const uint08 XapiProcessStartupFsNotifyBytes[] =
    {
        0x64, 0xA1, 0x20, 0x00, 0x00, 0x00, 0x8B, 0x80,
        0x50, 0x02, 0x00, 0x00
    };
    const uint08 XapiFsCallbackABytes[] =
    {
        0x64, 0xA1, 0x20, 0x00, 0x00, 0x00, 0x8B, 0x80,
        0x50, 0x02, 0x00, 0x00
    };
    const uint08 XapiFsCallbackBBytes[] =
    {
        0x64, 0xA1, 0x20, 0x00, 0x00, 0x00, 0x8B, 0x80,
        0x50, 0x02, 0x00, 0x00
    };

    const uint32 XapiThreadStartup = 0x00133215;
    const uint32 XapiInitProcess = 0x001346E6;
    const uint32 XLaunchNewImageA = 0x001325AC;
    const uint32 XapiProcessGetter = 0x0013331B;
    const uint32 XapiBugCheckGuard = 0x0013BB39;
    const uint32 XapiAfterBugCheckGuard = 0x0013BB4D;
    const uint32 XapiPerThreadData = 0x0013BB4F;
    const uint32 XapiGlobalDataFallback = 0x0013BB72;
    const uint32 XapiProcessStartupFsNotify = 0x001336AE;
    const uint32 XapiProcessStartupAfterFsNotify = 0x001336F0;
    const uint32 XapiFsCallbackA = 0x00136B98;
    const uint32 XapiAfterFsCallbackA = 0x00136BB1;
    const uint32 XapiFsCallbackB = 0x00136C0C;
    const uint32 XapiAfterFsCallbackB = 0x00136C25;

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

    if(!EmuBytesMatch(XapiProcessStartupFsNotify, XapiProcessStartupFsNotifyBytes, sizeof(XapiProcessStartupFsNotifyBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; XapiProcessStartup FS notify bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(XapiFsCallbackA, XapiFsCallbackABytes, sizeof(XapiFsCallbackABytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; Xapi FS callback A bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    if(!EmuBytesMatch(XapiFsCallbackB, XapiFsCallbackBBytes, sizeof(XapiFsCallbackBBytes), pXbeHeader))
    {
        printf("Emu (0x%lX): NestopiaX 1.3 bootstrap skipped; Xapi FS callback B bytes did not match.\n", GetCurrentThreadId());
        return;
    }

    printf("Emu (0x%lX): Installing NestopiaX 1.3 bootstrap HLE patches.\n", GetCurrentThreadId());
    printf("Emu (0x%lX): 0x%.08lX -> EmuXapiThreadStartup\n", GetCurrentThreadId(), XapiThreadStartup);
    printf("Emu (0x%lX): 0x%.08lX -> EmuNestopiaX13XapiInitProcess\n", GetCurrentThreadId(), XapiInitProcess);
    printf("Emu (0x%lX): 0x%.08lX -> EmuNestopiaX13XLaunchNewImageA\n", GetCurrentThreadId(), XLaunchNewImageA);
    printf("Emu (0x%lX): 0x%.08lX -> EmuNestopiaX13GetXapiProcess\n", GetCurrentThreadId(), XapiProcessGetter);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (skip FS bugcheck guard)\n", GetCurrentThreadId(), XapiBugCheckGuard, XapiAfterBugCheckGuard);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (use XAPI global data fallback)\n", GetCurrentThreadId(), XapiPerThreadData, XapiGlobalDataFallback);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (skip FS notify)\n", GetCurrentThreadId(), XapiProcessStartupFsNotify, XapiProcessStartupAfterFsNotify);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (skip FS callback A)\n", GetCurrentThreadId(), XapiFsCallbackA, XapiAfterFsCallbackA);
    printf("Emu (0x%lX): 0x%.08lX -> 0x%.08lX (skip FS callback B)\n", GetCurrentThreadId(), XapiFsCallbackB, XapiAfterFsCallbackB);

    EmuInstallWrapper((void*)XapiThreadStartup, XTL::EmuXapiThreadStartup);
    EmuInstallWrapper((void*)XapiInitProcess, EmuNestopiaX13XapiInitProcess);
    EmuInstallWrapper((void*)XLaunchNewImageA, EmuNestopiaX13XLaunchNewImageA);
    EmuInstallWrapper((void*)XapiProcessGetter, EmuNestopiaX13GetXapiProcess);
    EmuInstallWrapper((void*)XapiBugCheckGuard, (void*)XapiAfterBugCheckGuard);
    EmuInstallWrapper((void*)XapiPerThreadData, (void*)XapiGlobalDataFallback);
    EmuInstallWrapper((void*)XapiProcessStartupFsNotify, (void*)XapiProcessStartupAfterFsNotify);
    EmuInstallWrapper((void*)XapiFsCallbackA, (void*)XapiAfterFsCallbackA);
    EmuInstallWrapper((void*)XapiFsCallbackB, (void*)XapiAfterFsCallbackB);
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
