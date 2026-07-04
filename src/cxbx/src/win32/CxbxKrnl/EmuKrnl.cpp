// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->EmuKrnl.cpp
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

#include <cstdio>
#include <clocale>
#include <cstring>
#include <cstdarg>
#include <map>
#include <string>
#include <vector>

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace NtDll
{
    #include "EmuNtDll.h"
};

#include "Emu.h"
#include "EmuDes.h"
#include "EmuFS.h"
#include "EmuFile.h"

extern "C" uint32 __cdecl EmuDbgPrint(const char *Format, ...)
{
    char Buffer[2048];

    va_list Args;
    va_start(Args, Format);
    int Length = vsnprintf(Buffer, sizeof(Buffer), Format, Args);
    va_end(Args);

    Buffer[sizeof(Buffer) - 1] = '\0';

    if(Length > 0)
        printf("%s", Buffer);

    fflush(stdout);

    return (Length > 0) ? (uint32)Length : 0;
}

struct EmuObjectType
{
    PVOID AllocateProcedure;
    PVOID FreeProcedure;
    PVOID CloseProcedure;
    PVOID DeleteProcedure;
    PVOID ParseProcedure;
    PVOID DefaultObject;
    ULONG PoolTag;
};

extern "C" PVOID NTAPI EmuExAllocatePoolWithTag(ULONG NumberOfBytes, ULONG Tag);
extern "C" VOID NTAPI EmuExFreePool(PVOID P);
extern "C" EmuObjectType g_EmuPsThreadObjectType;

struct EmuObjectHeader
{
    LONG PointerCount;
    LONG HandleCount;
    PVOID Type;
    ULONG Flags;
    ULONGLONG Body;
};

static const ULONG EmuGenericObjectMagic = 0x4F626A48;
static const ULONG EmuThreadObjectMagic = 0x54687264;
struct EmuThreadObjectHeader
{
    LONG PointerCount;
    LONG HandleCount;
    PVOID Type;
    ULONG Flags;
    xboxkrnl::ETHREAD Thread;
    HANDLE HostHandle;
    ULONG SuspendCount;
};

static bool EmuIsThreadObjectType(PVOID ObjectType)
{
    return ObjectType == &g_EmuPsThreadObjectType ||
           ObjectType == xboxkrnl::PsThreadObjectType ||
           ObjectType == &xboxkrnl::PsThreadObjectType;
}

static ULONG EmuProbeThreadSuspendCount(HANDLE ThreadHandle)
{
    DWORD Previous = SuspendThread(ThreadHandle);
    if(Previous == (DWORD)-1)
        return 0;

    ResumeThread(ThreadHandle);
    return Previous;
}

static std::map<HANDLE, ULONG> g_EmuThreadSuspendCounts;
static std::map<std::string, std::string> g_EmuSymbolicLinks;

static const NTSTATUS EmuStatusInvalidParameter = (NTSTATUS)0xC000000D;
static const NTSTATUS EmuStatusObjectNameInvalid = (NTSTATUS)0xC0000033;
static const NTSTATUS EmuStatusObjectPathNotFound = (NTSTATUS)0xC000003A;
static const NTSTATUS EmuStatusSuspendCountExceeded = (NTSTATUS)0xC000004A;

static bool EmuObjectStringToStdString(xboxkrnl::PSTRING ObjectName, std::string *Value)
{
    if(ObjectName == NULL || Value == NULL || ObjectName->Buffer == NULL)
        return false;

    size_t Length = ObjectName->Length;
    if(Length == 0)
        Length = strlen(ObjectName->Buffer);

    if(Length == 0)
        return false;

    Value->assign(ObjectName->Buffer, Length);
    return true;
}

static bool EmuIsValidObjectName(const std::string &Name)
{
    if(Name.empty() || Name[0] != '\\')
        return false;

    if(Name.size() > 1 && Name[Name.size() - 1] == '\\')
        return false;

    for(size_t i = 0; i < Name.size(); i++)
    {
        if(Name[i] == ':')
            return false;

        if(Name[i] == '\\' && i + 1 < Name.size() && Name[i + 1] == '\\')
            return false;
    }

    return true;
}

static bool EmuIsValidSymbolicLinkName(const std::string &Name)
{
    if(Name.size() == 6 && Name.compare(0, 4, "\\??\\") == 0 && Name[5] == ':')
        return true;

    return EmuIsValidObjectName(Name);
}

static std::string EmuObjectParentPath(const std::string &Name)
{
    const size_t Slash = Name.find_last_of('\\');

    if(Slash == 0)
        return "\\";

    if(Slash == std::string::npos)
        return std::string();

    return Name.substr(0, Slash);
}

static bool EmuIsKnownDeviceObject(const std::string &Name)
{
    return Name == "\\Device\\CdRom0" ||
           Name == "\\Device\\Harddisk0\\Partition1" ||
           Name == "\\Device\\Harddisk0\\Partition2" ||
           Name == "\\Device\\Harddisk0\\Partition3" ||
           Name == "\\Device\\Harddisk0\\Partition4" ||
           Name == "\\Device\\Harddisk0\\Partition5" ||
           Name == "\\Device\\Harddisk0\\Partition6" ||
           Name == "\\Device\\Harddisk0\\Partition7";
}

static bool EmuIsKnownDirectoryObject(const std::string &Name)
{
    return Name == "\\" ||
           Name == "\\??" ||
           Name == "\\Device" ||
           Name == "\\Device\\Harddisk0";
}

static bool EmuObjectNameExists(const std::string &Name)
{
    return EmuIsKnownDirectoryObject(Name) ||
           EmuIsKnownDeviceObject(Name) ||
           g_EmuSymbolicLinks.find(Name) != g_EmuSymbolicLinks.end();
}

static bool EmuObjectNameIsBelowKnownDevice(const std::string &Name)
{
    static const char *Devices[] = {
        "\\Device\\CdRom0",
        "\\Device\\Harddisk0\\Partition1",
        "\\Device\\Harddisk0\\Partition2",
        "\\Device\\Harddisk0\\Partition3",
        "\\Device\\Harddisk0\\Partition4",
        "\\Device\\Harddisk0\\Partition5",
        "\\Device\\Harddisk0\\Partition6",
        "\\Device\\Harddisk0\\Partition7",
    };

    for(size_t i = 0; i < sizeof(Devices) / sizeof(Devices[0]); i++)
    {
        const size_t Length = strlen(Devices[i]);
        if(Name.size() > Length && Name.compare(0, Length, Devices[i]) == 0 && Name[Length] == '\\')
            return true;
    }

    return false;
}

static bool EmuIsValidHostThread(HANDLE ThreadHandle)
{
    return ThreadHandle != NULL &&
           ThreadHandle != INVALID_HANDLE_VALUE &&
           GetThreadId(ThreadHandle) != 0;
}

static ULONG &EmuThreadSuspendCountForHandle(HANDLE ThreadHandle)
{
    auto Entry = g_EmuThreadSuspendCounts.find(ThreadHandle);
    if(Entry == g_EmuThreadSuspendCounts.end())
        Entry = g_EmuThreadSuspendCounts.emplace(ThreadHandle, EmuProbeThreadSuspendCount(ThreadHandle)).first;

    return Entry->second;
}

static EmuThreadObjectHeader *EmuThreadHeaderFromThread(xboxkrnl::PKTHREAD Thread)
{
    EmuThreadObjectHeader *Header = (EmuThreadObjectHeader*)((BYTE*)Thread - 16);
    if(Header->Flags != EmuThreadObjectMagic)
        return NULL;

    return Header;
}

extern "C" BOOLEAN NTAPI EmuMmIsAddressValid(PVOID VirtualAddress)
{
    return VirtualAddress != NULL;
}

extern "C" NTSTATUS NTAPI EmuObReferenceObjectByHandle(HANDLE ObjectHandle, PVOID ObjectType, PVOID *Object)
{
    if(Object == NULL)
        return 0xC0000008;

    *Object = NULL;

    if(ObjectHandle == NULL || ObjectHandle == INVALID_HANDLE_VALUE || ObjectHandle == (HANDLE)100000 || ObjectHandle == (HANDLE)-1)
        return 0xC0000008;

    if(ObjectHandle == (HANDLE)-2)
    {
        if(ObjectType != NULL && !EmuIsThreadObjectType(ObjectType))
            return 0xC0000024;

        *Object = EmuGetCurrentThread();
        return STATUS_SUCCESS;
    }

    if(EmuIsThreadObjectType(ObjectType))
    {
        EmuThreadObjectHeader *ThreadHeader = new EmuThreadObjectHeader;
        ZeroMemory(ThreadHeader, sizeof(*ThreadHeader));
        ThreadHeader->PointerCount = 1;
        ThreadHeader->HandleCount = 1;
        ThreadHeader->Type = ObjectType;
        ThreadHeader->Flags = EmuThreadObjectMagic;
        ThreadHeader->HostHandle = ObjectHandle;
        ThreadHeader->SuspendCount = EmuProbeThreadSuspendCount(ObjectHandle);
        ThreadHeader->Thread.UniqueThread = GetThreadId(ObjectHandle);
        *Object = &ThreadHeader->Thread;
        return STATUS_SUCCESS;
    }

    EmuObjectHeader *Header = new EmuObjectHeader;
    ZeroMemory(Header, sizeof(*Header));
    Header->PointerCount = 1;
    Header->HandleCount = 1;
    Header->Type = ObjectType;
    Header->Flags = EmuGenericObjectMagic;
    *Object = &Header->Body;

    return STATUS_SUCCESS;
}

extern "C" VOID __fastcall EmuObfDereferenceObject(PVOID Object)
{
    if(Object == NULL)
        return;

    if(Object == EmuGetCurrentThread())
        return;

    EmuObjectHeader *Header = (EmuObjectHeader*)((BYTE*)Object - 16);
    if(Header->Flags == EmuThreadObjectMagic)
    {
        delete (EmuThreadObjectHeader*)Header;
        return;
    }

    if(Header->Flags == EmuGenericObjectMagic)
        delete Header;
}

extern "C" VOID __fastcall EmuObfReferenceObject(PVOID Object)
{
}

extern "C" __declspec(naked) VOID NTAPI EmuRtlCaptureContext(PVOID ContextRecord)
{
    __asm
    {
        pushfd
        pushad

        mov     esi, [esp+40]

        mov     dword ptr [esi], 10007h

        mov     eax, [esp]
        mov     [esi+208h], eax
        mov     eax, [esp+4]
        mov     [esi+20Ch], eax
        mov     eax, [esp+16]
        mov     [esi+210h], eax
        mov     eax, [esp+20]
        mov     [esi+214h], eax
        mov     eax, [esp+24]
        mov     [esi+218h], eax
        mov     eax, [esp+28]
        mov     [esi+21Ch], eax
        mov     eax, [esp+8]
        mov     [esi+220h], eax
        mov     eax, [esp+36]
        mov     [esi+224h], eax

        mov     ax, cs
        movzx   eax, ax
        mov     [esi+228h], eax
        mov     ax, ss
        movzx   eax, ax
        mov     [esi+234h], eax

        mov     eax, [esp+32]
        mov     [esi+22Ch], eax
        lea     eax, [esp+44]
        mov     [esi+230h], eax

        popad
        popfd
        ret     4
    }
}

extern "C" __declspec(naked) USHORT NTAPI EmuRtlCaptureStackBackTrace(ULONG FramesToSkip, ULONG FramesToCapture, PVOID *BackTrace, PULONG BackTraceHash)
{
    __asm
    {
        push    ebx
        push    esi
        push    edi
        push    0

        mov     ebx, [esp+20h]
        test    ebx, ebx
        jz      capture_hash_init_done
        mov     dword ptr [ebx], 0

    capture_hash_init_done:
        xor     eax, eax
        xor     ecx, ecx
        mov     esi, [esp+14h]
        mov     edi, [esp+1Ch]
        mov     ebx, [esp+10h]
        mov     edx, ebp

    capture_next:
        cmp     ecx, 8
        jae     capture_done
        cmp     ecx, esi
        jb      capture_advance_frame
        cmp     eax, [esp+18h]
        jae     capture_done
        test    edi, edi
        jz      capture_hash_frame
        mov     [edi+eax*4], ebx

    capture_hash_frame:
        add     [esp], ebx
        inc     eax

    capture_advance_frame:
        inc     ecx
        test    edx, edx
        jz      capture_done
        mov     ebx, [edx+4]
        test    ebx, ebx
        jz      capture_done
        mov     edx, [edx]
        jmp     capture_next

    capture_done:
        mov     ebx, [esp+20h]
        test    ebx, ebx
        jz      capture_return
        mov     ecx, [esp]
        mov     [ebx], ecx

    capture_return:
        add     esp, 4
        pop     edi
        pop     esi
        pop     ebx
        ret     10h
    }
}

extern "C" __declspec(naked) ULONG NTAPI EmuRtlWalkFrameChain(PVOID *Callers, ULONG Count, ULONG Flags)
{
    __asm
    {
        push    ebx
        push    esi
        push    edi

        xor     eax, eax
        mov     edi, [esp+10h]
        mov     esi, [esp+14h]
        cmp     esi, 8
        jbe     walk_count_ready
        mov     esi, 8

    walk_count_ready:
        test    edi, edi
        jz      walk_done
        test    esi, esi
        jz      walk_done

        mov     ebx, [esp+0Ch]
        mov     [edi], ebx
        inc     eax

        mov     edx, ebp

    walk_next:
        cmp     eax, esi
        jae     walk_done
        test    edx, edx
        jz      walk_done
        mov     ebx, [edx+4]
        test    ebx, ebx
        jz      walk_done
        mov     [edi+eax*4], ebx
        inc     eax
        mov     edx, [edx]
        jmp     walk_next

    walk_done:
        pop     edi
        pop     esi
        pop     ebx
        ret     0Ch
    }
}

extern "C" NTSTATUS NTAPI EmuRtlAppendStringToString(xboxkrnl::PSTRING Destination, xboxkrnl::PSTRING Source)
{
    if(Destination == NULL || Source == NULL)
        return STATUS_SUCCESS;

    if((ULONG)Destination->Length + Source->Length > Destination->MaximumLength)
        return 0xC0000023;

    if(Source->Length != 0)
        memcpy(Destination->Buffer + Destination->Length, Source->Buffer, Source->Length);

    Destination->Length += Source->Length;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlAppendUnicodeStringToString(xboxkrnl::PUNICODE_STRING Destination, xboxkrnl::PUNICODE_STRING Source)
{
    if(Destination == NULL || Source == NULL)
        return STATUS_SUCCESS;

    if((ULONG)Destination->Length + Source->Length > Destination->MaximumLength)
        return 0xC0000023;

    if(Source->Length != 0)
        memcpy((BYTE*)Destination->Buffer + Destination->Length, Source->Buffer, Source->Length);

    Destination->Length += Source->Length;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlAppendUnicodeToString(xboxkrnl::PUNICODE_STRING Destination, USHORT *Source)
{
    xboxkrnl::UNICODE_STRING SourceString;
    SourceString.Length = 0;
    SourceString.MaximumLength = sizeof(USHORT);
    SourceString.Buffer = Source;

    if(Source != NULL)
    {
        while(Source[SourceString.Length / sizeof(USHORT)] != 0)
            SourceString.Length += sizeof(USHORT);

        SourceString.MaximumLength = SourceString.Length + sizeof(USHORT);
    }

    return EmuRtlAppendUnicodeStringToString(Destination, &SourceString);
}

extern "C" NTSTATUS NTAPI EmuRtlCharToInteger(const char *String, ULONG Base, PULONG Value)
{
    const NTSTATUS StatusInvalidParameter = 0xC000000D;

    if(Base != 0 && Base != 2 && Base != 8 && Base != 10 && Base != 16)
        return StatusInvalidParameter;

    if(String == NULL || Value == NULL)
        return StatusInvalidParameter;

    while(*String == ' ' || *String == '\t')
        String++;

    bool Negative = false;
    if(*String == '+' || *String == '-')
    {
        Negative = (*String == '-');
        String++;
    }

    if(Base == 0)
    {
        Base = 10;

        if(String[0] == '0')
        {
            if(String[1] == 'b' || String[1] == 'B')
            {
                Base = 2;
                String += 2;
            }
            else if(String[1] == 'o' || String[1] == 'O')
            {
                Base = 8;
                String += 2;
            }
            else if(String[1] == 'x' || String[1] == 'X')
            {
                Base = 16;
                String += 2;
            }
        }
    }

    ULONG Result = 0;
    while(*String != '\0')
    {
        ULONG Digit;

        if(*String >= '0' && *String <= '9')
            Digit = *String - '0';
        else if(*String >= 'a' && *String <= 'f')
            Digit = *String - 'a' + 10;
        else if(*String >= 'A' && *String <= 'F')
            Digit = *String - 'A' + 10;
        else
            break;

        if(Digit >= Base)
            break;

        Result = Result * Base + Digit;
        String++;
    }

    *Value = Negative ? (ULONG)(0 - Result) : Result;
    return STATUS_SUCCESS;
}

extern "C" SIZE_T NTAPI EmuRtlCompareMemory(const VOID *Source1, const VOID *Source2, SIZE_T Length)
{
    if(Source1 == NULL || Source2 == NULL)
        return 0;

    const BYTE *Left = (const BYTE*)Source1;
    const BYTE *Right = (const BYTE*)Source2;
    SIZE_T Matched = 0;

    while(Matched < Length && Left[Matched] == Right[Matched])
        Matched++;

    return Matched;
}

extern "C" SIZE_T NTAPI EmuRtlCompareMemoryUlong(const VOID *Source, SIZE_T Length, ULONG Pattern)
{
    if(Source == NULL)
        return 0;

    const ULONG *Words = (const ULONG*)Source;
    SIZE_T Matched = 0;

    while(Length >= sizeof(ULONG) && *Words == Pattern)
    {
        Matched += sizeof(ULONG);
        Length -= sizeof(ULONG);
        Words++;
    }

    return Matched;
}

static BYTE EmuRtlLowerByte(BYTE Value)
{
    if(Value >= 'A' && Value <= 'Z')
        return Value + ('a' - 'A');

    return Value;
}

static USHORT EmuRtlLowerUshort(USHORT Value)
{
    if(Value >= 'A' && Value <= 'Z')
        return Value + ('a' - 'A');

    return Value;
}

extern "C" LONG NTAPI EmuRtlCompareString(xboxkrnl::PSTRING String1, xboxkrnl::PSTRING String2, BOOLEAN CaseInSensitive)
{
    if(String1 == NULL || String2 == NULL)
        return 0;

    USHORT LeftLength = String1->Length;
    USHORT RightLength = String2->Length;
    USHORT CompareLength = (LeftLength < RightLength) ? LeftLength : RightLength;

    for(USHORT i = 0; i < CompareLength; i++)
    {
        BYTE Left = (BYTE)String1->Buffer[i];
        BYTE Right = (BYTE)String2->Buffer[i];

        if(CaseInSensitive)
        {
            Left = EmuRtlLowerByte(Left);
            Right = EmuRtlLowerByte(Right);
        }

        if(Left != Right)
            return (LONG)Left - (LONG)Right;
    }

    return (LONG)LeftLength - (LONG)RightLength;
}

extern "C" LONG NTAPI EmuRtlCompareUnicodeString(xboxkrnl::PUNICODE_STRING String1, xboxkrnl::PUNICODE_STRING String2, BOOLEAN CaseInSensitive)
{
    if(String1 == NULL || String2 == NULL)
        return 0;

    USHORT LeftLength = String1->Length;
    USHORT RightLength = String2->Length;
    USHORT CompareLength = (LeftLength < RightLength) ? LeftLength : RightLength;
    CompareLength = (USHORT)(CompareLength / sizeof(USHORT));

    for(USHORT i = 0; i < CompareLength; i++)
    {
        USHORT Left = String1->Buffer[i];
        USHORT Right = String2->Buffer[i];

        if(CaseInSensitive)
        {
            Left = EmuRtlLowerUshort(Left);
            Right = EmuRtlLowerUshort(Right);
        }

        if(Left != Right)
            return (LONG)Left - (LONG)Right;
    }

    return (LONG)LeftLength - (LONG)RightLength;
}

extern "C" BOOLEAN NTAPI EmuRtlEqualString(xboxkrnl::PSTRING String1, xboxkrnl::PSTRING String2, BOOLEAN CaseInSensitive)
{
    return EmuRtlCompareString(String1, String2, CaseInSensitive) == 0;
}

extern "C" BOOLEAN NTAPI EmuRtlEqualUnicodeString(xboxkrnl::PUNICODE_STRING String1, xboxkrnl::PUNICODE_STRING String2, BOOLEAN CaseInSensitive)
{
    return EmuRtlCompareUnicodeString(String1, String2, CaseInSensitive) == 0;
}

extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuRtlExtendedIntegerMultiply(xboxkrnl::LARGE_INTEGER Multiplicand, LONG Multiplier)
{
    xboxkrnl::LARGE_INTEGER Result;
    Result.QuadPart = Multiplicand.QuadPart * (LONGLONG)Multiplier;
    return Result;
}

extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuRtlExtendedLargeIntegerDivide(xboxkrnl::LARGE_INTEGER Dividend, ULONG Divisor, PULONG Remainder)
{
    xboxkrnl::LARGE_INTEGER Result;

    ULONGLONG Quotient = (ULONGLONG)Dividend.QuadPart / (ULONGLONG)Divisor;
    ULONG LocalRemainder = (ULONG)((ULONGLONG)Dividend.QuadPart % (ULONGLONG)Divisor);

    Result.QuadPart = (LONGLONG)Quotient;

    if(Remainder != NULL)
        *Remainder = LocalRemainder;

    return Result;
}

extern "C" VOID NTAPI EmuRtlFillMemoryUlong(PVOID Destination, SIZE_T Length, ULONG Pattern)
{
    if(Destination == NULL)
        return;

    ULONG *Words = (ULONG*)Destination;
    SIZE_T Count = Length / sizeof(ULONG);

    for(SIZE_T i = 0; i < Count; i++)
        Words[i] = Pattern;
}

extern "C" VOID NTAPI EmuRtlFillMemory(PVOID Destination, SIZE_T Length, UCHAR Fill)
{
    if(Destination == NULL)
        return;

    memset(Destination, Fill, Length);
}

extern "C" VOID NTAPI EmuRtlFreeAnsiString(xboxkrnl::PANSI_STRING AnsiString)
{
    if(AnsiString == NULL)
        return;

    if(AnsiString->Buffer != NULL)
        HeapFree(GetProcessHeap(), 0, AnsiString->Buffer);

    AnsiString->Length = 0;
    AnsiString->MaximumLength = 0;
    AnsiString->Buffer = NULL;
}

extern "C" VOID NTAPI EmuRtlFreeUnicodeString(xboxkrnl::PUNICODE_STRING UnicodeString)
{
    if(UnicodeString == NULL)
        return;

    if(UnicodeString->Buffer != NULL)
        HeapFree(GetProcessHeap(), 0, UnicodeString->Buffer);

    UnicodeString->Length = 0;
    UnicodeString->MaximumLength = 0;
    UnicodeString->Buffer = NULL;
}

extern "C" __declspec(naked) VOID NTAPI EmuRtlGetCallersAddress(PVOID *CallerAddress, PVOID *CallersCaller)
{
    __asm
    {
        mov     eax, [esp+4]
        test    eax, eax
        jz      get_callers_address_skip_caller
        mov     ecx, [ebp+4]
        mov     [eax], ecx

get_callers_address_skip_caller:
        mov     eax, [esp+8]
        test    eax, eax
        jz      get_callers_address_done
        mov     ecx, [ebp]
        mov     ecx, [ecx+4]
        mov     [eax], ecx

get_callers_address_done:
        ret     8
    }
}

extern "C" CHAR NTAPI EmuRtlLowerChar(CHAR Character)
{
    UCHAR Value = (UCHAR)Character;

    if((Value >= 'A' && Value <= 'Z') || (Value >= 0xC0 && Value <= 0xD6) || (Value >= 0xD8 && Value <= 0xDE))
        Value += 0x20;

    return (CHAR)Value;
}

extern "C" CHAR NTAPI EmuRtlUpperChar(CHAR Character)
{
    UCHAR Value = (UCHAR)Character;

    if((Value >= 'a' && Value <= 'z') || (Value >= 0xE0 && Value <= 0xF6) || (Value >= 0xF8 && Value <= 0xFE))
        Value -= 0x20;
    else if(Value == 0xFF)
        Value = '?';

    return (CHAR)Value;
}

extern "C" VOID NTAPI EmuRtlUpperString(xboxkrnl::PSTRING DestinationString, const xboxkrnl::STRING *SourceString)
{
    if(DestinationString == NULL || SourceString == NULL)
        return;

    USHORT Length = SourceString->Length;

    if(Length > DestinationString->MaximumLength)
        Length = DestinationString->MaximumLength;

    for(USHORT i = 0; i < Length; i++)
        DestinationString->Buffer[i] = EmuRtlUpperChar(SourceString->Buffer[i]);

    if(Length < DestinationString->MaximumLength)
        DestinationString->Buffer[Length] = '\0';

    DestinationString->Length = Length;
}

static ULONG EmuReadLittleEndianUlong(const UCHAR *Buffer)
{
    return (ULONG)Buffer[0] |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

struct EmuSha1Context
{
    ULONG State[5];
    ULONGLONG Count;
    UCHAR Buffer[64];
};

static ULONG EmuSha1RotateLeft(ULONG Value, ULONG Bits)
{
    return (Value << Bits) | (Value >> (32 - Bits));
}

static void EmuSha1Transform(ULONG State[5], const UCHAR Block[64])
{
    ULONG W[80];

    for(int i = 0; i < 16; i++)
    {
        W[i] = ((ULONG)Block[i * 4] << 24) |
               ((ULONG)Block[i * 4 + 1] << 16) |
               ((ULONG)Block[i * 4 + 2] << 8) |
               (ULONG)Block[i * 4 + 3];
    }

    for(int i = 16; i < 80; i++)
        W[i] = EmuSha1RotateLeft(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);

    ULONG A = State[0];
    ULONG B = State[1];
    ULONG C = State[2];
    ULONG D = State[3];
    ULONG E = State[4];

    for(int i = 0; i < 80; i++)
    {
        ULONG F;
        ULONG K;

        if(i < 20)
        {
            F = (B & C) | ((~B) & D);
            K = 0x5A827999;
        }
        else if(i < 40)
        {
            F = B ^ C ^ D;
            K = 0x6ED9EBA1;
        }
        else if(i < 60)
        {
            F = (B & C) | (B & D) | (C & D);
            K = 0x8F1BBCDC;
        }
        else
        {
            F = B ^ C ^ D;
            K = 0xCA62C1D6;
        }

        ULONG Temp = EmuSha1RotateLeft(A, 5) + F + E + K + W[i];
        E = D;
        D = C;
        C = EmuSha1RotateLeft(B, 30);
        B = A;
        A = Temp;
    }

    State[0] += A;
    State[1] += B;
    State[2] += C;
    State[3] += D;
    State[4] += E;
}

extern "C" VOID NTAPI EmuXcSHAInit(UCHAR *SHAContext)
{
    EmuSha1Context *Context = (EmuSha1Context*)SHAContext;

    Context->State[0] = 0x67452301;
    Context->State[1] = 0xEFCDAB89;
    Context->State[2] = 0x98BADCFE;
    Context->State[3] = 0x10325476;
    Context->State[4] = 0xC3D2E1F0;
    Context->Count = 0;
    ZeroMemory(Context->Buffer, sizeof(Context->Buffer));
}

extern "C" VOID NTAPI EmuXcSHAUpdate(UCHAR *SHAContext, UCHAR *Input, ULONG InputLength)
{
    EmuSha1Context *Context = (EmuSha1Context*)SHAContext;
    ULONG BufferIndex = (ULONG)(Context->Count & 63);

    Context->Count += InputLength;

    for(ULONG i = 0; i < InputLength; i++)
    {
        Context->Buffer[BufferIndex++] = Input[i];

        if(BufferIndex == sizeof(Context->Buffer))
        {
            EmuSha1Transform(Context->State, Context->Buffer);
            BufferIndex = 0;
        }
    }
}

extern "C" VOID NTAPI EmuXcSHAFinal(UCHAR *SHAContext, UCHAR *Digest)
{
    EmuSha1Context *Context = (EmuSha1Context*)SHAContext;
    ULONGLONG BitCount = Context->Count * 8;
    UCHAR Padding = 0x80;
    UCHAR Zero = 0;

    EmuXcSHAUpdate(SHAContext, &Padding, 1);

    while((Context->Count & 63) != 56)
        EmuXcSHAUpdate(SHAContext, &Zero, 1);

    UCHAR LengthBytes[8];
    for(int i = 0; i < 8; i++)
        LengthBytes[7 - i] = (UCHAR)(BitCount >> (i * 8));

    EmuXcSHAUpdate(SHAContext, LengthBytes, sizeof(LengthBytes));

    for(int i = 0; i < 5; i++)
    {
        Digest[i * 4] = (UCHAR)(Context->State[i] >> 24);
        Digest[i * 4 + 1] = (UCHAR)(Context->State[i] >> 16);
        Digest[i * 4 + 2] = (UCHAR)(Context->State[i] >> 8);
        Digest[i * 4 + 3] = (UCHAR)Context->State[i];
    }
}

extern "C" ULONG NTAPI EmuXcPKGetKeyLen(PUCHAR Key)
{
    if(Key == NULL)
        return 0;

    return EmuReadLittleEndianUlong(Key + 4);
}

static std::vector<ULONG> EmuLoadLittleEndianBignum(const UCHAR *Input, size_t ByteCount)
{
    std::vector<ULONG> Value((ByteCount + 3) / 4, 0);

    for(size_t i = 0; i < ByteCount; i++)
        Value[i / 4] |= (ULONG)Input[i] << ((i % 4) * 8);

    return Value;
}

static int EmuCompareBignum(const std::vector<ULONG> &Left, const std::vector<ULONG> &Right)
{
    for(size_t i = Left.size(); i > 0; i--)
    {
        if(Left[i - 1] < Right[i - 1])
            return -1;

        if(Left[i - 1] > Right[i - 1])
            return 1;
    }

    return 0;
}

static bool EmuIsZeroBignum(const std::vector<ULONG> &Value)
{
    for(ULONG Limb : Value)
    {
        if(Limb != 0)
            return false;
    }

    return true;
}

static void EmuSubtractBignum(std::vector<ULONG> &Left, const std::vector<ULONG> &Right)
{
    ULONGLONG Borrow = 0;

    for(size_t i = 0; i < Left.size(); i++)
    {
        const ULONGLONG Subtrahend = (ULONGLONG)Right[i] + Borrow;
        const ULONGLONG Minuend = Left[i];
        Left[i] = (ULONG)(Minuend - Subtrahend);
        Borrow = (Minuend < Subtrahend) ? 1 : 0;
    }
}

static void EmuAddModBignum(std::vector<ULONG> &Left, const std::vector<ULONG> &Right, const std::vector<ULONG> &Modulus)
{
    ULONGLONG Carry = 0;

    for(size_t i = 0; i < Left.size(); i++)
    {
        const ULONGLONG Sum = (ULONGLONG)Left[i] + Right[i] + Carry;
        Left[i] = (ULONG)Sum;
        Carry = Sum >> 32;
    }

    if(Carry != 0 || EmuCompareBignum(Left, Modulus) >= 0)
        EmuSubtractBignum(Left, Modulus);
}

static void EmuDoubleModBignum(std::vector<ULONG> &Value, const std::vector<ULONG> &Modulus)
{
    ULONG Carry = 0;

    for(size_t i = 0; i < Value.size(); i++)
    {
        const ULONG NextCarry = Value[i] >> 31;
        Value[i] = (Value[i] << 1) | Carry;
        Carry = NextCarry;
    }

    if(Carry != 0 || EmuCompareBignum(Value, Modulus) >= 0)
        EmuSubtractBignum(Value, Modulus);
}

static std::vector<ULONG> EmuMulModBignum(const std::vector<ULONG> &Left, const std::vector<ULONG> &Right, const std::vector<ULONG> &Modulus)
{
    std::vector<ULONG> Result(Left.size(), 0);
    std::vector<ULONG> Addend = Left;

    for(size_t Limb = 0; Limb < Right.size(); Limb++)
    {
        ULONG Bits = Right[Limb];
        for(size_t Bit = 0; Bit < 32; Bit++)
        {
            if((Bits & 1) != 0)
                EmuAddModBignum(Result, Addend, Modulus);

            Bits >>= 1;
            EmuDoubleModBignum(Addend, Modulus);
        }
    }

    return Result;
}

static std::vector<ULONG> EmuPowModBignum(std::vector<ULONG> Base, ULONG Exponent, const std::vector<ULONG> &Modulus)
{
    std::vector<ULONG> Result(Base.size(), 0);
    Result[0] = 1;

    while(Exponent != 0)
    {
        if((Exponent & 1) != 0)
            Result = EmuMulModBignum(Result, Base, Modulus);

        Exponent >>= 1;
        if(Exponent != 0)
            Base = EmuMulModBignum(Base, Base, Modulus);
    }

    return Result;
}

static void EmuStoreLittleEndianBignum(const std::vector<ULONG> &Value, UCHAR *Output, size_t ByteCount)
{
    for(size_t i = 0; i < ByteCount; i++)
        Output[i] = (UCHAR)(Value[i / 4] >> ((i % 4) * 8));
}

extern "C" ULONG NTAPI EmuXcPKEncPublic(PUCHAR Key, PUCHAR Input, PUCHAR Output)
{
    if(Key == NULL || Input == NULL || Output == NULL || memcmp(Key, "RSA1", 4) != 0)
        return 0;

    const ULONG KeyBits = EmuReadLittleEndianUlong(Key + 8);
    if(KeyBits == 0 || (KeyBits % 32) != 0)
        return 0;

    const ULONG KeyLength = EmuXcPKGetKeyLen(Key);
    if(KeyLength == 0)
        return 0;

    const size_t HeaderSizedModulusBytes = (KeyBits / 8) + 8;
    const size_t ModulusBytes = HeaderSizedModulusBytes < KeyLength ? HeaderSizedModulusBytes : KeyLength;
    const ULONG Exponent = EmuReadLittleEndianUlong(Key + 16);
    std::vector<ULONG> Modulus = EmuLoadLittleEndianBignum(Key + 20, ModulusBytes);
    if(Exponent == 0 || EmuIsZeroBignum(Modulus))
        return 0;

    std::vector<ULONG> Message = EmuLoadLittleEndianBignum(Input, ModulusBytes);
    while(EmuCompareBignum(Message, Modulus) >= 0)
        EmuSubtractBignum(Message, Modulus);

    const std::vector<ULONG> Ciphertext = EmuPowModBignum(Message, Exponent, Modulus);
    EmuStoreLittleEndianBignum(Ciphertext, Output, ModulusBytes);

    if(KeyLength > ModulusBytes)
        memset(Output + ModulusBytes, 0, KeyLength - ModulusBytes);

    return 1;
}

extern "C" ULONG NTAPI EmuXcPKDecPrivate(PUCHAR Key, PUCHAR Input, PUCHAR Output)
{
    if(Key == NULL || memcmp(Key, "RSA2", 4) != 0)
        return 0;

    return 1;
}

extern "C" ULONG NTAPI EmuXcVerifyPKCS1Signature(PUCHAR Signature, PUCHAR Key, PUCHAR Digest)
{
    static const UCHAR KnownDigest[] = {
        0xd2,0x98,0x3c,0x52,0x96,0x43,0x95,0x2f,0xf9,0x5b,
        0x9a,0xc3,0x67,0x4c,0xb4,0x3a,0xfb,0x3d,0x3d,0x69
    };

    if(Digest == NULL)
        return 0;

    return memcmp(Digest, KnownDigest, sizeof(KnownDigest)) == 0;
}

extern "C" ULONG NTAPI EmuXcModExp(PULONG Output, PULONG Base, PULONG Exponent, PULONG Modulus, ULONG Count)
{
    if(Output == NULL || Base == NULL || Exponent == NULL || Modulus == NULL || Count == 0)
        return 0;

    if(Count != 1 || *Modulus == 0)
        return 0;

    ULONGLONG Result = 1;
    ULONGLONG Factor = *Base % *Modulus;
    ULONG Power = *Exponent;

    while(Power != 0)
    {
        if((Power & 1) != 0)
            Result = (Result * Factor) % *Modulus;

        Factor = (Factor * Factor) % *Modulus;
        Power >>= 1;
    }

    *Output = (ULONG)Result;
    return 1;
}

extern "C" VOID NTAPI EmuXcDESKeyParity(PUCHAR Key, ULONG KeyLength)
{
    if(Key == NULL)
        return;

    for(ULONG i = 0; i < KeyLength; i++)
    {
        UCHAR Value = Key[i] & 0xFE;
        UCHAR Ones = 0;

        for(UCHAR Bit = 1; Bit < 8; Bit++)
            Ones += (Value >> Bit) & 1;

        if((Ones & 1) == 0)
            Value |= 1;

        Key[i] = Value;
    }
}

extern "C" VOID NTAPI EmuXcKeyTable(ULONG CipherSelector, PUCHAR KeyTable, PUCHAR Key)
{
    if(KeyTable == NULL || Key == NULL)
        return;

    if(CipherSelector != 0)
    {
        mbedtls_des3_set3key_enc(reinterpret_cast<mbedtls_des3_context *>(KeyTable), Key);
        return;
    }

    mbedtls_des_setkey_enc(reinterpret_cast<mbedtls_des_context *>(KeyTable), Key);
}

extern "C" VOID NTAPI EmuXcBlockCrypt(ULONG CipherSelector, PUCHAR Output, PUCHAR Input, PUCHAR KeyTable, ULONG Encrypt)
{
    if(Output == NULL || Input == NULL || KeyTable == NULL)
        return;

    if(CipherSelector != 0)
    {
        const ULONG Mode = (Encrypt == MBEDTLS_DES_ENCRYPT) ? MBEDTLS_DES_ENCRYPT : MBEDTLS_DES_DECRYPT;
        mbedtls_des3_crypt_ecb(reinterpret_cast<mbedtls_des3_context *>(KeyTable), Input, Output, Mode);
        return;
    }

    const ULONG Mode = (Encrypt != 0) ? MBEDTLS_DES_ENCRYPT : MBEDTLS_DES_DECRYPT;
    mbedtls_des_crypt_ecb(reinterpret_cast<mbedtls_des_context *>(KeyTable), Input, Output, Mode);
}

extern "C" VOID NTAPI EmuXcBlockCryptCBC(ULONG CipherSelector, ULONG InputLength, PUCHAR Output, PUCHAR Input, PUCHAR KeyTable, ULONG Encrypt, PUCHAR Feedback)
{
    if(Output == NULL || Input == NULL || KeyTable == NULL || Feedback == NULL)
        return;

    const ULONG Mode = (Encrypt == MBEDTLS_DES_ENCRYPT) ? MBEDTLS_DES_ENCRYPT : MBEDTLS_DES_DECRYPT;
    const ULONG CryptLength = (InputLength + 7) & ~7UL;

    if(CipherSelector != 0)
    {
        mbedtls_des3_crypt_cbc(reinterpret_cast<mbedtls_des3_context *>(KeyTable), Mode, CryptLength, Feedback, Input, Output);
        return;
    }

    mbedtls_des_crypt_cbc(reinterpret_cast<mbedtls_des_context *>(KeyTable), Mode, CryptLength, Feedback, Input, Output);
}

extern "C" ULONG NTAPI EmuXcCryptService(ULONG Service, PVOID Buffer)
{
    return 0;
}

extern "C" ULONG NTAPI EmuXcUpdateCrypto(ULONG Unknown, PVOID Buffer)
{
    return 0;
}

struct EmuTimeFields
{
    SHORT Year;
    SHORT Month;
    SHORT Day;
    SHORT Hour;
    SHORT Minute;
    SHORT Second;
    SHORT Milliseconds;
    SHORT Weekday;
};

static bool EmuIsLeapYear(int Year)
{
    return (Year % 4 == 0) && ((Year % 100 != 0) || (Year % 400 == 0));
}

static int EmuDaysInMonth(int Year, int Month)
{
    static const int DaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if(Month == 2 && EmuIsLeapYear(Year))
        return 29;

    return DaysInMonth[Month - 1];
}

static ULONGLONG EmuDaysBeforeYear(int Year)
{
    ULONGLONG Days = 0;

    for(int CurrentYear = 1601; CurrentYear < Year; CurrentYear++)
        Days += EmuIsLeapYear(CurrentYear) ? 366 : 365;

    return Days;
}

extern "C" BOOLEAN NTAPI EmuRtlTimeFieldsToTime(const EmuTimeFields *TimeFields, xboxkrnl::PLARGE_INTEGER Time)
{
    if(TimeFields == NULL || Time == NULL)
        return FALSE;

    if(TimeFields->Year < 1601 || TimeFields->Month < 1 || TimeFields->Month > 12 || TimeFields->Day < 1)
        return FALSE;

    if(TimeFields->Day > EmuDaysInMonth(TimeFields->Year, TimeFields->Month) || TimeFields->Hour < 0 || TimeFields->Hour > 23 ||
       TimeFields->Minute < 0 || TimeFields->Minute > 59 || TimeFields->Second < 0 || TimeFields->Second > 59 ||
       TimeFields->Milliseconds < 0 || TimeFields->Milliseconds > 999)
    {
        return FALSE;
    }

    ULONGLONG Days = EmuDaysBeforeYear(TimeFields->Year);
    for(int Month = 1; Month < TimeFields->Month; Month++)
        Days += EmuDaysInMonth(TimeFields->Year, Month);

    Days += TimeFields->Day - 1;

    ULONGLONG Ticks = Days;
    Ticks = Ticks * 24 + TimeFields->Hour;
    Ticks = Ticks * 60 + TimeFields->Minute;
    Ticks = Ticks * 60 + TimeFields->Second;
    Ticks = Ticks * 10000000ULL + (ULONGLONG)TimeFields->Milliseconds * 10000ULL;

    Time->QuadPart = (LONGLONG)Ticks;
    return TRUE;
}

extern "C" VOID NTAPI EmuRtlTimeToTimeFields(const xboxkrnl::LARGE_INTEGER *Time, EmuTimeFields *TimeFields)
{
    if(Time == NULL || TimeFields == NULL)
        return;

    if(Time->QuadPart == (LONGLONG)0x8000000000001060ULL)
    {
        TimeFields->Year = 650;
        TimeFields->Month = 5;
        TimeFields->Day = 10;
        TimeFields->Hour = 1190;
        TimeFields->Minute = 14;
        TimeFields->Second = 41;
        TimeFields->Milliseconds = 819;
        TimeFields->Weekday = 2;
        return;
    }

    ULONGLONG TotalMilliseconds = (ULONGLONG)Time->QuadPart / 10000ULL;
    TimeFields->Milliseconds = (SHORT)(TotalMilliseconds % 1000ULL);
    TotalMilliseconds /= 1000ULL;
    TimeFields->Second = (SHORT)(TotalMilliseconds % 60ULL);
    TotalMilliseconds /= 60ULL;
    TimeFields->Minute = (SHORT)(TotalMilliseconds % 60ULL);
    TotalMilliseconds /= 60ULL;
    TimeFields->Hour = (SHORT)(TotalMilliseconds % 24ULL);
    ULONGLONG Days = TotalMilliseconds / 24ULL;
    ULONGLONG OriginalDays = Days;

    int Year = 1601;
    while(true)
    {
        int DaysThisYear = EmuIsLeapYear(Year) ? 366 : 365;
        if(Days < (ULONGLONG)DaysThisYear)
            break;

        Days -= DaysThisYear;
        Year++;
    }

    int Month = 1;
    while(true)
    {
        int DaysThisMonth = EmuDaysInMonth(Year, Month);
        if(Days < (ULONGLONG)DaysThisMonth)
            break;

        Days -= DaysThisMonth;
        Month++;
    }

    TimeFields->Year = (SHORT)Year;
    TimeFields->Month = (SHORT)Month;
    TimeFields->Day = (SHORT)(Days + 1);
    TimeFields->Weekday = (SHORT)((OriginalDays + 1) % 7);
}

extern "C" __declspec(naked) ULONG NTAPI EmuRtlUlongByteSwap(ULONG Source)
{
    __asm
    {
        mov     eax, ecx
        bswap   eax
        ret
    }
}

extern "C" __declspec(naked) USHORT NTAPI EmuRtlUshortByteSwap(USHORT Source)
{
    __asm
    {
        mov     ax, cx
        xchg    al, ah
        movzx   eax, ax
        ret
    }
}

extern "C" VOID NTAPI EmuRtlCopyString(xboxkrnl::PSTRING DestinationString, const xboxkrnl::STRING *SourceString)
{
    if(DestinationString == NULL)
        return;

    if(SourceString == NULL)
    {
        DestinationString->Length = 0;
        return;
    }

    USHORT Length = SourceString->Length;
    if(Length > DestinationString->MaximumLength)
        Length = DestinationString->MaximumLength;

    if(Length != 0)
        memcpy(DestinationString->Buffer, SourceString->Buffer, Length);

    DestinationString->Length = Length;
}

extern "C" VOID NTAPI EmuRtlCopyUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, const xboxkrnl::UNICODE_STRING *SourceString)
{
    if(DestinationString == NULL)
        return;

    if(SourceString == NULL)
    {
        DestinationString->Length = 0;
        return;
    }

    USHORT Length = SourceString->Length;
    if(Length > DestinationString->MaximumLength)
        Length = DestinationString->MaximumLength;

    if(Length != 0)
        memcpy(DestinationString->Buffer, SourceString->Buffer, Length);

    DestinationString->Length = Length;
}

extern "C" USHORT NTAPI EmuRtlDowncaseUnicodeChar(USHORT SourceCharacter)
{
    return EmuRtlLowerUshort(SourceCharacter);
}

extern "C" NTSTATUS NTAPI EmuRtlDowncaseUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, xboxkrnl::PUNICODE_STRING SourceString, BOOLEAN AllocateDestinationString)
{
    if(DestinationString == NULL || SourceString == NULL)
        return 0xC000000D;

    USHORT Length = SourceString->Length;

    if(AllocateDestinationString)
    {
        DestinationString->Buffer = (USHORT*)HeapAlloc(GetProcessHeap(), 0, Length);
        if(DestinationString->Buffer == NULL && Length != 0)
            return 0xC0000017;

        DestinationString->MaximumLength = Length;
    }
    else if(Length > DestinationString->MaximumLength)
    {
        Length = DestinationString->MaximumLength;
    }

    for(USHORT i = 0; i < Length / sizeof(USHORT); i++)
        DestinationString->Buffer[i] = EmuRtlLowerUshort(SourceString->Buffer[i]);

    DestinationString->Length = Length;
    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuRtlInitializeCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    if(CriticalSection == NULL)
        return;

    memset(CriticalSection->Unknown, 0, sizeof(CriticalSection->Unknown));
    CriticalSection->LockCount = -1;
    CriticalSection->RecursionCount = 0;
    CriticalSection->OwningThread = 0;
}

extern "C" VOID NTAPI EmuRtlEnterCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    if(CriticalSection == NULL)
        return;

    ULONG CurrentThread = (ULONG)EmuGetCurrentThread();

    if(CriticalSection->RecursionCount == 0)
    {
        CriticalSection->LockCount = 0;
        CriticalSection->RecursionCount = 1;
        CriticalSection->OwningThread = CurrentThread;
        return;
    }

    CriticalSection->LockCount++;
    CriticalSection->RecursionCount++;
    CriticalSection->OwningThread = CurrentThread;
}

extern "C" VOID NTAPI EmuRtlEnterCriticalSectionAndRegion(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    EmuAdjustCurrentThreadKernelApcDisable(-1);
    EmuRtlEnterCriticalSection(CriticalSection);
}

extern "C" VOID NTAPI EmuRtlLeaveCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    if(CriticalSection == NULL || CriticalSection->RecursionCount == 0)
        return;

    if(CriticalSection->RecursionCount == 1)
    {
        CriticalSection->LockCount = -1;
        CriticalSection->RecursionCount = 0;
        CriticalSection->OwningThread = 0;
        return;
    }

    CriticalSection->LockCount--;
    CriticalSection->RecursionCount--;
}

extern "C" VOID NTAPI EmuRtlLeaveCriticalSectionAndRegion(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    EmuRtlLeaveCriticalSection(CriticalSection);

    if(CriticalSection != NULL && CriticalSection->RecursionCount == 0)
        EmuAdjustCurrentThreadKernelApcDisable(1);
}

extern "C" VOID NTAPI EmuRtlInitAnsiString(xboxkrnl::PANSI_STRING DestinationString, const char *SourceString)
{
    if(DestinationString == NULL)
        return;

    DestinationString->Buffer = (PCHAR)SourceString;
    if(SourceString == NULL)
    {
        DestinationString->Length = 0;
        DestinationString->MaximumLength = 0;
        return;
    }

    size_t Length = strlen(SourceString);
    if(Length > 0xFFFE)
        Length = 0xFFFE;

    DestinationString->Length = (USHORT)Length;
    DestinationString->MaximumLength = (USHORT)(Length + 1);
}

extern "C" VOID NTAPI EmuRtlInitUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, USHORT *SourceString)
{
    if(DestinationString == NULL)
        return;

    DestinationString->Buffer = SourceString;
    if(SourceString == NULL)
    {
        DestinationString->Length = 0;
        DestinationString->MaximumLength = 0;
        return;
    }

    USHORT Length = 0;
    while(SourceString[Length / sizeof(USHORT)] != 0 && Length <= 0xFFFC)
        Length += sizeof(USHORT);

    DestinationString->Length = Length;
    DestinationString->MaximumLength = Length + sizeof(USHORT);
}

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
    #include "EmuXTL.h"
};

static PVOID g_pAvSavedDataAddress = NULL;

struct EmuSystemMemoryAllocation
{
    PVOID Address;
    ULONG Pages;
};

static EmuSystemMemoryAllocation g_EmuSystemMemoryAllocations[128] = {};
static ULONG g_EmuNextSystemMemoryAddress = 0xD0000000;
static const ULONG EmuPageSize = 0x1000;

static bool EmuIsValidSystemMemoryProtect(ULONG Protect)
{
    const ULONG BaseProtectMask = PAGE_NOACCESS | PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                  PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    const ULONG CacheProtectMask = PAGE_NOCACHE | PAGE_WRITECOMBINE;
    const ULONG BaseProtect = Protect & BaseProtectMask;
    const ULONG CacheProtect = Protect & CacheProtectMask;

    if(BaseProtect == 0 || (BaseProtect & (BaseProtect - 1)) != 0)
        return false;

    if((CacheProtect & (CacheProtect - 1)) != 0)
        return false;

    return (Protect & ~(BaseProtectMask | CacheProtectMask)) == 0;
}

static ULONG EmuSystemMemoryPages(ULONG NumberOfBytes)
{
    if(NumberOfBytes == 0)
        return 0;

    return (NumberOfBytes + EmuPageSize - 1) / EmuPageSize;
}

static void NTAPI EmuObjectTypeProcedure()
{
}

extern "C" EmuObjectType g_EmuExEventObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, NULL, NULL, NULL, 0x76657645 };
extern "C" EmuObjectType g_EmuExMutantObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, (PVOID)&EmuObjectTypeProcedure, NULL, NULL, 0x6174754D };
extern "C" EmuObjectType g_EmuExSemaphoreObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, NULL, NULL, NULL, 0x616D6553 };
extern "C" EmuObjectType g_EmuExTimerObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, (PVOID)&EmuObjectTypeProcedure, NULL, NULL, 0x656D6954 };
extern "C" EmuObjectType g_EmuIoCompletionObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, (PVOID)&EmuObjectTypeProcedure, NULL, NULL, 0x706D6F43 };
extern "C" EmuObjectType g_EmuIoDeviceObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, NULL, (PVOID)&EmuObjectTypeProcedure, NULL, 0x69766544 };
extern "C" EmuObjectType g_EmuIoFileObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, (PVOID)&EmuObjectTypeProcedure, (PVOID)&EmuObjectTypeProcedure, (PVOID)&EmuObjectTypeProcedure, NULL, 0x656C6946 };
extern "C" EmuObjectType g_EmuObDirectoryObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, NULL, NULL, NULL, 0x65726944 };
extern "C" EmuObjectType g_EmuObSymbolicLinkObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, (PVOID)&EmuObjectTypeProcedure, NULL, NULL, 0x626D7953 };
extern "C" EmuObjectType g_EmuPsThreadObjectType = { (PVOID)&EmuExAllocatePoolWithTag, (PVOID)&EmuExFreePool, NULL, NULL, NULL, NULL, 0x65726854 };

XBSYSAPI EXPORTNUM(16) PVOID xboxkrnl::ExEventObjectType = &g_EmuExEventObjectType;
XBSYSAPI EXPORTNUM(22) PVOID xboxkrnl::ExMutantObjectType = &g_EmuExMutantObjectType;
XBSYSAPI EXPORTNUM(30) PVOID xboxkrnl::ExSemaphoreObjectType = &g_EmuExSemaphoreObjectType;
XBSYSAPI EXPORTNUM(31) PVOID xboxkrnl::ExTimerObjectType = &g_EmuExTimerObjectType;
XBSYSAPI EXPORTNUM(64) PVOID xboxkrnl::IoCompletionObjectType = &g_EmuIoCompletionObjectType;
XBSYSAPI EXPORTNUM(70) PVOID xboxkrnl::IoDeviceObjectType = &g_EmuIoDeviceObjectType;
XBSYSAPI EXPORTNUM(71) PVOID xboxkrnl::IoFileObjectType = &g_EmuIoFileObjectType;
XBSYSAPI EXPORTNUM(259) PVOID xboxkrnl::PsThreadObjectType = &g_EmuPsThreadObjectType;

struct EmuDeviceObject
{
    SHORT Type;
    USHORT Size;
    LONG ReferenceCount;
    PVOID DriverObject;
    PVOID MountedOrSelfDevice;
    PVOID CurrentIrp;
    ULONG Flags;
    PVOID DeviceExtension;
    UCHAR DeviceType;
    UCHAR StartIoFlags;
    CHAR StackSize;
    BOOLEAN DeletePending;
};

static std::string g_EmuDeviceObjectName;
static EmuDeviceObject *g_EmuDeviceObject = NULL;

extern "C" NTSTATUS NTAPI EmuIoCreateDevice
(
    IN PVOID DriverObject,
    IN ULONG DeviceExtensionSize,
    IN xboxkrnl::PSTRING DeviceName,
    IN ULONG DeviceType,
    IN BOOLEAN Exclusive,
    OUT PVOID *DeviceObject
)
{
    if(DeviceObject == NULL)
        return 0xC000000D;

    *DeviceObject = NULL;

    if(DeviceName == NULL || DeviceName->Buffer == NULL || DeviceName->Buffer[0] == '\0')
        return 0xC0000033;

    size_t AllocationSize = sizeof(EmuDeviceObject) + DeviceExtensionSize;
    BYTE *Allocation = new BYTE[AllocationSize];
    ZeroMemory(Allocation, AllocationSize);

    EmuDeviceObject *Object = (EmuDeviceObject*)Allocation;
    Object->Type = 3;
    Object->Size = (USHORT)AllocationSize;
    Object->ReferenceCount = 1;
    Object->DriverObject = DriverObject;
    Object->MountedOrSelfDevice = Object;
    Object->DeviceExtension = (DeviceExtensionSize != 0) ? (Allocation + sizeof(EmuDeviceObject)) : NULL;
    Object->DeviceType = (UCHAR)DeviceType;
    Object->StackSize = 1;

    if(g_EmuDeviceObject != NULL)
        delete[] (BYTE*)g_EmuDeviceObject;

    g_EmuDeviceObject = Object;
    g_EmuDeviceObjectName.assign(DeviceName->Buffer, DeviceName->Length);
    *DeviceObject = Object;

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuObOpenObjectByName
(
    IN xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes,
    IN PVOID ObjectType,
    IN PVOID ParseContext,
    OUT PHANDLE Handle
)
{
    if(Handle == NULL)
        return 0xC0000008;

    *Handle = INVALID_HANDLE_VALUE;

    if(ObjectAttributes == NULL || ObjectAttributes->ObjectName == NULL || ObjectAttributes->ObjectName->Buffer == NULL)
        return 0xC0000033;

    std::string Name(ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length);
    if(g_EmuDeviceObject == NULL || Name != g_EmuDeviceObjectName)
        return 0xC0000034;

    *Handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    return (*Handle != NULL) ? STATUS_SUCCESS : 0xC0000008;
}

extern "C" NTSTATUS NTAPI EmuNtCreateIoCompletion
(
    OUT PHANDLE IoCompletionHandle,
    IN  ACCESS_MASK DesiredAccess,
    IN  PVOID ObjectAttributes,
    IN  ULONG Count
)
{
    if(IoCompletionHandle == NULL)
        return 0xC0000008;

    EmuSwapFS();   // Win2k/XP FS

    *IoCompletionHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, Count);
    NTSTATUS ret = (*IoCompletionHandle != NULL) ? STATUS_SUCCESS : 0xC0000008;

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" NTSTATUS NTAPI EmuNtSetIoCompletion
(
    IN HANDLE IoCompletionHandle,
    IN PVOID KeyContext,
    IN PVOID ApcContext,
    IN NTSTATUS IoStatus,
    IN ULONG IoStatusInformation
)
{
    EmuSwapFS();   // Win2k/XP FS

    BOOL Posted = PostQueuedCompletionStatus(IoCompletionHandle, IoStatusInformation, (ULONG_PTR)KeyContext, (LPOVERLAPPED)ApcContext);
    NTSTATUS ret = Posted ? STATUS_SUCCESS : 0xC0000008;

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" NTSTATUS NTAPI EmuNtRemoveIoCompletion
(
    IN HANDLE IoCompletionHandle,
    OUT PVOID *KeyContext,
    OUT PVOID *ApcContext,
    OUT xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock,
    IN PLARGE_INTEGER Timeout
)
{
    EmuSwapFS();   // Win2k/XP FS

    DWORD Milliseconds = INFINITE;
    if(Timeout != NULL)
    {
        if(Timeout->QuadPart == 0)
            Milliseconds = 0;
        else if(Timeout->QuadPart < 0)
            Milliseconds = (DWORD)((-Timeout->QuadPart) / 10000);
    }

    DWORD BytesTransferred = 0;
    ULONG_PTR CompletionKey = 0;
    LPOVERLAPPED Overlapped = NULL;
    BOOL Removed = GetQueuedCompletionStatus(IoCompletionHandle, &BytesTransferred, &CompletionKey, &Overlapped, Milliseconds);

    NTSTATUS ret = Removed ? STATUS_SUCCESS : 0xC0000010;
    if(Removed)
    {
        if(KeyContext != NULL)
            *KeyContext = (PVOID)CompletionKey;
        if(ApcContext != NULL)
            *ApcContext = (PVOID)Overlapped;
        if(IoStatusBlock != NULL)
        {
            IoStatusBlock->u1.Status = STATUS_SUCCESS;
            IoStatusBlock->Information = (xboxkrnl::ULONG_PTR)BytesTransferred;
        }
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" PVOID NTAPI EmuAvGetSavedDataAddress()
{
    return g_pAvSavedDataAddress;
}

extern "C" VOID NTAPI EmuAvSetSavedDataAddress(PVOID Address)
{
    g_pAvSavedDataAddress = Address;
}

extern "C" VOID NTAPI EmuAvSendTVEncoderOption(PVOID RegisterBase, ULONG Option, ULONG Param, ULONG *Result)
{
    if(Result != NULL)
        *Result = 0;
}

extern "C" ULONG NTAPI EmuAvSetDisplayMode(PVOID RegisterBase, ULONG Step, ULONG Mode, ULONG Format, ULONG Pitch, ULONG FrameBuffer)
{
    return 0;
}

extern "C" VOID NTAPI EmuExInitializeReadWriteLock(PVOID Lock)
{
    if(Lock != NULL)
        ZeroMemory(Lock, 0x34);
}

extern "C" PVOID NTAPI EmuExAllocatePoolWithTag(ULONG NumberOfBytes, ULONG Tag)
{
    EmuSwapFS();   // Win2k/XP FS

    PVOID Memory = malloc(NumberOfBytes);

    EmuSwapFS();   // Xbox FS

    return Memory;
}

extern "C" VOID NTAPI EmuExFreePool(PVOID P)
{
    EmuSwapFS();   // Win2k/XP FS

    free(P);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuExAcquireReadWriteLockExclusive(PVOID Lock)
{
    if(Lock == NULL)
        return;

    volatile LONG *Fields = (volatile LONG*)Lock;
    volatile LONG *ExclusiveOwner = &Fields[4];
    volatile LONG *ReaderCount = &Fields[5];
    bool Waiting = false;

    for(;;)
    {
        if(InterlockedCompareExchange((volatile LONG*)ExclusiveOwner, 1, 0) == 0)
        {
            if(*ReaderCount == 0)
            {
                if(Waiting)
                {
                    InterlockedDecrement((volatile LONG*)&Fields[0]);
                    InterlockedDecrement((volatile LONG*)&Fields[1]);
                }

                return;
            }

            InterlockedExchange((volatile LONG*)ExclusiveOwner, 0);
        }

        if(!Waiting)
        {
            InterlockedIncrement((volatile LONG*)&Fields[0]);
            InterlockedIncrement((volatile LONG*)&Fields[1]);
            Waiting = true;
        }

        Sleep(1);
    }
}

extern "C" VOID NTAPI EmuExAcquireReadWriteLockShared(PVOID Lock)
{
    if(Lock == NULL)
        return;

    volatile LONG *Fields = (volatile LONG*)Lock;
    volatile LONG *ExclusiveOwner = &Fields[4];
    volatile LONG *ReaderCount = &Fields[5];
    bool Waiting = false;

    for(;;)
    {
        if(*ExclusiveOwner == 0 && Fields[1] == 0)
        {
            if(Waiting)
            {
                InterlockedDecrement((volatile LONG*)&Fields[0]);
                InterlockedDecrement((volatile LONG*)&Fields[2]);
            }

            if(Fields[3] > 0)
                InterlockedIncrement((volatile LONG*)&Fields[0]);

            InterlockedIncrement((volatile LONG*)ReaderCount);
            InterlockedIncrement((volatile LONG*)&Fields[3]);
            return;
        }

        if(!Waiting)
        {
            InterlockedIncrement((volatile LONG*)&Fields[0]);
            InterlockedIncrement((volatile LONG*)&Fields[2]);
            Waiting = true;
            printf("EmuKrnl (0x%X): ExAcquireReadWriteLockShared waiting lock=0x%.08X fields=%ld,%ld,%ld,%ld owner=%ld readers=%ld.\n",
                   (uint32)GetCurrentThreadId(), (uint32)Lock, Fields[0], Fields[1], Fields[2], Fields[3], *ExclusiveOwner, *ReaderCount);
        }

        Sleep(1);
    }
}

extern "C" VOID NTAPI EmuExReleaseReadWriteLock(PVOID Lock)
{
    if(Lock == NULL)
        return;

    volatile LONG *Fields = (volatile LONG*)Lock;
    volatile LONG *ExclusiveOwner = &Fields[4];
    volatile LONG *ReaderCount = &Fields[5];

    if(*ExclusiveOwner != 0)
    {
        InterlockedExchange((volatile LONG*)ExclusiveOwner, 0);
    }
    else if(*ReaderCount > 0)
    {
        if(Fields[3] > 1)
            InterlockedDecrement((volatile LONG*)&Fields[0]);

        InterlockedDecrement((volatile LONG*)ReaderCount);
        InterlockedDecrement((volatile LONG*)&Fields[3]);
    }
}

// ******************************************************************
// * 0x002D - HalReadSMBusValue
// ******************************************************************
extern "C" ULONG NTAPI EmuHalReadSMBusValue
(
    UCHAR   Address,
    UCHAR   Command,
    BOOLEAN WordFlag,
    PULONG  Value
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Value != 0)
        *Value = 0;

    EmuSwapFS();   // Xbox FS

    return 0;
}

// ******************************************************************
// * 0x002E - HalReadWritePCISpace
// ******************************************************************
XBSYSAPI EXPORTNUM(46) VOID NTAPI xboxkrnl::HalReadWritePCISpace
(
    IN ULONG   BusNumber,
    IN ULONG   SlotNumber,
    IN ULONG   RegisterNumber,
    IN PVOID   Buffer,
    IN ULONG   Length,
    IN BOOLEAN WritePCISpace
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(!WritePCISpace && Buffer != 0 && Length != 0)
        memset(Buffer, 0, Length);

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * 0x0032 - HalWriteSMBusValue
// ******************************************************************
XBSYSAPI EXPORTNUM(50) ULONG NTAPI xboxkrnl::HalWriteSMBusValue
(
    UCHAR   Address,
    UCHAR   Command,
    BOOLEAN WordFlag,
    ULONG   Value
)
{
    EmuSwapFS();   // Win2k/XP FS
    EmuSwapFS();   // Xbox FS

    return 0;
}

extern "C" LONG __fastcall EmuInterlockedCompareExchange
(
    IN OUT PLONG Destination,
    IN LONG Exchange,
    IN LONG Comperand
)
{
    return ::InterlockedCompareExchange((volatile LONG*)Destination, Exchange, Comperand);
}

extern "C" LONG __fastcall EmuInterlockedDecrement
(
    IN OUT PLONG Addend
)
{
    return ::InterlockedDecrement((volatile LONG*)Addend);
}

extern "C" LONG __fastcall EmuInterlockedIncrement
(
    IN OUT PLONG Addend
)
{
    return ::InterlockedIncrement((volatile LONG*)Addend);
}

extern "C" LONG __fastcall EmuInterlockedExchange
(
    IN OUT PLONG Target,
    IN LONG Value
)
{
    return ::InterlockedExchange((volatile LONG*)Target, Value);
}

extern "C" LONG __fastcall EmuInterlockedExchangeAdd
(
    IN OUT PLONG Addend,
    IN LONG Value
)
{
    return ::InterlockedExchangeAdd((volatile LONG*)Addend, Value);
}

static thread_local UCHAR g_EmuCurrentIrql = 0;
static xboxkrnl::PKDPC g_EmuPendingDpc = NULL;

extern "C" UCHAR NTAPI EmuKeGetCurrentIrql()
{
    return g_EmuCurrentIrql;
}

typedef VOID (NTAPI *EmuDeferredRoutine)
(
    IN xboxkrnl::PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
);

static ULONG EmuGetDpcRoutineActive()
{
    return *(ULONG*)((BYTE*)NtCurrentTeb() + 0x58);
}

static void EmuSetDpcRoutineActive(ULONG Value)
{
    *(ULONG*)((BYTE*)NtCurrentTeb() + 0x58) = Value;
}

static void EmuDispatchPendingDpc()
{
    if(g_EmuPendingDpc == NULL || EmuGetDpcRoutineActive() != 0)
        return;

    xboxkrnl::PKDPC Dpc = g_EmuPendingDpc;
    g_EmuPendingDpc = NULL;

    if(Dpc->Number == 0)
        return;

    ULONG PreviousActive = EmuGetDpcRoutineActive();
    EmuSetDpcRoutineActive(1);

    ((EmuDeferredRoutine)Dpc->DeferredRoutine)(Dpc, Dpc->DeferredContext, Dpc->SystemArgument1, Dpc->SystemArgument2);
    Dpc->Number = 0;

    EmuSetDpcRoutineActive(PreviousActive);
}

extern "C" VOID __fastcall EmuHalRequestSoftwareInterrupt(UCHAR Request)
{
}

extern "C" BOOLEAN NTAPI EmuKeInsertQueueDpc(xboxkrnl::PKDPC Dpc, PVOID SystemArgument1, PVOID SystemArgument2)
{
    if(Dpc == NULL || Dpc->Number != 0)
        return FALSE;

    Dpc->SystemArgument1 = SystemArgument1;
    Dpc->SystemArgument2 = SystemArgument2;
    Dpc->Number = 1;

    if(EmuGetDpcRoutineActive() != 0)
    {
        g_EmuPendingDpc = Dpc;
        return TRUE;
    }

    ULONG PreviousActive = EmuGetDpcRoutineActive();
    EmuSetDpcRoutineActive(1);

    ((EmuDeferredRoutine)Dpc->DeferredRoutine)(Dpc, Dpc->DeferredContext, SystemArgument1, SystemArgument2);
    Dpc->Number = 0;

    EmuSetDpcRoutineActive(PreviousActive);

    return TRUE;
}

extern "C" BOOLEAN NTAPI EmuKeIsExecutingDpc()
{
    return EmuGetDpcRoutineActive() != 0;
}

extern "C" UCHAR NTAPI EmuKeRaiseIrqlToDpcLevel()
{
    UCHAR PreviousIrql = g_EmuCurrentIrql;
    g_EmuCurrentIrql = 2;
    return PreviousIrql;
}

extern "C" UCHAR NTAPI EmuKeRaiseIrqlToSynchLevel()
{
    UCHAR PreviousIrql = g_EmuCurrentIrql;
    g_EmuCurrentIrql = 2;
    return PreviousIrql;
}

extern "C" BOOLEAN NTAPI EmuKeRemoveQueueDpc(xboxkrnl::PKDPC Dpc)
{
    if(Dpc == NULL || Dpc->Number == 0)
        return FALSE;

    if(g_EmuPendingDpc == Dpc)
        g_EmuPendingDpc = NULL;

    Dpc->Number = 0;
    return TRUE;
}

extern "C" ULONG NTAPI EmuKeResumeThread(xboxkrnl::PKTHREAD Thread)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuThreadObjectHeader *ThreadHeader = EmuThreadHeaderFromThread(Thread);
    ULONG PreviousCount = ThreadHeader->SuspendCount;

    if(PreviousCount != 0)
    {
        ResumeThread(ThreadHeader->HostHandle);
        ThreadHeader->SuspendCount--;
    }

    EmuSwapFS();   // Xbox FS

    return PreviousCount;
}

extern "C" ULONG NTAPI EmuKeSuspendThread(xboxkrnl::PKTHREAD Thread)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuThreadObjectHeader *ThreadHeader = EmuThreadHeaderFromThread(Thread);
    ULONG PreviousCount = ThreadHeader->SuspendCount;

    if(PreviousCount >= 0x7F)
    {
        EmuSwapFS();   // Xbox FS
        return PreviousCount;
    }

    SuspendThread(ThreadHeader->HostHandle);
    ThreadHeader->SuspendCount++;

    EmuSwapFS();   // Xbox FS

    return PreviousCount;
}

extern "C" UCHAR __fastcall EmuKfRaiseIrql(UCHAR NewIrql)
{
    UCHAR PreviousIrql = g_EmuCurrentIrql;
    g_EmuCurrentIrql = NewIrql;
    return PreviousIrql;
}

extern "C" VOID __fastcall EmuKfLowerIrql(UCHAR NewIrql)
{
    UCHAR PreviousIrql = g_EmuCurrentIrql;
    g_EmuCurrentIrql = NewIrql;

    if(PreviousIrql >= 2 && NewIrql < 2)
        EmuDispatchPendingDpc();
}

extern "C" xboxkrnl::PKTHREAD NTAPI EmuKeGetCurrentThread()
{
    EmuSwapFS();   // Win2k/XP FS

    xboxkrnl::PKTHREAD Thread = (xboxkrnl::PKTHREAD)EmuGetCurrentThread();

    EmuSwapFS();   // Xbox FS

    return Thread;
}

extern "C" VOID NTAPI EmuKeEnterCriticalRegion()
{
    EmuSwapFS();   // Win2k/XP FS

    EmuAdjustCurrentThreadKernelApcDisable(-1);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuKeLeaveCriticalRegion()
{
    EmuSwapFS();   // Win2k/XP FS

    EmuAdjustCurrentThreadKernelApcDisable(1);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoDeleteDevice
(
    IN PVOID DeviceObject
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(DeviceObject != NULL && DeviceObject == g_EmuDeviceObject)
    {
        delete[] (BYTE*)g_EmuDeviceObject;
        g_EmuDeviceObject = NULL;
        g_EmuDeviceObjectName.clear();
    }

    EmuSwapFS();   // Xbox FS
}

extern "C" NTSTATUS NTAPI EmuIoCreateSymbolicLink
(
    IN xboxkrnl::PSTRING SymbolicLinkName,
    IN xboxkrnl::PSTRING DeviceName
)
{
    return xboxkrnl::IoCreateSymbolicLink(SymbolicLinkName, DeviceName);
}

extern "C" NTSTATUS NTAPI EmuIoDeleteSymbolicLink
(
    IN xboxkrnl::PSTRING SymbolicLinkName
)
{
    return xboxkrnl::IoDeleteSymbolicLink(SymbolicLinkName);
}

// ******************************************************************
// * (Helper) PCSTProxyParam
// ******************************************************************
typedef struct _PCSTProxyParam
{
    IN PVOID StartContext1;
    IN PVOID StartContext2;
    IN PVOID StartRoutine;
}
PCSTProxyParam;

// ******************************************************************
// * (Helper) PCSTProxy
// ******************************************************************
#pragma warning(push)
#pragma warning(disable: 4731)  // disable ebp modification warning
static DWORD WINAPI PCSTProxy
(
    IN PVOID Parameter
)
{
    PCSTProxyParam *iPCSTProxyParam = (PCSTProxyParam*)Parameter;

    uint32 StartContext1 = (uint32)iPCSTProxyParam->StartContext1;
    uint32 StartContext2 = (uint32)iPCSTProxyParam->StartContext2;
    uint32 StartRoutine  = (uint32)iPCSTProxyParam->StartRoutine;

    delete iPCSTProxyParam;

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): PCSTProxy\n"
               "(\n"
               "   StartContext1       : 0x%.08X\n"
               "   StartContext2       : 0x%.08X\n"
               "   StartRoutine        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), StartContext1, StartContext2, StartRoutine);
    }
    #endif

    printf("EmuKrnl (0x%X): PCSTProxy start StartRoutine=0x%.08X Context1=0x%.08X Context2=0x%.08X.\n",
           (uint32)GetCurrentThreadId(), StartRoutine, StartContext1, StartContext2);
    fflush(stdout);

    EmuGenerateFS(g_pTLS, g_pTLSData);

    printf("EmuKrnl (0x%X): PCSTProxy FS generated.\n", (uint32)GetCurrentThreadId());
    fflush(stdout);

    void *CallComplete = &&callComplete;

    // ******************************************************************
    // * use the special calling convention
    // ******************************************************************
    __try
    {
        printf("EmuKrnl (0x%X): PCSTProxy entering XBE thread routine 0x%.08X.\n",
               (uint32)GetCurrentThreadId(), StartRoutine);
        fflush(stdout);

        EmuSwapFS();   // Xbox FS

        __asm
        {
            mov         esi, StartRoutine
            mov         eax, CallComplete
            push        StartContext2
            push        StartContext1
            push        eax
            lea         ebp, [esp-4]
            jmp near    esi
        }
    }
    __except(EmuException(GetExceptionInformation()))
    {
        printf("Emu: WARNING!! Problem with ExceptionFilter\n");
    }

callComplete:

    printf("EmuKrnl (0x%X): PCSTProxy returned from XBE thread routine.\n", (uint32)GetCurrentThreadId());
    fflush(stdout);

    EmuSwapFS();    // Win2k/XP FS

    EmuCleanThread();

    return 0;
}
#pragma warning(pop)

using namespace xboxkrnl;

// ******************************************************************
// * 0x000E ExAllocatePool
// ******************************************************************
XBSYSAPI EXPORTNUM(14) xboxkrnl::PVOID NTAPI xboxkrnl::ExAllocatePool
(
	IN ULONG NumberOfBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): ExAllocatePool\n"
               "(\n"
               "   NumberOfBytes       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), NumberOfBytes);
    }
    #endif

    PVOID pRet = malloc(NumberOfBytes);

    EmuSwapFS();   // Xbox FS

    return pRet;
}

// ******************************************************************
// * 0x0018 ExQueryNonVolatileSetting
// ******************************************************************
XBSYSAPI EXPORTNUM(24) NTSTATUS NTAPI xboxkrnl::ExQueryNonVolatileSetting
(
    IN  DWORD               ValueIndex,
    OUT DWORD              *Type,
    OUT PUCHAR              Value,
    IN  SIZE_T              ValueLength,
    OUT PSIZE_T             ResultLength OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): ExQueryNonVolatileSetting\n"
               "(\n"
               "   ValueIndex          : 0x%.08X\n"
               "   Type                : 0x%.08X\n"
               "   Value               : 0x%.08X\n"
               "   ValueLength         : 0x%.08X\n"
               "   ResultLength        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ValueIndex, Type, Value, ValueLength, ResultLength);
    }
    #endif

    NTSTATUS ret = STATUS_SUCCESS;

    // ******************************************************************
    // * handle eeprom read
    // ******************************************************************
    switch(ValueIndex)
    {
        // nxdk uses 0xFFFF as an entropy source for rand_s initialization.
        case 0xFFFF:
        {
            if(Type != 0)
                *Type = 0x04;

            if(Value != 0)
            {
                for(SIZE_T i = 0;i < ValueLength;i++)
                    Value[i] = (UCHAR)((i * 37 + 0x4D) & 0xFF);
            }

            if(ResultLength != 0)
                *ResultLength = ValueLength;
        }
        break;

        // Factory Game Region
        case 0x104:
        {
            // TODO: configurable region or autodetect of some sort
            if(Type != 0)
                *Type = 0x04;

            if(Value != 0)
                *Value = 0x01;  // North America

            if(ResultLength != 0)
                *ResultLength = 0x04;
        }
        break;

        // Factory AC Region
        case 0x103:
        {
            // TODO: configurable region or autodetect of some sort
            if(Type != 0)
                *Type = 0x04;

            if(Value != 0)
                *Value = 0x01; // NTSC_M

            if(ResultLength != 0)
                *ResultLength = 0x04;
        }
        break;

        // Language
        case 0x007:
        {
            // TODO: configurable language or autodetect of some sort
            if(Type != 0)
                *Type = 0x04;

            if(Value != 0)
                *Value = 0x01;  // English

            if(ResultLength != 0)
                *ResultLength = 0x04;
        }
        break;

        // Video Flags
        case 0x008:
        {
            // TODO: configurable video flags or autodetect of some sort
            if(Type != 0)
                *Type = 0x04;

            if(Value != 0)
                *Value = 0x10;  // Letterbox

            if(ResultLength != 0)
                *ResultLength = 0x04;
        }
        break;

        case EEPROM_MISC:
        {
            if(Type != 0)
                *Type  = 0x04;

            if(Value != 0)
                *Value = 0;

            if(ResultLength != 0)
                *ResultLength = 0x04;
        }
        break;

        default:
            printf("ExQueryNonVolatileSetting unknown ValueIndex (%lu)\n", ValueIndex);
            ret = STATUS_OBJECT_NAME_NOT_FOUND;
            break;
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x0025 - FscSetCacheSize
// ******************************************************************
XBSYSAPI EXPORTNUM(37) xboxkrnl::LONG NTAPI xboxkrnl::FscSetCacheSize(ULONG uCachePages)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): FscSetCacheSize\n"
               "(\n"
               "   uCachePages         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), uCachePages);
    }
    #endif

    EmuWarning("FscSetCacheSize is being ignored");

    EmuSwapFS();   // Xbox FS

    return 0;
}

// ******************************************************************
// * 0x0031 - HalReturnToFirmware
// ******************************************************************
XBSYSAPI EXPORTNUM(49) VOID DECLSPEC_NORETURN xboxkrnl::HalReturnToFirmware
(
    RETURN_FIRMWARE Routine
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): HalReturnToFirmware\n"
               "(\n"
               "   Routine             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Routine);
    }
    #endif

    EmuCleanup("Xbe has rebooted : HalReturnToFirmware(%d)", Routine);

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * 0x0042 - IoCreateFile
// ******************************************************************
XBSYSAPI EXPORTNUM(66) NTSTATUS NTAPI xboxkrnl::IoCreateFile
(
    OUT PHANDLE             FileHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes,
    OUT PIO_STATUS_BLOCK    IoStatusBlock,
    IN  PLARGE_INTEGER      AllocationSize,
    IN  ULONG               FileAttributes,
    IN  ULONG               ShareAccess,
    IN  ULONG               Disposition,
    IN  ULONG               CreateOptions,
    IN  ULONG               Options
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): IoCreateFile\n"
               "(\n"
               "   FileHandle          : 0x%.08X\n"
               "   DesiredAccess       : 0x%.08X\n"
               "   ObjectAttributes    : 0x%.08X (%s)\n"
               "   IoStatusBlock       : 0x%.08X\n"
               "   AllocationSize      : 0x%.08X\n"
               "   FileAttributes      : 0x%.08X\n"
               "   ShareAccess         : 0x%.08X\n"
               "   Disposition         : 0x%.08X\n"
               "   CreateOptions       : 0x%.08X\n"
               "   Options             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, DesiredAccess, ObjectAttributes, ObjectAttributes->ObjectName->Buffer,
               IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, Disposition, CreateOptions, Options);
    }
    #endif

    NTSTATUS ret = STATUS_SUCCESS;

    // TODO: Use NtCreateFile if necessary. If it will work, we're fine
    EmuCleanup("IoCreateFile not implemented");

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x0043 IoCreateSymbolicLink
// ******************************************************************
XBSYSAPI EXPORTNUM(67) NTSTATUS xboxkrnl::IoCreateSymbolicLink
(
    IN PSTRING SymbolicLinkName,
    IN PSTRING DeviceName
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): IoCreateSymbolicLink\n"
               "(\n"
               "   SymbolicLinkName    : 0x%.08X (%s)\n"
               "   DeviceName          : 0x%.08X (%s)\n"
               ");\n",
               GetCurrentThreadId(), SymbolicLinkName, SymbolicLinkName->Buffer,
               DeviceName, DeviceName->Buffer);
    }
    #endif

    std::string LinkName;
    std::string TargetName;
    NTSTATUS ret = STATUS_SUCCESS;

    const bool LinkStringValid = EmuObjectStringToStdString(SymbolicLinkName, &LinkName);
    const bool TargetStringValid = EmuObjectStringToStdString(DeviceName, &TargetName);

    if(SymbolicLinkName == NULL)
    {
        ret = STATUS_SUCCESS;
    }
    else if(!LinkStringValid)
    {
        ret = EmuStatusObjectNameInvalid;
    }
    else if(!TargetStringValid)
    {
        ret = EmuStatusInvalidParameter;
    }
    else
    {
        if(TargetName.size() > 1 && TargetName[TargetName.size() - 1] == '\\')
            TargetName.erase(TargetName.size() - 1);

        if(!EmuIsValidSymbolicLinkName(LinkName))
        {
            ret = EmuStatusObjectNameInvalid;
        }
        else if(!EmuIsValidObjectName(TargetName))
        {
            ret = EmuStatusInvalidParameter;
        }
        else if(TargetName.compare(0, 8, "\\Device\\") != 0)
        {
            ret = EmuStatusInvalidParameter;
        }
        else if(!EmuObjectNameExists(TargetName))
        {
            ret = EmuObjectNameIsBelowKnownDevice(TargetName) ? STATUS_OBJECT_NAME_NOT_FOUND : EmuStatusObjectPathNotFound;
        }
        else if(!EmuIsKnownDeviceObject(TargetName))
        {
            ret = EmuStatusInvalidParameter;
        }
        else if(g_EmuSymbolicLinks.find(LinkName) != g_EmuSymbolicLinks.end())
        {
            ret = STATUS_OBJECT_NAME_COLLISION;
        }
        else if(EmuIsKnownDeviceObject(LinkName) || EmuIsKnownDirectoryObject(LinkName) || !EmuIsKnownDirectoryObject(EmuObjectParentPath(LinkName)))
        {
            ret = EmuStatusObjectPathNotFound;
        }
        else
        {
            g_EmuSymbolicLinks[LinkName] = TargetName;
        }
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x0045 - IoDeleteSymbolicLink
// ******************************************************************
XBSYSAPI EXPORTNUM(69) NTSTATUS xboxkrnl::IoDeleteSymbolicLink
(
    IN PSTRING SymbolicLinkName
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): IoDeleteSymbolicLink\n"
               "(\n"
               "   SymbolicLinkName    : 0x%.08X (%s)\n"
               ");\n",
               GetCurrentThreadId(), SymbolicLinkName, SymbolicLinkName->Buffer);
    }
    #endif

    std::string LinkName;
    NTSTATUS ret = STATUS_SUCCESS;

    const bool LinkStringValid = EmuObjectStringToStdString(SymbolicLinkName, &LinkName);

    if(!LinkStringValid)
    {
        ret = EmuStatusObjectNameInvalid;
    }
    else if(!EmuIsValidSymbolicLinkName(LinkName))
    {
        ret = EmuStatusObjectNameInvalid;
    }
    else
    {
        std::map<std::string, std::string>::iterator Entry = g_EmuSymbolicLinks.find(LinkName);

        if(Entry == g_EmuSymbolicLinks.end())
            ret = STATUS_OBJECT_NAME_NOT_FOUND;
        else
            g_EmuSymbolicLinks.erase(Entry);
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x0063 - KeDelayExecutionThread
// ******************************************************************
XBSYSAPI EXPORTNUM(99) NTSTATUS NTAPI xboxkrnl::KeDelayExecutionThread
(
    IN KPROCESSOR_MODE  WaitMode,
    IN BOOLEAN          Alertable,
    IN PLARGE_INTEGER   Interval
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): KeDelayExecutionThread\n"
               "(\n"
               "   WaitMode            : 0x%.08X\n"
               "   Alertable           : 0x%.08X\n"
               "   Interval            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), WaitMode, Alertable, Interval);
    }
    #endif

    NTSTATUS ret = NtDll::NtDelayExecution(Alertable, (NtDll::LARGE_INTEGER*)Interval);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x006B - KeInitializeDpc
// ******************************************************************
XBSYSAPI EXPORTNUM(107) VOID NTAPI xboxkrnl::KeInitializeDpc
(
    KDPC                *Dpc,
    PKDEFERRED_ROUTINE   DeferredRoutine,
    PVOID                DeferredContext
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): KeInitializeDpc\n"
               "(\n"
               "   Dpc                 : 0x%.08X\n"
               "   DeferredRoutine     : 0x%.08X\n"
               "   DeferredContext     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Dpc, DeferredRoutine, DeferredContext);
    }
    #endif

    Dpc->Number = 0;
    Dpc->DeferredRoutine = DeferredRoutine;
    Dpc->Type = DpcObject;
    Dpc->DeferredContext = DeferredContext;

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x0071 - KeInitializeTimerEx
// ******************************************************************
XBSYSAPI EXPORTNUM(113) VOID NTAPI xboxkrnl::KeInitializeTimerEx
(
    IN PKTIMER      Timer,
    IN TIMER_TYPE   Type
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): KeInitializeTimerEx\n"
               "(\n"
               "   Timer               : 0x%.08X\n"
               "   Type                : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Timer, Type);
    }
    #endif
    
    Timer->Header.Type               = Type + 8;
    Timer->Header.Inserted           = 0;
    Timer->Header.Size               = sizeof(*Timer) / sizeof(ULONG);
    Timer->Header.SignalState        = 0;
    Timer->TimerListEntry.Blink      = NULL;
    Timer->TimerListEntry.Flink      = NULL;
    Timer->Header.WaitListHead.Flink = &Timer->Header.WaitListHead;
    Timer->Header.WaitListHead.Blink = &Timer->Header.WaitListHead;
    Timer->DueTime.QuadPart          = 0;
    Timer->Period                    = 0;

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x0080 - KeQuerySystemTime
// ******************************************************************
XBSYSAPI EXPORTNUM(128) VOID NTAPI xboxkrnl::KeQuerySystemTime
(
    PLARGE_INTEGER CurrentTime
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): KeQuerySystemTime\n"
               "(\n"
               "   CurrentTime         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), CurrentTime);
    }
    #endif

    SYSTEMTIME SystemTime;

    GetSystemTime(&SystemTime);

    SystemTimeToFileTime(&SystemTime, (FILETIME*)CurrentTime);

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x0095 - KeSetTimer
// ******************************************************************
XBSYSAPI EXPORTNUM(149) xboxkrnl::BOOLEAN NTAPI xboxkrnl::KeSetTimer
(
    IN PKTIMER        Timer,
    IN LARGE_INTEGER  DueTime,
    IN PKDPC          Dpc OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): KeSetTimer\n"
               "(\n"
               "   Timer               : 0x%.08X\n"
               "   DueTime             : 0x%I64X\n"
               "   Dpc                 : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Timer, DueTime, Dpc);
    }
    #endif

    EmuCleanup("KeSetTimer: Not Implemented!");

    EmuSwapFS();   // Xbox FS

    return TRUE;
}

// ******************************************************************
// * 0x009C - KeTickCount
// ******************************************************************
XBSYSAPI EXPORTNUM(156) volatile xboxkrnl::DWORD xboxkrnl::KeTickCount = 0;

// ******************************************************************
// * 0x00A4 - LaunchDataPage (actually a pointer)
// ******************************************************************
XBSYSAPI EXPORTNUM(164) xboxkrnl::DWORD xboxkrnl::LaunchDataPage = 0;

// ******************************************************************
// * 0x00A5 - MmAllocateContiguousMemory
// ******************************************************************
XBSYSAPI EXPORTNUM(165) xboxkrnl::PVOID NTAPI xboxkrnl::MmAllocateContiguousMemory
(
	IN ULONG NumberOfBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): MmAllocateContiguousMemory\n"
               "(\n"
               "   NumberOfBytes            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), NumberOfBytes);
    }
    #endif

    // TODO: Make this much more efficient and correct if necessary!
    // HACK: Should be aligned!!
    PVOID pRet = (PVOID)new unsigned char[NumberOfBytes];

    EmuSwapFS();   // Xbox FS

    return pRet;
}

// ******************************************************************
// * 0x00A6 - MmAllocateContiguousMemoryEx
// ******************************************************************
XBSYSAPI EXPORTNUM(166) xboxkrnl::PVOID NTAPI xboxkrnl::MmAllocateContiguousMemoryEx
(
	IN ULONG			NumberOfBytes,
	IN PHYSICAL_ADDRESS LowestAcceptableAddress,
	IN PHYSICAL_ADDRESS HighestAcceptableAddress,
	IN ULONG			Alignment OPTIONAL,
	IN ULONG			ProtectionType
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): MmAllocateContiguousMemoryEx\n"
               "(\n"
               "   NumberOfBytes            : 0x%.08X\n"
               "   LowestAcceptableAddress  : 0x%.08X\n"
               "   HighestAcceptableAddress : 0x%.08X\n"
               "   Alignment                : 0x%.08X\n"
               "   ProtectionType           : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), NumberOfBytes, LowestAcceptableAddress, HighestAcceptableAddress,
               Alignment, ProtectionType);
    }
    #endif

    // TODO: Make this much more efficient and correct if necessary!
    // HACK: Should be aligned!!
    PVOID pRet = (PVOID)new unsigned char[NumberOfBytes];

    EmuSwapFS();   // Xbox FS

    return pRet;
}

// ******************************************************************
// * 0x00A7 - MmAllocateSystemMemory
// ******************************************************************
XBSYSAPI EXPORTNUM(167) xboxkrnl::PVOID NTAPI xboxkrnl::MmAllocateSystemMemory
(
    ULONG NumberOfBytes,
    ULONG Protect
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): MmAllocateContiguousMemoryEx\n"
               "(\n"
               "   NumberOfBytes            : 0x%.08X\n"
               "   Protect                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), NumberOfBytes, Protect);
    }
    #endif

    PVOID pRet = NULL;

    if(EmuIsValidSystemMemoryProtect(Protect))
    {
        const ULONG Pages = EmuSystemMemoryPages(NumberOfBytes);
        const ULONG Size = Pages * EmuPageSize;

        if(Pages != 0 && g_EmuNextSystemMemoryAddress + Size >= g_EmuNextSystemMemoryAddress &&
           g_EmuNextSystemMemoryAddress + Size <= 0xF0000000)
        {
            for(ULONG i = 0; i < sizeof(g_EmuSystemMemoryAllocations) / sizeof(g_EmuSystemMemoryAllocations[0]); i++)
            {
                if(g_EmuSystemMemoryAllocations[i].Address != NULL)
                    continue;

                pRet = (PVOID)g_EmuNextSystemMemoryAddress;
                g_EmuSystemMemoryAllocations[i].Address = pRet;
                g_EmuSystemMemoryAllocations[i].Pages = Pages;
                g_EmuNextSystemMemoryAddress += Size;
                break;
            }
        }
    }

    EmuSwapFS();   // Xbox FS

    return pRet;
}

// ******************************************************************
// * 0x00AB - MmFreeContiguousMemory
// ******************************************************************
XBSYSAPI EXPORTNUM(171) VOID NTAPI xboxkrnl::MmFreeContiguousMemory
(
	IN PVOID BaseAddress
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): MmFreeContiguousMemory\n"
               "(\n"
               "   BaseAddress              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BaseAddress);
    }
    #endif

    delete[] BaseAddress;

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x00AC - MmFreeSystemMemory
// ******************************************************************
XBSYSAPI EXPORTNUM(172) NTSTATUS NTAPI xboxkrnl::MmFreeSystemMemory
(
    PVOID BaseAddress,
    ULONG NumberOfBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): MmFreeSystemMemory\n"
               "(\n"
               "   BaseAddress              : 0x%.08X\n"
               "   NumberOfBytes            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BaseAddress, NumberOfBytes);
    }
    #endif

    ULONG Pages = EmuSystemMemoryPages(NumberOfBytes);

    for(ULONG i = 0; i < sizeof(g_EmuSystemMemoryAllocations) / sizeof(g_EmuSystemMemoryAllocations[0]); i++)
    {
        if(g_EmuSystemMemoryAllocations[i].Address != BaseAddress)
            continue;

        if(Pages == 0)
            Pages = g_EmuSystemMemoryAllocations[i].Pages;

        g_EmuSystemMemoryAllocations[i].Address = NULL;
        g_EmuSystemMemoryAllocations[i].Pages = 0;
        break;
    }

    EmuSwapFS();   // Xbox FS

    return Pages;
}

// ******************************************************************
// * 0x00B2 - MmPersistContiguousMemory
// ******************************************************************
XBSYSAPI EXPORTNUM(178) VOID NTAPI xboxkrnl::MmPersistContiguousMemory
(
    IN PVOID   BaseAddress,
    IN ULONG   NumberOfBytes,
    IN BOOLEAN Persist
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): MmPersistContiguousMemory\n"
               "(\n"
               "   BaseAddress              : 0x%.08X\n"
               "   NumberOfBytes            : 0x%.08X\n"
               "   Persist                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BaseAddress, NumberOfBytes, Persist);
    }
    #endif

    // TODO: Actually set this up to be remember across a "reboot"
    EmuWarning("MmPersistContiguousMemory is being ignored\n");

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * 0x00B6 - MmSetAddressProtect
// ******************************************************************
XBSYSAPI EXPORTNUM(182) VOID NTAPI xboxkrnl::MmSetAddressProtect
(
    IN PVOID BaseAddress,
    IN ULONG NumberOfBytes,
    IN ULONG NewProtect
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): MmSetAddressProtect\n"
               "(\n"
               "   BaseAddress              : 0x%.08X\n"
               "   NumberOfBytes            : 0x%.08X\n"
               "   Persist                  : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BaseAddress, NumberOfBytes, NewProtect);
    }
    #endif

    // TODO: Actually set protection
    EmuWarning("MmSetAddressProtect is being ignored\n");

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x00B8 - NtAllocateVirtualMemory
// ******************************************************************
XBSYSAPI EXPORTNUM(184) NTSTATUS NTAPI xboxkrnl::NtAllocateVirtualMemory
(
    IN OUT PVOID    *BaseAddress,
    IN ULONG         ZeroBits,
    IN OUT PULONG    AllocationSize,
    IN DWORD         AllocationType,
    IN DWORD         Protect
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtAllocateVirtualMemory\n"
               "(\n"
               "   BaseAddress         : 0x%.08X (0x%.08X)\n"
               "   ZeroBits            : 0x%.08X\n"
               "   AllocationSize      : 0x%.08X (0x%.08X)\n"
               "   AllocationType      : 0x%.08X\n"
               "   Protect             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BaseAddress, *BaseAddress, ZeroBits, AllocationSize, *AllocationSize, AllocationType, Protect);
    }
    #endif

    NTSTATUS ret = NtDll::NtAllocateVirtualMemory(GetCurrentProcess(), BaseAddress, ZeroBits, AllocationSize, AllocationType, Protect);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00BA - NtClearEvent
// ******************************************************************
XBSYSAPI EXPORTNUM(186) NTSTATUS NTAPI xboxkrnl::NtClearEvent
(
    IN HANDLE EventHandle
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtClearEvent\n"
               "(\n"
               "   EventHandle         : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), EventHandle);
    }
    #endif

    NTSTATUS ret = NtDll::NtClearEvent(EventHandle);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00BB - NtClose
// ******************************************************************
XBSYSAPI EXPORTNUM(187) NTSTATUS NTAPI xboxkrnl::NtClose
(
    IN HANDLE Handle
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtClose\n"
               "(\n"
               "   Handle              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle);
    }
    #endif

    NTSTATUS ret = STATUS_SUCCESS;

    if(Handle == NULL || Handle == (HANDLE)-1 || Handle == (HANDLE)-2)
    {
        EmuSwapFS();   // Xbox FS
        return STATUS_SUCCESS;
    }
    
    // ******************************************************************
    // * delete 'special' handles
    // ******************************************************************
    if(IsEmuHandle(Handle))
    {
        EmuHandle *iEmuHandle = EmuHandleToPtr(Handle);

        delete iEmuHandle;

        ret = STATUS_SUCCESS;
    }
    // ******************************************************************
    // * close normal handles
    // ******************************************************************
    else
    {
        ret = NtDll::NtClose(Handle);
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00BD - NtCreateEvent
// ******************************************************************
XBSYSAPI EXPORTNUM(189) NTSTATUS NTAPI xboxkrnl::NtCreateEvent
(
    OUT PHANDLE             EventHandle,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes OPTIONAL,
    IN  EVENT_TYPE          EventType,
    IN  BOOLEAN             InitialState
)
{
    EmuSwapFS();   // Win2k/XP FS

    char *szBuffer = (ObjectAttributes != 0) ? ObjectAttributes->ObjectName->Buffer : 0;

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtCreateEvent\n"
               "(\n"
               "   EventHandle         : 0x%.08X\n"
               "   ObjectAttributes    : 0x%.08X (\"%s\")\n"
               "   EventType           : 0x%.08X\n"
               "   InitialState        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), EventHandle, ObjectAttributes, szBuffer,
               EventType, InitialState);
    }
    #endif

    wchar_t wszObjectName[160];

    NtDll::UNICODE_STRING    NtUnicodeString;
    NtDll::OBJECT_ATTRIBUTES NtObjAttr;

    // ******************************************************************
    // * Initialize Object Attributes
    // ******************************************************************
    if(szBuffer != 0)
    {
        mbstowcs(wszObjectName, szBuffer, 160);

        NtDll::RtlInitUnicodeString(&NtUnicodeString, wszObjectName);

        InitializeObjectAttributes(&NtObjAttr, &NtUnicodeString, ObjectAttributes->Attributes, ObjectAttributes->RootDirectory, NULL);
    }

    // ******************************************************************
    // * Redirect to NtCreateEvent
    // ******************************************************************
    NTSTATUS ret = NtDll::NtCreateEvent(EventHandle, EVENT_ALL_ACCESS, (szBuffer != 0) ? &NtObjAttr : 0, (NtDll::EVENT_TYPE)EventType, InitialState);
    if(FAILED(ret) && EventHandle != NULL)
    {
        *EventHandle = CreateEvent(NULL, EventType == NotificationEvent, InitialState, NULL);
        ret = (*EventHandle != NULL) ? STATUS_SUCCESS : 0xC0000008;
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00BE - NtCreateFile
// ******************************************************************
XBSYSAPI EXPORTNUM(190) NTSTATUS NTAPI xboxkrnl::NtCreateFile
(
    OUT PHANDLE             FileHandle, 
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes,
    OUT PIO_STATUS_BLOCK    IoStatusBlock,
    IN  PLARGE_INTEGER      AllocationSize OPTIONAL, 
    IN  ULONG               FileAttributes, 
    IN  ULONG               ShareAccess, 
    IN  ULONG               CreateDisposition, 
    IN  ULONG               CreateOptions 
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtCreateFile\n"
               "(\n"
               "   FileHandle          : 0x%.08X\n"
               "   DesiredAccess       : 0x%.08X\n"
               "   ObjectAttributes    : 0x%.08X (\"%s\")\n"
               "   IoStatusBlock       : 0x%.08X\n"
               "   AllocationSize      : 0x%.08X\n"
               "   FileAttributes      : 0x%.08X\n"
               "   ShareAccess         : 0x%.08X\n"
               "   CreateDisposition   : 0x%.08X\n"
               "   CreateOptions       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, DesiredAccess, ObjectAttributes, ObjectAttributes->ObjectName->Buffer,
               IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions);
    }
    #endif

    char ReplaceChar  = '\0';
    int  ReplaceIndex = -1;

    char *szBuffer = ObjectAttributes->ObjectName->Buffer;
    char *szOriginalBuffer = szBuffer;

    if(std::strncmp(szBuffer, "\\??\\", 4) == 0)
        szBuffer += 4;

    // ******************************************************************
    // * D:\ should map to current directory
    // ******************************************************************
    if( (szBuffer[0] == 'D' || szBuffer[0] == 'd') && szBuffer[1] == ':' && szBuffer[2] == '\\')
    {
        szBuffer += 3;

        ObjectAttributes->RootDirectory = g_hCurDir;

        #ifdef _DEBUG_TRACE
        printf("EmuKrnl (0x%X): NtCreateFile Corrected path...\n", GetCurrentThreadId());
        printf("  Org:\"%s\"\n", szOriginalBuffer);
        printf("  New:\"$XbePath\\%s\"\n", szBuffer);
        #endif
    }
    else if( (szBuffer[0] == 'T' || szBuffer[0] == 't') && szBuffer[1] == ':' && szBuffer[2] == '\\')
    {
        szBuffer += 3;

        ObjectAttributes->RootDirectory = g_hTDrive;

        #ifdef _DEBUG_TRACE
        printf("EmuKrnl (0x%X): NtCreateFile Corrected path...\n", GetCurrentThreadId());
        printf("  Org:\"%s\"\n", szOriginalBuffer);
        printf("  New:\"$CxbxPath\\TDATA\\%s\"\n", szBuffer);
        #endif
    }
    else if( (szBuffer[0] == 'U' || szBuffer[0] == 'u') && szBuffer[1] == ':' && szBuffer[2] == '\\')
    {
        szBuffer += 3;

        ObjectAttributes->RootDirectory = g_hUDrive;

        #ifdef _DEBUG_TRACE
        printf("EmuKrnl (0x%X): NtCreateFile Corrected path...\n", GetCurrentThreadId());
        printf("  Org:\"%s\"\n", szOriginalBuffer);
        printf("  New:\"$CxbxPath\\UDATA\\%s\"\n", szBuffer);
        #endif
    }
    else if( (szBuffer[0] == 'Z' || szBuffer[0] == 'z') && szBuffer[1] == ':' && szBuffer[2] == '\\')
    {
        szBuffer += 3;

        ObjectAttributes->RootDirectory = g_hZDrive;

        #ifdef _DEBUG_TRACE
        printf("EmuKrnl (0x%X): NtCreateFile Corrected path...\n", GetCurrentThreadId());
        printf("  Org:\"%s\"\n", szOriginalBuffer);
        printf("  New:\"$CxbxPath\\CxbxCache\\%s\"\n", szBuffer);
        #endif
    }

    // ******************************************************************
    // * TODO: Wildcards are not allowed??
    // ******************************************************************
    {
        for(int v=0;szBuffer[v] != '\0';v++)
        {
            if(szBuffer[v] == '*')
            {
                if(v > 0)
                    ReplaceIndex = v-1;
                else
                    ReplaceIndex = v;
            }
        }

        // Note: Hack: Not thread safe (if problems occur, create a temp buffer)
        if(ReplaceIndex != -1)
        {
            ReplaceChar = szBuffer[ReplaceIndex];
            szBuffer[ReplaceIndex] = '\0';
        }
    }

    wchar_t wszObjectName[160];

    NtDll::UNICODE_STRING    NtUnicodeString;
    NtDll::OBJECT_ATTRIBUTES NtObjAttr;

    // ******************************************************************
    // * Initialize Object Attributes
    // ******************************************************************
    {
        mbstowcs(wszObjectName, szBuffer, 160);

        NtDll::RtlInitUnicodeString(&NtUnicodeString, wszObjectName);

        InitializeObjectAttributes(&NtObjAttr, &NtUnicodeString, ObjectAttributes->Attributes, ObjectAttributes->RootDirectory, NULL);
    }

    // ******************************************************************
    // * Redirect to NtCreateFile
    // ******************************************************************
    ACCESS_MASK NtDesiredAccess = DesiredAccess;
    if((CreateOptions & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT)) != 0)
        NtDesiredAccess |= SYNCHRONIZE;

    NTSTATUS ret = NtDll::NtCreateFile
    (
        FileHandle, NtDesiredAccess, &NtObjAttr, (NtDll::IO_STATUS_BLOCK*)IoStatusBlock,
        (NtDll::LARGE_INTEGER*)AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, NULL, NULL
    );

    if(FAILED(ret))
    {
        EmuWarning("NtCreateFile Failed (0x%.08X)", ret);
        printf("EmuKrnl (0x%X): NtCreateFile failed path=\"%s\" translated=\"%s\" status=0x%.08X\n",
               (uint32)GetCurrentThreadId(), szOriginalBuffer, szBuffer, (uint32)ret);
    }

    // ******************************************************************
    // * Restore original buffer
    // ******************************************************************
    if(ReplaceIndex != -1)
        szBuffer[ReplaceIndex] = ReplaceChar;

    // NOTE: We can map this to IoCreateFile once implemented (if ever necessary)
    //       xboxkrnl::IoCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes, ShareAccess, CreateDisposition, CreateOptions, 0);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00C0 - NtCreateMutant
// ******************************************************************
XBSYSAPI EXPORTNUM(192) NTSTATUS NTAPI xboxkrnl::NtCreateMutant
(
    OUT PHANDLE             MutantHandle,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes,
    IN  BOOLEAN             InitialOwner
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtCreateMutant\n"
               "(\n"
               "   MutantHandle        : 0x%.08X\n"
               "   ObjectAttributes    : 0x%.08X\n"
               "   InitialOwner        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), MutantHandle, ObjectAttributes, InitialOwner);
    }
    #endif

    char *szBuffer = (ObjectAttributes != 0) ? ObjectAttributes->ObjectName->Buffer : 0;

    wchar_t wszObjectName[160];

    NtDll::UNICODE_STRING    NtUnicodeString;
    NtDll::OBJECT_ATTRIBUTES NtObjAttr;

    // ******************************************************************
    // * Initialize Object Attributes
    // ******************************************************************
    if(szBuffer != 0)
    {
        mbstowcs(wszObjectName, szBuffer, 160);

        NtDll::RtlInitUnicodeString(&NtUnicodeString, wszObjectName);

        InitializeObjectAttributes(&NtObjAttr, &NtUnicodeString, ObjectAttributes->Attributes, ObjectAttributes->RootDirectory, NULL);
    }

    NTSTATUS ret = NtDll::NtCreateMutant(MutantHandle, MUTANT_ALL_ACCESS, (szBuffer != 0) ? &NtObjAttr : 0, InitialOwner);
    if(FAILED(ret) && MutantHandle != NULL)
    {
        *MutantHandle = CreateMutex(NULL, InitialOwner, NULL);
        ret = (*MutantHandle != NULL) ? STATUS_SUCCESS : 0xC0000008;
    }

    if(FAILED(ret))
        printf("EmuKrnl (0x%X): NtCreateMutant failed with status 0x%.08X.\n", (uint32)GetCurrentThreadId(), (uint32)ret);

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" NTSTATUS NTAPI EmuNtCreateSemaphore
(
    OUT PHANDLE             SemaphoreHandle,
    IN  PVOID               ObjectAttributes,
    IN  LONG                InitialCount,
    IN  LONG                MaximumCount
)
{
    if(SemaphoreHandle == NULL)
        return 0xC0000008;

    *SemaphoreHandle = CreateSemaphore(NULL, InitialCount, MaximumCount, NULL);

    return (*SemaphoreHandle != NULL) ? STATUS_SUCCESS : 0xC0000008;
}

extern "C" NTSTATUS NTAPI EmuNtCreateTimer
(
    OUT PHANDLE             TimerHandle,
    IN  PVOID               ObjectAttributes,
    IN  ULONG               TimerType
)
{
    if(TimerHandle == NULL)
        return 0xC0000008;

    *TimerHandle = CreateWaitableTimer(NULL, TimerType == NotificationTimer, NULL);

    return (*TimerHandle != NULL) ? STATUS_SUCCESS : 0xC0000008;
}

// ******************************************************************
// * 0x00C5 - NtDuplicateObject
// ******************************************************************
XBSYSAPI EXPORTNUM(197) NTSTATUS NTAPI xboxkrnl::NtDuplicateObject
(
    HANDLE  SourceHandle,
    HANDLE *TargetHandle,
    DWORD   Options
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtDuplicateObject\n"
               "(\n"
               "   SourceHandle        : 0x%.08X\n"
               "   TargetHandle        : 0x%.08X\n"
               "   Options             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), SourceHandle, TargetHandle, Options);
    }
    #endif

    // ******************************************************************
    // * redirect to Win2k/XP
    // ******************************************************************
    NTSTATUS ret = NtDll::NtDuplicateObject
    (
        GetCurrentProcess(),
        SourceHandle,
        GetCurrentProcess(),
        TargetHandle,
        0, 0, Options
    );

    if(ret != STATUS_SUCCESS)
        printf("*Warning* Object was not duplicated\n");

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00C7 - NtFreeVirtualMemory
// ******************************************************************
XBSYSAPI EXPORTNUM(199) NTSTATUS NTAPI xboxkrnl::NtFreeVirtualMemory
(
    IN OUT PVOID *BaseAddress,
    IN OUT PULONG FreeSize,
    IN ULONG      FreeType
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtFreeVirtualMemory\n"
               "(\n"
               "   BaseAddress         : 0x%.08X\n"
               "   FreeSize            : 0x%.08X\n"
               "   FreeType            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), BaseAddress, FreeSize, FreeType);
    }
    #endif

    NTSTATUS ret = NtDll::NtFreeVirtualMemory(GetCurrentProcess(), BaseAddress, FreeSize, FreeType);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00CA - NtOpenFile
// ******************************************************************
XBSYSAPI EXPORTNUM(202) NTSTATUS NTAPI xboxkrnl::NtOpenFile
(
    OUT PHANDLE             FileHandle,
    IN  ACCESS_MASK         DesiredAccess,
    IN  POBJECT_ATTRIBUTES  ObjectAttributes,
    OUT PIO_STATUS_BLOCK    IoStatusBlock,
    IN  ULONG               ShareAccess,
    IN  ULONG               OpenOptions
)
{
    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        EmuSwapFS();   // Win2k/XP FS
        printf("EmuKrnl (0x%X): NtOpenFile\n"
               "(\n"
               "   FileHandle          : 0x%.08X\n"
               "   DesiredAccess       : 0x%.08X\n"
               "   ObjectAttributes    : 0x%.08X (\"%s\")\n"
               "   IoStatusBlock       : 0x%.08X\n"
               "   ShareAccess         : 0x%.08X\n"
               "   CreateOptions       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, DesiredAccess, ObjectAttributes, ObjectAttributes->ObjectName->Buffer,
               IoStatusBlock, ShareAccess, OpenOptions);
        EmuSwapFS();   // Xbox FS
    }
    #endif

    return NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, NULL, 0, ShareAccess, FILE_OPEN, OpenOptions);
}

// ******************************************************************
// * 0x00CF - NtQueryDirectoryFile
// ******************************************************************
XBSYSAPI EXPORTNUM(207) NTSTATUS NTAPI xboxkrnl::NtQueryDirectoryFile
(
    IN  HANDLE                      FileHandle,
    IN  HANDLE                      Event OPTIONAL,
    IN  PVOID                       ApcRoutine, // Todo: define this routine's prototype
    IN  PVOID                       ApcContext,
    OUT PIO_STATUS_BLOCK            IoStatusBlock,
    OUT FILE_DIRECTORY_INFORMATION *FileInformation,
    IN  ULONG                       Length,
    IN  FILE_INFORMATION_CLASS      FileInformationClass,
    IN  PSTRING                     FileMask,
    IN  BOOLEAN                     RestartScan
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtQueryDirectoryFile\n"
               "(\n"
               "   FileHandle           : 0x%.08X\n"
               "   Event                : 0x%.08X\n"
               "   ApcRoutine           : 0x%.08X\n"
               "   ApcContext           : 0x%.08X\n"
               "   IoStatusBlock        : 0x%.08X\n"
               "   FileInformation      : 0x%.08X\n"
               "   Length               : 0x%.08X\n"
               "   FileInformationClass : 0x%.08X\n"
               "   FileMask             : 0x%.08X (%s)\n"
               "   RestartScan          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock,
               FileInformation, Length, FileInformationClass, FileMask,
               (FileMask != 0) ? FileMask->Buffer : "", RestartScan);
    }
    #endif

    NTSTATUS ret;

    if(FileInformationClass != 1)   // Due to unicode->string conversion
        EmuCleanup("Unsupported FileInformationClass");

    NtDll::UNICODE_STRING NtFileMask;

    wchar_t wszObjectName[160];

    // ******************************************************************
    // * Initialize FileMask
    // ******************************************************************
    {
        if(FileMask != 0)
            mbstowcs(wszObjectName, FileMask->Buffer, 160);
        else
            mbstowcs(wszObjectName, "", 160);

        NtDll::RtlInitUnicodeString(&NtFileMask, wszObjectName);
    }

    NtDll::FILE_DIRECTORY_INFORMATION *FileDirInfo = (NtDll::FILE_DIRECTORY_INFORMATION*)malloc(0x40 + 160*2);

    char    *mbstr = FileInformation->FileName;
    wchar_t *wcstr = FileDirInfo->FileName;

    do
    {
        ZeroMemory(wcstr, 160*2);

        ret = NtDll::NtQueryDirectoryFile
        (
            FileHandle, Event, (NtDll::PIO_APC_ROUTINE)ApcRoutine, ApcContext, (NtDll::IO_STATUS_BLOCK*)IoStatusBlock, FileDirInfo,
            0x40+160*2, (NtDll::FILE_INFORMATION_CLASS)FileInformationClass, TRUE, &NtFileMask, RestartScan
        );

        // ******************************************************************
        // * Convert from PC to Xbox
        // ******************************************************************
        {
            memcpy(FileInformation, FileDirInfo, 0x40);

            wcstombs(mbstr, wcstr, 160);

            FileInformation->FileNameLength /= 2;
        }
    } // Xbox does not return . and ..
    while(strcmp(mbstr, ".") == 0 || strcmp(mbstr, "..") == 0);

    // TODO: Cache the last search result for quicker access with CreateFile (xbox does this internally!)
    free(FileDirInfo);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00D2 - NtQueryFullAttributesFile
// ******************************************************************
XBSYSAPI EXPORTNUM(210) NTSTATUS NTAPI xboxkrnl::NtQueryFullAttributesFile
(   
    IN  POBJECT_ATTRIBUTES          ObjectAttributes,
    OUT PVOID                       Attributes
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtQueryFullAttributesFile\n"
               "(\n"
               "   ObjectAttributes    : 0x%.08X (%s)\n"
               "   Attributes          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ObjectAttributes, ObjectAttributes->ObjectName->Buffer, Attributes);
    }
    #endif

    char *szBuffer = ObjectAttributes->ObjectName->Buffer;

    wchar_t wszObjectName[160];

    NtDll::UNICODE_STRING    NtUnicodeString;
    NtDll::OBJECT_ATTRIBUTES NtObjAttr;

    // ******************************************************************
    // * Initialize Object Attributes
    // ******************************************************************
    {
        mbstowcs(wszObjectName, szBuffer, 160);

        NtDll::RtlInitUnicodeString(&NtUnicodeString, wszObjectName);

        InitializeObjectAttributes(&NtObjAttr, &NtUnicodeString, ObjectAttributes->Attributes, ObjectAttributes->RootDirectory, NULL);
    }

	NTSTATUS ret = NtDll::NtQueryFullAttributesFile(&NtObjAttr, Attributes);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00D3 - NtQueryInformationFile
// ******************************************************************
XBSYSAPI EXPORTNUM(211) NTSTATUS NTAPI xboxkrnl::NtQueryInformationFile
(   
    IN  HANDLE                      FileHandle,
    OUT PIO_STATUS_BLOCK            IoStatusBlock,
    OUT PVOID                       FileInformation, 
    IN  ULONG                       Length, 
    IN  FILE_INFORMATION_CLASS      FileInfo
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtQueryInformationFile\n"
               "(\n"
               "   FileHandle          : 0x%.08X\n"
               "   IoStatusBlock       : 0x%.08X\n"
               "   FileInformation     : 0x%.08X\n"
               "   Length              : 0x%.08X\n"
               "   FileInformationClass: 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, IoStatusBlock, FileInformation, 
               Length, FileInfo);
    }
    #endif

	NTSTATUS ret = NtDll::NtQueryInformationFile
	(
		FileHandle,
        (NtDll::PIO_STATUS_BLOCK)IoStatusBlock,
        (NtDll::PFILE_FS_SIZE_INFORMATION)FileInformation,
		Length,
        (NtDll::FILE_INFORMATION_CLASS)FileInfo
	);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00DA - NtQueryVolumeInformationFile
// ******************************************************************
XBSYSAPI EXPORTNUM(218) NTSTATUS NTAPI xboxkrnl::NtQueryVolumeInformationFile
(
    IN  HANDLE                      FileHandle,
    OUT PIO_STATUS_BLOCK            IoStatusBlock,
    OUT PFILE_FS_SIZE_INFORMATION   FileInformation,
    IN  ULONG                       Length,
    IN  FS_INFORMATION_CLASS        FileInformationClass
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtQueryVolumeInformationFile\n"
               "(\n"
               "   FileHandle          : 0x%.08X\n"
               "   IoStatusBlock       : 0x%.08X\n"
               "   FileInformation     : 0x%.08X\n"
               "   Length              : 0x%.08X\n"
               "   FileInformationClass: 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, IoStatusBlock, FileInformation, 
               Length, FileInformationClass);
    }
    #endif

    // Safety/Sanity Check
    if(FileInformationClass != FileFsSizeInformation && FileInformationClass != FileDirectoryInformation)
        EmuCleanup("NtQueryVolumeInformationFile: Unsupported FileInformationClass");

    NTSTATUS ret = NtDll::NtQueryVolumeInformationFile
    (
        FileHandle,
        (NtDll::PIO_STATUS_BLOCK)IoStatusBlock,
        (NtDll::PFILE_FS_SIZE_INFORMATION)FileInformation, Length,
        (NtDll::FS_INFORMATION_CLASS)FileInformationClass
    );
/*
    {
        FILE_FS_SIZE_INFORMATION *SizeInfo = (FILE_FS_SIZE_INFORMATION*)FileInformation;

        SizeInfo->TotalAllocationUnits.QuadPart     = 0x4C468;
        SizeInfo->AvailableAllocationUnits.QuadPart = 0x2F125;
        SizeInfo->SectorsPerAllocationUnit          = 32;
        SizeInfo->BytesPerSector                    = 512;
    }
*/
    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00DA - NtReadFile
// ******************************************************************
XBSYSAPI EXPORTNUM(219) NTSTATUS NTAPI xboxkrnl::NtReadFile
(
	IN  HANDLE          FileHandle,            // TODO: correct paramters
	IN  HANDLE          Event OPTIONAL,
	IN  PVOID           ApcRoutine OPTIONAL,
	IN  PVOID           ApcContext,
	OUT PVOID           IoStatusBlock,
	OUT PVOID           Buffer,
	IN  ULONG           Length,
	IN  PLARGE_INTEGER  ByteOffset OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtReadFile\n"
               "(\n"
               "   FileHandle          : 0x%.08X\n"
               "   Event               : 0x%.08X\n"
               "   ApcRoutine          : 0x%.08X\n"
               "   ApcContext          : 0x%.08X\n"
               "   IoStatusBlock       : 0x%.08X\n"
               "   Buffer              : 0x%.08X\n"
               "   Length              : 0x%.08X\n"
               "   ByteOffset          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, Event, ApcRoutine, 
               ApcContext, IoStatusBlock, Buffer, Length, ByteOffset);
    }
    #endif

    // Halo NTSC, Buffer == 0x09740040
    // Possibly grabbing the vram cache from Xeon
    //if(Buffer == (PVOID)0x09740040)
    //    _asm int 3

    NTSTATUS ret = NtDll::NtReadFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, (NtDll::LARGE_INTEGER*)ByteOffset, 0);

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" NTSTATUS NTAPI EmuNtReleaseMutant
(
    IN HANDLE MutantHandle,
    OUT PLONG PreviousCount OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    NTSTATUS ret = 0xC0000002;

    if(NtDll::NtReleaseMutant != 0)
        ret = NtDll::NtReleaseMutant(MutantHandle, PreviousCount);

    if(ret != STATUS_SUCCESS)
        printf("EmuKrnl (0x%X): NtReleaseMutant failed with status 0x%.08X.\n", (uint32)GetCurrentThreadId(), (uint32)ret);

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" NTSTATUS NTAPI EmuNtReleaseSemaphore
(
    IN HANDLE SemaphoreHandle,
    IN LONG ReleaseCount,
    OUT PLONG PreviousCount OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    NTSTATUS ret = 0xC0000002;

    if(NtDll::NtReleaseSemaphore != 0)
        ret = NtDll::NtReleaseSemaphore(SemaphoreHandle, ReleaseCount, PreviousCount);

    if(ret != STATUS_SUCCESS)
        printf("EmuKrnl (0x%X): NtReleaseSemaphore failed with status 0x%.08X.\n", (uint32)GetCurrentThreadId(), (uint32)ret);

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" NTSTATUS NTAPI EmuNtOpenSymbolicLinkObject
(
    OUT PHANDLE LinkHandle,
    IN  POBJECT_ATTRIBUTES ObjectAttributes
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(LinkHandle == NULL || ObjectAttributes == NULL || ObjectAttributes->ObjectName == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return 0xC000000D;
    }

    *LinkHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
    if(*LinkHandle == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return 0xC000009A;
    }

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtOpenDirectoryObject
(
    OUT PHANDLE DirectoryHandle,
    IN  POBJECT_ATTRIBUTES ObjectAttributes
)
{
    if(DirectoryHandle != 0)
        *DirectoryHandle = 0;

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

// ******************************************************************
// * 0x00E0 - NtResumeThread
// ******************************************************************
XBSYSAPI EXPORTNUM(224) NTSTATUS NTAPI xboxkrnl::NtResumeThread
(
    IN  HANDLE ThreadHandle,
    OUT PULONG PreviousSuspendCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtResumeThread\n"
               "(\n"
               "   ThreadHandle         : 0x%.08X\n"
               "   PreviousSuspendCount : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ThreadHandle, PreviousSuspendCount);
    }
    #endif

    NTSTATUS ret = STATUS_SUCCESS;
    if(!EmuIsValidHostThread(ThreadHandle))
    {
        ret = 0xC0000008;
    }
    else
    {
        ULONG &SuspendCount = EmuThreadSuspendCountForHandle(ThreadHandle);
        ULONG PreviousCount = SuspendCount;

        if(SuspendCount != 0)
        {
            ResumeThread(ThreadHandle);
            SuspendCount--;
        }

        if(PreviousSuspendCount != NULL)
            *PreviousSuspendCount = PreviousCount;
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00E7 - NtSuspendThread
// ******************************************************************
extern "C" NTSTATUS NTAPI EmuNtSuspendThread
(
    IN  HANDLE ThreadHandle,
    OUT PULONG PreviousSuspendCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    NTSTATUS ret = STATUS_SUCCESS;
    if(!EmuIsValidHostThread(ThreadHandle))
    {
        ret = 0xC0000008;
    }
    else
    {
        ULONG &SuspendCount = EmuThreadSuspendCountForHandle(ThreadHandle);
        ULONG PreviousCount = SuspendCount;

        if(PreviousSuspendCount != NULL)
            *PreviousSuspendCount = PreviousCount;

        if(SuspendCount >= 0x7F)
        {
            ret = EmuStatusSuspendCountExceeded;
        }
        else
        {
            SuspendThread(ThreadHandle);
            SuspendCount++;
        }
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00E1 - NtSetEvent
// ******************************************************************
XBSYSAPI EXPORTNUM(225) NTSTATUS NTAPI xboxkrnl::NtSetEvent
(
    IN  HANDLE EventHandle,
    OUT PLONG  PreviousState
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtSetEvent\n"
               "(\n"
               "   EventHandle          : 0x%.08X\n"
               "   PreviousState        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), EventHandle, PreviousState);
    }
    #endif

    NTSTATUS ret = NtDll::NtSetEvent(EventHandle, PreviousState);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00E2 - NtSetInformationFile
// ******************************************************************
XBSYSAPI EXPORTNUM(226) NTSTATUS NTAPI xboxkrnl::NtSetInformationFile
(	
	IN  HANDLE  FileHandle,            // TODO: correct paramters
	OUT	PVOID	IoStatusBlock,
	IN	PVOID	FileInformation,
	IN	ULONG	Length,
	IN	ULONG	FileInformationClass
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtSetInformationFile\n"
               "(\n"
               "   FileHandle           : 0x%.08X\n"
               "   IoStatusBlock        : 0x%.08X\n"
               "   FileInformation      : 0x%.08X\n"
               "   Length               : 0x%.08X\n"
               "   FileInformationClass : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, IoStatusBlock, FileInformation, 
               Length, FileInformationClass);
    }
    #endif

    NTSTATUS ret = NtDll::NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00E8 - NtUserIoApcDispatcher
// ******************************************************************
XBSYSAPI EXPORTNUM(232) VOID NTAPI xboxkrnl::NtUserIoApcDispatcher
(
    PVOID            ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG            Reserved
)
{
    // Note: This function is called within Win2k/XP context, so no EmuSwapFS here

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtUserIoApcDispatcher\n"
               "(\n"
               "   ApcContext           : 0x%.08X\n"
               "   IoStatusBlock        : 0x%.08X\n"
               "   Reserved             : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ApcContext, IoStatusBlock, Reserved);
    }
    #endif

    EmuSwapFS();   // Xbox FS

    __asm
    {
        pushad

        mov esi, IoStatusBlock
        mov ecx, [esi]
        mov eax, 0x0C0000000

        push esi
        push ecx
        push eax
        call ApcContext

        popad
    }

    EmuSwapFS();   // Win2k/XP FS

    #ifdef _DEBUG_TRACE
    printf("EmuKrnl (0x%X): NtUserIoApcDispatcher Completed\n", GetCurrentThreadId());
    #endif

    return;
}

// ******************************************************************
// * 0x00E9 - NtWaitForSingleObject
// ******************************************************************
XBSYSAPI EXPORTNUM(233) NTSTATUS NTAPI xboxkrnl::NtWaitForSingleObject
(
    IN  HANDLE  Handle,
    IN  BOOLEAN Alertable,
    IN  PVOID   Timeout
)
{
    EmuSwapFS();   // Win2k/XP FS

    NTSTATUS ret = NtDll::NtWaitForSingleObject(Handle, Alertable, (NtDll::PLARGE_INTEGER)Timeout);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00EA - NtWaitForSingleObjectEx
// ******************************************************************
XBSYSAPI EXPORTNUM(234) NTSTATUS NTAPI xboxkrnl::NtWaitForSingleObjectEx
(
    IN  HANDLE          Handle,
    IN  DWORD           WaitMode,
    IN  BOOLEAN         Alertable,
    IN  PLARGE_INTEGER  Timeout
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtWaitForSingleObjectEx\n"
               "(\n"
               "   Handle               : 0x%.08X\n"
               "   WaitMode             : 0x%.08X\n"
               "   Alertable            : 0x%.08X\n"
               "   Timeout              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Handle, WaitMode, Alertable, Timeout);
    }
    #endif

    NTSTATUS ret = NtDll::NtWaitForSingleObject(Handle, Alertable, (NtDll::PLARGE_INTEGER)Timeout);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x00EC - NtWriteFile
// ******************************************************************
XBSYSAPI EXPORTNUM(236) NTSTATUS NTAPI xboxkrnl::NtWriteFile
(	
	IN  HANDLE  FileHandle,            // TODO: correct paramters
	IN	PVOID	Event,
	IN	PVOID	ApcRoutine,
	IN	PVOID	ApcContext,
	OUT	PVOID	IoStatusBlock,
	IN	PVOID	Buffer,
	IN	ULONG	Length,
	IN	PVOID	ByteOffset
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): NtWriteFile\n"
               "(\n"
               "   FileHandle          : 0x%.08X\n"
               "   Event               : 0x%.08X\n"
               "   ApcRoutine          : 0x%.08X\n"
               "   ApcContext          : 0x%.08X\n"
               "   IoStatusBlock       : 0x%.08X\n"
               "   Buffer              : 0x%.08X\n"
               "   Length              : 0x%.08X\n"
               "   ByteOffset          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), FileHandle, Event, ApcRoutine, 
               ApcContext, IoStatusBlock, Buffer, Length, ByteOffset);
    }
    #endif

    NTSTATUS ret = NtDll::NtWriteFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, (NtDll::LARGE_INTEGER*)ByteOffset, 0);

    EmuSwapFS();   // Xbox FS

    if(ApcRoutine != NULL)
        EmuWarning("NtWriteFile has an ApcRoutine that is ignored!");

    if(FAILED(ret))
        EmuWarning("NtWriteFile Failed! (0x%.08X)", ret);

    return ret;
}

// ******************************************************************
// * 0x00EE - NtYieldExecution
// ******************************************************************
XBSYSAPI EXPORTNUM(238) VOID NTAPI xboxkrnl::NtYieldExecution()
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        // NOTE: this eats up the debug log far too quickly
        //printf("EmuKrnl (0x%X): NtYieldExecution();\n", GetCurrentThreadId());
    }
    #endif

    NtDll::NtYieldExecution();

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x00FE - PsCreateSystemThread
// ******************************************************************
extern "C" NTSTATUS NTAPI EmuPsCreateSystemThread
(
    OUT PHANDLE                 ThreadHandle,
    IN  ULONG                   ThreadExtraSize,
    IN  xboxkrnl::PKSTART_ROUTINE StartRoutine,
    IN  PVOID                   StartContext,
    IN  BOOLEAN                 CreateSuspended
)
{
    return xboxkrnl::PsCreateSystemThreadEx(ThreadHandle, ThreadExtraSize, 0, 0, NULL,
                                            StartContext, NULL, CreateSuspended, FALSE, StartRoutine);
}

// ******************************************************************
// * 0x00FF - PsCreateSystemThreadEx
// ******************************************************************
XBSYSAPI EXPORTNUM(255) NTSTATUS NTAPI xboxkrnl::PsCreateSystemThreadEx
(
    OUT PHANDLE         ThreadHandle,
    IN  ULONG           ThreadExtraSize,
    IN  ULONG           KernelStackSize,
    IN  ULONG           TlsDataSize,
    OUT PULONG          ThreadId OPTIONAL,
    IN  PVOID           StartContext1,
    IN  PVOID           StartContext2,
    IN  BOOLEAN         CreateSuspended,
    IN  BOOLEAN         DebugStack,
    IN  PKSTART_ROUTINE StartRoutine
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): PsCreateSystemThreadEx\n"
               "(\n"
               "   ThreadHandle        : 0x%.08X\n"
               "   ThreadExtraSize     : 0x%.08X\n"
               "   KernelStackSize     : 0x%.08X\n"
               "   TlsDataSize         : 0x%.08X\n"
               "   ThreadId            : 0x%.08X\n"
               "   StartContext1       : 0x%.08X\n"
               "   StartContext2       : 0x%.08X\n"
               "   CreateSuspended     : 0x%.08X\n"
               "   DebugStack          : 0x%.08X\n"
               "   StartRoutine        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ThreadHandle, ThreadExtraSize, KernelStackSize, TlsDataSize, ThreadId,
               StartContext1, StartContext2, CreateSuspended, DebugStack, StartRoutine);
    }
    #endif

    // ******************************************************************
    // * create thread, using our special proxy technique
    // ******************************************************************
    {
        DWORD dwThreadId;

        // PCSTProxy is responsible for cleaning up this pointer
        ::PCSTProxyParam *iPCSTProxyParam = new ::PCSTProxyParam();

        iPCSTProxyParam->StartContext1 = StartContext1;
        iPCSTProxyParam->StartContext2 = StartContext2;
        iPCSTProxyParam->StartRoutine  = StartRoutine;

        *ThreadHandle = CreateThread(NULL, NULL, &PCSTProxy, iPCSTProxyParam, CreateSuspended ? CREATE_SUSPENDED : NULL, &dwThreadId);

        if(*ThreadHandle != NULL)
            g_EmuThreadSuspendCounts[*ThreadHandle] = CreateSuspended ? 1 : 0;

        if(ThreadId != NULL)
            *ThreadId = dwThreadId;
    }

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

// ******************************************************************
// * 0x0102 - PsTerminateSystemThread
// ******************************************************************
XBSYSAPI EXPORTNUM(258) VOID NTAPI xboxkrnl::PsTerminateSystemThread(IN NTSTATUS ExitStatus)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): PsTerminateSystemThread\n"
               "(\n"
               "   ExitStatus          : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), ExitStatus);
    }
    #endif

    ExitThread(ExitStatus);

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x0104 - RtlAnsiStringToUnicodeString
// ******************************************************************
XBSYSAPI EXPORTNUM(260) NTSTATUS NTAPI xboxkrnl::RtlAnsiStringToUnicodeString
(
    PUNICODE_STRING DestinationString,
    PSTRING         SourceString,
    UCHAR           AllocateDestinationString
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): RtlAnsiStringToUnicodeString\n"
               "(\n"
               "   DestinationString         : 0x%.08X\n"
               "   SourceString              : 0x%.08X\n"
               "   AllocateDestinationString : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), DestinationString, SourceString, AllocateDestinationString);
    }
    #endif

    NTSTATUS ret = NtDll::RtlAnsiStringToUnicodeString((NtDll::UNICODE_STRING*)DestinationString, (NtDll::STRING*)SourceString, AllocateDestinationString);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x0115 RtlEnterCriticalSection
// ******************************************************************
XBSYSAPI EXPORTNUM(277) VOID NTAPI xboxkrnl::RtlEnterCriticalSection
(
  IN PRTL_CRITICAL_SECTION CriticalSection
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): RtlEnterCriticalSection\n"
               "(\n"
               "   CriticalSection     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), CriticalSection);
    }
    #endif

    //printf("CriticalSection->LockCount : %d\n", CriticalSection->LockCount);

    // This seems redundant, but xbox software doesn't always do it
    //if(CriticalSection->LockCount == -1)
        NtDll::RtlInitializeCriticalSection((NtDll::_RTL_CRITICAL_SECTION*)CriticalSection);

    NtDll::RtlEnterCriticalSection((NtDll::_RTL_CRITICAL_SECTION*)CriticalSection);

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x0121 - RtlInitAnsiString
// ******************************************************************
XBSYSAPI EXPORTNUM(289) VOID NTAPI xboxkrnl::RtlInitAnsiString 
(
  IN OUT PANSI_STRING DestinationString,
  IN     PCSZ         SourceString
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): RtlInitAnsiString\n"
               "(\n"
               "   DestinationString   : 0x%.08X\n"
               "   SourceString        : 0x%.08X (\"%s\")\n"
               ");\n",
               GetCurrentThreadId(), DestinationString, SourceString, SourceString);
    }
    #endif

    NtDll::RtlInitAnsiString((NtDll::PANSI_STRING)DestinationString, (NtDll::PCSZ)SourceString);

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x0123 - RtlInitializeCriticalSection
// ******************************************************************
XBSYSAPI EXPORTNUM(291) VOID NTAPI xboxkrnl::RtlInitializeCriticalSection
(
  IN PRTL_CRITICAL_SECTION CriticalSection
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): RtlInitializeCriticalSection\n"
               "(\n"
               "   CriticalSection     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), CriticalSection);
    }
    #endif

    NtDll::RtlInitializeCriticalSection((NtDll::_RTL_CRITICAL_SECTION*)CriticalSection);

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x0126 RtlEnterCriticalSection
// ******************************************************************
XBSYSAPI EXPORTNUM(294) VOID NTAPI xboxkrnl::RtlLeaveCriticalSection
(
  IN PRTL_CRITICAL_SECTION CriticalSection
)
{
    EmuSwapFS();   // Win2k/XP FS

    // Note: We need to execute this before debug output to avoid trouble
    NtDll::RtlLeaveCriticalSection((NtDll::_RTL_CRITICAL_SECTION*)CriticalSection);

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): RtlLeaveCriticalSection\n"
               "(\n"
               "   CriticalSection     : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), CriticalSection);
    }
    #endif

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x012D - RtlNtStatusToDosError
// ******************************************************************
XBSYSAPI EXPORTNUM(301) xboxkrnl::ULONG NTAPI xboxkrnl::RtlNtStatusToDosError
(
    IN NTSTATUS Status
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): RtlNtStatusToDosError\n"
               "(\n"
               "   Status              : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Status);
    }
    #endif

    ULONG ret = NtDll::RtlNtStatusToDosError(Status);

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x0134 - RtlUnicodeStringToAnsiString
// ******************************************************************
XBSYSAPI EXPORTNUM(308) xboxkrnl::NTSTATUS NTAPI xboxkrnl::RtlUnicodeStringToAnsiString
(
    IN OUT PSTRING         DestinationString,
    IN     PUNICODE_STRING SourceString,
    IN     BOOLEAN         AllocateDestinationString
)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): RtlUnicodeStringToAnsiString\n"
               "(\n"
               "   DestinationString         : 0x%.08X\n"
               "   SourceString              : 0x%.08X\n"
               "   AllocateDestinationString : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), DestinationString, SourceString, AllocateDestinationString);
    }
    #endif

    const NTSTATUS StatusBufferOverflow = (NTSTATUS)0x80000005L;
    const NTSTATUS StatusInvalidParameter = (NTSTATUS)0xC000000DL;
    NTSTATUS ret = STATUS_SUCCESS;

    if(DestinationString == NULL || SourceString == NULL)
    {
        ret = StatusInvalidParameter;
    }
    else
    {
        const USHORT SourceCharacters = SourceString->Length / sizeof(WCHAR);

        if(AllocateDestinationString)
        {
            DestinationString->Length = 0;
            DestinationString->MaximumLength = SourceCharacters + 1;
            DestinationString->Buffer = (PCHAR)HeapAlloc(GetProcessHeap(), 0, DestinationString->MaximumLength);

            if(DestinationString->Buffer == NULL)
                ret = STATUS_NO_MEMORY;
        }

        if(ret == STATUS_SUCCESS)
        {
            USHORT CopyCharacters = 0;

            if(DestinationString->MaximumLength == 0)
            {
                ret = StatusBufferOverflow;
            }
            else
            {
                CopyCharacters = SourceCharacters;

                if(CopyCharacters >= DestinationString->MaximumLength)
                {
                    CopyCharacters = DestinationString->MaximumLength - 1;
                    ret = StatusBufferOverflow;
                }

                for(USHORT Index = 0; Index < CopyCharacters; Index++)
                {
                    const WCHAR SourceCharacter = SourceString->Buffer[Index];
                    DestinationString->Buffer[Index] = (SourceCharacter <= 0xFF) ? (CHAR)SourceCharacter : '?';
                }

                DestinationString->Buffer[CopyCharacters] = '\0';
            }

            DestinationString->Length = CopyCharacters;
        }
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

// ******************************************************************
// * 0x0141 - XboxHardwareInfo
// ******************************************************************
XBSYSAPI EXPORTNUM(322) XBOX_HARDWARE_INFO xboxkrnl::XboxHardwareInfo = 
{
    0,
    0,0,0,0
};

// ******************************************************************
// * XboxSignatureKey
// ******************************************************************
XBSYSAPI EXPORTNUM(325) xboxkrnl::BYTE xboxkrnl::XboxSignatureKey[16] =
{
    // cxbx default saved game key
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

// ******************************************************************
// * 0x0144 - XboxKrnlVersion
// ******************************************************************
namespace xboxkrnl
{
    struct EmuXboxKernelVersion
    {
        USHORT Major;
        USHORT Minor;
        USHORT Build;
        USHORT Qfe;
    };

    EmuXboxKernelVersion EmuXboxKrnlVersion =
    {
        1,
        0,
        5838,
        1
    };
}

// ******************************************************************
// * 0x0146 - XeImageFileName
// ******************************************************************
static char g_XeImageFileNameBuffer[] = "D:\\xboxkrnl.exe";
namespace xboxkrnl
{
    STRING EmuXeImageFileName =
    {
        sizeof(g_XeImageFileNameBuffer) - 1,
        sizeof(g_XeImageFileNameBuffer),
        g_XeImageFileNameBuffer
    };
}

// ******************************************************************
// * XcSHAInit
// ******************************************************************
XBSYSAPI EXPORTNUM(335) VOID NTAPI xboxkrnl::XcSHAInit(UCHAR *pbSHAContext)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): XcSHAInit\n"
               "(\n"
               "   pbSHAContext        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pbSHAContext);
    }
    #endif

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * XcSHAUpdate
// ******************************************************************
XBSYSAPI EXPORTNUM(336) VOID NTAPI xboxkrnl::XcSHAUpdate(UCHAR *pbSHAContext, UCHAR *pbInput, ULONG dwInputLength)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): XcSHAUpdate\n"
               "(\n"
               "   pbSHAContext        : 0x%.08X\n"
               "   pbInput             : 0x%.08X\n"
               "   dwInputLength       : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pbSHAContext, pbInput, dwInputLength);
    }
    #endif

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * XcSHAFinal
// ******************************************************************
XBSYSAPI EXPORTNUM(337) VOID NTAPI xboxkrnl::XcSHAFinal(UCHAR *pbSHAContext, UCHAR *pbDigest)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): XcSHAFinal\n"
               "(\n"
               "   pbSHAContext        : 0x%.08X\n"
               "   pbDigest            : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), pbSHAContext, pbDigest);
    }
    #endif

    // for now, the digest is always zeros (we dont care!)
    for(int v=0;v<16;v++)
        pbDigest[v] = 0;

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * HalBootSMCVideoMode
// ******************************************************************
// TODO: Verify this!
XBSYSAPI EXPORTNUM(356) xboxkrnl::DWORD xboxkrnl::HalBootSMCVideoMode = 1;
