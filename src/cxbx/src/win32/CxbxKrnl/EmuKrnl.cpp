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

#include <cstdio>
#include <clocale>
#include <cstring>
#include <cstdarg>
#include <malloc.h>
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
extern "C" ULONG g_EmuDisplayPitch;   // defined in Emu.cpp; scanout capture width

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
static const ULONG EmuObjectBodyOffset = 16;
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
static std::map<PVOID, BYTE*> g_EmuObjectAllocations;
static std::map<PVOID, HANDLE> g_EmuObjectHandles;
static std::map<PVOID, std::string> g_EmuObjectNames;
static std::map<std::string, PVOID> g_EmuNamedObjects;
static PVOID g_EmuPsThreadNotifyRoutines[8] = {};
static ULONG g_EmuPsThreadNotifyRoutineCount = 0;

extern "C"
{
ULONG g_EmuObpObjectHandleTable[4] = {};
ULONG g_EmuHalDiskCachePartitionCount = 2;
CHAR g_EmuHalDiskModelNumber[32] = "Cxbx Virtual Disk";
CHAR g_EmuHalDiskSerialNumber[32] = "CXBX000000000001";
UCHAR g_EmuKdDebuggerEnabled = FALSE;
UCHAR g_EmuKdDebuggerNotPresent = TRUE;
UCHAR g_EmuXboxEEPROMKey[16] = {
    0x43, 0x78, 0x62, 0x78, 0x45, 0x45, 0x50, 0x52,
    0x4F, 0x4D, 0x4B, 0x65, 0x79, 0x30, 0x30, 0x31
};
UCHAR g_EmuXboxHDKey[16] = {
    0x43, 0x78, 0x62, 0x78, 0x48, 0x44, 0x4B, 0x65,
    0x79, 0x56, 0x69, 0x72, 0x74, 0x30, 0x30, 0x31
};
UCHAR g_EmuXboxLANKey[16] = {
    0x43, 0x78, 0x62, 0x78, 0x4C, 0x41, 0x4E, 0x4B,
    0x65, 0x79, 0x56, 0x69, 0x72, 0x74, 0x30, 0x31
};
UCHAR g_EmuXboxAlternateSignatureKeys[32] = {
    0x43, 0x78, 0x62, 0x78, 0x41, 0x6C, 0x74, 0x53,
    0x69, 0x67, 0x4B, 0x65, 0x79, 0x30, 0x30, 0x31,
    0x43, 0x78, 0x62, 0x78, 0x41, 0x6C, 0x74, 0x53,
    0x69, 0x67, 0x4B, 0x65, 0x79, 0x30, 0x30, 0x32
};
UCHAR g_EmuXePublicKeyData[160] = {
    'R', 'S', 'A', '1',
    0x88, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x01, 0x00,
    0xC7
};
ULONG g_EmuIdexChannelObject[16] = {};
}

static ULONG g_EmuHalTrayState = 0x10;
static ULONG g_EmuHalTrayStateChangeCount = 0;
static ULONG g_EmuHalDisabledInterruptMask = 0;
static ULONG g_EmuHalSoftwareInterruptMask = 0;
static BOOLEAN g_EmuSecureTrayEjectEnabled = FALSE;
static BOOLEAN g_EmuResetOrShutdownPending = FALSE;
static BOOLEAN g_EmuShutdownInitiated = FALSE;
static ULONG g_EmuLastSmcScratchValue = 0;
static ULONG g_EmuPhyInitialized = 0;
static ULONG g_EmuPhyLinkState = 0;
static ULONG g_EmuFscCachePages = 0;

static const NTSTATUS EmuStatusInvalidParameter = (NTSTATUS)0xC000000D;
static const NTSTATUS EmuStatusInvalidDeviceRequest = (NTSTATUS)0xC0000010;
static const NTSTATUS EmuStatusObjectNameInvalid = (NTSTATUS)0xC0000033;
static const NTSTATUS EmuStatusObjectPathNotFound = (NTSTATUS)0xC000003A;
static const NTSTATUS EmuStatusObjectTypeMismatch = (NTSTATUS)0xC0000024;
static const NTSTATUS EmuStatusSuspendCountExceeded = (NTSTATUS)0xC000004A;
static const NTSTATUS EmuStatusInsufficientResources = (NTSTATUS)0xC000009A;
static const NTSTATUS EmuStatusInvalidHandle = (NTSTATUS)0xC0000008;

extern "C" VOID NTAPI EmuDbgBreakPoint()
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): DbgBreakPoint ignored.\n", GetCurrentThreadId());

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuDbgBreakPointWithStatus(ULONG Status)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): DbgBreakPointWithStatus status=0x%.08lX ignored.\n",
           GetCurrentThreadId(), Status);

    EmuSwapFS();   // Xbox FS
}

extern "C" ULONG NTAPI EmuDbgPrompt(PCHAR Prompt, PCHAR Response, ULONG Length)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Prompt != NULL)
        printf("EmuKrnl (0x%lX): DbgPrompt prompt=\"%s\".\n", GetCurrentThreadId(), Prompt);

    if(Response != NULL && Length != 0)
        Response[0] = '\0';

    EmuSwapFS();   // Xbox FS

    return 0;
}

static bool EmuIsWritableMemoryRange(PVOID Address, SIZE_T Size)
{
    if(Address == NULL || Size == 0)
        return false;

    ULONG_PTR Current = (ULONG_PTR)Address;
    ULONG_PTR End = Current + Size;
    if(End < Current)
        return false;

    while(Current < End)
    {
        MEMORY_BASIC_INFORMATION MemoryInfo;
        if(VirtualQuery((PVOID)Current, &MemoryInfo, sizeof(MemoryInfo)) != sizeof(MemoryInfo))
            return false;

        if(MemoryInfo.State != MEM_COMMIT || (MemoryInfo.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
            return false;

        switch(MemoryInfo.Protect & 0xFF)
        {
            case PAGE_READWRITE:
            case PAGE_WRITECOPY:
            case PAGE_EXECUTE_READWRITE:
            case PAGE_EXECUTE_WRITECOPY:
                break;

            default:
                return false;
        }

        ULONG_PTR RegionEnd = (ULONG_PTR)MemoryInfo.BaseAddress + MemoryInfo.RegionSize;
        if(RegionEnd <= Current)
            return false;

        Current = RegionEnd;
    }

    return true;
}

typedef int (__cdecl *EmuGuestExceptionHandler)(PEXCEPTION_RECORD ExceptionRecord, void *EstablisherFrame, PCONTEXT ContextRecord, void *DispatcherContext);

static bool EmuRaiseGuestExceptionRecord(PEXCEPTION_RECORD ExceptionRecord, ULONG GuestEip, ULONG GuestEsp, ULONG GuestEbp)
{
    void *Registration = NULL;

    __asm
    {
        mov eax, fs:[0]
        mov Registration, eax
    }

    EXCEPTION_RECORD LocalExceptionRecord;
    if(ExceptionRecord != NULL)
        LocalExceptionRecord = *ExceptionRecord;
    else
        ZeroMemory(&LocalExceptionRecord, sizeof(LocalExceptionRecord));

    LocalExceptionRecord.ExceptionAddress = (PVOID)GuestEip;

    CONTEXT ContextRecord;
    ZeroMemory(&ContextRecord, sizeof(ContextRecord));
    ContextRecord.ContextFlags = CONTEXT_CONTROL;
    ContextRecord.Eip = GuestEip;
    ContextRecord.Esp = GuestEsp;
    ContextRecord.Ebp = GuestEbp;

    while(Registration != NULL && Registration != (void*)-1)
    {
        EmuGuestExceptionHandler Handler = *(EmuGuestExceptionHandler*)((uint08*)Registration + 4);
        if(Handler != NULL)
        {
            int Disposition = Handler(&LocalExceptionRecord, Registration, &ContextRecord, NULL);
            if(Disposition != ExceptionContinueSearch)
                return true;
        }

        Registration = *(void**)Registration;
    }

    return false;
}

static bool EmuRaiseGuestException(NTSTATUS Status, ULONG GuestEip, ULONG GuestEsp, ULONG GuestEbp)
{
    EXCEPTION_RECORD ExceptionRecord;
    ZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
    ExceptionRecord.ExceptionCode = Status;

    return EmuRaiseGuestExceptionRecord(&ExceptionRecord, GuestEip, GuestEsp, GuestEbp);
}

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

static EmuObjectHeader *EmuObjectHeaderFromObject(PVOID Object)
{
    if(Object == NULL || Object == EmuGetCurrentThread())
        return NULL;

    EmuObjectHeader *Header = (EmuObjectHeader*)((BYTE*)Object - EmuObjectBodyOffset);
    if(!EmuIsWritableMemoryRange(Header, sizeof(*Header)))
        return NULL;

    if(Header->Flags != EmuGenericObjectMagic && Header->Flags != EmuThreadObjectMagic)
        return NULL;

    return Header;
}

static bool EmuObjectTypeMatches(PVOID Object, PVOID ObjectType)
{
    if(ObjectType == NULL)
        return true;

    if(Object == EmuGetCurrentThread())
        return EmuIsThreadObjectType(ObjectType);

    EmuObjectHeader *Header = EmuObjectHeaderFromObject(Object);
    if(Header == NULL)
        return false;

    return Header->Type == ObjectType || (Header->Flags == EmuThreadObjectMagic && EmuIsThreadObjectType(ObjectType));
}

static void EmuReleaseObjectStorage(PVOID Object, EmuObjectHeader *Header)
{
    g_EmuObjectHandles.erase(Object);

    auto NameEntry = g_EmuObjectNames.find(Object);
    if(NameEntry != g_EmuObjectNames.end())
    {
        g_EmuNamedObjects.erase(NameEntry->second);
        g_EmuObjectNames.erase(NameEntry);
    }

    auto AllocationEntry = g_EmuObjectAllocations.find(Object);
    if(AllocationEntry != g_EmuObjectAllocations.end())
    {
        BYTE *Allocation = AllocationEntry->second;
        g_EmuObjectAllocations.erase(AllocationEntry);
        delete[] Allocation;
        return;
    }

    if(Header->Flags == EmuThreadObjectMagic)
        delete (EmuThreadObjectHeader*)Header;
    else
        delete Header;
}

static NTSTATUS EmuOpenHandleForObject(PVOID Object, HANDLE *Handle)
{
    if(Handle == NULL)
        return EmuStatusInvalidParameter;

    *Handle = INVALID_HANDLE_VALUE;

    if(Object == NULL)
        return EmuStatusInvalidParameter;

    HANDLE SourceHandle = NULL;
    if(Object == EmuGetCurrentThread())
        SourceHandle = GetCurrentThread();
    else
    {
        auto ExistingHandle = g_EmuObjectHandles.find(Object);
        if(ExistingHandle != g_EmuObjectHandles.end())
            SourceHandle = ExistingHandle->second;

        EmuObjectHeader *Header = EmuObjectHeaderFromObject(Object);
        if(Header != NULL && Header->Flags == EmuThreadObjectMagic)
            SourceHandle = ((EmuThreadObjectHeader*)Header)->HostHandle;
    }

    HANDLE NewHandle = NULL;
    if(SourceHandle != NULL && SourceHandle != INVALID_HANDLE_VALUE &&
       DuplicateHandle(GetCurrentProcess(), SourceHandle, GetCurrentProcess(), &NewHandle, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        *Handle = NewHandle;
        return STATUS_SUCCESS;
    }

    NewHandle = CreateEventA(NULL, TRUE, FALSE, NULL);
    if(NewHandle == NULL)
        return EmuStatusInsufficientResources;

    *Handle = NewHandle;
    g_EmuObjectHandles[Object] = NewHandle;

    EmuObjectHeader *Header = EmuObjectHeaderFromObject(Object);
    if(Header != NULL)
        Header->HandleCount++;

    return STATUS_SUCCESS;
}

static bool EmuObjectAttributesName(xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes, std::string *Name)
{
    return ObjectAttributes != NULL && EmuObjectStringToStdString(ObjectAttributes->ObjectName, Name);
}

static bool EmuObjectAttributesToHostPath(xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes, std::string *Path)
{
    if(!EmuObjectAttributesName(ObjectAttributes, Path))
        return false;

    if(Path->compare(0, 4, "\\??\\") == 0)
        Path->erase(0, 4);

    return !Path->empty();
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

extern "C" NTSTATUS NTAPI EmuObCreateObject
(
    IN  PVOID ObjectType,
    IN  xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
    IN  ULONG ObjectSize,
    OUT PVOID *Object
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Object == NULL || ObjectType == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInvalidParameter;
    }

    *Object = NULL;

    const ULONG AllocationSize = EmuObjectBodyOffset + ((ObjectSize != 0) ? ObjectSize : sizeof(ULONGLONG));
    BYTE *Allocation = new BYTE[AllocationSize];
    if(Allocation == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInsufficientResources;
    }

    ZeroMemory(Allocation, AllocationSize);
    EmuObjectHeader *Header = (EmuObjectHeader*)Allocation;
    Header->PointerCount = 1;
    Header->HandleCount = 0;
    Header->Type = ObjectType;
    Header->Flags = EmuGenericObjectMagic;

    *Object = (PVOID)(Allocation + EmuObjectBodyOffset);
    g_EmuObjectAllocations[*Object] = Allocation;

    std::string Name;
    if(EmuObjectAttributesName(ObjectAttributes, &Name))
        g_EmuObjectNames[*Object] = Name;

    printf("EmuKrnl (0x%lX): ObCreateObject type=%p size=0x%.08lX object=%p.\n",
           GetCurrentThreadId(), ObjectType, ObjectSize, *Object);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuObInsertObject
(
    IN  PVOID Object,
    IN  xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
    IN  ULONG ObjectPointerBias,
    OUT PHANDLE Handle
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuObjectHeader *Header = EmuObjectHeaderFromObject(Object);
    if(Object == NULL || Header == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInvalidParameter;
    }

    std::string Name;
    bool HasName = EmuObjectAttributesName(ObjectAttributes, &Name);
    if(!HasName)
    {
        auto ExistingName = g_EmuObjectNames.find(Object);
        if(ExistingName != g_EmuObjectNames.end())
        {
            Name = ExistingName->second;
            HasName = !Name.empty();
        }
    }

    if(HasName)
    {
        if(!EmuIsValidObjectName(Name) && !EmuIsValidSymbolicLinkName(Name))
        {
            EmuSwapFS();   // Xbox FS
            return EmuStatusObjectNameInvalid;
        }

        if(g_EmuNamedObjects.find(Name) != g_EmuNamedObjects.end() && g_EmuNamedObjects[Name] != Object)
        {
            EmuSwapFS();   // Xbox FS
            return STATUS_OBJECT_NAME_COLLISION;
        }

        g_EmuNamedObjects[Name] = Object;
        g_EmuObjectNames[Object] = Name;
    }

    Header->PointerCount += 1 + (LONG)ObjectPointerBias;

    NTSTATUS Status = STATUS_SUCCESS;
    if(Handle != NULL)
        Status = EmuOpenHandleForObject(Object, Handle);

    printf("EmuKrnl (0x%lX): ObInsertObject object=%p name=\"%s\" handle=%p status=0x%.08lX.\n",
           GetCurrentThreadId(), Object, HasName ? Name.c_str() : "", Handle != NULL ? *Handle : NULL, Status);

    EmuSwapFS();   // Xbox FS

    return Status;
}

extern "C" VOID NTAPI EmuObMakeTemporaryObject(PVOID Object)
{
    EmuSwapFS();   // Win2k/XP FS

    auto NameEntry = g_EmuObjectNames.find(Object);
    if(NameEntry != g_EmuObjectNames.end())
    {
        g_EmuNamedObjects.erase(NameEntry->second);
        g_EmuObjectNames.erase(NameEntry);
    }

    EmuSwapFS();   // Xbox FS
}

extern "C" NTSTATUS NTAPI EmuObOpenObjectByPointer(PVOID Object, PVOID ObjectType, PHANDLE Handle)
{
    EmuSwapFS();   // Win2k/XP FS

    if(!EmuObjectTypeMatches(Object, ObjectType))
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusObjectTypeMismatch;
    }

    NTSTATUS Status = EmuOpenHandleForObject(Object, Handle);

    EmuSwapFS();   // Xbox FS

    return Status;
}

extern "C" NTSTATUS NTAPI EmuObReferenceObjectByPointer(PVOID Object, PVOID ObjectType);

extern "C" VOID __fastcall EmuObfDereferenceObject(PVOID Object)
{
    if(Object == NULL)
        return;

    if(Object == EmuGetCurrentThread())
        return;

    EmuObjectHeader *Header = EmuObjectHeaderFromObject(Object);
    if(Header == NULL)
        return;

    if(Header->PointerCount > 0)
        Header->PointerCount--;

    if(Header->PointerCount == 0 && Header->HandleCount == 0)
        EmuReleaseObjectStorage(Object, Header);
}

extern "C" VOID __fastcall EmuObfReferenceObject(PVOID Object)
{
    EmuObjectHeader *Header = EmuObjectHeaderFromObject(Object);
    if(Header != NULL)
        Header->PointerCount++;
}

extern "C" NTSTATUS NTAPI EmuObReferenceObjectByName
(
    IN  xboxkrnl::PSTRING ObjectName,
    IN  ULONG Attributes,
    IN  PVOID ObjectType,
    IN  PVOID ParseContext OPTIONAL,
    OUT PVOID *Object
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Object == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInvalidParameter;
    }

    *Object = NULL;

    std::string Name;
    if(!EmuObjectStringToStdString(ObjectName, &Name))
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusObjectNameInvalid;
    }

    auto Existing = g_EmuNamedObjects.find(Name);
    if(Existing != g_EmuNamedObjects.end())
    {
        if(!EmuObjectTypeMatches(Existing->second, ObjectType))
        {
            EmuSwapFS();   // Xbox FS
            return EmuStatusObjectTypeMismatch;
        }

        EmuObjectHeader *Header = EmuObjectHeaderFromObject(Existing->second);
        if(Header != NULL)
            Header->PointerCount++;

        *Object = Existing->second;

        EmuSwapFS();   // Xbox FS
        return STATUS_SUCCESS;
    }

    printf("EmuKrnl (0x%lX): ObReferenceObjectByName name=\"%s\" not found.\n",
           GetCurrentThreadId(), Name.c_str());

    EmuSwapFS();   // Xbox FS

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

extern "C" NTSTATUS NTAPI EmuObReferenceObjectByPointer(PVOID Object, PVOID ObjectType)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Object == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInvalidParameter;
    }

    if(!EmuObjectTypeMatches(Object, ObjectType))
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusObjectTypeMismatch;
    }

    EmuObjectHeader *Header = EmuObjectHeaderFromObject(Object);
    if(Header != NULL)
        Header->PointerCount++;

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
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

extern "C" VOID NTAPI EmuRtlAssert(PVOID FailedAssertion, PVOID FileName, ULONG LineNumber, PCHAR Message)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): RtlAssert assertion=%s file=%s line=%lu message=%s.\n",
           GetCurrentThreadId(),
           FailedAssertion != NULL ? (const char*)FailedAssertion : "",
           FileName != NULL ? (const char*)FileName : "",
           LineNumber,
           Message != NULL ? Message : "");

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuRtlRip(PVOID ApiName, PVOID Expression, PVOID Message, PVOID Address)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): RtlRip api=%s expression=%s message=%s address=%p.\n",
           GetCurrentThreadId(),
           ApiName != NULL ? (const char*)ApiName : "",
           Expression != NULL ? (const char*)Expression : "",
           Message != NULL ? (const char*)Message : "",
           Address);

    EmuSwapFS();   // Xbox FS
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

static USHORT EmuRtlUpperUshort(USHORT Value)
{
    if(Value >= 'a' && Value <= 'z')
        return Value - ('a' - 'A');

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

extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuRtlExtendedMagicDivide(xboxkrnl::LARGE_INTEGER Dividend,
                                                                    xboxkrnl::LARGE_INTEGER MagicDivisor,
                                                                    xboxkrnl::CCHAR ShiftCount)
{
    xboxkrnl::LARGE_INTEGER Result;

#if defined(__SIZEOF_INT128__)
    __int128 Product = (__int128)Dividend.QuadPart * (__int128)MagicDivisor.QuadPart;
    Result.QuadPart = (LONGLONG)(Product >> 64);
    if(ShiftCount > 0)
        Result.QuadPart >>= ShiftCount;
#else
    Result.QuadPart = (MagicDivisor.QuadPart != 0) ? (Dividend.QuadPart / MagicDivisor.QuadPart) : 0;
    if(ShiftCount > 0)
        Result.QuadPart >>= ShiftCount;
#endif

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

extern "C" VOID NTAPI EmuRtlMoveMemory(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    if(Destination == NULL || Source == NULL || Length == 0)
        return;

    memmove(Destination, Source, Length);
}

extern "C" VOID NTAPI EmuRtlZeroMemory(PVOID Destination, SIZE_T Length)
{
    if(Destination == NULL || Length == 0)
        return;

    memset(Destination, 0, Length);
}

extern "C" LONG NTAPI EmuRtlVsnprintf(PCHAR Buffer, SIZE_T Count, const char *Format, va_list Args)
{
    if(Buffer == NULL || Count == 0 || Format == NULL)
        return -1;

    int Written = vsnprintf(Buffer, Count, Format, Args);
    Buffer[Count - 1] = '\0';

    return (Written < 0) ? -1 : (LONG)Written;
}

extern "C" LONG NTAPI EmuRtlVsprintf(PCHAR Buffer, const char *Format, va_list Args)
{
    if(Buffer == NULL || Format == NULL)
        return -1;

    int Written = vsprintf(Buffer, Format, Args);
    return (Written < 0) ? -1 : (LONG)Written;
}

extern "C" LONG NTAPI EmuRtlSnprintf(PCHAR Buffer, SIZE_T Count, const char *Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    LONG Written = EmuRtlVsnprintf(Buffer, Count, Format, Args);
    va_end(Args);
    return Written;
}

extern "C" LONG NTAPI EmuRtlSprintf(PCHAR Buffer, const char *Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    LONG Written = EmuRtlVsprintf(Buffer, Format, Args);
    va_end(Args);
    return Written;
}

static bool EmuRtlFormatUnsignedInteger(ULONG Value, ULONG Base, char *Buffer, ULONG BufferLength)
{
    static const char Digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    if(Buffer == NULL || BufferLength == 0 || Base < 2 || Base > 36)
        return false;

    char Reversed[33];
    ULONG Count = 0;

    do
    {
        Reversed[Count++] = Digits[Value % Base];
        Value /= Base;
    } while(Value != 0 && Count < sizeof(Reversed));

    if(Count + 1 > BufferLength)
        return false;

    for(ULONG i = 0; i < Count; i++)
        Buffer[i] = Reversed[Count - i - 1];

    Buffer[Count] = '\0';
    return true;
}

extern "C" NTSTATUS NTAPI EmuRtlIntegerToChar(ULONG Value, ULONG Base, ULONG Length, PCHAR String)
{
    if(String == NULL)
        return EmuStatusInvalidParameter;

    if(Base == 0)
        Base = 10;

    if(Base != 2 && Base != 8 && Base != 10 && Base != 16)
        return EmuStatusInvalidParameter;

    char Buffer[33];
    if(!EmuRtlFormatUnsignedInteger(Value, Base, Buffer, sizeof(Buffer)))
        return EmuStatusInvalidParameter;

    const ULONG RequiredLength = (ULONG)strlen(Buffer);
    if(Length != 0 && RequiredLength > Length)
        return 0xC0000023;

    memcpy(String, Buffer, RequiredLength);
    if(Length == 0 || RequiredLength < Length)
        String[RequiredLength] = '\0';

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlIntegerToUnicodeString(ULONG Value, ULONG Base, xboxkrnl::PUNICODE_STRING String)
{
    if(String == NULL)
        return EmuStatusInvalidParameter;

    char Buffer[33];
    NTSTATUS Status = EmuRtlIntegerToChar(Value, Base, sizeof(Buffer), Buffer);
    if(Status != STATUS_SUCCESS)
        return Status;

    const ULONG RequiredLength = (ULONG)strlen(Buffer) * sizeof(USHORT);
    if(RequiredLength > String->MaximumLength)
        return 0xC0000023;

    if(RequiredLength != 0 && String->Buffer == NULL)
        return EmuStatusInvalidParameter;

    for(ULONG i = 0; i < RequiredLength / sizeof(USHORT); i++)
        String->Buffer[i] = (USHORT)(UCHAR)Buffer[i];

    String->Length = (USHORT)RequiredLength;

    if(String->Buffer != NULL && RequiredLength + sizeof(USHORT) <= String->MaximumLength)
        String->Buffer[RequiredLength / sizeof(USHORT)] = 0;

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlUnicodeStringToInteger(xboxkrnl::PUNICODE_STRING String, ULONG Base, PULONG Value)
{
    if(String == NULL || Value == NULL)
        return EmuStatusInvalidParameter;

    char Buffer[128];
    ULONG CharacterCount = String->Length / sizeof(USHORT);
    if(CharacterCount >= sizeof(Buffer))
        CharacterCount = sizeof(Buffer) - 1;

    for(ULONG i = 0; i < CharacterCount; i++)
    {
        USHORT Character = String->Buffer[i];
        Buffer[i] = (Character <= 0xFF) ? (CHAR)Character : '?';
    }

    Buffer[CharacterCount] = '\0';
    return EmuRtlCharToInteger(Buffer, Base, Value);
}

extern "C" VOID NTAPI EmuRtlMapGenericMask(PACCESS_MASK AccessMask, PGENERIC_MAPPING GenericMapping)
{
    if(AccessMask == NULL || GenericMapping == NULL)
        return;

    ACCESS_MASK Mask = *AccessMask;

    if((Mask & GENERIC_READ) != 0)
        Mask |= GenericMapping->GenericRead;

    if((Mask & GENERIC_WRITE) != 0)
        Mask |= GenericMapping->GenericWrite;

    if((Mask & GENERIC_EXECUTE) != 0)
        Mask |= GenericMapping->GenericExecute;

    if((Mask & GENERIC_ALL) != 0)
        Mask |= GenericMapping->GenericAll;

    Mask &= ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
    *AccessMask = Mask;
}

extern "C" NTSTATUS NTAPI EmuRtlMultiByteToUnicodeN(USHORT *UnicodeString,
                                                    ULONG MaxBytesInUnicodeString,
                                                    PULONG BytesInUnicodeString,
                                                    const CHAR *MultiByteString,
                                                    ULONG BytesInMultiByteString)
{
    ULONG CharacterCount = MaxBytesInUnicodeString / sizeof(USHORT);
    if(CharacterCount > BytesInMultiByteString)
        CharacterCount = BytesInMultiByteString;

    if(BytesInUnicodeString != NULL)
        *BytesInUnicodeString = CharacterCount * sizeof(USHORT);

    if(UnicodeString != NULL && MultiByteString != NULL)
    {
        for(ULONG i = 0; i < CharacterCount; i++)
            UnicodeString[i] = (USHORT)(UCHAR)MultiByteString[i];
    }

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlMultiByteToUnicodeSize(PULONG BytesInUnicodeString,
                                                       const CHAR *MultiByteString,
                                                       ULONG BytesInMultiByteString)
{
    if(BytesInUnicodeString == NULL)
        return EmuStatusInvalidParameter;

    *BytesInUnicodeString = BytesInMultiByteString * sizeof(USHORT);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlUnicodeToMultiByteN(CHAR *MultiByteString,
                                                    ULONG MaxBytesInMultiByteString,
                                                    PULONG BytesInMultiByteString,
                                                    const USHORT *UnicodeString,
                                                    ULONG BytesInUnicodeString)
{
    ULONG CharacterCount = BytesInUnicodeString / sizeof(USHORT);
    if(CharacterCount > MaxBytesInMultiByteString)
        CharacterCount = MaxBytesInMultiByteString;

    if(BytesInMultiByteString != NULL)
        *BytesInMultiByteString = CharacterCount;

    if(MultiByteString != NULL && UnicodeString != NULL)
    {
        for(ULONG i = 0; i < CharacterCount; i++)
            MultiByteString[i] = (UnicodeString[i] <= 0xFF) ? (CHAR)UnicodeString[i] : '?';
    }

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlUnicodeToMultiByteSize(PULONG BytesInMultiByteString,
                                                       const USHORT *UnicodeString,
                                                       ULONG BytesInUnicodeString)
{
    if(BytesInMultiByteString == NULL)
        return EmuStatusInvalidParameter;

    *BytesInMultiByteString = BytesInUnicodeString / sizeof(USHORT);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuRtlUpcaseUnicodeToMultiByteN(CHAR *MultiByteString,
                                                          ULONG MaxBytesInMultiByteString,
                                                          PULONG BytesInMultiByteString,
                                                          const USHORT *UnicodeString,
                                                          ULONG BytesInUnicodeString)
{
    ULONG CharacterCount = BytesInUnicodeString / sizeof(USHORT);
    if(CharacterCount > MaxBytesInMultiByteString)
        CharacterCount = MaxBytesInMultiByteString;

    if(BytesInMultiByteString != NULL)
        *BytesInMultiByteString = CharacterCount;

    if(MultiByteString != NULL && UnicodeString != NULL)
    {
        for(ULONG i = 0; i < CharacterCount; i++)
        {
            USHORT Character = EmuRtlUpperUshort(UnicodeString[i]);
            MultiByteString[i] = (Character <= 0xFF) ? (CHAR)Character : '?';
        }
    }

    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuRtlUnwind(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue)
{
    EmuSwapFS();   // Win2k/XP FS
    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuRtlRaiseException(PEXCEPTION_RECORD ExceptionRecord)
{
    EmuSwapFS();   // Win2k/XP FS

    ULONG GuestEip;
    ULONG GuestEsp;
    ULONG GuestEbp;

    __asm
    {
        mov eax, [ebp+4]
        mov GuestEip, eax
        lea eax, [ebp+12]
        mov GuestEsp, eax
        mov eax, [ebp]
        mov GuestEbp, eax
    }

    EXCEPTION_RECORD LocalExceptionRecord;
    if(ExceptionRecord != NULL)
    {
        LocalExceptionRecord = *ExceptionRecord;
    }
    else
    {
        ZeroMemory(&LocalExceptionRecord, sizeof(LocalExceptionRecord));
        LocalExceptionRecord.ExceptionCode = EmuStatusInvalidParameter;
    }

    EmuSwapFS();   // Xbox FS
    EmuRaiseGuestExceptionRecord(&LocalExceptionRecord, GuestEip, GuestEsp, GuestEbp);
}

extern "C" VOID NTAPI EmuRtlRaiseStatus(NTSTATUS Status)
{
    EmuSwapFS();   // Win2k/XP FS

    ULONG GuestEip;
    ULONG GuestEsp;
    ULONG GuestEbp;

    __asm
    {
        mov eax, [ebp+4]
        mov GuestEip, eax
        lea eax, [ebp+12]
        mov GuestEsp, eax
        mov eax, [ebp]
        mov GuestEbp, eax
    }

    EmuSwapFS();   // Xbox FS
    EmuRaiseGuestException(Status, GuestEip, GuestEsp, GuestEbp);
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

static void EmuStoreLittleEndianUlong(UCHAR *Buffer, ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
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

extern "C" VOID NTAPI EmuXcRC4Key(PUCHAR KeyStruct, ULONG KeyLength, PUCHAR Key)
{
    if(KeyStruct == NULL)
        return;

    for(ULONG i = 0; i < 256; i++)
        KeyStruct[i] = (UCHAR)i;

    KeyStruct[256] = 0;
    KeyStruct[257] = 0;

    if(Key == NULL || KeyLength == 0)
        return;

    UCHAR J = 0;
    for(ULONG i = 0; i < 256; i++)
    {
        J = (UCHAR)(J + KeyStruct[i] + Key[i % KeyLength]);

        UCHAR Temp = KeyStruct[i];
        KeyStruct[i] = KeyStruct[J];
        KeyStruct[J] = Temp;
    }
}

extern "C" VOID NTAPI EmuXcRC4Crypt(PUCHAR KeyStruct, ULONG InputLength, PUCHAR Input)
{
    if(KeyStruct == NULL || Input == NULL || InputLength == 0)
        return;

    UCHAR X = KeyStruct[256];
    UCHAR Y = KeyStruct[257];

    for(ULONG i = 0; i < InputLength; i++)
    {
        X = (UCHAR)(X + 1);
        Y = (UCHAR)(Y + KeyStruct[X]);

        UCHAR Temp = KeyStruct[X];
        KeyStruct[X] = KeyStruct[Y];
        KeyStruct[Y] = Temp;

        const UCHAR K = KeyStruct[(UCHAR)(KeyStruct[X] + KeyStruct[Y])];
        Input[i] ^= K;
    }

    KeyStruct[256] = X;
    KeyStruct[257] = Y;
}

extern "C" VOID NTAPI EmuXcHMAC
(
    PUCHAR KeyMaterial,
    ULONG KeyMaterialLength,
    PUCHAR Data,
    ULONG DataLength,
    PUCHAR Data2,
    ULONG Data2Length,
    PUCHAR Digest
)
{
    if(Digest == NULL)
        return;

    UCHAR KeyBlock[64] = {};
    UCHAR HashedKey[20] = {};

    if(KeyMaterial != NULL && KeyMaterialLength > 64)
    {
        EmuSha1Context KeyContext;
        EmuXcSHAInit((UCHAR*)&KeyContext);
        EmuXcSHAUpdate((UCHAR*)&KeyContext, KeyMaterial, KeyMaterialLength);
        EmuXcSHAFinal((UCHAR*)&KeyContext, HashedKey);
        memcpy(KeyBlock, HashedKey, sizeof(HashedKey));
    }
    else if(KeyMaterial != NULL && KeyMaterialLength != 0)
    {
        memcpy(KeyBlock, KeyMaterial, KeyMaterialLength);
    }

    UCHAR InnerPad[64];
    UCHAR OuterPad[64];
    for(ULONG i = 0; i < sizeof(KeyBlock); i++)
    {
        InnerPad[i] = (UCHAR)(KeyBlock[i] ^ 0x36);
        OuterPad[i] = (UCHAR)(KeyBlock[i] ^ 0x5C);
    }

    UCHAR InnerDigest[20];
    EmuSha1Context Context;
    EmuXcSHAInit((UCHAR*)&Context);
    EmuXcSHAUpdate((UCHAR*)&Context, InnerPad, sizeof(InnerPad));

    if(Data != NULL && DataLength != 0)
        EmuXcSHAUpdate((UCHAR*)&Context, Data, DataLength);

    if(Data2 != NULL && Data2Length != 0)
        EmuXcSHAUpdate((UCHAR*)&Context, Data2, Data2Length);

    EmuXcSHAFinal((UCHAR*)&Context, InnerDigest);

    EmuXcSHAInit((UCHAR*)&Context);
    EmuXcSHAUpdate((UCHAR*)&Context, OuterPad, sizeof(OuterPad));
    EmuXcSHAUpdate((UCHAR*)&Context, InnerDigest, sizeof(InnerDigest));
    EmuXcSHAFinal((UCHAR*)&Context, Digest);
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

static const UCHAR EmuDesInitialPermutation[64] = {
    58, 50, 42, 34, 26, 18, 10, 2,
    60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6,
    64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1,
    59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5,
    63, 55, 47, 39, 31, 23, 15, 7};

static const UCHAR EmuDesFinalPermutation[64] = {
    40, 8, 48, 16, 56, 24, 64, 32,
    39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30,
    37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28,
    35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26,
    33, 1, 41, 9, 49, 17, 57, 25};

static const UCHAR EmuDesExpansionPermutation[48] = {
    32, 1, 2, 3, 4, 5,
    4, 5, 6, 7, 8, 9,
    8, 9, 10, 11, 12, 13,
    12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21,
    20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29,
    28, 29, 30, 31, 32, 1};

static const UCHAR EmuDesPermutation[32] = {
    16, 7, 20, 21,
    29, 12, 28, 17,
    1, 15, 23, 26,
    5, 18, 31, 10,
    2, 8, 24, 14,
    32, 27, 3, 9,
    19, 13, 30, 6,
    22, 11, 4, 25};

static const UCHAR EmuDesPermutedChoice1[56] = {
    57, 49, 41, 33, 25, 17, 9,
    1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27,
    19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,
    7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29,
    21, 13, 5, 28, 20, 12, 4};

static const UCHAR EmuDesPermutedChoice2[48] = {
    14, 17, 11, 24, 1, 5,
    3, 28, 15, 6, 21, 10,
    23, 19, 12, 4, 26, 8,
    16, 7, 27, 20, 13, 2,
    41, 52, 31, 37, 47, 55,
    30, 40, 51, 45, 33, 48,
    44, 49, 39, 56, 34, 53,
    46, 42, 50, 36, 29, 32};

static const UCHAR EmuDesKeyRotations[16] = {
    1, 1, 2, 2, 2, 2, 2, 2,
    1, 2, 2, 2, 2, 2, 2, 1};

static const UCHAR EmuDesSBoxes[8][64] = {
    {
        14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
        0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
        4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
        15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13},
    {
        15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
        3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
        0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
        13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9},
    {
        10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
        13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
        13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
        1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12},
    {
        7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
        13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
        10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
        3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14},
    {
        2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
        14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
        4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
        11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3},
    {
        12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
        10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
        9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
        4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13},
    {
        4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
        13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
        1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
        6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12},
    {
        13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
        1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
        7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
        2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11}};

static const UCHAR EmuDesXboxSubkeyMap[48] = {
    2, 3, 4, 5, 6, 7,
    38, 39, 40, 41, 42, 43,
    10, 11, 12, 13, 14, 15,
    46, 47, 48, 49, 50, 51,
    18, 19, 20, 21, 22, 23,
    54, 55, 56, 57, 58, 59,
    26, 27, 28, 29, 30, 31,
    62, 63, 32, 33, 34, 35};

static ULONGLONG EmuDesPermute(ULONGLONG Value, const UCHAR *Table, int OutputBits, int InputBits)
{
    ULONGLONG Output = 0;

    for(int i = 0; i < OutputBits; i++)
        Output = (Output << 1) | ((Value >> (InputBits - Table[i])) & 1);

    return Output;
}

static ULONGLONG EmuDesReadBlock(const UCHAR *Input)
{
    ULONGLONG Value = 0;

    for(int i = 0; i < 8; i++)
        Value = (Value << 8) | Input[i];

    return Value;
}

static void EmuDesWriteBlock(UCHAR *Output, ULONGLONG Value)
{
    for(int i = 7; i >= 0; i--)
    {
        Output[i] = (UCHAR)Value;
        Value >>= 8;
    }
}

static ULONG EmuDesRotateLeft28(ULONG Value, UCHAR Bits)
{
    return ((Value << Bits) | (Value >> (28 - Bits))) & 0x0FFFFFFF;
}

static void EmuDesPackXboxSubkey(ULONGLONG Subkey, ULONG *Low, ULONG *High)
{
    *Low = 0;
    *High = 0;

    for(int i = 0; i < 48; i++)
    {
        if(((Subkey >> (47 - i)) & 1) == 0)
            continue;

        const UCHAR Position = EmuDesXboxSubkeyMap[i];
        if(Position < 32)
            *Low |= 1UL << Position;
        else
            *High |= 1UL << (Position - 32);
    }
}

static ULONGLONG EmuDesUnpackXboxSubkey(ULONG Low, ULONG High)
{
    ULONGLONG Subkey = 0;

    for(int i = 0; i < 48; i++)
    {
        const UCHAR Position = EmuDesXboxSubkeyMap[i];
        const ULONG Source = Position < 32 ? Low : High;
        const UCHAR Bit = Position < 32 ? Position : Position - 32;
        Subkey = (Subkey << 1) | ((Source >> Bit) & 1);
    }

    return Subkey;
}

static void EmuDesCreateXboxKeySchedule(UCHAR *KeyTable, const UCHAR *Key)
{
    const ULONGLONG KeyValue = EmuDesReadBlock(Key);
    const ULONGLONG PermutedKey = EmuDesPermute(KeyValue, EmuDesPermutedChoice1, 56, 64);
    ULONG Left = (ULONG)(PermutedKey >> 28);
    ULONG Right = (ULONG)(PermutedKey & 0x0FFFFFFF);

    for(int i = 0; i < 16; i++)
    {
        Left = EmuDesRotateLeft28(Left, EmuDesKeyRotations[i]);
        Right = EmuDesRotateLeft28(Right, EmuDesKeyRotations[i]);

        const ULONGLONG Joined = ((ULONGLONG)Left << 28) | Right;
        const ULONGLONG Subkey = EmuDesPermute(Joined, EmuDesPermutedChoice2, 48, 56);
        ULONG Low;
        ULONG High;
        EmuDesPackXboxSubkey(Subkey, &Low, &High);
        EmuStoreLittleEndianUlong(KeyTable + i * 8, Low);
        EmuStoreLittleEndianUlong(KeyTable + i * 8 + 4, High);
    }
}

static void EmuDesLoadXboxKeySchedule(const UCHAR *KeyTable, ULONGLONG Subkeys[16])
{
    for(int i = 0; i < 16; i++)
    {
        const ULONG Low = EmuReadLittleEndianUlong(KeyTable + i * 8);
        const ULONG High = EmuReadLittleEndianUlong(KeyTable + i * 8 + 4);
        Subkeys[i] = EmuDesUnpackXboxSubkey(Low, High);
    }
}

static ULONG EmuDesFeistel(ULONG HalfBlock, ULONGLONG Subkey)
{
    const ULONGLONG Expanded = EmuDesPermute(HalfBlock, EmuDesExpansionPermutation, 48, 32) ^ Subkey;
    ULONG SBoxOutput = 0;

    for(int i = 0; i < 8; i++)
    {
        const UCHAR Block = (UCHAR)((Expanded >> (42 - i * 6)) & 0x3F);
        const UCHAR Row = (UCHAR)(((Block & 0x20) >> 4) | (Block & 1));
        const UCHAR Column = (UCHAR)((Block >> 1) & 0x0F);
        SBoxOutput = (SBoxOutput << 4) | EmuDesSBoxes[i][Row * 16 + Column];
    }

    return (ULONG)EmuDesPermute(SBoxOutput, EmuDesPermutation, 32, 32);
}

static void EmuDesCryptBlock(const UCHAR *Input, UCHAR *Output, const ULONGLONG Subkeys[16], bool Encrypt)
{
    const ULONGLONG Permuted = EmuDesPermute(EmuDesReadBlock(Input), EmuDesInitialPermutation, 64, 64);
    ULONG Left = (ULONG)(Permuted >> 32);
    ULONG Right = (ULONG)Permuted;

    for(int i = 0; i < 16; i++)
    {
        const int SubkeyIndex = Encrypt ? i : 15 - i;
        const ULONG NextLeft = Right;
        Right = Left ^ EmuDesFeistel(Right, Subkeys[SubkeyIndex]);
        Left = NextLeft;
    }

    const ULONGLONG Final = EmuDesPermute(((ULONGLONG)Right << 32) | Left, EmuDesFinalPermutation, 64, 64);
    EmuDesWriteBlock(Output, Final);
}

static void EmuXcDesCryptBlock(ULONG CipherSelector, UCHAR *Output, const UCHAR *Input, const UCHAR *KeyTable, bool Encrypt)
{
    ULONGLONG Subkeys[3][16];

    if(CipherSelector == 0)
    {
        EmuDesLoadXboxKeySchedule(KeyTable, Subkeys[0]);
        EmuDesCryptBlock(Input, Output, Subkeys[0], Encrypt);
        return;
    }

    UCHAR Temp[8];
    EmuDesLoadXboxKeySchedule(KeyTable, Subkeys[0]);
    EmuDesLoadXboxKeySchedule(KeyTable + 128, Subkeys[1]);
    EmuDesLoadXboxKeySchedule(KeyTable + 256, Subkeys[2]);

    if(Encrypt)
    {
        EmuDesCryptBlock(Input, Temp, Subkeys[0], true);
        EmuDesCryptBlock(Temp, Temp, Subkeys[1], false);
        EmuDesCryptBlock(Temp, Output, Subkeys[2], true);
        return;
    }

    EmuDesCryptBlock(Input, Temp, Subkeys[2], false);
    EmuDesCryptBlock(Temp, Temp, Subkeys[1], true);
    EmuDesCryptBlock(Temp, Output, Subkeys[0], false);
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
        EmuDesCreateXboxKeySchedule(KeyTable, Key);
        EmuDesCreateXboxKeySchedule(KeyTable + 128, Key + 8);
        EmuDesCreateXboxKeySchedule(KeyTable + 256, Key + 16);
        return;
    }

    EmuDesCreateXboxKeySchedule(KeyTable, Key);
}

extern "C" VOID NTAPI EmuXcBlockCrypt(ULONG CipherSelector, PUCHAR Output, PUCHAR Input, PUCHAR KeyTable, ULONG Encrypt)
{
    if(Output == NULL || Input == NULL || KeyTable == NULL)
        return;

    const bool EncryptBlock = CipherSelector == 0 ? Encrypt != 0 : Encrypt == MBEDTLS_DES_ENCRYPT;
    EmuXcDesCryptBlock(CipherSelector, Output, Input, KeyTable, EncryptBlock);
}

extern "C" VOID NTAPI EmuXcBlockCryptCBC(ULONG CipherSelector, ULONG InputLength, PUCHAR Output, PUCHAR Input, PUCHAR KeyTable, ULONG Encrypt, PUCHAR Feedback)
{
    if(Output == NULL || Input == NULL || KeyTable == NULL || Feedback == NULL)
        return;

    const bool EncryptBlock = Encrypt == MBEDTLS_DES_ENCRYPT;
    const ULONG CryptLength = (InputLength + 7) & ~7UL;

    for(ULONG Offset = 0; Offset < CryptLength; Offset += 8)
    {
        UCHAR Block[8];

        if(EncryptBlock)
        {
            for(int i = 0; i < 8; i++)
                Block[i] = Input[Offset + i] ^ Feedback[i];

            EmuXcDesCryptBlock(CipherSelector, Output + Offset, Block, KeyTable, true);
            memcpy(Feedback, Output + Offset, 8);
            continue;
        }

        memcpy(Block, Input + Offset, 8);
        EmuXcDesCryptBlock(CipherSelector, Output + Offset, Input + Offset, KeyTable, false);

        for(int i = 0; i < 8; i++)
            Output[Offset + i] ^= Feedback[i];

        memcpy(Feedback, Block, 8);
    }
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

extern "C" BOOLEAN NTAPI EmuRtlCreateUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, const USHORT *SourceString)
{
    if(DestinationString == NULL)
        return FALSE;

    DestinationString->Length = 0;
    DestinationString->MaximumLength = 0;
    DestinationString->Buffer = NULL;

    if(SourceString == NULL)
        return TRUE;

    USHORT Length = 0;
    while(SourceString[Length / sizeof(USHORT)] != 0 && Length <= 0xFFFC)
        Length += sizeof(USHORT);

    DestinationString->MaximumLength = Length + sizeof(USHORT);
    DestinationString->Buffer = (USHORT*)HeapAlloc(GetProcessHeap(), 0, DestinationString->MaximumLength);
    if(DestinationString->Buffer == NULL)
    {
        DestinationString->MaximumLength = 0;
        return FALSE;
    }

    if(Length != 0)
        memcpy(DestinationString->Buffer, SourceString, Length);

    DestinationString->Buffer[Length / sizeof(USHORT)] = 0;
    DestinationString->Length = Length;

    return TRUE;
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

extern "C" USHORT NTAPI EmuRtlUpcaseUnicodeChar(USHORT SourceCharacter)
{
    return EmuRtlUpperUshort(SourceCharacter);
}

extern "C" NTSTATUS NTAPI EmuRtlUpcaseUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString,
                                                    xboxkrnl::PUNICODE_STRING SourceString,
                                                    BOOLEAN AllocateDestinationString)
{
    if(DestinationString == NULL || SourceString == NULL)
        return EmuStatusInvalidParameter;

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

    if(Length != 0 && DestinationString->Buffer == NULL)
        return EmuStatusInvalidParameter;

    for(USHORT i = 0; i < Length / sizeof(USHORT); i++)
        DestinationString->Buffer[i] = EmuRtlUpperUshort(SourceString->Buffer[i]);

    DestinationString->Length = Length;
    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuRtlInitializeCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    if(CriticalSection == NULL)
        return;

    if(!EmuIsWritableMemoryRange(CriticalSection, sizeof(*CriticalSection)))
    {
        printf("EmuKrnl (0x%lX): RtlInitializeCriticalSection ignored invalid pointer 0x%.08lX.\n",
               (uint32)GetCurrentThreadId(), (uint32)CriticalSection);
        return;
    }

    memset(CriticalSection->Unknown, 0, sizeof(CriticalSection->Unknown));
    CriticalSection->LockCount = -1;
    CriticalSection->RecursionCount = 0;
    CriticalSection->OwningThread = 0;
}

extern "C" BOOLEAN NTAPI EmuRtlTryEnterCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    if(CriticalSection == NULL || !EmuIsWritableMemoryRange(CriticalSection, sizeof(*CriticalSection)))
        return FALSE;

    ULONG CurrentThread = (ULONG)(::ULONG_PTR)EmuGetCurrentThread();

    if(CriticalSection->RecursionCount == 0)
    {
        CriticalSection->LockCount = 0;
        CriticalSection->RecursionCount = 1;
        CriticalSection->OwningThread = CurrentThread;
        return TRUE;
    }

    if(CriticalSection->OwningThread == CurrentThread)
    {
        CriticalSection->LockCount++;
        CriticalSection->RecursionCount++;
        return TRUE;
    }

    return FALSE;
}

extern "C" VOID NTAPI EmuRtlEnterCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    if(CriticalSection == NULL)
        return;

    if(!EmuIsWritableMemoryRange(CriticalSection, sizeof(*CriticalSection)))
    {
        printf("EmuKrnl (0x%lX): RtlEnterCriticalSection ignored invalid pointer 0x%.08lX.\n",
               (uint32)GetCurrentThreadId(), (uint32)CriticalSection);
        return;
    }

    ULONG CurrentThread = (ULONG)(::ULONG_PTR)EmuGetCurrentThread();

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
    if(CriticalSection == NULL || !EmuIsWritableMemoryRange(CriticalSection, sizeof(*CriticalSection)))
    {
        if(CriticalSection != NULL)
            printf("EmuKrnl (0x%lX): RtlEnterCriticalSectionAndRegion ignored invalid pointer 0x%.08lX.\n",
                   (uint32)GetCurrentThreadId(), (uint32)CriticalSection);

        return;
    }

    EmuAdjustCurrentThreadKernelApcDisable(-1);
    EmuRtlEnterCriticalSection(CriticalSection);
}

extern "C" VOID NTAPI EmuRtlLeaveCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection)
{
    if(CriticalSection == NULL)
        return;

    if(!EmuIsWritableMemoryRange(CriticalSection, sizeof(*CriticalSection)))
    {
        printf("EmuKrnl (0x%lX): RtlLeaveCriticalSection ignored invalid pointer 0x%.08lX.\n",
               (uint32)GetCurrentThreadId(), (uint32)CriticalSection);
        return;
    }

    if(CriticalSection->RecursionCount == 0)
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
    if(CriticalSection == NULL || !EmuIsWritableMemoryRange(CriticalSection, sizeof(*CriticalSection)))
    {
        if(CriticalSection != NULL)
            printf("EmuKrnl (0x%lX): RtlLeaveCriticalSectionAndRegion ignored invalid pointer 0x%.08lX.\n",
                   (uint32)GetCurrentThreadId(), (uint32)CriticalSection);

        return;
    }

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

struct EmuContiguousMemoryAllocation
{
    PVOID Address;
    ULONG Size;
    ULONG PhysicalAddress;
};

struct EmuIoSpaceMapping
{
    PVOID Address;
    ULONG Size;
    ULONG PhysicalAddress;
    ULONG Protect;
    bool OwnsAllocation;
};

struct EmuKernelStackAllocation
{
    PVOID BaseAddress;
    PVOID StackBase;
    PVOID StackLimit;
    ULONG Size;
    BOOLEAN DebuggerThread;
};

struct EmuMmStatistics
{
    ULONG Length;
    ULONG TotalPhysicalPages;
    ULONG AvailablePages;
    ULONG VirtualMemoryBytesCommitted;
    ULONG VirtualMemoryBytesReserved;
    ULONG CachePagesCommitted;
    ULONG PoolPagesCommitted;
    ULONG StackPagesCommitted;
    ULONG ImagePagesCommitted;
};

static EmuSystemMemoryAllocation g_EmuSystemMemoryAllocations[128] = {};
static EmuContiguousMemoryAllocation g_EmuContiguousMemoryAllocations[128] = {};
static EmuIoSpaceMapping g_EmuIoSpaceMappings[64] = {};
static EmuKernelStackAllocation g_EmuKernelStackAllocations[64] = {};
static ULONG g_EmuNextSystemMemoryAddress = 0xD0000000;
static ULONG g_EmuNextContiguousPhysicalAddress = 0x00100000;
static const ULONG EmuPageSize = 0x1000;
static const ULONG EmuXboxPhysicalMemoryBytes = 64 * 1024 * 1024;
static const ULONG EmuMmioPassthroughBase = 0xF0000000;

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

static ULONG EmuRoundToPageSize(ULONG NumberOfBytes)
{
    return EmuSystemMemoryPages(NumberOfBytes) * EmuPageSize;
}

static void EmuTrackKernelStack(PVOID BaseAddress, PVOID StackBase, PVOID StackLimit, ULONG Size, BOOLEAN DebuggerThread)
{
    if(BaseAddress == NULL || StackBase == NULL || StackLimit == NULL || Size == 0)
        return;

    for(ULONG i = 0; i < sizeof(g_EmuKernelStackAllocations) / sizeof(g_EmuKernelStackAllocations[0]); i++)
    {
        if(g_EmuKernelStackAllocations[i].BaseAddress != NULL)
            continue;

        g_EmuKernelStackAllocations[i].BaseAddress = BaseAddress;
        g_EmuKernelStackAllocations[i].StackBase = StackBase;
        g_EmuKernelStackAllocations[i].StackLimit = StackLimit;
        g_EmuKernelStackAllocations[i].Size = Size;
        g_EmuKernelStackAllocations[i].DebuggerThread = DebuggerThread;
        return;
    }

    printf("EmuKrnl (0x%lX): MmCreateKernelStack tracking table full for base=%p size=0x%.08lX.\n",
           GetCurrentThreadId(), BaseAddress, Size);
}

static EmuKernelStackAllocation *EmuFindKernelStack(PVOID StackBase, PVOID StackLimit)
{
    const ULONG_PTR Limit = (ULONG_PTR)StackLimit;

    for(ULONG i = 0; i < sizeof(g_EmuKernelStackAllocations) / sizeof(g_EmuKernelStackAllocations[0]); i++)
    {
        EmuKernelStackAllocation *Allocation = &g_EmuKernelStackAllocations[i];
        if(Allocation->BaseAddress == NULL)
            continue;

        const ULONG_PTR Base = (ULONG_PTR)Allocation->BaseAddress;
        const ULONG_PTR End = Base + Allocation->Size;

        if(Allocation->StackBase == StackBase || Allocation->StackLimit == StackLimit ||
           (Limit >= Base && Limit < End))
            return Allocation;
    }

    return NULL;
}

static void EmuUntrackKernelStack(EmuKernelStackAllocation *Allocation)
{
    if(Allocation == NULL)
        return;

    Allocation->BaseAddress = NULL;
    Allocation->StackBase = NULL;
    Allocation->StackLimit = NULL;
    Allocation->Size = 0;
    Allocation->DebuggerThread = FALSE;
}

static ULONG EmuCommittedKernelStackBytes()
{
    ULONG Total = 0;

    for(ULONG i = 0; i < sizeof(g_EmuKernelStackAllocations) / sizeof(g_EmuKernelStackAllocations[0]); i++)
        Total += g_EmuKernelStackAllocations[i].Size;

    return Total;
}

static bool EmuAddressRangeContains(PVOID BaseAddress, ULONG Size, PVOID Address)
{
    const ULONG_PTR Base = (ULONG_PTR)BaseAddress;
    const ULONG_PTR End = Base + Size;
    const ULONG_PTR Value = (ULONG_PTR)Address;

    return Base != 0 && End >= Base && Value >= Base && Value < End;
}

static EmuIoSpaceMapping *EmuFindIoSpaceMapping(PVOID Address)
{
    for(ULONG i = 0; i < sizeof(g_EmuIoSpaceMappings) / sizeof(g_EmuIoSpaceMappings[0]); i++)
    {
        if(EmuAddressRangeContains(g_EmuIoSpaceMappings[i].Address, g_EmuIoSpaceMappings[i].Size, Address))
            return &g_EmuIoSpaceMappings[i];
    }

    return NULL;
}

static void EmuTrackIoSpaceMapping(PVOID Address, ULONG Size, ULONG PhysicalAddress, ULONG Protect, bool OwnsAllocation)
{
    if(Address == NULL || Size == 0)
        return;

    for(ULONG i = 0; i < sizeof(g_EmuIoSpaceMappings) / sizeof(g_EmuIoSpaceMappings[0]); i++)
    {
        if(g_EmuIoSpaceMappings[i].Address != NULL)
            continue;

        g_EmuIoSpaceMappings[i].Address = Address;
        g_EmuIoSpaceMappings[i].Size = EmuRoundToPageSize(Size);
        g_EmuIoSpaceMappings[i].PhysicalAddress = PhysicalAddress;
        g_EmuIoSpaceMappings[i].Protect = Protect;
        g_EmuIoSpaceMappings[i].OwnsAllocation = OwnsAllocation;
        return;
    }

    printf("EmuKrnl (0x%lX): MmMapIoSpace mapping table full for physical=0x%.08lX size=0x%.08lX.\n",
           GetCurrentThreadId(), PhysicalAddress, Size);
}

static void EmuUntrackIoSpaceMapping(EmuIoSpaceMapping *Mapping)
{
    if(Mapping == NULL)
        return;

    Mapping->Address = NULL;
    Mapping->Size = 0;
    Mapping->PhysicalAddress = 0;
    Mapping->Protect = 0;
    Mapping->OwnsAllocation = false;
}

static NTSTATUS EmuProtectVirtualMemory(PVOID BaseAddress, ULONG NumberOfBytes, ULONG NewProtect, PULONG OldProtect)
{
    if(BaseAddress == NULL || NumberOfBytes == 0 || !EmuIsValidSystemMemoryProtect(NewProtect))
        return EmuStatusInvalidParameter;

    EmuIoSpaceMapping *Mapping = EmuFindIoSpaceMapping(BaseAddress);
    if(Mapping != NULL && (ULONG_PTR)Mapping->Address >= EmuMmioPassthroughBase)
    {
        if(OldProtect != NULL)
            *OldProtect = Mapping->Protect;

        Mapping->Protect = NewProtect;
        return STATUS_SUCCESS;
    }

    DWORD PreviousProtect = 0;
    if(!VirtualProtect(BaseAddress, NumberOfBytes, NewProtect, &PreviousProtect))
        return STATUS_UNSUCCESSFUL;

    if(OldProtect != NULL)
        *OldProtect = PreviousProtect;

    if(Mapping != NULL)
        Mapping->Protect = NewProtect;

    return STATUS_SUCCESS;
}

static ULONG EmuQueryAddressProtect(PVOID VirtualAddress)
{
    EmuIoSpaceMapping *Mapping = EmuFindIoSpaceMapping(VirtualAddress);
    if(Mapping != NULL)
        return Mapping->Protect;

    MEMORY_BASIC_INFORMATION MemoryInfo;
    if(VirtualQuery(VirtualAddress, &MemoryInfo, sizeof(MemoryInfo)) != sizeof(MemoryInfo))
        return 0;

    return MemoryInfo.Protect;
}

static void EmuTrackContiguousMemoryAllocation(PVOID Address, ULONG Size)
{
    if(Address == NULL)
        return;

    for(ULONG i = 0; i < sizeof(g_EmuContiguousMemoryAllocations) / sizeof(g_EmuContiguousMemoryAllocations[0]); i++)
    {
        if(g_EmuContiguousMemoryAllocations[i].Address != NULL)
            continue;

        g_EmuContiguousMemoryAllocations[i].Address = Address;
        g_EmuContiguousMemoryAllocations[i].Size = Size;
        g_EmuContiguousMemoryAllocations[i].PhysicalAddress = g_EmuNextContiguousPhysicalAddress;
        g_EmuNextContiguousPhysicalAddress += EmuRoundToPageSize(Size);
        return;
    }
}

// Allocate contiguous "physical" memory from the low Xbox-RAM window the launcher
// reserved in this process (0x01000000..0x04000000). Keeping these below 64 MiB
// makes the host pointer equal the Xbox physical address in its low 28 bits --
// the invariant the NV2A DMA engine and nxdk pbkit assume when they program and
// then poll pushbuffer/surface addresses. Bump-allocate and commit on demand;
// return NULL (caller falls back to the heap) if the window is exhausted or was
// never reserved (e.g. an oversized image).
static const ULONG g_EmuXboxRamLimit = 0x04000000;
static ULONG g_EmuXboxRamNext = 0x01000000;

static void *EmuAllocateContiguousLow(ULONG Size, ULONG Alignment)
{
    if(Alignment < 0x1000)
        Alignment = 0x1000;

    ULONG Base = (g_EmuXboxRamNext + (Alignment - 1)) & ~(Alignment - 1);
    if(Base < g_EmuXboxRamNext || Base + Size > g_EmuXboxRamLimit || Base + Size < Base)
        return NULL;

    void *p = VirtualAlloc((void*)(uintptr_t)Base, Size, MEM_COMMIT, PAGE_READWRITE);
    if(p == NULL)
    {
        // Ran past the launcher's reservation (or it was never made); stop trying.
        g_EmuXboxRamNext = g_EmuXboxRamLimit;
        return NULL;
    }

    g_EmuXboxRamNext = Base + Size;
    return p;
}

// True for blocks handed out by EmuAllocateContiguousLow (VirtualAlloc-backed);
// those must not be released with delete[]. The bump allocator has no free list,
// so freeing simply decommits the pages back to the reservation.
static bool EmuIsLowXboxRam(PVOID Address)
{
    const ULONG Value = (ULONG)Address;
    return Value >= 0x01000000 && Value < g_EmuXboxRamLimit;
}

static void EmuUntrackContiguousMemoryAllocation(PVOID Address)
{
    if(Address == NULL)
        return;

    for(ULONG i = 0; i < sizeof(g_EmuContiguousMemoryAllocations) / sizeof(g_EmuContiguousMemoryAllocations[0]); i++)
    {
        if(g_EmuContiguousMemoryAllocations[i].Address != Address)
            continue;

        g_EmuContiguousMemoryAllocations[i].Address = NULL;
        g_EmuContiguousMemoryAllocations[i].Size = 0;
        g_EmuContiguousMemoryAllocations[i].PhysicalAddress = 0;
        return;
    }
}

static ULONG EmuQuerySystemMemoryAllocationSize(PVOID Address)
{
    const ULONG Value = (ULONG)Address;

    for(ULONG i = 0; i < sizeof(g_EmuSystemMemoryAllocations) / sizeof(g_EmuSystemMemoryAllocations[0]); i++)
    {
        const ULONG Base = (ULONG)g_EmuSystemMemoryAllocations[i].Address;
        const ULONG Size = g_EmuSystemMemoryAllocations[i].Pages * EmuPageSize;

        if(Base != 0 && Value >= Base && Value < Base + Size)
            return Size - (Value - Base);
    }

    return 0;
}

static ULONG EmuQueryContiguousMemoryPhysicalAddress(PVOID Address)
{
    const ULONG Value = (ULONG)Address;

    for(ULONG i = 0; i < sizeof(g_EmuContiguousMemoryAllocations) / sizeof(g_EmuContiguousMemoryAllocations[0]); i++)
    {
        const ULONG Base = (ULONG)g_EmuContiguousMemoryAllocations[i].Address;
        const ULONG Size = g_EmuContiguousMemoryAllocations[i].Size;

        if(Base != 0 && Value >= Base && Value < Base + Size)
            return g_EmuContiguousMemoryAllocations[i].PhysicalAddress + (Value - Base);
    }

    return 0;
}

static ULONG EmuQueryContiguousMemoryAllocationSize(PVOID Address)
{
    const ULONG Value = (ULONG)Address;

    for(ULONG i = 0; i < sizeof(g_EmuContiguousMemoryAllocations) / sizeof(g_EmuContiguousMemoryAllocations[0]); i++)
    {
        const ULONG Base = (ULONG)g_EmuContiguousMemoryAllocations[i].Address;
        const ULONG Size = g_EmuContiguousMemoryAllocations[i].Size;

        if(Base != 0 && Value >= Base && Value < Base + Size)
            return Size - (Value - Base);
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Bridges for the NV2A model (Emu.cpp). In this HLE emulator the guest programs
// the NV2A with raw host pointers into MmAllocateContiguousMemory blocks (the
// pushbuffer PUT, texture offsets), so the NV2A pusher/texture code reads guest
// data straight from host memory. These let it find the enclosing block base
// (a pushbuffer's start, given its end) and reverse a fake "physical" address
// back to its host pointer, for titles that go through MmGetPhysicalAddress.
// ---------------------------------------------------------------------------
extern "C" ULONG EmuContiguousBlockBase(ULONG HostAddress, ULONG *BlockSize)
{
    for(ULONG i = 0; i < sizeof(g_EmuContiguousMemoryAllocations) / sizeof(g_EmuContiguousMemoryAllocations[0]); i++)
    {
        const ULONG Base = (ULONG)g_EmuContiguousMemoryAllocations[i].Address;
        const ULONG Size = g_EmuContiguousMemoryAllocations[i].Size;

        if(Base != 0 && HostAddress >= Base && HostAddress < Base + Size)
        {
            if(BlockSize != NULL)
                *BlockSize = Size;
            return Base;
        }
    }

    return 0;
}

extern "C" ULONG EmuContiguousHostFromPhysical(ULONG PhysicalAddress)
{
    for(ULONG i = 0; i < sizeof(g_EmuContiguousMemoryAllocations) / sizeof(g_EmuContiguousMemoryAllocations[0]); i++)
    {
        const ULONG Base = (ULONG)g_EmuContiguousMemoryAllocations[i].Address;
        const ULONG Phys = g_EmuContiguousMemoryAllocations[i].PhysicalAddress;
        const ULONG Size = g_EmuContiguousMemoryAllocations[i].Size;

        if(Base != 0 && PhysicalAddress >= Phys && PhysicalAddress < Phys + Size)
            return Base + (PhysicalAddress - Phys);
    }

    return 0;
}

static ULONG EmuCommittedSystemMemoryBytes()
{
    ULONG Total = 0;

    for(ULONG i = 0; i < sizeof(g_EmuSystemMemoryAllocations) / sizeof(g_EmuSystemMemoryAllocations[0]); i++)
        Total += g_EmuSystemMemoryAllocations[i].Pages * EmuPageSize;

    return Total;
}

static ULONG EmuCommittedContiguousMemoryBytes()
{
    ULONG Total = 0;

    for(ULONG i = 0; i < sizeof(g_EmuContiguousMemoryAllocations) / sizeof(g_EmuContiguousMemoryAllocations[0]); i++)
        Total += g_EmuContiguousMemoryAllocations[i].Size;

    return Total;
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

struct EmuIrp
{
    ULONG Magic;
    USHORT Size;
    xboxkrnl::CCHAR StackSize;
    UCHAR MajorFunction;
    BOOLEAN MustComplete;
    PVOID DeviceObject;
    PVOID UserBuffer;
    ULONG Length;
    xboxkrnl::PLARGE_INTEGER StartingOffset;
    PVOID Event;
    xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock;
    ULONG IoControlCode;
    PVOID InputBuffer;
    ULONG InputBufferLength;
    PVOID OutputBuffer;
    ULONG OutputBufferLength;
    BOOLEAN InternalDeviceIoControl;
};

static const ULONG EmuIrpMagic = 0x49727045;

static std::string g_EmuDeviceObjectName;
static EmuDeviceObject *g_EmuDeviceObject = NULL;

static EmuIrp *EmuAllocateIrpObject(xboxkrnl::CCHAR StackSize)
{
    if(StackSize <= 0)
        StackSize = 1;

    EmuIrp *Irp = new EmuIrp;
    ZeroMemory(Irp, sizeof(*Irp));
    Irp->Magic = EmuIrpMagic;
    Irp->Size = sizeof(*Irp);
    Irp->StackSize = StackSize;
    return Irp;
}

static EmuIrp *EmuGetIrp(PVOID Irp)
{
    EmuIrp *Object = (EmuIrp*)Irp;
    if(Object == NULL || Object->Magic != EmuIrpMagic)
        return NULL;

    return Object;
}

static VOID EmuCompleteIrp(PVOID Irp, NTSTATUS Status, ULONG Information)
{
    EmuIrp *Object = EmuGetIrp(Irp);
    if(Object != NULL && Object->IoStatusBlock != NULL)
    {
        Object->IoStatusBlock->u1.Status = Status;
        Object->IoStatusBlock->Information = (xboxkrnl::ULONG_PTR)(::ULONG_PTR)Information;
    }
}

extern "C" PVOID NTAPI EmuIoAllocateIrp
(
    IN xboxkrnl::CCHAR StackSize,
    IN BOOLEAN ChargeQuota
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuIrp *Irp = EmuAllocateIrpObject(StackSize);

    printf("EmuKrnl (0x%lX): IoAllocateIrp stack=%d charge=%lu result=%p.\n",
           GetCurrentThreadId(), (int)StackSize, (ULONG)ChargeQuota, Irp);

    EmuSwapFS();   // Xbox FS

    return Irp;
}

extern "C" PVOID NTAPI EmuIoBuildAsynchronousFsdRequest
(
    IN ULONG MajorFunction,
    IN PVOID DeviceObject,
    IN PVOID Buffer,
    IN ULONG Length,
    IN xboxkrnl::PLARGE_INTEGER StartingOffset,
    OUT xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuIrp *Irp = EmuAllocateIrpObject(1);
    Irp->MajorFunction = (UCHAR)MajorFunction;
    Irp->DeviceObject = DeviceObject;
    Irp->UserBuffer = Buffer;
    Irp->Length = Length;
    Irp->StartingOffset = StartingOffset;
    Irp->IoStatusBlock = IoStatusBlock;

    printf("EmuKrnl (0x%lX): IoBuildAsynchronousFsdRequest major=0x%.08lX device=%p length=0x%.08lX result=%p.\n",
           GetCurrentThreadId(), MajorFunction, DeviceObject, Length, Irp);

    EmuSwapFS();   // Xbox FS

    return Irp;
}

extern "C" PVOID NTAPI EmuIoBuildDeviceIoControlRequest
(
    IN ULONG IoControlCode,
    IN PVOID DeviceObject,
    IN PVOID InputBuffer,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer,
    IN ULONG OutputBufferLength,
    IN BOOLEAN InternalDeviceIoControl,
    IN PVOID Event,
    OUT xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuIrp *Irp = EmuAllocateIrpObject(1);
    Irp->MajorFunction = InternalDeviceIoControl ? 0x0F : 0x0E;
    Irp->DeviceObject = DeviceObject;
    Irp->IoControlCode = IoControlCode;
    Irp->InputBuffer = InputBuffer;
    Irp->InputBufferLength = InputBufferLength;
    Irp->OutputBuffer = OutputBuffer;
    Irp->OutputBufferLength = OutputBufferLength;
    Irp->InternalDeviceIoControl = InternalDeviceIoControl;
    Irp->Event = Event;
    Irp->IoStatusBlock = IoStatusBlock;

    printf("EmuKrnl (0x%lX): IoBuildDeviceIoControlRequest code=0x%.08lX device=%p in=0x%.08lX out=0x%.08lX result=%p.\n",
           GetCurrentThreadId(), IoControlCode, DeviceObject, InputBufferLength, OutputBufferLength, Irp);

    EmuSwapFS();   // Xbox FS

    return Irp;
}

extern "C" PVOID NTAPI EmuIoBuildSynchronousFsdRequest
(
    IN ULONG MajorFunction,
    IN PVOID DeviceObject,
    IN PVOID Buffer,
    IN ULONG Length,
    IN xboxkrnl::PLARGE_INTEGER StartingOffset,
    IN PVOID Event,
    OUT xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuIrp *Irp = EmuAllocateIrpObject(1);
    Irp->MajorFunction = (UCHAR)MajorFunction;
    Irp->DeviceObject = DeviceObject;
    Irp->UserBuffer = Buffer;
    Irp->Length = Length;
    Irp->StartingOffset = StartingOffset;
    Irp->Event = Event;
    Irp->IoStatusBlock = IoStatusBlock;

    printf("EmuKrnl (0x%lX): IoBuildSynchronousFsdRequest major=0x%.08lX device=%p length=0x%.08lX event=%p result=%p.\n",
           GetCurrentThreadId(), MajorFunction, DeviceObject, Length, Event, Irp);

    EmuSwapFS();   // Xbox FS

    return Irp;
}

extern "C" NTSTATUS NTAPI EmuIoCheckShareAccess
(
    IN ACCESS_MASK DesiredAccess,
    IN ULONG DesiredShareAccess,
    IN PVOID FileObject,
    IN PVOID ShareAccess,
    IN BOOLEAN Update
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoCheckShareAccess access=0x%.08lX share=0x%.08lX file=%p update=%lu.\n",
           GetCurrentThreadId(), DesiredAccess, DesiredShareAccess, FileObject, (ULONG)Update);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

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
            IoStatusBlock->Information = (xboxkrnl::ULONG_PTR)(::ULONG_PTR)BytesTransferred;
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
    {
        // Option 6 == VIDEO_ENC_GET_SETTINGS: report the AV pack + TV standard the
        // guest video HAL uses to enumerate display modes. Return "standard AV
        // pack, NTSC-M" (AV_PACK_STANDARD 0x01 | VIDEO_REGION_NTSCM 0x100) so nxdk
        // and the XDK find the 640x480 60Hz mode. Returning 0 (AV_PACK_NONE) means
        // no mode matches -> XVideoSetMode fails without allocating a framebuffer
        // -> XVideoGetFB() returns NULL.
        *Result = (Option == 6) ? 0x00000101 : 0;
    }
}

extern "C" ULONG NTAPI EmuAvSetDisplayMode(PVOID RegisterBase, ULONG Step, ULONG Mode, ULONG Format, ULONG Pitch, ULONG FrameBuffer)
{
    EmuSwapFS();   // Win2k/XP FS
    // Report where the scanout frame lives + its geometry. FrameBuffer is the
    // physical/virtual address of the displayed surface; Pitch is bytes/scanline;
    // Mode/Format encode resolution and pixel format.
    printf("EmuKrnl (0x%lX): AvSetDisplayMode step=%lu mode=0x%.08lX format=0x%.08lX "
           "pitch=0x%.08lX (%lu) framebuffer=0x%.08lX\n",
           GetCurrentThreadId(), Step, Mode, Format, Pitch, Pitch, FrameBuffer);
    fflush(stdout);
    // Publish the scanline pitch so the NV2A scanout capture can recover the
    // display width when the CRTC base is flipped. See Emu.cpp EmuNv2aDumpScanout.
    if(Pitch != 0)
        g_EmuDisplayPitch = Pitch;
    EmuSwapFS();   // Xbox FS
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

    if(Memory == NULL)
    {
        printf("EmuKrnl (0x%lX): *ALLOC FAILED* ExAllocatePoolWithTag bytes=0x%lX tag=0x%.08lX\n",
               GetCurrentThreadId(), NumberOfBytes, Tag);
        fflush(stdout);
    }

    EmuSwapFS();   // Xbox FS

    return Memory;
}

extern "C" ULONG NTAPI EmuExQueryPoolBlockSize(PVOID PoolBlock)
{
    EmuSwapFS();   // Win2k/XP FS

    ULONG Size = 0;
    if(PoolBlock != NULL)
        Size = (ULONG)_msize(PoolBlock);

    printf("EmuKrnl (0x%lX): ExQueryPoolBlockSize block=%p size=0x%.08lX.\n",
           GetCurrentThreadId(), PoolBlock, Size);

    EmuSwapFS();   // Xbox FS

    return Size;
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
// * 0x0009 - HalReadSMCTrayState
// ******************************************************************
extern "C" ULONG NTAPI EmuHalReadSMCTrayState
(
    PULONG TrayState,
    PULONG TrayStateChangeCount
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(TrayState != NULL && EmuIsWritableMemoryRange(TrayState, sizeof(*TrayState)))
        *TrayState = g_EmuHalTrayState;

    if(TrayStateChangeCount != NULL && EmuIsWritableMemoryRange(TrayStateChangeCount, sizeof(*TrayStateChangeCount)))
        *TrayStateChangeCount = g_EmuHalTrayStateChangeCount;

    printf("EmuKrnl (0x%lX): HalReadSMCTrayState state=0x%.08lX changes=0x%.08lX.\n",
           GetCurrentThreadId(), g_EmuHalTrayState, g_EmuHalTrayStateChangeCount);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
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

extern "C" ULONG NTAPI EmuHalWriteSMBusValueCompat(ULONG Value)
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuLastSmcScratchValue = Value;

    printf("EmuKrnl (0x%lX): HalWriteSMBusValue compat value=0x%.08lX.\n",
           GetCurrentThreadId(), Value);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
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

extern "C" VOID NTAPI EmuREAD_PORT_BUFFER_UCHAR(PUCHAR Port, PUCHAR Buffer, ULONG Count)
{
    (void)Port;

    if(Buffer != NULL && Count != 0)
        ZeroMemory(Buffer, Count * sizeof(UCHAR));
}

extern "C" VOID NTAPI EmuREAD_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count)
{
    (void)Port;

    if(Buffer != NULL && Count != 0)
        ZeroMemory(Buffer, Count * sizeof(USHORT));
}

extern "C" VOID NTAPI EmuREAD_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count)
{
    (void)Port;

    if(Buffer != NULL && Count != 0)
        ZeroMemory(Buffer, Count * sizeof(ULONG));
}

extern "C" VOID NTAPI EmuWRITE_PORT_BUFFER_UCHAR(PUCHAR Port, PUCHAR Buffer, ULONG Count)
{
    (void)Port;
    (void)Buffer;
    (void)Count;
}

extern "C" VOID NTAPI EmuWRITE_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count)
{
    (void)Port;
    (void)Buffer;
    (void)Count;
}

extern "C" VOID NTAPI EmuWRITE_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count)
{
    (void)Port;
    (void)Buffer;
    (void)Count;
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

extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuExInterlockedAddLargeInteger
(
    IN OUT xboxkrnl::PLARGE_INTEGER Addend,
    IN xboxkrnl::LARGE_INTEGER Increment,
    IN PVOID Lock
)
{
    (void)Lock;

    xboxkrnl::LARGE_INTEGER Previous;
    Previous.QuadPart = 0;

    if(Addend == NULL)
        return Previous;

    LONGLONG OldValue;
    LONGLONG NewValue;
    do
    {
        OldValue = Addend->QuadPart;
        NewValue = OldValue + Increment.QuadPart;
    } while(::InterlockedCompareExchange64((volatile LONGLONG*)&Addend->QuadPart, NewValue, OldValue) != OldValue);

    Previous.QuadPart = OldValue;
    return Previous;
}

extern "C" VOID NTAPI EmuExInterlockedAddLargeStatistic
(
    IN OUT xboxkrnl::PLARGE_INTEGER Addend,
    IN ULONG Increment
)
{
    if(Addend == NULL)
        return;

    LONGLONG OldValue;
    LONGLONG NewValue;
    do
    {
        OldValue = Addend->QuadPart;
        NewValue = OldValue + Increment;
    } while(::InterlockedCompareExchange64((volatile LONGLONG*)&Addend->QuadPart, NewValue, OldValue) != OldValue);
}

extern "C" LONGLONG NTAPI EmuExInterlockedCompareExchange64
(
    IN OUT LONGLONG *Destination,
    IN LONGLONG *Exchange,
    IN LONGLONG *Comperand,
    IN PVOID Lock
)
{
    (void)Lock;

    if(Destination == NULL || Exchange == NULL || Comperand == NULL)
        return 0;

    return ::InterlockedCompareExchange64((volatile LONGLONG*)Destination, *Exchange, *Comperand);
}

extern "C" LONG __fastcall EmuInterlockedDecrementExport(IN OUT PLONG Addend)
{
    return EmuInterlockedDecrement(Addend);
}

extern "C" LONG __fastcall EmuInterlockedExchangeExport(IN OUT PLONG Target, IN LONG Value)
{
    return EmuInterlockedExchange(Target, Value);
}

extern "C" LONG __fastcall EmuInterlockedExchangeAddExport(IN OUT PLONG Addend, IN LONG Value)
{
    return EmuInterlockedExchangeAdd(Addend, Value);
}

extern "C" LONG __fastcall EmuInterlockedIncrementExport(IN OUT PLONG Addend)
{
    return EmuInterlockedIncrement(Addend);
}

struct EmuSingleListEntry
{
    EmuSingleListEntry *Next;
};

extern "C" PVOID __fastcall EmuInterlockedFlushSList(PVOID ListHead)
{
    if(ListHead == NULL)
        return NULL;

    EmuSingleListEntry *Head = (EmuSingleListEntry*)ListHead;
    EmuSingleListEntry *First;

    do
    {
        First = Head->Next;
    } while(::InterlockedCompareExchangePointer((volatile PVOID*)&Head->Next, NULL, First) != First);

    return First;
}

extern "C" PVOID __fastcall EmuInterlockedPopEntrySList(PVOID ListHead)
{
    if(ListHead == NULL)
        return NULL;

    EmuSingleListEntry *Head = (EmuSingleListEntry*)ListHead;
    EmuSingleListEntry *First;
    EmuSingleListEntry *Next;

    do
    {
        First = Head->Next;
        if(First == NULL)
            return NULL;

        Next = First->Next;
    } while(::InterlockedCompareExchangePointer((volatile PVOID*)&Head->Next, Next, First) != First);

    return First;
}

extern "C" PVOID __fastcall EmuInterlockedPushEntrySList(PVOID ListHead, PVOID ListEntry)
{
    if(ListHead == NULL || ListEntry == NULL)
        return NULL;

    EmuSingleListEntry *Head = (EmuSingleListEntry*)ListHead;
    EmuSingleListEntry *Entry = (EmuSingleListEntry*)ListEntry;
    EmuSingleListEntry *First;

    do
    {
        First = Head->Next;
        Entry->Next = First;
    } while(::InterlockedCompareExchangePointer((volatile PVOID*)&Head->Next, Entry, First) != First);

    return First;
}

extern "C" xboxkrnl::PLIST_ENTRY __fastcall EmuExfInterlockedInsertHeadList
(
    IN OUT xboxkrnl::PLIST_ENTRY ListHead,
    IN OUT xboxkrnl::PLIST_ENTRY ListEntry
)
{
    if(ListHead == NULL || ListEntry == NULL)
        return NULL;

    xboxkrnl::PLIST_ENTRY First = ListHead->Flink;
    if(First == NULL)
        First = ListHead;

    ListEntry->Flink = First;
    ListEntry->Blink = ListHead;
    First->Blink = ListEntry;
    ListHead->Flink = ListEntry;

    if(ListHead->Blink == NULL || ListHead->Blink == ListHead)
        ListHead->Blink = ListEntry;

    return (First == ListHead) ? NULL : First;
}

extern "C" xboxkrnl::PLIST_ENTRY __fastcall EmuExfInterlockedInsertTailList
(
    IN OUT xboxkrnl::PLIST_ENTRY ListHead,
    IN OUT xboxkrnl::PLIST_ENTRY ListEntry
)
{
    if(ListHead == NULL || ListEntry == NULL)
        return NULL;

    xboxkrnl::PLIST_ENTRY Last = ListHead->Blink;
    if(Last == NULL)
        Last = ListHead;

    ListEntry->Flink = ListHead;
    ListEntry->Blink = Last;
    Last->Flink = ListEntry;
    ListHead->Blink = ListEntry;

    if(ListHead->Flink == NULL || ListHead->Flink == ListHead)
        ListHead->Flink = ListEntry;

    return (Last == ListHead) ? NULL : Last;
}

extern "C" xboxkrnl::PLIST_ENTRY __fastcall EmuExfInterlockedRemoveHeadList
(
    IN OUT xboxkrnl::PLIST_ENTRY ListHead
)
{
    if(ListHead == NULL)
        return NULL;

    xboxkrnl::PLIST_ENTRY First = ListHead->Flink;
    if(First == NULL || First == ListHead)
        return NULL;

    xboxkrnl::PLIST_ENTRY Next = First->Flink;
    ListHead->Flink = Next;
    if(Next != NULL)
        Next->Blink = ListHead;

    if(ListHead->Blink == First)
        ListHead->Blink = ListHead;

    First->Flink = NULL;
    First->Blink = NULL;
    return First;
}

static thread_local UCHAR g_EmuCurrentIrql = 0;
static xboxkrnl::PKDPC g_EmuPendingDpc = NULL;
extern "C"
{
ULONGLONG g_EmuKeInterruptTime = 0;
ULONGLONG g_EmuKeSystemTime = 0;
ULONG g_EmuKeTimeIncrement = 10000;
ULONG g_EmuKiBugCheckData[5] = {};
ULONG g_EmuMmGlobalData[4] = {};
}

struct EmuSimpleDispatcherObject
{
    xboxkrnl::DISPATCHER_HEADER Header;
    LONG Limit;
};

struct EmuSimpleQueue
{
    xboxkrnl::DISPATCHER_HEADER Header;
    xboxkrnl::LIST_ENTRY EntryListHead;
    ULONG Count;
};

static void EmuInitializeListHead(xboxkrnl::LIST_ENTRY *List)
{
    if(List != NULL)
    {
        List->Flink = List;
        List->Blink = List;
    }
}

static void EmuInitializeDispatcherHeader(xboxkrnl::DISPATCHER_HEADER *Header, UCHAR Type, LONG SignalState)
{
    if(Header == NULL || !EmuIsWritableMemoryRange(Header, sizeof(*Header)))
        return;

    Header->Type = Type;
    Header->Absolute = 0;
    Header->Size = sizeof(*Header) / sizeof(ULONG);
    Header->Inserted = 0;
    Header->SignalState = SignalState;
    EmuInitializeListHead(&Header->WaitListHead);
}

static void EmuRefreshKernelTimeGlobals()
{
    FILETIME FileTime;
    GetSystemTimeAsFileTime(&FileTime);
    g_EmuKeSystemTime = ((ULONGLONG)FileTime.dwHighDateTime << 32) | FileTime.dwLowDateTime;
    g_EmuKeInterruptTime = (ULONGLONG)GetTickCount() * 10000;
    xboxkrnl::KeTickCount = GetTickCount();
}

extern "C" BOOLEAN NTAPI EmuKeInsertDeviceQueue(PVOID DeviceQueue, PVOID DeviceQueueEntry);
extern "C" LONG NTAPI EmuKeInsertQueue(PVOID QueueObject, PVOID Entry);
extern "C" PVOID NTAPI EmuKeRemoveDeviceQueue(PVOID DeviceQueue);
extern "C" VOID __fastcall EmuKfLowerIrql(UCHAR NewIrql);

extern "C" VOID NTAPI EmuKeBugCheck(ULONG BugCheckCode)
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuKiBugCheckData[0] = BugCheckCode;
    printf("EmuKrnl (0x%lX): KeBugCheck code=0x%.08lX caller=0x%.08lX.\n",
           GetCurrentThreadId(), BugCheckCode, (ULONG)_ReturnAddress());
    EmuCleanup("Guest called KeBugCheck(0x%.08lX)", BugCheckCode);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuKeBugCheckEx(ULONG BugCheckCode, ULONG Parameter1, ULONG Parameter2, ULONG Parameter3, ULONG Parameter4)
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuKiBugCheckData[0] = BugCheckCode;
    g_EmuKiBugCheckData[1] = Parameter1;
    g_EmuKiBugCheckData[2] = Parameter2;
    g_EmuKiBugCheckData[3] = Parameter3;
    g_EmuKiBugCheckData[4] = Parameter4;
    printf("EmuKrnl (0x%lX): KeBugCheckEx code=0x%.08lX p1=0x%.08lX p2=0x%.08lX p3=0x%.08lX p4=0x%.08lX.\n",
           GetCurrentThreadId(), BugCheckCode, Parameter1, Parameter2, Parameter3, Parameter4);
    EmuCleanup("Guest called KeBugCheckEx(0x%.08lX)", BugCheckCode);

    EmuSwapFS();   // Xbox FS
}

extern "C" BOOLEAN NTAPI EmuKeCancelTimer(xboxkrnl::PKTIMER Timer)
{
    EmuSwapFS();   // Win2k/XP FS

    BOOLEAN WasInserted = FALSE;
    if(Timer != NULL && EmuIsWritableMemoryRange(Timer, sizeof(*Timer)))
    {
        WasInserted = Timer->Header.Inserted != 0;
        Timer->Header.Inserted = 0;
        Timer->Header.SignalState = 0;
        Timer->Dpc = NULL;
    }

    EmuSwapFS();   // Xbox FS

    return WasInserted;
}

extern "C" VOID NTAPI EmuKeInitializeApc(PVOID Apc, PVOID Thread, UCHAR ApcStateIndex, PVOID KernelRoutine,
                                         PVOID RundownRoutine, PVOID NormalRoutine, UCHAR ApcMode, PVOID NormalContext)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Apc != NULL && EmuIsWritableMemoryRange(Apc, 0x30))
        ZeroMemory(Apc, 0x30);

    printf("EmuKrnl (0x%lX): KeInitializeApc apc=%p thread=%p kernel=%p normal=%p.\n",
           GetCurrentThreadId(), Apc, Thread, KernelRoutine, NormalRoutine);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuKeInitializeDeviceQueue(PVOID DeviceQueue)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleQueue *Queue = (EmuSimpleQueue*)DeviceQueue;
    if(Queue != NULL && EmuIsWritableMemoryRange(Queue, sizeof(*Queue)))
    {
        EmuInitializeDispatcherHeader(&Queue->Header, 0x14, 0);
        EmuInitializeListHead(&Queue->EntryListHead);
        Queue->Count = 0;
    }

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuKeInitializeEvent(PVOID Event, ULONG Type, BOOLEAN State)
{
    EmuSwapFS();   // Win2k/XP FS
    EmuInitializeDispatcherHeader((xboxkrnl::DISPATCHER_HEADER*)Event, (UCHAR)Type, State ? 1 : 0);
    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuKeInitializeMutant(PVOID Mutant, BOOLEAN InitialOwner)
{
    EmuSwapFS();   // Win2k/XP FS
    EmuInitializeDispatcherHeader((xboxkrnl::DISPATCHER_HEADER*)Mutant, 0x02, InitialOwner ? 0 : 1);
    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuKeInitializeQueue(PVOID QueueObject, ULONG Count)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleQueue *Queue = (EmuSimpleQueue*)QueueObject;
    if(Queue != NULL && EmuIsWritableMemoryRange(Queue, sizeof(*Queue)))
    {
        EmuInitializeDispatcherHeader(&Queue->Header, 0x04, 0);
        EmuInitializeListHead(&Queue->EntryListHead);
        Queue->Count = Count;
    }

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuKeInitializeSemaphore(PVOID Semaphore, LONG Count, LONG Limit)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleDispatcherObject *Object = (EmuSimpleDispatcherObject*)Semaphore;
    if(Object != NULL && EmuIsWritableMemoryRange(Object, sizeof(*Object)))
    {
        EmuInitializeDispatcherHeader(&Object->Header, 0x05, Count);
        Object->Limit = Limit;
    }

    EmuSwapFS();   // Xbox FS
}

extern "C" BOOLEAN NTAPI EmuKeInsertByKeyDeviceQueue(PVOID DeviceQueue, PVOID DeviceQueueEntry, ULONG SortKey)
{
    return EmuKeInsertDeviceQueue(DeviceQueue, DeviceQueueEntry);
}

extern "C" BOOLEAN NTAPI EmuKeInsertDeviceQueue(PVOID DeviceQueue, PVOID DeviceQueueEntry)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleQueue *Queue = (EmuSimpleQueue*)DeviceQueue;
    BOOLEAN WasBusy = FALSE;
    if(Queue != NULL && EmuIsWritableMemoryRange(Queue, sizeof(*Queue)))
    {
        WasBusy = Queue->Count != 0;
        Queue->Count++;
        Queue->Header.SignalState = (Queue->Count != 0) ? 1 : 0;
    }

    EmuSwapFS();   // Xbox FS

    return WasBusy;
}

extern "C" LONG NTAPI EmuKeInsertHeadQueue(PVOID QueueObject, PVOID Entry)
{
    return EmuKeInsertQueue(QueueObject, Entry);
}

extern "C" LONG NTAPI EmuKeInsertQueue(PVOID QueueObject, PVOID Entry)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleQueue *Queue = (EmuSimpleQueue*)QueueObject;
    LONG PreviousCount = 0;
    if(Queue != NULL && EmuIsWritableMemoryRange(Queue, sizeof(*Queue)))
    {
        PreviousCount = Queue->Header.SignalState;
        Queue->Count++;
        Queue->Header.SignalState++;
    }

    EmuSwapFS();   // Xbox FS

    return PreviousCount;
}

extern "C" BOOLEAN NTAPI EmuKeInsertQueueApc(PVOID Apc, PVOID SystemArgument1, PVOID SystemArgument2, UCHAR Increment)
{
    EmuSwapFS();   // Win2k/XP FS
    printf("EmuKrnl (0x%lX): KeInsertQueueApc apc=%p arg1=%p arg2=%p increment=%lu.\n",
           GetCurrentThreadId(), Apc, SystemArgument1, SystemArgument2, (ULONG)Increment);
    EmuSwapFS();   // Xbox FS
    return TRUE;
}

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
    if(Request < 32)
        g_EmuHalSoftwareInterruptMask |= (1u << Request);
}

extern "C" VOID __fastcall EmuHalClearSoftwareInterrupt(UCHAR Request)
{
    if(Request < 32)
        g_EmuHalSoftwareInterruptMask &= ~(1u << Request);
}

extern "C" VOID NTAPI EmuHalDisableSystemInterrupt(UCHAR BusInterruptLevel)
{
    EmuSwapFS();   // Win2k/XP FS

    if(BusInterruptLevel < 32)
        g_EmuHalDisabledInterruptMask |= (1u << BusInterruptLevel);

    printf("EmuKrnl (0x%lX): HalDisableSystemInterrupt level=0x%.02X mask=0x%.08lX.\n",
           GetCurrentThreadId(), BusInterruptLevel, g_EmuHalDisabledInterruptMask);

    EmuSwapFS();   // Xbox FS
}

// 0x002B - HalEnableSystemInterrupt: the complement of HalDisableSystemInterrupt;
// clear the interrupt's mask bit so it is delivered again.
extern "C" VOID NTAPI EmuHalEnableSystemInterrupt(UCHAR BusInterruptLevel, UCHAR InterruptMode)
{
    EmuSwapFS();   // Win2k/XP FS

    if(BusInterruptLevel < 32)
        g_EmuHalDisabledInterruptMask &= ~(1u << BusInterruptLevel);

    printf("EmuKrnl (0x%lX): HalEnableSystemInterrupt level=0x%.02X mode=0x%.02X mask=0x%.08lX.\n",
           GetCurrentThreadId(), BusInterruptLevel, InterruptMode, g_EmuHalDisabledInterruptMask);

    EmuSwapFS();   // Xbox FS
}

// 0x016E - HalWriteSMCScratchRegister: the SMC scratch dword carries reboot/quick-
// boot flags across a warm reset. No SMC hardware here; retain the value so a
// subsequent read (were one wired) is consistent.
static ULONG g_EmuSmcScratchRegister = 0;
extern "C" VOID NTAPI EmuHalWriteSMCScratchRegister(ULONG ScratchRegister)
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuSmcScratchRegister = ScratchRegister;

    printf("EmuKrnl (0x%lX): HalWriteSMCScratchRegister value=0x%.08lX.\n",
           GetCurrentThreadId(), ScratchRegister);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuHalEnableSecureTrayEject(ULONG BusInterruptLevel, BOOLEAN Enable)
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuSecureTrayEjectEnabled = Enable;

    if(BusInterruptLevel < 32 && Enable)
        g_EmuHalDisabledInterruptMask &= ~(1u << BusInterruptLevel);

    printf("EmuKrnl (0x%lX): HalEnableSecureTrayEject level=0x%.08lX enable=%lu.\n",
           GetCurrentThreadId(), BusInterruptLevel, (ULONG)Enable);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuHalEnableSecureTrayEjectCompat()
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuSecureTrayEjectEnabled = TRUE;

    printf("EmuKrnl (0x%lX): HalEnableSecureTrayEject compat enable=1.\n",
           GetCurrentThreadId());

    EmuSwapFS();   // Xbox FS
}

extern "C" BOOLEAN NTAPI EmuHalIsResetOrShutdownPending()
{
    return (g_EmuResetOrShutdownPending || g_EmuShutdownInitiated) ? TRUE : FALSE;
}

extern "C" VOID NTAPI EmuHalInitiateShutdown()
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuResetOrShutdownPending = TRUE;
    g_EmuShutdownInitiated = TRUE;

    printf("EmuKrnl (0x%lX): HalInitiateShutdown marked pending.\n",
           GetCurrentThreadId());

    EmuSwapFS();   // Xbox FS
}

extern "C" ULONG NTAPI EmuPhyInitialize(ULONG ForceReset, ULONG Reserved)
{
    EmuSwapFS();   // Win2k/XP FS

    g_EmuPhyInitialized = 1;
    g_EmuPhyLinkState = 0;

    printf("EmuKrnl (0x%lX): PhyInitialize force=0x%.08lX reserved=0x%.08lX link=0x%.08lX.\n",
           GetCurrentThreadId(), ForceReset, Reserved, g_EmuPhyLinkState);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" ULONG NTAPI EmuPhyGetLinkState(ULONG ForceUpdate)
{
    EmuSwapFS();   // Win2k/XP FS

    if(!g_EmuPhyInitialized)
        g_EmuPhyInitialized = 1;

    ULONG LinkState = g_EmuPhyLinkState;

    printf("EmuKrnl (0x%lX): PhyGetLinkState force=0x%.08lX link=0x%.08lX.\n",
           GetCurrentThreadId(), ForceUpdate, LinkState);

    EmuSwapFS();   // Xbox FS

    return LinkState;
}

extern "C" VOID NTAPI EmuHalRegisterShutdownNotification(PVOID ShutdownRegistration, BOOLEAN Register)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): HalRegisterShutdownNotification registration=%p register=%lu.\n",
           GetCurrentThreadId(), ShutdownRegistration, (ULONG)Register);

    // An UNregister mid-boot is a teardown-on-failure tell (an LTCG D3D device
    // that could not finish CreateDevice unwinds through here first). Print
    // the guest return-address candidates from the caller's stack so the
    // failing init function's error path is attributed without a guest
    // debugger.
    if(!Register)
    {
        ULONG *Stack = (ULONG *)&ShutdownRegistration;
        int Printed = 0;

        for(int i = 0; i < 512 && Printed < 24; i++)
        {
            if(IsBadReadPtr(&Stack[i], sizeof(ULONG)))
                break;

            ULONG Word = Stack[i];

            if(Word >= 0x00011000 && Word < 0x00400000)
            {
                printf("EmuKrnl (0x%lX): HalShutdown unwind stack[%03d] = 0x%.08lX\n",
                       GetCurrentThreadId(), i, Word);
                Printed++;
            }
        }

        fflush(stdout);
    }

    EmuSwapFS();   // Xbox FS
}

typedef BOOLEAN (NTAPI *EmuInterruptServiceRoutine)(PVOID Interrupt, PVOID ServiceContext);

struct EmuKInterrupt
{
    EmuInterruptServiceRoutine ServiceRoutine;
    PVOID ServiceContext;
    ULONG BusInterruptLevel;
    ULONG Irql;
    BOOLEAN Connected;
    BOOLEAN ShareVector;
    ULONG Mode;
    ULONG ServiceCount;
    ULONG DispatchCode[22];
};

static PVOID g_EmuInterruptList[16] = {};

static ULONG EmuInterruptVectorToIrq(ULONG Vector)
{
    if(Vector >= 0x30)
        return Vector - 0x30;

    return Vector;
}

extern "C" ULONG NTAPI EmuHalGetInterruptVector(ULONG BusInterruptLevel, PUCHAR Irql)
{
    EmuSwapFS();   // Win2k/XP FS

    ULONG Vector = 0;
    if(BusInterruptLevel < sizeof(g_EmuInterruptList) / sizeof(g_EmuInterruptList[0]))
    {
        Vector = 0x30 + BusInterruptLevel;
        if(Irql != NULL && EmuIsWritableMemoryRange(Irql, sizeof(*Irql)))
            *Irql = (UCHAR)BusInterruptLevel;
    }

    printf("EmuKrnl (0x%lX): HalGetInterruptVector level=0x%.08lX vector=0x%.08lX.\n",
           GetCurrentThreadId(), BusInterruptLevel, Vector);

    EmuSwapFS();   // Xbox FS

    return Vector;
}

// ---------------------------------------------------------------------------
// Vertical-blank interrupt delivery
//
// A natively-running XDK title programs the NV2A directly and blocks in D3D's
// BlockUntilVerticalBlank until the display interrupt fires. Cxbx has no real
// GPU generating vblanks, so this thread synthesizes them: ~60x/second it raises
// the CRTC vblank in the NV2A model and invokes the guest's connected level-3
// ISR (guest code, hence the FS dance from PCSTProxy). The ISR acks the source
// and queues the DPC that signals the vblank event, releasing the render loop.
// ---------------------------------------------------------------------------
extern "C" void EmuNv2aRaiseVblank();
extern "C" void EmuNv2aAckVblank();
extern "C" void EmuNv2aEnableGpuInterrupts();
extern "C" int EmuNv2aVblankEnabled();
extern "C" int EmuNv2aRenderStarted();

static const ULONG EmuDisplayInterruptLevel = 3;
static volatile LONG g_EmuVblankThreadStarted = 0;

static DWORD WINAPI EmuVblankThread(LPVOID)
{
    EmuGenerateFS(g_pTLS, g_pTLSData);

    printf("EmuKrnl (0x%lX): vblank thread started.\n", GetCurrentThreadId());
    fflush(stdout);

    for(;;)
    {
        Sleep(16);   // ~60 Hz

        EmuKInterrupt *Interrupt = (EmuKInterrupt*)g_EmuInterruptList[EmuDisplayInterruptLevel];
        if(Interrupt == NULL || !Interrupt->Connected || Interrupt->ServiceRoutine == NULL)
            continue;

        if(!EmuNv2aVblankEnabled())
            continue;

        // Hold off until render-state init is done (first pushbuffer submitted);
        // firing the ISR during early GPU init races the main thread and crashes.
        if(!EmuNv2aRenderStarted())
            continue;

        EmuNv2aRaiseVblank();

        EmuSwapFS();   // Xbox FS
        __try
        {
            Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        EmuSwapFS();   // Win2k/XP FS

        // The ISR acknowledges vblank out-of-band (legacy port I/O), so clear the
        // pending bit here; otherwise PMC_INTR_0 stays PCRTC-pending forever and
        // the title's interrupt state machine deadlocks.
        EmuNv2aAckVblank();

        // Re-enable PMC interrupts on the title's behalf: its vblank path masks
        // them and re-enables via a callback slot that is null in this HLE, so
        // without this its vblank-wait loop stays masked and never advances.
        EmuNv2aEnableGpuInterrupts();
    }

    return 0;
}

static void EmuStartVblankThread()
{
    // Opt-in: synthesizing vblanks lets a natively-running XDK title advance past
    // BlockUntilVerticalBlank into its frame loop, but the native D3D8 display ISR
    // still expects driver-side state this HLE does not fully provide, so firing it
    // can destabilize the title. Off by default (clean stall, probes unaffected);
    // set CXBX_ENABLE_VBLANK=1 to drive it while iterating on the ISR path.
    if(getenv("CXBX_ENABLE_VBLANK") == NULL)
        return;

    if(InterlockedExchange(&g_EmuVblankThreadStarted, 1) == 0)
    {
        printf("EmuKrnl (0x%lX): starting vblank delivery thread.\n", GetCurrentThreadId());
        fflush(stdout);
        CreateThread(NULL, 0, EmuVblankThread, NULL, 0, NULL);
    }
}

// The APU/audio interrupt (bus levels 5/6) is edge-driven by buffer completion on
// real hardware; an audio-clocked title (e.g. FCEUltra) blocks in its init/main
// loop until its audio ISR runs. This HLE has no APU DMA to raise it, so synthesize
// it: once an audio-level interrupt is connected, fire its ServiceRoutine on a timer.
static const ULONG EmuAudioInterruptLevels[2] = { 5, 6 };
static volatile LONG g_EmuAudioThreadStarted = 0;

extern "C" void EmuAciSignalAudioInterrupt();   // Emu.cpp: raise GLOB_STA PCM-out status

static DWORD WINAPI EmuAudioInterruptThread(LPVOID)
{
    EmuGenerateFS(g_pTLS, g_pTLSData);

    printf("EmuKrnl (0x%lX): audio-interrupt thread started.\n", GetCurrentThreadId());
    fflush(stdout);

    for(;;)
    {
        Sleep(8);   // ~125 Hz, roughly an APU buffer cadence

        for(int i = 0; i < 2; i++)
        {
            EmuKInterrupt *Interrupt = (EmuKInterrupt*)g_EmuInterruptList[EmuAudioInterruptLevels[i]];
            if(Interrupt == NULL || !Interrupt->Connected || Interrupt->ServiceRoutine == NULL)
                continue;

            // Raise a buffer-completion status so the ISR treats this as a real
            // interrupt (it masks GLOB_STA & 0x51 and ignores spurious ones).
            EmuAciSignalAudioInterrupt();

            EmuSwapFS();   // Xbox FS
            __try
            {
                Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            EmuSwapFS();   // Win2k/XP FS
        }
    }

    return 0;
}

// AC97 bus-master DMA delivery: once a title sets a channel's run bit, tick the
// register-side DMA model (EmuAciDmaAdvance in Emu.cpp -- CIV/PICB/SR/GLOB_STA
// advancement) and deliver the buffer-completion interrupt to the connected
// audio-level ISR. Unlike the timer-based CXBX_APU_IRQ thread this only fires
// when the title actually programmed and ran a buffer queue, matching the
// edge-driven hardware behavior.
extern "C" int EmuAciDmaAdvance();
static volatile LONG g_EmuAciDmaThreadStarted = 0;

static DWORD WINAPI EmuAciDmaThread(LPVOID)
{
    EmuGenerateFS(g_pTLS, g_pTLSData);

    printf("EmuKrnl (0x%lX): AC97 DMA thread started.\n", GetCurrentThreadId());
    fflush(stdout);

    for(;;)
    {
        Sleep(5);   // ~200 Hz buffer cadence

        if(EmuAciDmaAdvance() == 0)
            continue;

        for(int i = 0; i < 2; i++)
        {
            EmuKInterrupt *Interrupt = (EmuKInterrupt*)g_EmuInterruptList[EmuAudioInterruptLevels[i]];
            if(Interrupt == NULL || !Interrupt->Connected || Interrupt->ServiceRoutine == NULL)
                continue;

            EmuSwapFS();   // Xbox FS
            __try
            {
                Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
            }
            EmuSwapFS();   // Win2k/XP FS
        }
    }

    return 0;
}

extern "C" void EmuAciStartDmaThread()
{
    if(InterlockedExchange(&g_EmuAciDmaThreadStarted, 1) == 0)
    {
        printf("EmuKrnl (0x%lX): starting AC97 DMA thread.\n", GetCurrentThreadId());
        fflush(stdout);
        CreateThread(NULL, 0, EmuAciDmaThread, NULL, 0, NULL);
    }
}

static void EmuStartAudioInterruptThread()
{
    // Opt-in only: the synthesized APU interrupt fires an ISR asynchronously and
    // can deadlock a title against a DSOUND critical section (see the FCEUltra
    // trace). Gate it on its own env var, decoupled from CXBX_AUTOBOOT_ROM, so a
    // title can auto-boot a ROM without the async ISR. Leaves the probes alone.
    char enabled[8] = {0};
    if(GetEnvironmentVariableA("CXBX_APU_IRQ", enabled, sizeof(enabled)) == 0)
        return;

    if(InterlockedExchange(&g_EmuAudioThreadStarted, 1) == 0)
    {
        printf("EmuKrnl (0x%lX): starting audio-interrupt delivery thread.\n", GetCurrentThreadId());
        fflush(stdout);
        CreateThread(NULL, 0, EmuAudioInterruptThread, NULL, 0, NULL);
    }
}

// The USB host-controller interrupt (bus level 1). An nxdk title connects an
// OHCI ISR and then busy-waits on a memory flag its ISR/DPC updates once per
// frame (start-of-frame). This HLE has no real controller ticking the frame
// counter, so synthesize it: raise a SOF source and fire the connected level-1
// ISR on a timer, letting the title's USB frame processing advance.
static const ULONG EmuUsbInterruptLevel = 1;
static volatile LONG g_EmuUsbThreadStarted = 0;

extern "C" void EmuUsb0SignalInterrupt();   // Emu.cpp: raise HcInterruptStatus SOF

static DWORD WINAPI EmuUsbInterruptThread(LPVOID)
{
    EmuGenerateFS(g_pTLS, g_pTLSData);

    printf("EmuKrnl (0x%lX): USB-interrupt thread started.\n", GetCurrentThreadId());
    fflush(stdout);

    for(;;)
    {
        Sleep(8);   // ~125 Hz, roughly an OHCI frame cadence

        EmuKInterrupt *Interrupt = (EmuKInterrupt*)g_EmuInterruptList[EmuUsbInterruptLevel];
        if(Interrupt == NULL || !Interrupt->Connected || Interrupt->ServiceRoutine == NULL)
            continue;

        EmuUsb0SignalInterrupt();   // start-of-frame source pending

        EmuSwapFS();   // Xbox FS
        __try
        {
            Interrupt->ServiceRoutine(Interrupt, Interrupt->ServiceContext);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
        }
        EmuSwapFS();   // Win2k/XP FS
    }

    return 0;
}

static void EmuStartUsbInterruptThread()
{
    // Opt-in (CXBX_USB_IRQ=1): firing an ISR asynchronously can destabilize a
    // title, so keep it off by default and leave the probes untouched.
    char enabled[8] = {0};
    if(GetEnvironmentVariableA("CXBX_USB_IRQ", enabled, sizeof(enabled)) == 0)
        return;

    if(InterlockedExchange(&g_EmuUsbThreadStarted, 1) == 0)
    {
        printf("EmuKrnl (0x%lX): starting USB-interrupt delivery thread.\n", GetCurrentThreadId());
        fflush(stdout);
        CreateThread(NULL, 0, EmuUsbInterruptThread, NULL, 0, NULL);
    }
}

extern "C" BOOLEAN NTAPI EmuKeConnectInterrupt(PVOID InterruptObject)
{
    EmuSwapFS();   // Win2k/XP FS

    BOOLEAN Connected = FALSE;
    EmuKInterrupt *Interrupt = (EmuKInterrupt*)InterruptObject;

    bool Writable = EmuIsWritableMemoryRange(Interrupt, sizeof(*Interrupt));
    ULONG Level = Writable ? Interrupt->BusInterruptLevel : 0xFFFFFFFF;
    ULONG SlotCount = sizeof(g_EmuInterruptList) / sizeof(g_EmuInterruptList[0]);

    if(Writable && Level < SlotCount)
    {
        // A slot occupied by the SAME interrupt object is re-connectable: a
        // title's audio init may tear down and re-run (KeInitializeInterrupt
        // rewrites the object, clearing its Connected flag) and then connect
        // again -- z26x loops its whole audio setup on this succeeding.
        if(!Interrupt->Connected &&
           (g_EmuInterruptList[Level] == NULL || g_EmuInterruptList[Level] == Interrupt))
        {
            Interrupt->Connected = TRUE;
            g_EmuInterruptList[Level] = Interrupt;
            Connected = TRUE;
        }
    }

    printf("EmuKrnl (0x%lX): KeConnectInterrupt interrupt=0x%.08lX level=0x%lX connected=%lu.\n",
           GetCurrentThreadId(), (ULONG)InterruptObject, Level, (ULONG)Connected);

    if(Connected && Interrupt->BusInterruptLevel == EmuDisplayInterruptLevel)
        EmuStartVblankThread();

    if(Connected && (Level == EmuAudioInterruptLevels[0] || Level == EmuAudioInterruptLevels[1]))
        EmuStartAudioInterruptThread();

    if(Connected && Level == EmuUsbInterruptLevel)
        EmuStartUsbInterruptThread();

    EmuSwapFS();   // Xbox FS

    return Connected;
}

// ******************************************************************
// * 0x0064 - KeDisconnectInterrupt
// ******************************************************************
// Detach a previously connected interrupt. Left a PANIC stub, nxdk pbkit's call
// (with a 1-pointer __stdcall arg) fell through the argument-less unimplemented
// trampoline, which cleaned zero stack args and left the caller's frame skewed
// -- corrupting ESP so the caller returned to garbage. Implementing it with the
// correct signature keeps the stack balanced (and actually detaches).
extern "C" BOOLEAN NTAPI EmuKeDisconnectInterrupt(PVOID InterruptObject)
{
    EmuSwapFS();   // Win2k/XP FS

    BOOLEAN WasConnected = FALSE;
    EmuKInterrupt *Interrupt = (EmuKInterrupt*)InterruptObject;

    if(EmuIsWritableMemoryRange(Interrupt, sizeof(*Interrupt)) &&
       Interrupt->BusInterruptLevel < sizeof(g_EmuInterruptList) / sizeof(g_EmuInterruptList[0]))
    {
        if(Interrupt->Connected)
        {
            Interrupt->Connected = FALSE;
            if(g_EmuInterruptList[Interrupt->BusInterruptLevel] == Interrupt)
                g_EmuInterruptList[Interrupt->BusInterruptLevel] = NULL;
            WasConnected = TRUE;
        }
    }

    printf("EmuKrnl (0x%lX): KeDisconnectInterrupt interrupt=0x%.08lX was=%lu.\n",
           GetCurrentThreadId(), (ULONG)InterruptObject, (ULONG)WasConnected);

    EmuSwapFS();   // Xbox FS

    return WasConnected;
}

extern "C" VOID NTAPI EmuKeInitializeInterrupt(PVOID InterruptObject, PVOID ServiceRoutine, PVOID ServiceContext,
                                               ULONG Vector, ULONG Irql, ULONG InterruptMode, BOOLEAN ShareVector)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuKInterrupt *Interrupt = (EmuKInterrupt*)InterruptObject;
    if(EmuIsWritableMemoryRange(Interrupt, sizeof(*Interrupt)))
    {
        Interrupt->ServiceRoutine = (EmuInterruptServiceRoutine)ServiceRoutine;
        Interrupt->ServiceContext = ServiceContext;
        Interrupt->BusInterruptLevel = EmuInterruptVectorToIrq(Vector);
        Interrupt->Irql = Irql;
        Interrupt->Connected = FALSE;
        Interrupt->ShareVector = ShareVector;
        Interrupt->Mode = InterruptMode;
        Interrupt->ServiceCount = 0;
        memset(Interrupt->DispatchCode, 0, sizeof(Interrupt->DispatchCode));
    }

    printf("EmuKrnl (0x%lX): KeInitializeInterrupt interrupt=0x%.08lX vector=0x%.08lX irql=0x%.08lX.\n",
           GetCurrentThreadId(), (ULONG)InterruptObject, Vector, Irql);

    EmuSwapFS();   // Xbox FS
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

// 0x005C - KeAlertResumeThread: alert the thread's alertable wait (a no-op here,
// as our waits are not alert-driven) and decrement its suspend count, returning
// the previous count -- same resume path as KeResumeThread.
extern "C" ULONG NTAPI EmuKeAlertResumeThread(xboxkrnl::PKTHREAD Thread)
{
    EmuSwapFS();   // Win2k/XP FS

    ULONG PreviousCount = 0;
    EmuThreadObjectHeader *ThreadHeader = EmuThreadHeaderFromThread(Thread);
    if(ThreadHeader != NULL)
    {
        PreviousCount = ThreadHeader->SuspendCount;
        if(PreviousCount != 0)
        {
            ResumeThread(ThreadHeader->HostHandle);
            ThreadHeader->SuspendCount--;
        }
    }

    EmuSwapFS();   // Xbox FS

    return PreviousCount;
}

// 0x005D - KeAlertThread: mark a thread alerted for the given mode. Our waits are
// not alert-driven, so report "was not previously alerted".
extern "C" BOOLEAN NTAPI EmuKeAlertThread(xboxkrnl::PKTHREAD Thread, UCHAR AlertMode)
{
    return FALSE;
}

// 0x005E - KeBoostPriorityThread: a transient scheduler priority boost. The host
// scheduler owns thread priorities here, so this is advisory only.
extern "C" VOID NTAPI EmuKeBoostPriorityThread(xboxkrnl::PKTHREAD Thread, LONG Increment)
{
}

extern "C" ULONG NTAPI EmuKeSuspendThread(xboxkrnl::PKTHREAD Thread)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuThreadObjectHeader *ThreadHeader = EmuThreadHeaderFromThread(Thread);
    ULONG PreviousCount = ThreadHeader->SuspendCount;

    if(PreviousCount >= 0x7F)
    {
        ULONG GuestEip;
        ULONG GuestEsp;
        ULONG GuestEbp;

#if defined(_MSC_VER) && defined(_M_IX86)
        __asm
        {
            mov eax, [ebp+4]
            mov GuestEip, eax
            lea eax, [ebp+12]
            mov GuestEsp, eax
            mov eax, [ebp]
            mov GuestEbp, eax
        }
#else
        GuestEip = 0;
        GuestEsp = 0;
        GuestEbp = 0;
#endif

        EmuSwapFS();   // Xbox FS
        EmuRaiseGuestException(EmuStatusSuspendCountExceeded, GuestEip, GuestEsp, GuestEbp);
        return PreviousCount;
    }

    SuspendThread(ThreadHeader->HostHandle);
    ThreadHeader->SuspendCount++;

    EmuSwapFS();   // Xbox FS

    return PreviousCount;
}

// ******************************************************************
// * 0x0091 - KeSetEvent
// ******************************************************************
// Signals a dispatcher event and returns its previous signal state. The XDK
// present/vblank path calls this every frame to wake the render thread that
// waits on the swap-done event; left as a PANIC stub, the event never signals
// and the title spins forever in its vblank loop. Only SignalState is modelled
// (the waiter polls it), which is enough to release the loop.
extern "C" LONG NTAPI EmuKeSetEvent(PVOID Event, LONG Increment, UCHAR Wait)
{
    EmuSwapFS();   // Win2k/XP FS

    LONG PreviousState = 0;
    xboxkrnl::DISPATCHER_HEADER *Header = (xboxkrnl::DISPATCHER_HEADER*)Event;

    if(Header != NULL && EmuIsWritableMemoryRange(Header, sizeof(*Header)))
    {
        PreviousState = Header->SignalState;
        Header->SignalState = 1;   // signaled
    }

    static ULONG s_LogCount = 0;
    if(s_LogCount < 4)
    {
        printf("EmuKrnl (0x%lX): KeSetEvent(event=%p increment=%ld wait=%lu) previous=%ld.\n",
               GetCurrentThreadId(), Event, Increment, (ULONG)Wait, PreviousState);
        fflush(stdout);
        s_LogCount++;
    }

    EmuSwapFS();   // Xbox FS

    return PreviousState;
}

extern "C" VOID NTAPI EmuKeLowerIrql(UCHAR NewIrql)
{
    EmuKfLowerIrql(NewIrql);
}

extern "C" LONG NTAPI EmuKePulseEvent(PVOID Event, LONG Increment, BOOLEAN Wait)
{
    EmuSwapFS();   // Win2k/XP FS

    LONG PreviousState = 0;
    xboxkrnl::DISPATCHER_HEADER *Header = (xboxkrnl::DISPATCHER_HEADER*)Event;
    if(Header != NULL && EmuIsWritableMemoryRange(Header, sizeof(*Header)))
    {
        PreviousState = Header->SignalState;
        Header->SignalState = 0;
    }

    EmuSwapFS();   // Xbox FS

    return PreviousState;
}

extern "C" LONG NTAPI EmuKeQueryBasePriorityThread(xboxkrnl::PKTHREAD Thread)
{
    return 0;
}

extern "C" ULONGLONG NTAPI EmuKeQueryInterruptTime()
{
    EmuRefreshKernelTimeGlobals();
    return g_EmuKeInterruptTime;
}

static const LONGLONG EMU_XBOX_ACPI_FREQUENCY = 3375000;
static LONGLONG g_EmuHostPerformanceCounterStart = 0;
static LONGLONG g_EmuHostPerformanceCounterFrequency = 0;
static volatile LONG g_EmuHostPerformanceCounterInitState = 0;

static LONGLONG EmuGetScaledPerformanceCounter(LONGLONG TargetFrequency)
{
    LARGE_INTEGER HostCounter;

    if(InterlockedCompareExchange(&g_EmuHostPerformanceCounterInitState, 2, 2) != 2)
    {
        if(InterlockedCompareExchange(&g_EmuHostPerformanceCounterInitState, 1, 0) == 0)
        {
            LARGE_INTEGER HostFrequency;
            QueryPerformanceFrequency(&HostFrequency);
            QueryPerformanceCounter(&HostCounter);
            g_EmuHostPerformanceCounterStart = HostCounter.QuadPart;
            g_EmuHostPerformanceCounterFrequency = HostFrequency.QuadPart;
            InterlockedExchange(&g_EmuHostPerformanceCounterInitState, 2);
        }
        else
        {
            while(InterlockedCompareExchange(&g_EmuHostPerformanceCounterInitState, 2, 2) != 2)
                Sleep(0);
        }
    }

    QueryPerformanceCounter(&HostCounter);

    // Keep the counter relative to title startup and scale without overflowing.
    LONGLONG Elapsed = HostCounter.QuadPart - g_EmuHostPerformanceCounterStart;
    LONGLONG Whole = (Elapsed / g_EmuHostPerformanceCounterFrequency) * TargetFrequency;
    LONGLONG Part = (Elapsed % g_EmuHostPerformanceCounterFrequency) * TargetFrequency /
                    g_EmuHostPerformanceCounterFrequency;
    return Whole + Part;
}

extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuKeQueryPerformanceCounter(xboxkrnl::PLARGE_INTEGER Frequency)
{
    if(Frequency != NULL)
        Frequency->QuadPart = EMU_XBOX_ACPI_FREQUENCY;

    xboxkrnl::LARGE_INTEGER Counter;
    Counter.QuadPart = EmuGetScaledPerformanceCounter(EMU_XBOX_ACPI_FREQUENCY);
    return Counter;
}

extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuKeQueryPerformanceFrequency()
{
    xboxkrnl::LARGE_INTEGER Frequency;
    Frequency.QuadPart = EMU_XBOX_ACPI_FREQUENCY;
    return Frequency;
}

extern "C" LONG NTAPI EmuKeReleaseMutant(PVOID Mutant, LONG Increment, BOOLEAN Abandoned, BOOLEAN Wait)
{
    EmuSwapFS();   // Win2k/XP FS

    LONG PreviousState = 0;
    xboxkrnl::DISPATCHER_HEADER *Header = (xboxkrnl::DISPATCHER_HEADER*)Mutant;
    if(Header != NULL && EmuIsWritableMemoryRange(Header, sizeof(*Header)))
    {
        PreviousState = Header->SignalState;
        Header->SignalState = 1;
    }

    EmuSwapFS();   // Xbox FS

    return PreviousState;
}

extern "C" LONG NTAPI EmuKeReleaseSemaphore(PVOID Semaphore, LONG Increment, LONG Adjustment, BOOLEAN Wait)
{
    EmuSwapFS();   // Win2k/XP FS

    LONG PreviousState = 0;
    EmuSimpleDispatcherObject *Object = (EmuSimpleDispatcherObject*)Semaphore;
    if(Object != NULL && EmuIsWritableMemoryRange(Object, sizeof(*Object)))
    {
        PreviousState = Object->Header.SignalState;
        Object->Header.SignalState += Adjustment;
        if(Object->Limit != 0 && Object->Header.SignalState > Object->Limit)
            Object->Header.SignalState = Object->Limit;
    }

    EmuSwapFS();   // Xbox FS

    return PreviousState;
}

extern "C" PVOID NTAPI EmuKeRemoveByKeyDeviceQueue(PVOID DeviceQueue, ULONG SortKey)
{
    return EmuKeRemoveDeviceQueue(DeviceQueue);
}

extern "C" PVOID NTAPI EmuKeRemoveDeviceQueue(PVOID DeviceQueue)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleQueue *Queue = (EmuSimpleQueue*)DeviceQueue;
    if(Queue != NULL && EmuIsWritableMemoryRange(Queue, sizeof(*Queue)) && Queue->Count != 0)
    {
        Queue->Count--;
        Queue->Header.SignalState = (Queue->Count != 0) ? 1 : 0;
    }

    EmuSwapFS();   // Xbox FS

    return NULL;
}

extern "C" BOOLEAN NTAPI EmuKeRemoveEntryDeviceQueue(PVOID DeviceQueueEntry)
{
    return DeviceQueueEntry != NULL;
}

extern "C" PVOID NTAPI EmuKeRemoveQueue(PVOID QueueObject, UCHAR WaitMode, xboxkrnl::PLARGE_INTEGER Timeout)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleQueue *Queue = (EmuSimpleQueue*)QueueObject;
    if(Queue != NULL && EmuIsWritableMemoryRange(Queue, sizeof(*Queue)) && Queue->Count != 0)
    {
        Queue->Count--;
        Queue->Header.SignalState--;
    }

    EmuSwapFS();   // Xbox FS

    return NULL;
}

extern "C" LONG NTAPI EmuKeResetEvent(PVOID Event)
{
    EmuSwapFS();   // Win2k/XP FS

    LONG PreviousState = 0;
    xboxkrnl::DISPATCHER_HEADER *Header = (xboxkrnl::DISPATCHER_HEADER*)Event;
    if(Header != NULL && EmuIsWritableMemoryRange(Header, sizeof(*Header)))
    {
        PreviousState = Header->SignalState;
        Header->SignalState = 0;
    }

    EmuSwapFS();   // Xbox FS

    return PreviousState;
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

extern "C" NTSTATUS NTAPI EmuIoDismountVolume
(
    IN PVOID DeviceObject
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoDismountVolume device=%p.\n",
           GetCurrentThreadId(), DeviceObject);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuIoDismountVolumeByName
(
    IN xboxkrnl::PSTRING VolumeName
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoDismountVolumeByName name=%s.\n",
           GetCurrentThreadId(), (VolumeName != NULL && VolumeName->Buffer != NULL) ? VolumeName->Buffer : "<null>");

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuIoDismountVolumeByFileHandle
(
    IN HANDLE FileHandle
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoDismountVolumeByFileHandle handle=%p.\n",
           GetCurrentThreadId(), FileHandle);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuIoFreeIrp
(
    IN PVOID Irp
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuIrp *Object = EmuGetIrp(Irp);
    if(Object != NULL)
    {
        Object->Magic = 0;
        delete Object;
    }

    printf("EmuKrnl (0x%lX): IoFreeIrp irp=%p owned=%lu.\n",
           GetCurrentThreadId(), Irp, (ULONG)(Object != NULL));

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoInitializeIrp
(
    IN OUT PVOID Irp,
    IN USHORT PacketSize,
    IN xboxkrnl::CCHAR StackSize
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Irp != NULL && PacketSize >= sizeof(EmuIrp))
    {
        ZeroMemory(Irp, PacketSize);
        EmuIrp *Object = (EmuIrp*)Irp;
        Object->Magic = EmuIrpMagic;
        Object->Size = PacketSize;
        Object->StackSize = StackSize;
    }

    printf("EmuKrnl (0x%lX): IoInitializeIrp irp=%p size=0x%.04X stack=%d.\n",
           GetCurrentThreadId(), Irp, PacketSize, (int)StackSize);

    EmuSwapFS();   // Xbox FS
}

extern "C" NTSTATUS NTAPI EmuIoInvalidDeviceRequest
(
    IN PVOID DeviceObject,
    IN PVOID Irp
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuCompleteIrp(Irp, EmuStatusInvalidDeviceRequest, 0);

    printf("EmuKrnl (0x%lX): IoInvalidDeviceRequest device=%p irp=%p.\n",
           GetCurrentThreadId(), DeviceObject, Irp);

    EmuSwapFS();   // Xbox FS

    return EmuStatusInvalidDeviceRequest;
}

extern "C" NTSTATUS NTAPI EmuIoQueryFileInformation
(
    IN PVOID FileObject,
    IN ULONG FileInformationClass,
    IN ULONG Length,
    OUT PVOID FileInformation,
    OUT PULONG ReturnedLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnedLength != NULL)
        *ReturnedLength = 0;
    if(FileInformation != NULL && Length != 0)
        ZeroMemory(FileInformation, Length);

    printf("EmuKrnl (0x%lX): IoQueryFileInformation file=%p class=0x%.08lX length=0x%.08lX.\n",
           GetCurrentThreadId(), FileObject, FileInformationClass, Length);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuIoQueryVolumeInformation
(
    IN PVOID FileObject,
    IN ULONG FsInformationClass,
    IN ULONG Length,
    OUT PVOID FsInformation,
    OUT PULONG ReturnedLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnedLength != NULL)
        *ReturnedLength = 0;
    if(FsInformation != NULL && Length != 0)
        ZeroMemory(FsInformation, Length);

    printf("EmuKrnl (0x%lX): IoQueryVolumeInformation file=%p class=0x%.08lX length=0x%.08lX.\n",
           GetCurrentThreadId(), FileObject, FsInformationClass, Length);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuIoQueueThreadIrp
(
    IN PVOID Irp
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoQueueThreadIrp irp=%p.\n",
           GetCurrentThreadId(), Irp);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoRemoveShareAccess
(
    IN PVOID FileObject,
    IN PVOID ShareAccess
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ShareAccess != NULL)
        ZeroMemory(ShareAccess, 16);

    printf("EmuKrnl (0x%lX): IoRemoveShareAccess file=%p share=%p.\n",
           GetCurrentThreadId(), FileObject, ShareAccess);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoSetIoCompletion
(
    IN HANDLE IoCompletionHandle,
    IN PVOID KeyContext,
    IN PVOID ApcContext,
    IN NTSTATUS IoStatus,
    IN ULONG IoStatusInformation
)
{
    EmuNtSetIoCompletion(IoCompletionHandle, KeyContext, ApcContext, IoStatus, IoStatusInformation);
}

extern "C" VOID NTAPI EmuIoSetShareAccess
(
    IN ACCESS_MASK DesiredAccess,
    IN ULONG DesiredShareAccess,
    IN PVOID FileObject,
    OUT PVOID ShareAccess
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ShareAccess != NULL)
        ZeroMemory(ShareAccess, 16);

    printf("EmuKrnl (0x%lX): IoSetShareAccess access=0x%.08lX share=0x%.08lX file=%p out=%p.\n",
           GetCurrentThreadId(), DesiredAccess, DesiredShareAccess, FileObject, ShareAccess);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoStartNextPacket
(
    IN PVOID DeviceObject,
    IN BOOLEAN Cancelable
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoStartNextPacket device=%p cancelable=%lu.\n",
           GetCurrentThreadId(), DeviceObject, (ULONG)Cancelable);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoStartNextPacketByKey
(
    IN PVOID DeviceObject,
    IN BOOLEAN Cancelable,
    IN ULONG Key
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoStartNextPacketByKey device=%p cancelable=%lu key=0x%.08lX.\n",
           GetCurrentThreadId(), DeviceObject, (ULONG)Cancelable, Key);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoStartPacket
(
    IN PVOID DeviceObject,
    IN PVOID Irp,
    IN PULONG Key,
    IN PVOID CancelFunction
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuCompleteIrp(Irp, STATUS_SUCCESS, 0);

    printf("EmuKrnl (0x%lX): IoStartPacket device=%p irp=%p key=%p cancel=%p.\n",
           GetCurrentThreadId(), DeviceObject, Irp, Key, CancelFunction);

    EmuSwapFS();   // Xbox FS
}

extern "C" NTSTATUS NTAPI EmuIoSynchronousDeviceIoControlRequest
(
    IN ULONG IoControlCode,
    IN PVOID DeviceObject,
    IN PVOID InputBuffer,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer,
    IN ULONG OutputBufferLength,
    OUT PULONG ReturnedOutputLength,
    IN BOOLEAN InternalDeviceIoControl
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnedOutputLength != NULL)
        *ReturnedOutputLength = 0;
    if(OutputBuffer != NULL && OutputBufferLength != 0)
        ZeroMemory(OutputBuffer, OutputBufferLength);

    printf("EmuKrnl (0x%lX): IoSynchronousDeviceIoControlRequest code=0x%.08lX device=%p in=0x%.08lX out=0x%.08lX internal=%lu.\n",
           GetCurrentThreadId(), IoControlCode, DeviceObject, InputBufferLength, OutputBufferLength, (ULONG)InternalDeviceIoControl);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuIoSynchronousFsdRequest
(
    IN ULONG MajorFunction,
    IN PVOID DeviceObject,
    IN PVOID Buffer,
    IN ULONG Length,
    IN xboxkrnl::PLARGE_INTEGER StartingOffset
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoSynchronousFsdRequest major=0x%.08lX device=%p buffer=%p length=0x%.08lX.\n",
           GetCurrentThreadId(), MajorFunction, DeviceObject, Buffer, Length);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuIofCallDriver
(
    IN PVOID DeviceObject,
    IN PVOID Irp
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuCompleteIrp(Irp, STATUS_SUCCESS, 0);

    printf("EmuKrnl (0x%lX): IofCallDriver device=%p irp=%p.\n",
           GetCurrentThreadId(), DeviceObject, Irp);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuIofCompleteRequest
(
    IN PVOID Irp,
    IN xboxkrnl::CCHAR PriorityBoost
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuCompleteIrp(Irp, STATUS_SUCCESS, 0);

    printf("EmuKrnl (0x%lX): IofCompleteRequest irp=%p boost=%d.\n",
           GetCurrentThreadId(), Irp, (int)PriorityBoost);

    EmuSwapFS();   // Xbox FS
}

extern "C" VOID NTAPI EmuIoMarkIrpMustComplete
(
    IN PVOID Irp
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuIrp *Object = EmuGetIrp(Irp);
    if(Object != NULL)
        Object->MustComplete = TRUE;

    printf("EmuKrnl (0x%lX): IoMarkIrpMustComplete irp=%p owned=%lu.\n",
           GetCurrentThreadId(), Irp, (ULONG)(Object != NULL));

    EmuSwapFS();   // Xbox FS
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

extern "C" NTSTATUS NTAPI EmuExReadWriteRefurbInfo(PVOID Buffer, ULONG BufferLength, BOOLEAN WriteMode)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Buffer == NULL && BufferLength != 0)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInvalidParameter;
    }

    if(!WriteMode && Buffer != NULL && BufferLength != 0)
        ZeroMemory(Buffer, BufferLength);

    printf("EmuKrnl (0x%lX): ExReadWriteRefurbInfo buffer=%p length=0x%.08lX write=%lu.\n",
           GetCurrentThreadId(), Buffer, BufferLength, (ULONG)WriteMode);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuExSaveNonVolatileSetting
(
    IN DWORD ValueIndex,
    IN DWORD Type,
    IN PUCHAR Value,
    IN xboxkrnl::SIZE_T ValueLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): ExSaveNonVolatileSetting index=0x%.08lX type=0x%.08lX value=%p length=0x%.08lX.\n",
           GetCurrentThreadId(), ValueIndex, Type, Value, (ULONG)ValueLength);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuExRaiseException(PEXCEPTION_RECORD ExceptionRecord)
{
    EmuRtlRaiseException(ExceptionRecord);
}

extern "C" VOID NTAPI EmuExRaiseStatus(NTSTATUS Status)
{
    EmuRtlRaiseStatus(Status);
}

extern "C" xboxkrnl::LONG NTAPI EmuFscGetCacheSize()
{
    return (xboxkrnl::LONG)g_EmuFscCachePages;
}

extern "C" VOID NTAPI EmuFscInvalidateIdleBlocks()
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): FscInvalidateIdleBlocks ignored.\n", GetCurrentThreadId());

    EmuSwapFS();   // Xbox FS
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

    g_EmuFscCachePages = uCachePages;

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
    void *CallerRet = __builtin_return_address(0);
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%X): HalReturnToFirmware(%d) called from guest 0x%.08X.\n",
           GetCurrentThreadId(), Routine, (uint32)CallerRet);
    fflush(stdout);

    // Reboot-decision trace (opt-in via CXBX_REBOOT_TRACE): walk the guest stack and
    // report every return-address-looking value inside the loaded XBE image, so the
    // call chain that led to the reboot can be reconstructed and disassembled.
    if(getenv("CXBX_REBOOT_TRACE") != NULL)
    {
        ULONG GuestEsp = 0;
        __asm { mov GuestEsp, esp }
        ULONG *Stk = (ULONG*)GuestEsp;
        printf("EmuKrnl (0x%X): REBOOT stack walk (esp=0x%.08X):\n", GetCurrentThreadId(), GuestEsp);
        for(int i = 0; i < 64; i++)
        {
            if(IsBadReadPtr(&Stk[i], 4))
                break;
            ULONG v = Stk[i];
            if(v >= 0x00010000 && v < 0x00080000)   // loaded XBE image range
                printf("  [esp+%03X] = 0x%.08X\n", i * 4, v);
        }
        fflush(stdout);
    }

    // Soft-mod bypass (opt-in): the launcher framework shared by the XDK samples /
    // z26x reboots (QuickReboot=2) to apply a kernel patch and re-run the app. A
    // user-mode HLE can't persist that patch, so the reboot just loops/exits. When
    // CXBX_SOFTMOD_BYPASS is set, return to the guest instead of terminating so the
    // caller falls through to its post-reboot (run-the-app) path.
    char bypass[8] = {0};
    if(Routine == 2 && GetEnvironmentVariableA("CXBX_SOFTMOD_BYPASS", bypass, sizeof(bypass)) != 0)
    {
        printf("EmuKrnl (0x%X): HalReturnToFirmware(2) BYPASSED -- returning to guest.\n",
               GetCurrentThreadId());
        fflush(stdout);
        EmuSwapFS();   // Xbox FS
        return;
    }

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

    EmuSwapFS();   // Xbox FS for nested thunk call
    NTSTATUS ret = xboxkrnl::NtCreateFile
    (
        FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize,
        FileAttributes, ShareAccess, Disposition, CreateOptions
    );
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): IoCreateFile handle=%p status=0x%.08lX.\n",
           GetCurrentThreadId(), (FileHandle != NULL) ? *FileHandle : NULL, ret);

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

    BOOLEAN WasInserted = Timer->Header.Inserted != 0;

    Timer->DueTime = *(ULARGE_INTEGER*)&DueTime;
    Timer->Dpc = Dpc;
    Timer->Header.SignalState = 0;
    Timer->Header.Inserted = 1;

    EmuSwapFS();   // Xbox FS

    if(Dpc != NULL)
        EmuKeInsertQueueDpc(Dpc, NULL, NULL);

    return WasInserted;
}

extern "C" NTSTATUS NTAPI EmuKeRestoreFloatingPointState(PVOID FloatingState)
{
    return STATUS_SUCCESS;
}

extern "C" PVOID NTAPI EmuKeRundownQueue(PVOID QueueObject)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuSimpleQueue *Queue = (EmuSimpleQueue*)QueueObject;
    if(Queue != NULL && EmuIsWritableMemoryRange(Queue, sizeof(*Queue)))
    {
        Queue->Count = 0;
        Queue->Header.SignalState = 0;
    }

    EmuSwapFS();   // Xbox FS

    return NULL;
}

extern "C" NTSTATUS NTAPI EmuKeSaveFloatingPointState(PVOID FloatingState)
{
    if(FloatingState != NULL && EmuIsWritableMemoryRange(FloatingState, 0x20))
        ZeroMemory(FloatingState, 0x20);

    return STATUS_SUCCESS;
}

extern "C" LONG NTAPI EmuKeSetBasePriorityThread(xboxkrnl::PKTHREAD Thread, LONG Increment)
{
    return 0;
}

extern "C" VOID NTAPI EmuKeSetDisableBoostThread(xboxkrnl::PKTHREAD Thread, BOOLEAN Disable)
{
}

extern "C" LONG NTAPI EmuKeSetEventBoostPriority(PVOID Event, xboxkrnl::PKTHREAD Thread)
{
    return EmuKeSetEvent(Event, 0, FALSE);
}

extern "C" LONG NTAPI EmuKeSetPriorityProcess(PVOID Process, LONG BasePriority)
{
    return BasePriority;
}

extern "C" LONG NTAPI EmuKeSetPriorityThread(xboxkrnl::PKTHREAD Thread, LONG Priority)
{
    return Priority;
}

// 0x0099 - KeSynchronizeExecution: run SynchronizeRoutine mutually exclusive with
// the interrupt's ISR. The routine is guest code and is entered with the Xbox FS
// still active (the guest called us that way), so invoke it directly and return
// its result. Full ISR exclusion would raise to the interrupt's SynchronizeIrql;
// our synthesized ISR delivery already defers while a guest holds a raised IRQL.
typedef xboxkrnl::BOOLEAN (NTAPI *EmuSynchronizeRoutine)(PVOID SynchronizeContext);
extern "C" xboxkrnl::BOOLEAN NTAPI EmuKeSynchronizeExecution
(
    PVOID Interrupt,
    PVOID SynchronizeRoutine,
    PVOID SynchronizeContext
)
{
    if(SynchronizeRoutine == NULL)
        return FALSE;

    xboxkrnl::BOOLEAN Result = FALSE;
    __try
    {
        Result = ((EmuSynchronizeRoutine)SynchronizeRoutine)(SynchronizeContext);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }

    return Result;
}

extern "C" xboxkrnl::BOOLEAN NTAPI EmuKeSetTimerEx
(
    IN xboxkrnl::PKTIMER Timer,
    IN xboxkrnl::LARGE_INTEGER DueTime,
    IN LONG Period,
    IN xboxkrnl::PKDPC Dpc OPTIONAL
)
{
    BOOLEAN WasInserted = xboxkrnl::KeSetTimer(Timer, DueTime, Dpc);
    if(Timer != NULL && EmuIsWritableMemoryRange(Timer, sizeof(*Timer)))
        Timer->Period = Period;

    return WasInserted;
}

extern "C" BOOLEAN NTAPI EmuKeTestAlertThread(UCHAR AlertMode)
{
    return FALSE;
}

extern "C" NTSTATUS NTAPI EmuKeWaitForMultipleObjects
(
    IN ULONG Count,
    IN PVOID Object[],
    IN ULONG WaitType,
    IN UCHAR WaitMode,
    IN BOOLEAN Alertable,
    IN xboxkrnl::PLARGE_INTEGER Timeout,
    IN PVOID WaitBlockArray
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): KeWaitForMultipleObjects count=%lu waitType=0x%.08lX alertable=%lu.\n",
           GetCurrentThreadId(), Count, WaitType, (ULONG)Alertable);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuKeWaitForSingleObject
(
    IN PVOID Object,
    IN ULONG WaitReason,
    IN UCHAR WaitMode,
    IN BOOLEAN Alertable,
    IN xboxkrnl::PLARGE_INTEGER Timeout
)
{
    EmuSwapFS();   // Win2k/XP FS

    xboxkrnl::DISPATCHER_HEADER *Header = (xboxkrnl::DISPATCHER_HEADER*)Object;
    if(Header != NULL && EmuIsWritableMemoryRange(Header, sizeof(*Header)) && Header->SignalState > 0 &&
       (Header->Type == 1 || Header->Type == 0x05))
    {
        Header->SignalState--;
    }

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" VOID NTAPI EmuKiUnlockDispatcherDatabase(UCHAR OldIrql)
{
    EmuKeLowerIrql(OldIrql);
}

// ******************************************************************
// * 0x0097 - KeStallExecutionProcessor
// ******************************************************************
extern "C" VOID NTAPI EmuKeStallExecutionProcessor(ULONG Microseconds)
{
    EmuSwapFS();   // Win2k/XP FS

    // ******************************************************************
    // * debug trace
    // ******************************************************************
    #ifdef _DEBUG_TRACE
    {
        printf("EmuKrnl (0x%X): KeStallExecutionProcessor\n"
               "(\n"
               "   Microseconds        : 0x%.08X\n"
               ");\n",
               GetCurrentThreadId(), Microseconds);
    }
    #endif

    if(Microseconds != 0)
    {
        ::LARGE_INTEGER Frequency;
        ::LARGE_INTEGER Start;
        ::LARGE_INTEGER Now;

        QueryPerformanceFrequency(&Frequency);
        QueryPerformanceCounter(&Start);

        if(Microseconds >= 2000)
        {
            DWORD Milliseconds = (Microseconds - 1000) / 1000;

            if(Milliseconds != 0)
                Sleep(Milliseconds);
        }

        ULONGLONG TargetTicks = ((ULONGLONG)Microseconds * (ULONGLONG)Frequency.QuadPart + 999999ULL) / 1000000ULL;

        do
        {
            QueryPerformanceCounter(&Now);

            if(Microseconds >= 1000)
                Sleep(0);
        }
        while((ULONGLONG)(Now.QuadPart - Start.QuadPart) < TargetTicks);
    }

    EmuSwapFS();   // Xbox FS
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
// * First-image framebuffer dump (diagnostic)
// *
// * When D3D8 allocates the 640x480x32 scanout surface, a host-side thread
// * snapshots it to %TEMP%\cxbx_frameN.bmp every few seconds so the rendered
// * frame can be inspected. This does not touch the NV2A/render path; it just
// * reads the surface memory. If the frame is black, the title renders through
// * the (un-rasterized) NV2A 3D pipeline; if it shows pixels, it software-fills.
// ******************************************************************
static volatile ULONG g_EmuFramebufferAddress = 0;

static DWORD WINAPI EmuFramebufferDumpThread(LPVOID)
{
    const ULONG W = 640, H = 480, DataSize = W * H * 4;
    char dir[MAX_PATH] = {0};
    GetTempPathA(sizeof(dir), dir);

    for(ULONG index = 0; ; )
    {
        Sleep(3000);

        ULONG fb = g_EmuFramebufferAddress;
        if(fb == 0)
            continue;

        char path[MAX_PATH];
        sprintf(path, "%scxbx_frame%lu.bmp", dir, index++);

        FILE *f = fopen(path, "wb");
        if(f == NULL)
            continue;

        unsigned char fh[14] = {0}, ih[40] = {0};
        ULONG fileSize = 54 + DataSize;
        fh[0] = 'B'; fh[1] = 'M';
        fh[2] = (unsigned char)fileSize;         fh[3] = (unsigned char)(fileSize >> 8);
        fh[4] = (unsigned char)(fileSize >> 16); fh[5] = (unsigned char)(fileSize >> 24);
        fh[10] = 54;
        ih[0] = 40;
        ih[4] = (unsigned char)W; ih[5] = (unsigned char)(W >> 8);
        LONG nh = -(LONG)H;   // negative height => top-down bitmap
        ih[8] = (unsigned char)nh;         ih[9]  = (unsigned char)(nh >> 8);
        ih[10] = (unsigned char)(nh >> 16); ih[11] = (unsigned char)(nh >> 24);
        ih[12] = 1;    // planes
        ih[14] = 32;   // bpp (BGRA, matches Xbox X8R8G8B8 little-endian)
        fwrite(fh, 1, 14, f);
        fwrite(ih, 1, 40, f);
        fwrite((const void *)fb, 1, DataSize, f);
        fclose(f);

        printf("EmuKrnl: dumped framebuffer 0x%.08lX -> %s\n", fb, path);
        fflush(stdout);
    }
}

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
    const ULONG AllocationSize = EmuRoundToPageSize(NumberOfBytes);
    PVOID pRet = NULL;
    if(AllocationSize != 0)
    {
        pRet = EmuAllocateContiguousLow(AllocationSize, 0x4000);   // low Xbox-RAM window
        if(pRet == NULL)
            pRet = (PVOID)new unsigned char[AllocationSize];       // window exhausted -> heap
    }
    EmuTrackContiguousMemoryAllocation(pRet, AllocationSize);

    // Large contiguous blocks are display/render surfaces (D3D8 front/back
    // buffers). The framebuffer-sized one (~640x480xbpp) is where the frame
    // lives; log size + address to locate it.
    if(pRet != NULL && AllocationSize >= 0x00040000)
    {
        printf("EmuKrnl (0x%lX): large contiguous alloc size=0x%.08lX (%lu KiB) -> 0x%.08lX\n",
               GetCurrentThreadId(), AllocationSize, AllocationSize / 1024, (ULONG)pRet);
        fflush(stdout);

        // 640x480x32 surface => the scanout framebuffer. Capture it and start
        // the snapshot thread (see EmuFramebufferDumpThread).
        if(AllocationSize == 0x0012C000 && g_EmuFramebufferAddress == 0)
        {
            g_EmuFramebufferAddress = (ULONG)pRet;
            CreateThread(NULL, 0, EmuFramebufferDumpThread, NULL, 0, NULL);
        }
    }

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
    const ULONG AllocationSize = EmuRoundToPageSize(NumberOfBytes);
    PVOID pRet = NULL;
    if(AllocationSize != 0)
    {
        pRet = EmuAllocateContiguousLow(AllocationSize, 0x4000);   // low Xbox-RAM window
        if(pRet == NULL)
            pRet = (PVOID)new unsigned char[AllocationSize];       // window exhausted -> heap
    }
    EmuTrackContiguousMemoryAllocation(pRet, AllocationSize);

    // Large contiguous blocks are display/render surfaces (D3D8 front/back
    // buffers). The framebuffer-sized one (~640x480xbpp) is where the frame
    // lives; log size + address to locate it.
    if(pRet != NULL && AllocationSize >= 0x00040000)
    {
        printf("EmuKrnl (0x%lX): large contiguous alloc size=0x%.08lX (%lu KiB) -> 0x%.08lX\n",
               GetCurrentThreadId(), AllocationSize, AllocationSize / 1024, (ULONG)pRet);
        fflush(stdout);

        // 640x480x32 surface => the scanout framebuffer. Capture it and start
        // the snapshot thread (see EmuFramebufferDumpThread).
        if(AllocationSize == 0x0012C000 && g_EmuFramebufferAddress == 0)
        {
            g_EmuFramebufferAddress = (ULONG)pRet;
            CreateThread(NULL, 0, EmuFramebufferDumpThread, NULL, 0, NULL);
        }
    }

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

    if(pRet == NULL)
    {
        ULONG Used = 0;
        for(ULONG i = 0; i < sizeof(g_EmuSystemMemoryAllocations) / sizeof(g_EmuSystemMemoryAllocations[0]); i++)
            if(g_EmuSystemMemoryAllocations[i].Address != NULL)
                Used++;
        printf("EmuKrnl (0x%lX): *ALLOC FAILED* MmAllocateContiguousMemoryEx bytes=0x%lX protect=0x%lX "
               "slots=%lu/128 next=0x%.08lX\n",
               GetCurrentThreadId(), NumberOfBytes, Protect, Used,
               (ULONG)g_EmuNextSystemMemoryAddress);
        fflush(stdout);
    }

    EmuSwapFS();   // Xbox FS

    return pRet;
}

// ******************************************************************
// * 0x00A8 - MmClaimGpuInstanceMemory
// ******************************************************************
extern "C" PVOID NTAPI EmuMmClaimGpuInstanceMemory
(
    xboxkrnl::SIZE_T NumberOfBytes,
    xboxkrnl::SIZE_T *NumberOfPaddingBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    static PVOID InstanceMemory = NULL;
    static xboxkrnl::SIZE_T InstanceMemorySize = 0;
    static const xboxkrnl::SIZE_T InstanceMemoryDefaultSize = 0x10000;
    static const xboxkrnl::SIZE_T InstanceMemoryPadding = 0x10000;

    if(NumberOfPaddingBytes != NULL)
        *NumberOfPaddingBytes = InstanceMemoryPadding;

    xboxkrnl::SIZE_T RequestedSize = InstanceMemoryDefaultSize;
    if(NumberOfBytes != (xboxkrnl::SIZE_T)-1 && NumberOfBytes > RequestedSize)
        RequestedSize = NumberOfBytes;

    RequestedSize = (RequestedSize + EmuPageSize - 1) & ~(xboxkrnl::SIZE_T)(EmuPageSize - 1);

    if(InstanceMemory == NULL || InstanceMemorySize < RequestedSize)
    {
        PVOID NewMemory = VirtualAlloc(NULL, RequestedSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(NewMemory != NULL)
        {
            InstanceMemory = NewMemory;
            InstanceMemorySize = RequestedSize;
        }
    }

    printf("EmuKrnl (0x%lX): MmClaimGpuInstanceMemory bytes=0x%lX padding=0x%lX result=%p.\n",
           GetCurrentThreadId(), (ULONG)NumberOfBytes, (ULONG)InstanceMemoryPadding, InstanceMemory);

    EmuSwapFS();   // Xbox FS

    return InstanceMemory;
}

// ******************************************************************
// * 0x00A9 - MmCreateKernelStack
// ******************************************************************
extern "C" PVOID NTAPI EmuMmCreateKernelStack
(
    IN ULONG NumberOfBytes,
    IN BOOLEAN DebuggerThread
)
{
    EmuSwapFS();   // Win2k/XP FS

    PVOID StackBase = NULL;
    const ULONG StackBytes = EmuRoundToPageSize(NumberOfBytes);

    if(StackBytes != 0 && StackBytes + EmuPageSize >= StackBytes)
    {
        const ULONG AllocationSize = StackBytes + EmuPageSize;
        PVOID AllocationBase = VirtualAlloc(NULL, AllocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if(AllocationBase != NULL)
        {
            DWORD OldProtect = 0;
            VirtualProtect(AllocationBase, EmuPageSize, PAGE_NOACCESS, &OldProtect);

            PVOID StackLimit = (PVOID)((unsigned char*)AllocationBase + EmuPageSize);
            StackBase = (PVOID)((unsigned char*)AllocationBase + AllocationSize);
            EmuTrackKernelStack(AllocationBase, StackBase, StackLimit, AllocationSize, DebuggerThread);
        }
    }

    printf("EmuKrnl (0x%lX): MmCreateKernelStack bytes=0x%.08lX debugger=%lu result=%p.\n",
           GetCurrentThreadId(), NumberOfBytes, (ULONG)DebuggerThread, StackBase);

    EmuSwapFS();   // Xbox FS

    return StackBase;
}

// ******************************************************************
// * 0x00AA - MmDeleteKernelStack
// ******************************************************************
extern "C" VOID NTAPI EmuMmDeleteKernelStack
(
    IN PVOID StackBase,
    IN PVOID StackLimit
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuKernelStackAllocation *Allocation = EmuFindKernelStack(StackBase, StackLimit);
    bool Freed = false;

    if(Allocation != NULL)
    {
        PVOID AllocationBase = Allocation->BaseAddress;
        EmuUntrackKernelStack(Allocation);
        Freed = VirtualFree(AllocationBase, 0, MEM_RELEASE) != 0;
    }

    printf("EmuKrnl (0x%lX): MmDeleteKernelStack stackBase=%p stackLimit=%p freed=%lu.\n",
           GetCurrentThreadId(), StackBase, StackLimit, (ULONG)Freed);

    EmuSwapFS();   // Xbox FS
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

    EmuUntrackContiguousMemoryAllocation(BaseAddress);
    if(EmuIsLowXboxRam(BaseAddress))
    {
        // Deliberately keep the pages committed. Two reasons:
        // - VirtualFree(Base, 0, MEM_DECOMMIT) decommits from Base to the END
        //   of the reservation (the whole low window is ONE reservation), so
        //   it silently destroyed every live allocation above the freed one.
        // - Titles free blocks that still have fire-and-forget async reads
        //   outstanding (Turok Evolution's video teardown); the kernel's
        //   canceled-IO completion then writes the IOSB into this memory, and
        //   a decommitted page turns that into an access violation raised at
        //   the title's next syscall.
        // The window is a fixed 48 MiB arena; leaving freed pages committed
        // costs at most that.
    }
    else
        delete[] (unsigned char *)BaseAddress;

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
// * 0x00AD - MmGetPhysicalAddress
// ******************************************************************
extern "C" PHYSICAL_ADDRESS NTAPI EmuMmGetPhysicalAddress
(
    IN PVOID BaseAddress
)
{
    EmuSwapFS();   // Win2k/XP FS

    PHYSICAL_ADDRESS PhysicalAddress = EmuQueryContiguousMemoryPhysicalAddress(BaseAddress);
    if(PhysicalAddress == 0)
        PhysicalAddress = (PHYSICAL_ADDRESS)(((::ULONG_PTR)BaseAddress) & (EmuXboxPhysicalMemoryBytes - 1));

    printf("EmuKrnl (0x%lX): MmGetPhysicalAddress base=%p physical=0x%.08lX.\n",
           GetCurrentThreadId(), BaseAddress, (ULONG)PhysicalAddress);

    EmuSwapFS();   // Xbox FS

    return PhysicalAddress;
}

// ******************************************************************
// * 0x00AF - MmLockUnlockBufferPages
// ******************************************************************
extern "C" VOID NTAPI EmuMmLockUnlockBufferPages
(
    IN PVOID BaseAddress,
    IN xboxkrnl::SIZE_T NumberOfBytes,
    IN BOOLEAN UnlockPages
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): MmLockUnlockBufferPages base=%p bytes=0x%.08lX unlock=%lu.\n",
           GetCurrentThreadId(), BaseAddress, (ULONG)NumberOfBytes, (ULONG)UnlockPages);

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * 0x00B0 - MmLockUnlockPhysicalPage
// ******************************************************************
extern "C" VOID NTAPI EmuMmLockUnlockPhysicalPage
(
    IN ULONG PhysicalAddress,
    IN BOOLEAN UnlockPage
)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): MmLockUnlockPhysicalPage physical=0x%.08lX unlock=%lu.\n",
           GetCurrentThreadId(), PhysicalAddress, (ULONG)UnlockPage);

    EmuSwapFS();   // Xbox FS
}

// ******************************************************************
// * 0x00B1 - MmMapIoSpace
// ******************************************************************
extern "C" PVOID NTAPI EmuMmMapIoSpace
(
    IN PHYSICAL_ADDRESS PhysicalAddress,
    IN ULONG NumberOfBytes,
    IN ULONG Protect
)
{
    EmuSwapFS();   // Win2k/XP FS

    PVOID Result = NULL;
    bool OwnsAllocation = false;

    if(NumberOfBytes != 0 && EmuIsValidSystemMemoryProtect(Protect))
    {
        ULONG HostAddress = EmuContiguousHostFromPhysical(PhysicalAddress);
        if(HostAddress != 0)
        {
            Result = (PVOID)(::ULONG_PTR)HostAddress;
        }
        else if(PhysicalAddress >= EmuMmioPassthroughBase)
        {
            Result = (PVOID)(::ULONG_PTR)PhysicalAddress;
        }
        else
        {
            Result = VirtualAlloc(NULL, EmuRoundToPageSize(NumberOfBytes), MEM_COMMIT | MEM_RESERVE, Protect);
            OwnsAllocation = Result != NULL;
        }

        EmuTrackIoSpaceMapping(Result, NumberOfBytes, PhysicalAddress, Protect, OwnsAllocation);
    }

    printf("EmuKrnl (0x%lX): MmMapIoSpace physical=0x%.08lX bytes=0x%.08lX protect=0x%.08lX result=%p owns=%lu.\n",
           GetCurrentThreadId(), (ULONG)PhysicalAddress, NumberOfBytes, Protect, Result, (ULONG)OwnsAllocation);

    EmuSwapFS();   // Xbox FS

    return Result;
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
// * 0x00B3 - MmQueryAddressProtect
// ******************************************************************
extern "C" ULONG NTAPI EmuMmQueryAddressProtect
(
    IN PVOID VirtualAddress
)
{
    EmuSwapFS();   // Win2k/XP FS

    ULONG Protect = EmuQueryAddressProtect(VirtualAddress);

    printf("EmuKrnl (0x%lX): MmQueryAddressProtect base=%p protect=0x%.08lX.\n",
           GetCurrentThreadId(), VirtualAddress, Protect);

    EmuSwapFS();   // Xbox FS

    return Protect;
}

// ******************************************************************
// * 0x00B4 - MmQueryAllocationSize
// ******************************************************************
extern "C" ULONG NTAPI EmuMmQueryAllocationSize
(
    IN PVOID BaseAddress
)
{
    EmuSwapFS();   // Win2k/XP FS

    ULONG Size = EmuQuerySystemMemoryAllocationSize(BaseAddress);
    if(Size == 0)
        Size = EmuQueryContiguousMemoryAllocationSize(BaseAddress);

    printf("EmuKrnl (0x%lX): MmQueryAllocationSize base=%p size=0x%.08lX.\n",
           GetCurrentThreadId(), BaseAddress, Size);

    EmuSwapFS();   // Xbox FS

    return Size;
}

// ******************************************************************
// * 0x00B5 - MmQueryStatistics
// ******************************************************************
extern "C" NTSTATUS NTAPI EmuMmQueryStatistics
(
    OUT EmuMmStatistics *MemoryStatistics
)
{
    EmuSwapFS();   // Win2k/XP FS

    NTSTATUS ret = STATUS_SUCCESS;

    if(MemoryStatistics == NULL || MemoryStatistics->Length != sizeof(EmuMmStatistics))
    {
        ret = EmuStatusInvalidParameter;
    }
    else
    {
        const ULONG SystemBytes = EmuCommittedSystemMemoryBytes();
        const ULONG ContiguousBytes = EmuCommittedContiguousMemoryBytes();
        const ULONG StackBytes = EmuCommittedKernelStackBytes();
        const ULONG CommittedBytes = SystemBytes + ContiguousBytes + StackBytes;

        MemoryStatistics->TotalPhysicalPages = EmuXboxPhysicalMemoryBytes / EmuPageSize;
        MemoryStatistics->AvailablePages = (CommittedBytes < EmuXboxPhysicalMemoryBytes) ?
                                           (EmuXboxPhysicalMemoryBytes - CommittedBytes) / EmuPageSize : 0;
        MemoryStatistics->VirtualMemoryBytesCommitted = SystemBytes;
        MemoryStatistics->VirtualMemoryBytesReserved = g_EmuNextSystemMemoryAddress - 0xD0000000;
        MemoryStatistics->CachePagesCommitted = 0;
        MemoryStatistics->PoolPagesCommitted = EmuSystemMemoryPages(ContiguousBytes);
        MemoryStatistics->StackPagesCommitted = EmuSystemMemoryPages(StackBytes);
        MemoryStatistics->ImagePagesCommitted = 0;
    }

    printf("EmuKrnl (0x%lX): MmQueryStatistics stats=%p ret=0x%.08lX.\n",
           GetCurrentThreadId(), MemoryStatistics, ret);

    EmuSwapFS();   // Xbox FS

    return ret;
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

    ULONG OldProtect = 0;
    NTSTATUS Status = EmuProtectVirtualMemory(BaseAddress, NumberOfBytes, NewProtect, &OldProtect);
    if(Status != STATUS_SUCCESS)
        EmuWarning("MmSetAddressProtect failed\n");

    printf("EmuKrnl (0x%lX): MmSetAddressProtect base=%p bytes=0x%.08lX new=0x%.08lX old=0x%.08lX status=0x%.08lX.\n",
           GetCurrentThreadId(), BaseAddress, NumberOfBytes, NewProtect, OldProtect, Status);

    EmuSwapFS();   // Xbox FS

    return;
}

// ******************************************************************
// * 0x00B7 - MmUnmapIoSpace
// ******************************************************************
extern "C" VOID NTAPI EmuMmUnmapIoSpace
(
    IN PVOID BaseAddress,
    IN ULONG NumberOfBytes
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuIoSpaceMapping *Mapping = EmuFindIoSpaceMapping(BaseAddress);
    bool Freed = false;

    if(Mapping != NULL)
    {
        if(Mapping->OwnsAllocation && Mapping->Address != NULL)
            Freed = VirtualFree(Mapping->Address, 0, MEM_RELEASE) != 0;

        EmuUntrackIoSpaceMapping(Mapping);
    }

    printf("EmuKrnl (0x%lX): MmUnmapIoSpace base=%p bytes=0x%.08lX freed=%lu.\n",
           GetCurrentThreadId(), BaseAddress, NumberOfBytes, (ULONG)Freed);

    EmuSwapFS();   // Xbox FS
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
    // * tolerate guest-object 'handles'
    // ******************************************************************
    // A value above 0x80000000 is not a host handle: on real hardware that
    // range holds kernel-object pointers and physical-map aliases, and titles
    // do pass such values to NtClose (Turok Evolution does at its video
    // teardown). Nothing in this tree creates the legacy 'EmuHandle' wrappers
    // any more, so the old `delete EmuHandleToPtr(Handle)` here freed an
    // ARBITRARY decoded address on the host heap -- a deferred heap
    // corruption that surfaced as random RtlFreeHeap crashes much later.
    // There is no host resource behind these values; just report success.
    if(IsEmuHandle(Handle))
    {
        printf("EmuKrnl (0x%X): NtClose ignored a guest-object handle 0x%.08X.\n",
               (uint32)GetCurrentThreadId(), (uint32)Handle);

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

// Opt-in file-IO trace (CXBX_FILEIO_TRACE): logs every open, read and
// file-position set as one-line "FIO|" records so a title's disc activity can
// be reconstructed from the run log. Read failure statuses are logged even
// without the switch — they were previously invisible passthroughs.
// CXBX_FILEIO_TRACE=2 additionally polls the IoStatusBlock of a PENDING
// async read (up to 2 s, without touching the title's event) and logs the
// completion status; it delays the guest's NtReadFile return, so it is a
// diagnostic mode only.
static int EmuFileIoTraceLevel()
{
    static int Level = -1;

    if(Level < 0)
    {
        const char *v = getenv("CXBX_FILEIO_TRACE");
        Level = (v == NULL) ? 0 : max(1, atoi(v));
    }

    return Level;
}

static bool EmuFileIoTraceEnabled()
{
    return EmuFileIoTraceLevel() >= 1;
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

    // Reject a malformed OBJECT_ATTRIBUTES instead of dereferencing a null/garbage
    // ObjectName->Buffer: a title (or a soft-mod launcher running post-reboot) can
    // pass one, and the path parsing below would crash the emulator on it.
    if(ObjectAttributes == NULL || ObjectAttributes->ObjectName == NULL ||
       ObjectAttributes->ObjectName->Buffer == NULL ||
       IsBadReadPtr(ObjectAttributes->ObjectName->Buffer, 4))
    {
        printf("EmuKrnl (0x%X): NtCreateFile rejected malformed ObjectAttributes.\n", GetCurrentThreadId());
        EmuSwapFS();   // Xbox FS
        return EmuStatusObjectNameInvalid;
    }

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
    else if( (szBuffer[0] == 'E' || szBuffer[0] == 'e') && szBuffer[1] == ':' && szBuffer[2] == '\\')
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
    // * \Device\Harddisk0\PartitionN should map to the XBE directory too,
    // * so titles that open the raw hard-disk partition (e.g. NestopiaX's ROM
    // * browser enumerating the disk) find their content instead of failing
    // * with ACCESS_DENIED and stalling before the UI ever renders.
    // ******************************************************************
    else if(_strnicmp(szBuffer, "\\Device\\Harddisk0\\Partition", 27) == 0 &&
            szBuffer[27] >= '0' && szBuffer[27] <= '9')
    {
        szBuffer += 28;                 // skip "\Device\Harddisk0\PartitionN"
        while(szBuffer[0] == '\\')
            szBuffer += 1;              // skip separator(s) to leave a relative path

        ObjectAttributes->RootDirectory = g_hCurDir;

        printf("EmuKrnl (0x%X): NtCreateFile mapped hard-disk partition to XBE dir: \"%s\"\n",
               GetCurrentThreadId(), szBuffer);
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

    if(EmuFileIoTraceEnabled())
    {
        printf("FIO| open tid=0x%X path=\"%s\" access=0x%.08X disp=0x%X opts=0x%X status=0x%.08X handle=0x%X\n",
               (uint32)GetCurrentThreadId(), szOriginalBuffer, (uint32)DesiredAccess,
               (uint32)CreateDisposition, (uint32)CreateOptions, (uint32)ret,
               FAILED(ret) ? 0 : (uint32)*FileHandle);
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

extern "C" NTSTATUS NTAPI EmuNtCancelTimer
(
    IN HANDLE TimerHandle,
    OUT PBOOLEAN CurrentState OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    BOOL Canceled = CancelWaitableTimer(TimerHandle);
    if(CurrentState != NULL)
        *CurrentState = FALSE;

    EmuSwapFS();   // Xbox FS

    return Canceled ? STATUS_SUCCESS : EmuStatusInvalidHandle;
}

extern "C" NTSTATUS NTAPI EmuNtCreateDirectoryObject
(
    OUT PHANDLE DirectoryHandle,
    IN xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes
)
{
    PVOID Object = NULL;
    NTSTATUS Status = EmuObCreateObject(&g_EmuObDirectoryObjectType, ObjectAttributes, sizeof(ULONG), &Object);
    if(Status != STATUS_SUCCESS)
        return Status;

    return EmuObInsertObject(Object, ObjectAttributes, 0, DirectoryHandle);
}

extern "C" NTSTATUS NTAPI EmuNtDeleteFile(IN xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes)
{
    EmuSwapFS();   // Win2k/XP FS

    std::string Path;
    NTSTATUS Status = EmuStatusObjectNameInvalid;
    if(EmuObjectAttributesToHostPath(ObjectAttributes, &Path))
    {
        if(DeleteFileA(Path.c_str()))
            Status = STATUS_SUCCESS;
        else
            Status = (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) ?
                     STATUS_OBJECT_NAME_NOT_FOUND : STATUS_UNSUCCESSFUL;
    }

    printf("EmuKrnl (0x%lX): NtDeleteFile path=\"%s\" status=0x%.08lX.\n",
           GetCurrentThreadId(), Path.c_str(), Status);

    EmuSwapFS();   // Xbox FS

    return Status;
}

static void EmuCompleteIoStatus(xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock, NTSTATUS Status, ::ULONG_PTR Information)
{
    if(IoStatusBlock == NULL)
        return;

    IoStatusBlock->u1.Status = Status;
    IoStatusBlock->Information = (xboxkrnl::ULONG_PTR)(::ULONG_PTR)Information;
}

extern "C" NTSTATUS NTAPI EmuNtDeviceIoControlFile
(
    IN HANDLE FileHandle,
    IN HANDLE Event OPTIONAL,
    IN PVOID ApcRoutine OPTIONAL,
    IN PVOID ApcContext,
    OUT xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock,
    IN ULONG IoControlCode,
    IN PVOID InputBuffer,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer,
    IN ULONG OutputBufferLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    DWORD Returned = 0;
    BOOL Ok = DeviceIoControl(FileHandle, IoControlCode, InputBuffer, InputBufferLength,
                              OutputBuffer, OutputBufferLength, &Returned, NULL);
    NTSTATUS Status = Ok ? STATUS_SUCCESS : EmuStatusInvalidDeviceRequest;

    EmuCompleteIoStatus(IoStatusBlock, Status, Returned);
    if(Event != NULL)
        SetEvent(Event);
    if(ApcRoutine != NULL)
        QueueUserAPC((PAPCFUNC)ApcRoutine, GetCurrentThread(), (::ULONG_PTR)ApcContext);

    EmuSwapFS();   // Xbox FS

    return Status;
}

extern "C" NTSTATUS NTAPI EmuNtFlushBuffersFile
(
    IN HANDLE FileHandle,
    OUT xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock
)
{
    EmuSwapFS();   // Win2k/XP FS

    BOOL Ok = FlushFileBuffers(FileHandle);
    NTSTATUS Status = Ok ? STATUS_SUCCESS : EmuStatusInvalidHandle;
    EmuCompleteIoStatus(IoStatusBlock, Status, 0);

    EmuSwapFS();   // Xbox FS

    return Status;
}

extern "C" NTSTATUS NTAPI EmuNtFsControlFile
(
    IN HANDLE FileHandle,
    IN HANDLE Event OPTIONAL,
    IN PVOID ApcRoutine OPTIONAL,
    IN PVOID ApcContext,
    OUT xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock,
    IN ULONG FsControlCode,
    IN PVOID InputBuffer,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer,
    IN ULONG OutputBufferLength
)
{
    return EmuNtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock,
                                    FsControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);
}

extern "C" NTSTATUS NTAPI EmuNtPulseEvent
(
    IN HANDLE EventHandle,
    OUT PLONG PreviousState OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(PreviousState != NULL)
        *PreviousState = 0;

    BOOL Ok = PulseEvent(EventHandle);

    EmuSwapFS();   // Xbox FS

    return Ok ? STATUS_SUCCESS : EmuStatusInvalidHandle;
}

extern "C" NTSTATUS NTAPI EmuNtQueueApcThread
(
    IN HANDLE ThreadHandle,
    IN PVOID ApcRoutine,
    IN PVOID ApcArgument1,
    IN PVOID ApcArgument2,
    IN PVOID ApcArgument3
)
{
    (void)ApcArgument2;
    (void)ApcArgument3;

    EmuSwapFS();   // Win2k/XP FS

    BOOL Ok = (ThreadHandle != NULL && ApcRoutine != NULL) ?
              QueueUserAPC((PAPCFUNC)ApcRoutine, ThreadHandle, (::ULONG_PTR)ApcArgument1) != 0 : FALSE;

    EmuSwapFS();   // Xbox FS

    return Ok ? STATUS_SUCCESS : EmuStatusInvalidParameter;
}

extern "C" NTSTATUS NTAPI EmuNtSetSystemTime
(
    IN xboxkrnl::PLARGE_INTEGER SystemTime,
    OUT xboxkrnl::PLARGE_INTEGER PreviousTime OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    EmuRefreshKernelTimeGlobals();
    if(PreviousTime != NULL)
        PreviousTime->QuadPart = (LONGLONG)g_EmuKeSystemTime;

    if(SystemTime != NULL)
        g_EmuKeSystemTime = (ULONGLONG)SystemTime->QuadPart;

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtSetTimerEx
(
    IN HANDLE TimerHandle,
    IN xboxkrnl::PLARGE_INTEGER DueTime,
    IN PVOID TimerApcRoutine OPTIONAL,
    IN PVOID TimerContext OPTIONAL,
    IN BOOLEAN ResumeTimer,
    IN LONG Period,
    OUT PBOOLEAN PreviousState OPTIONAL
)
{
    (void)ResumeTimer;

    EmuSwapFS();   // Win2k/XP FS

    ::LARGE_INTEGER HostDueTime;
    HostDueTime.QuadPart = (DueTime != NULL) ? DueTime->QuadPart : 0;

    BOOL Ok = SetWaitableTimer(TimerHandle, &HostDueTime, Period, (PTIMERAPCROUTINE)TimerApcRoutine, TimerContext, FALSE);
    if(PreviousState != NULL)
        *PreviousState = FALSE;

    EmuSwapFS();   // Xbox FS

    return Ok ? STATUS_SUCCESS : EmuStatusInvalidHandle;
}

extern "C" NTSTATUS NTAPI EmuNtSignalAndWaitForSingleObject
(
    IN HANDLE SignalHandle,
    IN HANDLE WaitHandle,
    IN BOOLEAN Alertable,
    IN xboxkrnl::PLARGE_INTEGER Timeout OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    DWORD Milliseconds = INFINITE;
    if(Timeout != NULL && Timeout->QuadPart == 0)
        Milliseconds = 0;

    DWORD Wait = SignalObjectAndWait(SignalHandle, WaitHandle, Milliseconds, Alertable);

    EmuSwapFS();   // Xbox FS

    return (Wait == WAIT_OBJECT_0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

// 0x00E6 - NtSignalAndWaitForSingleObjectEx: as NtSignalAndWaitForSingleObject but
// with an explicit wait mode; atomically signals one object then waits on another.
extern "C" NTSTATUS NTAPI EmuNtSignalAndWaitForSingleObjectEx
(
    IN HANDLE SignalHandle,
    IN HANDLE WaitHandle,
    IN UCHAR WaitMode,
    IN BOOLEAN Alertable,
    IN xboxkrnl::PLARGE_INTEGER Timeout OPTIONAL
)
{
    EmuSwapFS();   // Win2k/XP FS

    DWORD Milliseconds = INFINITE;
    if(Timeout != NULL && Timeout->QuadPart == 0)
        Milliseconds = 0;

    DWORD Wait = SignalObjectAndWait(SignalHandle, WaitHandle, Milliseconds, Alertable);

    EmuSwapFS();   // Xbox FS

    return (Wait == WAIT_OBJECT_0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

extern "C" NTSTATUS NTAPI EmuNtWaitForMultipleObjectsEx
(
    IN ULONG Count,
    IN HANDLE Handles[],
    IN ULONG WaitType,
    IN ULONG WaitMode,
    IN BOOLEAN Alertable,
    IN xboxkrnl::PLARGE_INTEGER Timeout OPTIONAL
)
{
    (void)WaitMode;

    EmuSwapFS();   // Win2k/XP FS

    DWORD Milliseconds = INFINITE;
    if(Timeout != NULL && Timeout->QuadPart == 0)
        Milliseconds = 0;

    DWORD Wait = WaitForMultipleObjectsEx(Count, Handles, WaitType != 0, Milliseconds, Alertable);
    NTSTATUS Status = (Wait >= WAIT_OBJECT_0 && Wait < WAIT_OBJECT_0 + Count) ? (NTSTATUS)(Wait - WAIT_OBJECT_0) : STATUS_UNSUCCESSFUL;

    EmuSwapFS();   // Xbox FS

    return Status;
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

    if(EmuFileIoTraceEnabled())
    {
        printf("FIO| dup tid=0x%X src=0x%X dst=0x%X status=0x%.08X\n",
               (uint32)GetCurrentThreadId(), SourceHandle,
               (TargetHandle != NULL) ? (uint32)*TargetHandle : 0, (uint32)ret);
    }

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
// * 0x00CC - NtProtectVirtualMemory
// ******************************************************************
extern "C" NTSTATUS NTAPI EmuNtProtectVirtualMemory
(
    IN OUT PVOID *BaseAddress,
    IN OUT PULONG RegionSize,
    IN ULONG NewProtect,
    OUT PULONG OldProtect
)
{
    EmuSwapFS();   // Win2k/XP FS

    NTSTATUS ret = EmuStatusInvalidParameter;

    if(BaseAddress != NULL && *BaseAddress != NULL && RegionSize != NULL && *RegionSize != 0 && OldProtect != NULL)
        ret = EmuProtectVirtualMemory(*BaseAddress, *RegionSize, NewProtect, OldProtect);

    printf("EmuKrnl (0x%lX): NtProtectVirtualMemory base=%p size=0x%.08lX new=0x%.08lX old=0x%.08lX ret=0x%.08lX.\n",
           GetCurrentThreadId(),
           (BaseAddress != NULL) ? *BaseAddress : NULL,
           (ULONG)((RegionSize != NULL) ? *RegionSize : 0),
           NewProtect,
           (ULONG)((OldProtect != NULL) ? *OldProtect : 0),
           ret);

    EmuSwapFS();   // Xbox FS

    return ret;
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

extern "C" NTSTATUS NTAPI EmuNtQueryDirectoryObject
(
    IN HANDLE DirectoryHandle,
    OUT PVOID Buffer,
    IN ULONG Length,
    IN BOOLEAN ReturnSingleEntry,
    IN BOOLEAN RestartScan,
    IN OUT PULONG Context,
    OUT PULONG ReturnLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnLength != NULL)
        *ReturnLength = 0;
    if(Context != NULL)
        *Context = 0;
    if(Buffer != NULL && Length != 0)
        ZeroMemory(Buffer, Length);

    printf("EmuKrnl (0x%lX): NtQueryDirectoryObject handle=%p length=0x%.08lX single=%lu restart=%lu.\n",
           GetCurrentThreadId(), DirectoryHandle, Length, (ULONG)ReturnSingleEntry, (ULONG)RestartScan);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtQueryEvent
(
    IN HANDLE EventHandle,
    IN ULONG EventInformationClass,
    OUT PVOID EventInformation,
    IN ULONG EventInformationLength,
    OUT PULONG ReturnLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnLength != NULL)
        *ReturnLength = EventInformationLength;
    if(EventInformation != NULL && EventInformationLength != 0)
        ZeroMemory(EventInformation, EventInformationLength);

    printf("EmuKrnl (0x%lX): NtQueryEvent handle=%p class=0x%.08lX length=0x%.08lX.\n",
           GetCurrentThreadId(), EventHandle, EventInformationClass, EventInformationLength);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtQueryIoCompletion
(
    IN HANDLE IoCompletionHandle,
    IN ULONG IoCompletionInformationClass,
    OUT PVOID IoCompletionInformation,
    IN ULONG IoCompletionInformationLength,
    OUT PULONG ReturnLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnLength != NULL)
        *ReturnLength = IoCompletionInformationLength;
    if(IoCompletionInformation != NULL && IoCompletionInformationLength != 0)
        ZeroMemory(IoCompletionInformation, IoCompletionInformationLength);

    printf("EmuKrnl (0x%lX): NtQueryIoCompletion handle=%p class=0x%.08lX length=0x%.08lX.\n",
           GetCurrentThreadId(), IoCompletionHandle, IoCompletionInformationClass, IoCompletionInformationLength);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtQueryMutant
(
    IN HANDLE MutantHandle,
    IN ULONG MutantInformationClass,
    OUT PVOID MutantInformation,
    IN ULONG MutantInformationLength,
    OUT PULONG ReturnLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnLength != NULL)
        *ReturnLength = MutantInformationLength;
    if(MutantInformation != NULL && MutantInformationLength != 0)
        ZeroMemory(MutantInformation, MutantInformationLength);

    printf("EmuKrnl (0x%lX): NtQueryMutant handle=%p class=0x%.08lX length=0x%.08lX.\n",
           GetCurrentThreadId(), MutantHandle, MutantInformationClass, MutantInformationLength);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtQuerySemaphore
(
    IN HANDLE SemaphoreHandle,
    IN ULONG SemaphoreInformationClass,
    OUT PVOID SemaphoreInformation,
    IN ULONG SemaphoreInformationLength,
    OUT PULONG ReturnLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnLength != NULL)
        *ReturnLength = SemaphoreInformationLength;
    if(SemaphoreInformation != NULL && SemaphoreInformationLength != 0)
        ZeroMemory(SemaphoreInformation, SemaphoreInformationLength);

    printf("EmuKrnl (0x%lX): NtQuerySemaphore handle=%p class=0x%.08lX length=0x%.08lX.\n",
           GetCurrentThreadId(), SemaphoreHandle, SemaphoreInformationClass, SemaphoreInformationLength);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtQuerySymbolicLinkObject
(
    IN HANDLE LinkHandle,
    IN OUT xboxkrnl::PSTRING LinkTarget,
    OUT PULONG ReturnedLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnedLength != NULL)
        *ReturnedLength = 0;
    if(LinkTarget != NULL && LinkTarget->Buffer != NULL && LinkTarget->MaximumLength != 0)
        LinkTarget->Buffer[0] = '\0';

    printf("EmuKrnl (0x%lX): NtQuerySymbolicLinkObject handle=%p target=%p.\n",
           GetCurrentThreadId(), LinkHandle, LinkTarget);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuNtQueryTimer
(
    IN HANDLE TimerHandle,
    IN ULONG TimerInformationClass,
    OUT PVOID TimerInformation,
    IN ULONG TimerInformationLength,
    OUT PULONG ReturnLength
)
{
    EmuSwapFS();   // Win2k/XP FS

    if(ReturnLength != NULL)
        *ReturnLength = TimerInformationLength;
    if(TimerInformation != NULL && TimerInformationLength != 0)
        ZeroMemory(TimerInformation, TimerInformationLength);

    printf("EmuKrnl (0x%lX): NtQueryTimer handle=%p class=0x%.08lX length=0x%.08lX.\n",
           GetCurrentThreadId(), TimerHandle, TimerInformationClass, TimerInformationLength);

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

// ******************************************************************
// * 0x00D9 - NtQueryVirtualMemory
// ******************************************************************
extern "C" NTSTATUS NTAPI EmuNtQueryVirtualMemory
(
    IN PVOID BaseAddress,
    OUT MEMORY_BASIC_INFORMATION *Buffer
)
{
    EmuSwapFS();   // Win2k/XP FS

    NTSTATUS ret = EmuStatusInvalidParameter;

    if(Buffer != NULL)
    {
        ZeroMemory(Buffer, sizeof(*Buffer));

        EmuIoSpaceMapping *Mapping = EmuFindIoSpaceMapping(BaseAddress);
        if(Mapping != NULL)
        {
            Buffer->BaseAddress = Mapping->Address;
            Buffer->AllocationBase = Mapping->Address;
            Buffer->AllocationProtect = Mapping->Protect;
            Buffer->RegionSize = Mapping->Size;
            Buffer->State = MEM_COMMIT;
            Buffer->Protect = Mapping->Protect;
            Buffer->Type = MEM_PRIVATE;
            ret = STATUS_SUCCESS;
        }
        else if(VirtualQuery(BaseAddress, Buffer, sizeof(*Buffer)) == sizeof(*Buffer))
        {
            ret = STATUS_SUCCESS;
        }
        else
        {
            ret = STATUS_UNSUCCESSFUL;
        }
    }

    printf("EmuKrnl (0x%lX): NtQueryVirtualMemory base=%p buffer=%p ret=0x%.08lX.\n",
           GetCurrentThreadId(), BaseAddress, Buffer, ret);

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

    // A failed read is a prime "dirty disc" trigger for titles — log it even
    // without the opt-in trace. STATUS_PENDING (0x103) is not a failure.
    if(EmuFileIoTraceEnabled() || FAILED(ret))
    {
        xboxkrnl::PIO_STATUS_BLOCK iosb = (xboxkrnl::PIO_STATUS_BLOCK)IoStatusBlock;
        uint32 info = (iosb != NULL && !IsBadReadPtr(iosb, sizeof(*iosb))) ? (uint32)iosb->Information : 0xFFFFFFFF;

        if(ByteOffset != NULL)
            printf("FIO| read tid=0x%X handle=0x%X off=0x%.08X%.08X len=0x%X event=0x%X apc=0x%X status=0x%.08X info=0x%X\n",
                   (uint32)GetCurrentThreadId(), FileHandle, (uint32)ByteOffset->u.HighPart,
                   (uint32)ByteOffset->u.LowPart, Length, Event, ApcRoutine, (uint32)ret, info);
        else
            printf("FIO| read tid=0x%X handle=0x%X off=current len=0x%X event=0x%X apc=0x%X status=0x%.08X info=0x%X\n",
                   (uint32)GetCurrentThreadId(), FileHandle, Length, Event, ApcRoutine, (uint32)ret, info);

        if(ret == 0x00000103 && EmuFileIoTraceLevel() >= 2 &&
           iosb != NULL && !IsBadReadPtr(iosb, sizeof(*iosb)))
        {
            // Wait on the FILE handle, not the title's event — a
            // synchronization event would be consumed by our wait, but a file
            // object stays signaled until the next IO starts.
            DWORD start = GetTickCount();
            DWORD wr = WaitForSingleObject(FileHandle, 2000);

            printf("FIO| read-completion tid=0x%X handle=0x%X wait=0x%lX status=0x%.08X info=0x%X waited=%lums\n",
                   (uint32)GetCurrentThreadId(), FileHandle, wr, (uint32)iosb->u1.Status,
                   (uint32)iosb->Information, GetTickCount() - start);
        }
    }

    EmuSwapFS();   // Xbox FS

    return ret;
}

extern "C" NTSTATUS NTAPI EmuNtReadFileScatter
(
    IN HANDLE FileHandle,
    IN HANDLE Event OPTIONAL,
    IN PVOID ApcRoutine OPTIONAL,
    IN PVOID ApcContext,
    OUT PVOID IoStatusBlock,
    OUT PVOID SegmentArray,
    IN ULONG Length,
    IN xboxkrnl::PLARGE_INTEGER ByteOffset OPTIONAL
)
{
    PVOID Buffer = NULL;
    if(SegmentArray != NULL)
        Buffer = *(PVOID*)SegmentArray;

    return xboxkrnl::NtReadFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset);
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

    if(EmuFileIoTraceEnabled())
    {
        // Class 14 = FilePositionInformation: the payload is the new file
        // offset, the piece needed to reconstruct offset-less reads.
        if(FileInformationClass == 14 && FileInformation != NULL && Length >= sizeof(LONGLONG))
        {
            PLARGE_INTEGER pos = (PLARGE_INTEGER)FileInformation;
            printf("FIO| seek tid=0x%X handle=0x%X pos=0x%.08X%.08X status=0x%.08X\n",
                   (uint32)GetCurrentThreadId(), FileHandle, (uint32)pos->u.HighPart,
                   (uint32)pos->u.LowPart, (uint32)ret);
        }
        else
        {
            printf("FIO| setinfo tid=0x%X handle=0x%X class=%u status=0x%.08X\n",
                   (uint32)GetCurrentThreadId(), FileHandle, (uint32)FileInformationClass, (uint32)ret);
        }
    }

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

    // XAPI overlapped IO passes this kernel export as the NtReadFile /
    // NtWriteFile ApcRoutine with the title's completion routine as
    // ApcContext. The real dispatcher (xboxkrnl 5455 NtUserIoApcDispatcher)
    // computes FileIOCompletionRoutine-style arguments from the IOSB:
    //   success: ApcContext(0, Information, IoStatusBlock)
    //   error  : ApcContext(RtlNtStatusToDosError(Status), 0, IoStatusBlock)
    // The old shim here always passed the 0xC0000000 severity MASK as
    // dwErrorCode (and Status as the byte count), so titles saw every
    // completed read as failed — Turok Evolution mapped that to its
    // dirty-disc screen.
    ULONG dwErrorCode = 0;
    ULONG dwBytes = 0;

    if(((ULONG)IoStatusBlock->u1.Status & 0xC0000000) == 0xC0000000)
        dwErrorCode = NtDll::RtlNtStatusToDosError(IoStatusBlock->u1.Status);
    else
        dwBytes = (ULONG)IoStatusBlock->Information;

    EmuSwapFS();   // Xbox FS

    __asm
    {
        pushad

        mov eax, IoStatusBlock
        mov ecx, dwBytes
        mov edx, dwErrorCode

        push eax
        push ecx
        push edx
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

extern "C" NTSTATUS NTAPI EmuNtWriteFileGather
(
    IN HANDLE FileHandle,
    IN PVOID Event,
    IN PVOID ApcRoutine,
    IN PVOID ApcContext,
    OUT PVOID IoStatusBlock,
    IN PVOID SegmentArray,
    IN ULONG Length,
    IN PVOID ByteOffset
)
{
    PVOID Buffer = NULL;
    if(SegmentArray != NULL)
        Buffer = *(PVOID*)SegmentArray;

    return xboxkrnl::NtWriteFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset);
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

extern "C" NTSTATUS NTAPI EmuPsQueryStatistics(PULONG Statistics)
{
    EmuSwapFS();   // Win2k/XP FS

    if(Statistics == NULL || !EmuIsWritableMemoryRange(Statistics, sizeof(ULONG) * 3) || Statistics[0] != sizeof(ULONG) * 3)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInvalidParameter;
    }

    Statistics[1] = GetCurrentThreadId();
    Statistics[2] = (ULONG)g_EmuThreadSuspendCounts.size();

    EmuSwapFS();   // Xbox FS

    return STATUS_SUCCESS;
}

extern "C" NTSTATUS NTAPI EmuPsSetCreateThreadNotifyRoutine(PVOID NotifyRoutine)
{
    EmuSwapFS();   // Win2k/XP FS

    if(NotifyRoutine == NULL)
    {
        EmuSwapFS();   // Xbox FS
        return EmuStatusInvalidParameter;
    }

    for(ULONG i = 0; i < sizeof(g_EmuPsThreadNotifyRoutines) / sizeof(g_EmuPsThreadNotifyRoutines[0]); i++)
    {
        if(g_EmuPsThreadNotifyRoutines[i] == NotifyRoutine)
        {
            EmuSwapFS();   // Xbox FS
            return STATUS_SUCCESS;
        }
    }

    for(ULONG i = 0; i < sizeof(g_EmuPsThreadNotifyRoutines) / sizeof(g_EmuPsThreadNotifyRoutines[0]); i++)
    {
        if(g_EmuPsThreadNotifyRoutines[i] != NULL)
            continue;

        g_EmuPsThreadNotifyRoutines[i] = NotifyRoutine;
        g_EmuPsThreadNotifyRoutineCount++;

        printf("EmuKrnl (0x%lX): PsSetCreateThreadNotifyRoutine routine=%p count=%lu.\n",
               GetCurrentThreadId(), NotifyRoutine, g_EmuPsThreadNotifyRoutineCount);

        EmuSwapFS();   // Xbox FS
        return STATUS_SUCCESS;
    }

    EmuSwapFS();   // Xbox FS

    return EmuStatusInsufficientResources;
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

extern "C" NTSTATUS NTAPI EmuXeLoadSection(PVOID Section)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): XeLoadSection section=%p compatibility no-op.\n",
           GetCurrentThreadId(), Section);

    EmuSwapFS();   // Xbox FS

    return (Section != NULL) ? STATUS_SUCCESS : EmuStatusInvalidParameter;
}

extern "C" NTSTATUS NTAPI EmuXeUnloadSection(PVOID Section)
{
    EmuSwapFS();   // Win2k/XP FS

    printf("EmuKrnl (0x%lX): XeUnloadSection section=%p compatibility no-op.\n",
           GetCurrentThreadId(), Section);

    EmuSwapFS();   // Xbox FS

    return (Section != NULL) ? STATUS_SUCCESS : EmuStatusInvalidParameter;
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
