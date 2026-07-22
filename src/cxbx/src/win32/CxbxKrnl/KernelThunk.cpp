// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->KernelThunkTable.cpp
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

#include "Cxbx.h"
#include "core/trace.h"
#include "emulation_runtime.h"

#include <cstdarg>

extern "C" uint32 __cdecl EmuDbgPrint(const char *Format, ...);
extern "C" VOID NTAPI EmuDbgBreakPoint();
extern "C" VOID NTAPI EmuDbgBreakPointWithStatus(ULONG Status);
extern "C" ULONG NTAPI EmuDbgPrompt(PCHAR Prompt, PCHAR Response, ULONG Length);
extern "C" PVOID NTAPI EmuAvGetSavedDataAddress();
extern "C" VOID NTAPI EmuAvSendTVEncoderOption(PVOID RegisterBase, ULONG Option, ULONG Param, ULONG *Result);
extern "C" ULONG NTAPI EmuAvSetDisplayMode(PVOID RegisterBase, ULONG Step, ULONG Mode, ULONG Format, ULONG Pitch, ULONG FrameBuffer);
extern "C" VOID NTAPI EmuAvSetSavedDataAddress(PVOID Address);
extern "C" VOID NTAPI EmuExAcquireReadWriteLockExclusive(PVOID Lock);
extern "C" VOID NTAPI EmuExAcquireReadWriteLockShared(PVOID Lock);
extern "C" PVOID NTAPI EmuExAllocatePoolWithTag(ULONG NumberOfBytes, ULONG Tag);
extern "C" VOID NTAPI EmuExFreePool(PVOID P);
extern "C" VOID NTAPI EmuExInitializeReadWriteLock(PVOID Lock);
extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuExInterlockedAddLargeInteger(xboxkrnl::PLARGE_INTEGER Addend, xboxkrnl::LARGE_INTEGER Increment, PVOID Lock);
extern "C" VOID NTAPI EmuExInterlockedAddLargeStatistic(xboxkrnl::PLARGE_INTEGER Addend, ULONG Increment);
extern "C" LONGLONG NTAPI EmuExInterlockedCompareExchange64(LONGLONG *Destination, LONGLONG *Exchange, LONGLONG *Comperand, PVOID Lock);
extern "C" ULONG NTAPI EmuExQueryPoolBlockSize(PVOID PoolBlock);
extern "C" NTSTATUS NTAPI EmuExReadWriteRefurbInfo(PVOID Buffer, ULONG BufferLength, BOOLEAN WriteMode);
extern "C" VOID NTAPI EmuExRaiseException(PEXCEPTION_RECORD ExceptionRecord);
extern "C" VOID NTAPI EmuExRaiseStatus(NTSTATUS Status);
extern "C" VOID NTAPI EmuExReleaseReadWriteLock(PVOID Lock);
extern "C" NTSTATUS NTAPI EmuExSaveNonVolatileSetting(DWORD ValueIndex, DWORD Type, PUCHAR Value, xboxkrnl::SIZE_T ValueLength);
extern "C" xboxkrnl::PLIST_ENTRY __fastcall EmuExfInterlockedInsertHeadList(xboxkrnl::PLIST_ENTRY ListHead, xboxkrnl::PLIST_ENTRY ListEntry);
extern "C" xboxkrnl::PLIST_ENTRY __fastcall EmuExfInterlockedInsertTailList(xboxkrnl::PLIST_ENTRY ListHead, xboxkrnl::PLIST_ENTRY ListEntry);
extern "C" xboxkrnl::PLIST_ENTRY __fastcall EmuExfInterlockedRemoveHeadList(xboxkrnl::PLIST_ENTRY ListHead);
extern "C" xboxkrnl::LONG NTAPI EmuFscGetCacheSize();
extern "C" VOID NTAPI EmuFscInvalidateIdleBlocks();
extern "C" NTSTATUS NTAPI EmuNtOpenDirectoryObject(PHANDLE DirectoryHandle, xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes);
extern "C" NTSTATUS NTAPI EmuNtOpenSymbolicLinkObject(PHANDLE LinkHandle, xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes);
extern "C" NTSTATUS NTAPI EmuNtCreateIoCompletion(PHANDLE IoCompletionHandle, ACCESS_MASK DesiredAccess, PVOID ObjectAttributes, ULONG Count);
extern "C" ULONG NTAPI EmuHalGetInterruptVector(ULONG BusInterruptLevel, PUCHAR Irql);
extern "C" BOOLEAN NTAPI EmuKeConnectInterrupt(PVOID InterruptObject);
extern "C" BOOLEAN NTAPI EmuKeDisconnectInterrupt(PVOID InterruptObject);
extern "C" VOID NTAPI EmuKeBugCheck(ULONG BugCheckCode);
extern "C" VOID NTAPI EmuKeBugCheckEx(ULONG BugCheckCode, ULONG Parameter1, ULONG Parameter2, ULONG Parameter3, ULONG Parameter4);
extern "C" BOOLEAN NTAPI EmuKeCancelTimer(xboxkrnl::PKTIMER Timer);
extern "C" VOID NTAPI EmuKeInitializeInterrupt(PVOID InterruptObject, PVOID ServiceRoutine, PVOID ServiceContext,
                                               ULONG Vector, ULONG Irql, ULONG InterruptMode, BOOLEAN ShareVector);
extern "C" VOID NTAPI EmuKeInitializeApc(PVOID Apc, PVOID Thread, UCHAR ApcStateIndex, PVOID KernelRoutine, PVOID RundownRoutine, PVOID NormalRoutine, UCHAR ApcMode, PVOID NormalContext);
extern "C" VOID NTAPI EmuKeInitializeDeviceQueue(PVOID DeviceQueue);
extern "C" VOID NTAPI EmuKeInitializeEvent(PVOID Event, ULONG Type, BOOLEAN State);
extern "C" VOID NTAPI EmuKeInitializeMutant(PVOID Mutant, BOOLEAN InitialOwner);
extern "C" VOID NTAPI EmuKeInitializeQueue(PVOID QueueObject, ULONG Count);
extern "C" VOID NTAPI EmuKeInitializeSemaphore(PVOID Semaphore, LONG Count, LONG Limit);
extern "C" BOOLEAN NTAPI EmuKeInsertByKeyDeviceQueue(PVOID DeviceQueue, PVOID DeviceQueueEntry, ULONG SortKey);
extern "C" BOOLEAN NTAPI EmuKeInsertDeviceQueue(PVOID DeviceQueue, PVOID DeviceQueueEntry);
extern "C" LONG NTAPI EmuKeInsertHeadQueue(PVOID QueueObject, PVOID Entry);
extern "C" LONG NTAPI EmuKeInsertQueue(PVOID QueueObject, PVOID Entry);
extern "C" BOOLEAN NTAPI EmuKeInsertQueueApc(PVOID Apc, PVOID SystemArgument1, PVOID SystemArgument2, UCHAR Increment);
extern "C" NTSTATUS NTAPI EmuNtCancelTimer(HANDLE TimerHandle, PBOOLEAN CurrentState);
extern "C" NTSTATUS NTAPI EmuNtCreateDirectoryObject(PHANDLE DirectoryHandle, xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes);
extern "C" NTSTATUS NTAPI EmuNtCreateSemaphore(PHANDLE SemaphoreHandle, PVOID ObjectAttributes, LONG InitialCount, LONG MaximumCount);
extern "C" NTSTATUS NTAPI EmuNtCreateTimer(PHANDLE TimerHandle, PVOID ObjectAttributes, ULONG TimerType);
extern "C" NTSTATUS NTAPI EmuNtDeleteFile(xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes);
extern "C" NTSTATUS NTAPI EmuNtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock, ULONG IoControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);
extern "C" NTSTATUS NTAPI EmuNtFlushBuffersFile(HANDLE FileHandle, xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock);
extern "C" NTSTATUS NTAPI EmuNtFsControlFile(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock, ULONG FsControlCode, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength);
extern "C" NTSTATUS NTAPI EmuNtSetIoCompletion(HANDLE IoCompletionHandle, PVOID KeyContext, PVOID ApcContext, NTSTATUS IoStatus, ULONG IoStatusInformation);
extern "C" NTSTATUS NTAPI EmuNtRemoveIoCompletion(HANDLE IoCompletionHandle, PVOID *KeyContext, PVOID *ApcContext, xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER Timeout);
extern "C" NTSTATUS NTAPI EmuNtPulseEvent(HANDLE EventHandle, PLONG PreviousState);
extern "C" NTSTATUS NTAPI EmuNtQueueApcThread(HANDLE ThreadHandle, PVOID ApcRoutine, PVOID ApcArgument1, PVOID ApcArgument2, PVOID ApcArgument3);
extern "C" NTSTATUS NTAPI EmuNtReleaseMutant(HANDLE MutantHandle, PLONG PreviousCount);
extern "C" NTSTATUS NTAPI EmuNtReleaseSemaphore(HANDLE SemaphoreHandle, LONG ReleaseCount, PLONG PreviousCount);
extern "C" NTSTATUS NTAPI EmuNtSetSystemTime(xboxkrnl::PLARGE_INTEGER SystemTime, xboxkrnl::PLARGE_INTEGER PreviousTime);
extern "C" NTSTATUS NTAPI EmuNtSetTimerEx(HANDLE TimerHandle, xboxkrnl::PLARGE_INTEGER DueTime, PVOID TimerApcRoutine, PVOID TimerContext, BOOLEAN ResumeTimer, LONG Period, PBOOLEAN PreviousState);
extern "C" NTSTATUS NTAPI EmuNtSignalAndWaitForSingleObject(HANDLE SignalHandle, HANDLE WaitHandle, BOOLEAN Alertable, xboxkrnl::PLARGE_INTEGER Timeout);
extern "C" NTSTATUS NTAPI EmuNtWaitForMultipleObjectsEx(ULONG Count, HANDLE Handles[], ULONG WaitType, ULONG WaitMode, BOOLEAN Alertable, xboxkrnl::PLARGE_INTEGER Timeout);
extern "C" ULONG NTAPI EmuHalReadSMCTrayState(PULONG TrayState, PULONG TrayStateChangeCount);
extern "C" ULONG NTAPI EmuHalReadSMBusValue(UCHAR Address, UCHAR Command, BOOLEAN WordFlag, PULONG Value);
extern "C" ULONG NTAPI EmuHalWriteSMBusValueCompat(ULONG Value);
extern "C" VOID NTAPI EmuHalRegisterShutdownNotification(PVOID ShutdownRegistration, BOOLEAN Register);
extern "C" VOID __fastcall EmuHalRequestSoftwareInterrupt(UCHAR Request);
extern "C" VOID __fastcall EmuHalClearSoftwareInterrupt(UCHAR Request);
extern "C" VOID NTAPI EmuHalDisableSystemInterrupt(UCHAR BusInterruptLevel);
extern "C" VOID NTAPI EmuHalEnableSystemInterrupt(UCHAR BusInterruptLevel, UCHAR InterruptMode);
extern "C" VOID NTAPI EmuHalWriteSMCScratchRegister(ULONG ScratchRegister);
extern "C" VOID NTAPI EmuHalEnableSecureTrayEject(ULONG BusInterruptLevel, BOOLEAN Enable);
extern "C" ULONG NTAPI EmuKeAlertResumeThread(xboxkrnl::PKTHREAD Thread);
extern "C" BOOLEAN NTAPI EmuKeAlertThread(xboxkrnl::PKTHREAD Thread, UCHAR AlertMode);
extern "C" VOID NTAPI EmuKeBoostPriorityThread(xboxkrnl::PKTHREAD Thread, LONG Increment);
extern "C" xboxkrnl::BOOLEAN NTAPI EmuKeSynchronizeExecution(PVOID Interrupt, PVOID SynchronizeRoutine, PVOID SynchronizeContext);
extern "C" NTSTATUS NTAPI EmuNtSignalAndWaitForSingleObjectEx(HANDLE SignalHandle, HANDLE WaitHandle, UCHAR WaitMode, BOOLEAN Alertable, xboxkrnl::PLARGE_INTEGER Timeout);
extern "C" VOID NTAPI EmuHalEnableSecureTrayEjectCompat();
extern "C" BOOLEAN NTAPI EmuHalIsResetOrShutdownPending();
extern "C" VOID NTAPI EmuHalInitiateShutdown();
extern "C" ULONG NTAPI EmuPhyGetLinkState(ULONG ForceUpdate);
extern "C" ULONG NTAPI EmuPhyInitialize(ULONG ForceReset, ULONG Reserved);
extern "C" ULONG g_EmuHalDiskCachePartitionCount;
extern "C" CHAR g_EmuHalDiskModelNumber[32];
extern "C" CHAR g_EmuHalDiskSerialNumber[32];
extern "C" NTSTATUS NTAPI EmuNtSuspendThread(HANDLE ThreadHandle, PULONG PreviousSuspendCount);
extern "C" NTSTATUS NTAPI EmuNtProtectVirtualMemory(PVOID *BaseAddress, PULONG RegionSize, ULONG NewProtect, PULONG OldProtect);
extern "C" NTSTATUS NTAPI EmuNtQueryDirectoryObject(HANDLE DirectoryHandle, PVOID Buffer, ULONG Length, BOOLEAN ReturnSingleEntry, BOOLEAN RestartScan, PULONG Context, PULONG ReturnLength);
extern "C" NTSTATUS NTAPI EmuNtQueryEvent(HANDLE EventHandle, ULONG EventInformationClass, PVOID EventInformation, ULONG EventInformationLength, PULONG ReturnLength);
extern "C" NTSTATUS NTAPI EmuNtQueryIoCompletion(HANDLE IoCompletionHandle, ULONG IoCompletionInformationClass, PVOID IoCompletionInformation, ULONG IoCompletionInformationLength, PULONG ReturnLength);
extern "C" NTSTATUS NTAPI EmuNtQueryMutant(HANDLE MutantHandle, ULONG MutantInformationClass, PVOID MutantInformation, ULONG MutantInformationLength, PULONG ReturnLength);
extern "C" NTSTATUS NTAPI EmuNtQuerySemaphore(HANDLE SemaphoreHandle, ULONG SemaphoreInformationClass, PVOID SemaphoreInformation, ULONG SemaphoreInformationLength, PULONG ReturnLength);
extern "C" NTSTATUS NTAPI EmuNtQuerySymbolicLinkObject(HANDLE LinkHandle, xboxkrnl::PSTRING LinkTarget, PULONG ReturnedLength);
extern "C" NTSTATUS NTAPI EmuNtQueryTimer(HANDLE TimerHandle, ULONG TimerInformationClass, PVOID TimerInformation, ULONG TimerInformationLength, PULONG ReturnLength);
extern "C" NTSTATUS NTAPI EmuNtQueryVirtualMemory(PVOID BaseAddress, MEMORY_BASIC_INFORMATION *Buffer);
extern "C" NTSTATUS NTAPI EmuNtReadFileScatter(HANDLE FileHandle, HANDLE Event, PVOID ApcRoutine, PVOID ApcContext, PVOID IoStatusBlock, PVOID SegmentArray, ULONG Length, xboxkrnl::PLARGE_INTEGER ByteOffset);
extern "C" NTSTATUS NTAPI EmuNtWriteFileGather(HANDLE FileHandle, PVOID Event, PVOID ApcRoutine, PVOID ApcContext, PVOID IoStatusBlock, PVOID SegmentArray, ULONG Length, PVOID ByteOffset);
extern "C" PVOID NTAPI EmuMmClaimGpuInstanceMemory(xboxkrnl::SIZE_T NumberOfBytes, xboxkrnl::SIZE_T *NumberOfPaddingBytes);
extern "C" PVOID NTAPI EmuMmCreateKernelStack(ULONG NumberOfBytes, BOOLEAN DebuggerThread);
extern "C" VOID NTAPI EmuMmDeleteKernelStack(PVOID StackBase, PVOID StackLimit);
extern "C" xboxkrnl::PHYSICAL_ADDRESS NTAPI EmuMmGetPhysicalAddress(PVOID BaseAddress);
extern "C" VOID NTAPI EmuMmLockUnlockBufferPages(PVOID BaseAddress, xboxkrnl::SIZE_T NumberOfBytes, BOOLEAN UnlockPages);
extern "C" VOID NTAPI EmuMmLockUnlockPhysicalPage(ULONG PhysicalAddress, BOOLEAN UnlockPage);
extern "C" PVOID NTAPI EmuMmMapIoSpace(xboxkrnl::PHYSICAL_ADDRESS PhysicalAddress, ULONG NumberOfBytes, ULONG Protect);
extern "C" ULONG NTAPI EmuMmQueryAddressProtect(PVOID VirtualAddress);
extern "C" ULONG NTAPI EmuMmQueryAllocationSize(PVOID BaseAddress);
extern "C" NTSTATUS NTAPI EmuMmQueryStatistics(PVOID MemoryStatistics);
extern "C" VOID NTAPI EmuMmUnmapIoSpace(PVOID BaseAddress, ULONG NumberOfBytes);
extern "C" LONG __fastcall EmuInterlockedCompareExchange(PLONG Destination, LONG Exchange, LONG Comperand);
extern "C" LONG __fastcall EmuInterlockedDecrement(PLONG Addend);
extern "C" LONG __fastcall EmuInterlockedIncrement(PLONG Addend);
extern "C" LONG __fastcall EmuInterlockedExchange(PLONG Target, LONG Value);
extern "C" LONG __fastcall EmuInterlockedExchangeAdd(PLONG Addend, LONG Value);
extern "C" LONG __fastcall EmuInterlockedDecrementExport(PLONG Addend);
extern "C" LONG __fastcall EmuInterlockedIncrementExport(PLONG Addend);
extern "C" LONG __fastcall EmuInterlockedExchangeExport(PLONG Target, LONG Value);
extern "C" LONG __fastcall EmuInterlockedExchangeAddExport(PLONG Addend, LONG Value);
extern "C" PVOID __fastcall EmuInterlockedFlushSList(PVOID ListHead);
extern "C" PVOID __fastcall EmuInterlockedPopEntrySList(PVOID ListHead);
extern "C" PVOID __fastcall EmuInterlockedPushEntrySList(PVOID ListHead, PVOID ListEntry);
extern "C" VOID NTAPI EmuREAD_PORT_BUFFER_UCHAR(PUCHAR Port, PUCHAR Buffer, ULONG Count);
extern "C" VOID NTAPI EmuREAD_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count);
extern "C" VOID NTAPI EmuREAD_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count);
extern "C" VOID NTAPI EmuWRITE_PORT_BUFFER_UCHAR(PUCHAR Port, PUCHAR Buffer, ULONG Count);
extern "C" VOID NTAPI EmuWRITE_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count);
extern "C" VOID NTAPI EmuWRITE_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count);
extern "C" UCHAR NTAPI EmuKeGetCurrentIrql();
extern "C" xboxkrnl::PKTHREAD NTAPI EmuKeGetCurrentThread();
extern "C" VOID NTAPI EmuKeEnterCriticalRegion();
extern "C" VOID NTAPI EmuKeLeaveCriticalRegion();
extern "C" BOOLEAN NTAPI EmuKeInsertQueueDpc(xboxkrnl::PKDPC Dpc, PVOID SystemArgument1, PVOID SystemArgument2);
extern "C" BOOLEAN NTAPI EmuKeIsExecutingDpc();
extern "C" UCHAR NTAPI EmuKeRaiseIrqlToDpcLevel();
extern "C" UCHAR NTAPI EmuKeRaiseIrqlToSynchLevel();
extern "C" BOOLEAN NTAPI EmuKeRemoveQueueDpc(xboxkrnl::PKDPC Dpc);
extern "C" VOID NTAPI EmuKeLowerIrql(UCHAR NewIrql);
extern "C" LONG NTAPI EmuKePulseEvent(PVOID Event, LONG Increment, BOOLEAN Wait);
extern "C" LONG NTAPI EmuKeQueryBasePriorityThread(xboxkrnl::PKTHREAD Thread);
extern "C" ULONGLONG NTAPI EmuKeQueryInterruptTime();
extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuKeQueryPerformanceCounter();
extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuKeQueryPerformanceFrequency();
extern "C" LONG NTAPI EmuKeReleaseMutant(PVOID Mutant, LONG Increment, BOOLEAN Abandoned, BOOLEAN Wait);
extern "C" LONG NTAPI EmuKeReleaseSemaphore(PVOID Semaphore, LONG Increment, LONG Adjustment, BOOLEAN Wait);
extern "C" PVOID NTAPI EmuKeRemoveByKeyDeviceQueue(PVOID DeviceQueue, ULONG SortKey);
extern "C" PVOID NTAPI EmuKeRemoveDeviceQueue(PVOID DeviceQueue);
extern "C" BOOLEAN NTAPI EmuKeRemoveEntryDeviceQueue(PVOID DeviceQueueEntry);
extern "C" PVOID NTAPI EmuKeRemoveQueue(PVOID QueueObject, UCHAR WaitMode, xboxkrnl::PLARGE_INTEGER Timeout);
extern "C" LONG NTAPI EmuKeResetEvent(PVOID Event);
extern "C" ULONG NTAPI EmuKeResumeThread(xboxkrnl::PKTHREAD Thread);
extern "C" NTSTATUS NTAPI EmuKeRestoreFloatingPointState(PVOID FloatingState);
extern "C" PVOID NTAPI EmuKeRundownQueue(PVOID QueueObject);
extern "C" NTSTATUS NTAPI EmuKeSaveFloatingPointState(PVOID FloatingState);
extern "C" LONG NTAPI EmuKeSetBasePriorityThread(xboxkrnl::PKTHREAD Thread, LONG Increment);
extern "C" VOID NTAPI EmuKeSetDisableBoostThread(xboxkrnl::PKTHREAD Thread, BOOLEAN Disable);
extern "C" LONG NTAPI EmuKeSetEventBoostPriority(PVOID Event, xboxkrnl::PKTHREAD Thread);
extern "C" LONG NTAPI EmuKeSetPriorityProcess(PVOID Process, LONG BasePriority);
extern "C" LONG NTAPI EmuKeSetPriorityThread(xboxkrnl::PKTHREAD Thread, LONG Priority);
extern "C" xboxkrnl::BOOLEAN NTAPI EmuKeSetTimerEx(xboxkrnl::PKTIMER Timer, xboxkrnl::LARGE_INTEGER DueTime, LONG Period, xboxkrnl::PKDPC Dpc);
extern "C" VOID NTAPI EmuKeStallExecutionProcessor(ULONG Microseconds);
extern "C" ULONG NTAPI EmuKeSuspendThread(xboxkrnl::PKTHREAD Thread);
extern "C" LONG NTAPI EmuKeSetEvent(PVOID Event, LONG Increment, UCHAR Wait);
extern "C" BOOLEAN NTAPI EmuKeTestAlertThread(UCHAR AlertMode);
extern "C" NTSTATUS NTAPI EmuKeWaitForMultipleObjects(ULONG Count, PVOID Object[], ULONG WaitType, UCHAR WaitMode, BOOLEAN Alertable, xboxkrnl::PLARGE_INTEGER Timeout, PVOID WaitBlockArray);
extern "C" NTSTATUS NTAPI EmuKeWaitForSingleObject(PVOID Object, ULONG WaitReason, UCHAR WaitMode, BOOLEAN Alertable, xboxkrnl::PLARGE_INTEGER Timeout);
extern "C" UCHAR __fastcall EmuKfRaiseIrql(UCHAR NewIrql);
extern "C" VOID __fastcall EmuKfLowerIrql(UCHAR NewIrql);
extern "C" VOID NTAPI EmuKiUnlockDispatcherDatabase(UCHAR OldIrql);
extern "C" ULONGLONG g_EmuKeInterruptTime;
extern "C" ULONGLONG g_EmuKeSystemTime;
extern "C" ULONG g_EmuKeTimeIncrement;
extern "C" ULONG g_EmuKiBugCheckData[5];
extern "C" ULONG g_EmuMmGlobalData[4];
extern "C" BOOLEAN NTAPI EmuMmIsAddressValid(PVOID VirtualAddress);
extern "C" NTSTATUS NTAPI EmuObReferenceObjectByHandle(HANDLE ObjectHandle, PVOID ObjectType, PVOID *Object);
extern "C" NTSTATUS NTAPI EmuObCreateObject(PVOID ObjectType, xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes, ULONG ObjectSize, PVOID *Object);
extern "C" NTSTATUS NTAPI EmuObInsertObject(PVOID Object, xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes, ULONG ObjectPointerBias, PHANDLE Handle);
extern "C" VOID NTAPI EmuObMakeTemporaryObject(PVOID Object);
extern "C" NTSTATUS NTAPI EmuObOpenObjectByName(xboxkrnl::POBJECT_ATTRIBUTES ObjectAttributes, PVOID ObjectType, PVOID ParseContext, PHANDLE Handle);
extern "C" NTSTATUS NTAPI EmuObOpenObjectByPointer(PVOID Object, PVOID ObjectType, PHANDLE Handle);
extern "C" NTSTATUS NTAPI EmuObReferenceObjectByName(xboxkrnl::PSTRING ObjectName, ULONG Attributes, PVOID ObjectType, PVOID ParseContext, PVOID *Object);
extern "C" NTSTATUS NTAPI EmuObReferenceObjectByPointer(PVOID Object, PVOID ObjectType);
extern "C" VOID __fastcall EmuObfDereferenceObject(PVOID Object);
extern "C" VOID __fastcall EmuObfReferenceObject(PVOID Object);
extern "C" ULONG g_EmuObpObjectHandleTable[4];
extern "C" PVOID NTAPI EmuIoAllocateIrp(xboxkrnl::CCHAR StackSize, BOOLEAN ChargeQuota);
extern "C" PVOID NTAPI EmuIoBuildAsynchronousFsdRequest(ULONG MajorFunction, PVOID DeviceObject, PVOID Buffer, ULONG Length, xboxkrnl::PLARGE_INTEGER StartingOffset, xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock);
extern "C" PVOID NTAPI EmuIoBuildDeviceIoControlRequest(ULONG IoControlCode, PVOID DeviceObject, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength, BOOLEAN InternalDeviceIoControl, PVOID Event, xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock);
extern "C" PVOID NTAPI EmuIoBuildSynchronousFsdRequest(ULONG MajorFunction, PVOID DeviceObject, PVOID Buffer, ULONG Length, xboxkrnl::PLARGE_INTEGER StartingOffset, PVOID Event, xboxkrnl::PIO_STATUS_BLOCK IoStatusBlock);
extern "C" NTSTATUS NTAPI EmuIoCheckShareAccess(ACCESS_MASK DesiredAccess, ULONG DesiredShareAccess, PVOID FileObject, PVOID ShareAccess, BOOLEAN Update);
extern "C" NTSTATUS NTAPI EmuIoCreateDevice(PVOID DriverObject, ULONG DeviceExtensionSize, xboxkrnl::PSTRING DeviceName, ULONG DeviceType, BOOLEAN Exclusive, PVOID *DeviceObject);
extern "C" VOID NTAPI EmuIoDeleteDevice(PVOID DeviceObject);
extern "C" NTSTATUS NTAPI EmuIoCreateSymbolicLink(xboxkrnl::PSTRING SymbolicLinkName, xboxkrnl::PSTRING DeviceName);
extern "C" NTSTATUS NTAPI EmuIoDeleteSymbolicLink(xboxkrnl::PSTRING SymbolicLinkName);
extern "C" NTSTATUS NTAPI EmuIoDismountVolume(PVOID DeviceObject);
extern "C" NTSTATUS NTAPI EmuIoDismountVolumeByName(xboxkrnl::PSTRING VolumeName);
extern "C" NTSTATUS NTAPI EmuIoDismountVolumeByFileHandle(HANDLE FileHandle);
extern "C" VOID NTAPI EmuIoFreeIrp(PVOID Irp);
extern "C" VOID NTAPI EmuIoInitializeIrp(PVOID Irp, USHORT PacketSize, xboxkrnl::CCHAR StackSize);
extern "C" NTSTATUS NTAPI EmuIoInvalidDeviceRequest(PVOID DeviceObject, PVOID Irp);
extern "C" NTSTATUS NTAPI EmuIoQueryFileInformation(PVOID FileObject, ULONG FileInformationClass, ULONG Length, PVOID FileInformation, PULONG ReturnedLength);
extern "C" NTSTATUS NTAPI EmuIoQueryVolumeInformation(PVOID FileObject, ULONG FsInformationClass, ULONG Length, PVOID FsInformation, PULONG ReturnedLength);
extern "C" VOID NTAPI EmuIoQueueThreadIrp(PVOID Irp);
extern "C" VOID NTAPI EmuIoRemoveShareAccess(PVOID FileObject, PVOID ShareAccess);
extern "C" VOID NTAPI EmuIoSetIoCompletion(HANDLE IoCompletionHandle, PVOID KeyContext, PVOID ApcContext, NTSTATUS IoStatus, ULONG IoStatusInformation);
extern "C" VOID NTAPI EmuIoSetShareAccess(ACCESS_MASK DesiredAccess, ULONG DesiredShareAccess, PVOID FileObject, PVOID ShareAccess);
extern "C" VOID NTAPI EmuIoStartNextPacket(PVOID DeviceObject, BOOLEAN Cancelable);
extern "C" VOID NTAPI EmuIoStartNextPacketByKey(PVOID DeviceObject, BOOLEAN Cancelable, ULONG Key);
extern "C" VOID NTAPI EmuIoStartPacket(PVOID DeviceObject, PVOID Irp, PULONG Key, PVOID CancelFunction);
extern "C" NTSTATUS NTAPI EmuIoSynchronousDeviceIoControlRequest(ULONG IoControlCode, PVOID DeviceObject, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength, PULONG ReturnedOutputLength, BOOLEAN InternalDeviceIoControl);
extern "C" NTSTATUS NTAPI EmuIoSynchronousFsdRequest(ULONG MajorFunction, PVOID DeviceObject, PVOID Buffer, ULONG Length, xboxkrnl::PLARGE_INTEGER StartingOffset);
extern "C" NTSTATUS NTAPI EmuIofCallDriver(PVOID DeviceObject, PVOID Irp);
extern "C" VOID NTAPI EmuIofCompleteRequest(PVOID Irp, xboxkrnl::CCHAR PriorityBoost);
extern "C" VOID NTAPI EmuIoMarkIrpMustComplete(PVOID Irp);
extern "C" NTSTATUS NTAPI EmuPsCreateSystemThread(PHANDLE ThreadHandle, ULONG ThreadExtraSize, xboxkrnl::PKSTART_ROUTINE StartRoutine, PVOID StartContext, BOOLEAN CreateSuspended);
extern "C" NTSTATUS NTAPI EmuPsQueryStatistics(PULONG Statistics);
extern "C" NTSTATUS NTAPI EmuPsSetCreateThreadNotifyRoutine(PVOID NotifyRoutine);
extern "C" NTSTATUS NTAPI EmuRtlAppendStringToString(xboxkrnl::PSTRING Destination, xboxkrnl::PSTRING Source);
extern "C" NTSTATUS NTAPI EmuRtlAppendUnicodeStringToString(xboxkrnl::PUNICODE_STRING Destination, xboxkrnl::PUNICODE_STRING Source);
extern "C" NTSTATUS NTAPI EmuRtlAppendUnicodeToString(xboxkrnl::PUNICODE_STRING Destination, USHORT *Source);
extern "C" VOID NTAPI EmuRtlCaptureContext(PVOID ContextRecord);
extern "C" USHORT NTAPI EmuRtlCaptureStackBackTrace(ULONG FramesToSkip, ULONG FramesToCapture, PVOID *BackTrace, PULONG BackTraceHash);
extern "C" VOID NTAPI EmuRtlAssert(PVOID FailedAssertion, PVOID FileName, ULONG LineNumber, PCHAR Message);
extern "C" NTSTATUS NTAPI EmuRtlCharToInteger(const char *String, ULONG Base, PULONG Value);
extern "C" SIZE_T NTAPI EmuRtlCompareMemory(const VOID *Source1, const VOID *Source2, SIZE_T Length);
extern "C" SIZE_T NTAPI EmuRtlCompareMemoryUlong(const VOID *Source, SIZE_T Length, ULONG Pattern);
extern "C" LONG NTAPI EmuRtlCompareString(xboxkrnl::PSTRING String1, xboxkrnl::PSTRING String2, BOOLEAN CaseInSensitive);
extern "C" LONG NTAPI EmuRtlCompareUnicodeString(xboxkrnl::PUNICODE_STRING String1, xboxkrnl::PUNICODE_STRING String2, BOOLEAN CaseInSensitive);
extern "C" VOID NTAPI EmuRtlCopyString(xboxkrnl::PSTRING DestinationString, const xboxkrnl::STRING *SourceString);
extern "C" VOID NTAPI EmuRtlCopyUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, const xboxkrnl::UNICODE_STRING *SourceString);
extern "C" BOOLEAN NTAPI EmuRtlCreateUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, const USHORT *SourceString);
extern "C" USHORT NTAPI EmuRtlDowncaseUnicodeChar(USHORT SourceCharacter);
extern "C" NTSTATUS NTAPI EmuRtlDowncaseUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, xboxkrnl::PUNICODE_STRING SourceString, BOOLEAN AllocateDestinationString);
extern "C" VOID NTAPI EmuRtlEnterCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection);
extern "C" VOID NTAPI EmuRtlEnterCriticalSectionAndRegion(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection);
extern "C" BOOLEAN NTAPI EmuRtlEqualString(xboxkrnl::PSTRING String1, xboxkrnl::PSTRING String2, BOOLEAN CaseInSensitive);
extern "C" BOOLEAN NTAPI EmuRtlEqualUnicodeString(xboxkrnl::PUNICODE_STRING String1, xboxkrnl::PUNICODE_STRING String2, BOOLEAN CaseInSensitive);
extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuRtlExtendedIntegerMultiply(xboxkrnl::LARGE_INTEGER Multiplicand, LONG Multiplier);
extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuRtlExtendedLargeIntegerDivide(xboxkrnl::LARGE_INTEGER Dividend, ULONG Divisor, PULONG Remainder);
extern "C" xboxkrnl::LARGE_INTEGER NTAPI EmuRtlExtendedMagicDivide(xboxkrnl::LARGE_INTEGER Dividend, xboxkrnl::LARGE_INTEGER MagicDivisor, xboxkrnl::CCHAR ShiftCount);
extern "C" VOID NTAPI EmuRtlFillMemory(PVOID Destination, SIZE_T Length, UCHAR Fill);
extern "C" VOID NTAPI EmuRtlFillMemoryUlong(PVOID Destination, SIZE_T Length, ULONG Pattern);
extern "C" VOID NTAPI EmuRtlFreeAnsiString(xboxkrnl::PANSI_STRING AnsiString);
extern "C" VOID NTAPI EmuRtlFreeUnicodeString(xboxkrnl::PUNICODE_STRING UnicodeString);
extern "C" VOID NTAPI EmuRtlGetCallersAddress(PVOID *CallerAddress, PVOID *CallersCaller);
extern "C" VOID NTAPI EmuRtlInitAnsiString(xboxkrnl::PANSI_STRING DestinationString, const char *SourceString);
extern "C" VOID NTAPI EmuRtlInitUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, USHORT *SourceString);
extern "C" VOID NTAPI EmuRtlInitializeCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection);
extern "C" NTSTATUS NTAPI EmuRtlIntegerToChar(ULONG Value, ULONG Base, ULONG Length, PCHAR String);
extern "C" NTSTATUS NTAPI EmuRtlIntegerToUnicodeString(ULONG Value, ULONG Base, xboxkrnl::PUNICODE_STRING String);
extern "C" VOID NTAPI EmuRtlLeaveCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection);
extern "C" VOID NTAPI EmuRtlLeaveCriticalSectionAndRegion(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection);
extern "C" CHAR NTAPI EmuRtlLowerChar(CHAR Character);
extern "C" VOID NTAPI EmuRtlMapGenericMask(PACCESS_MASK AccessMask, PGENERIC_MAPPING GenericMapping);
extern "C" VOID NTAPI EmuRtlMoveMemory(PVOID Destination, const VOID *Source, SIZE_T Length);
extern "C" NTSTATUS NTAPI EmuRtlMultiByteToUnicodeN(USHORT *UnicodeString, ULONG MaxBytesInUnicodeString, PULONG BytesInUnicodeString, const CHAR *MultiByteString, ULONG BytesInMultiByteString);
extern "C" NTSTATUS NTAPI EmuRtlMultiByteToUnicodeSize(PULONG BytesInUnicodeString, const CHAR *MultiByteString, ULONG BytesInMultiByteString);
extern "C" VOID NTAPI EmuRtlRaiseException(PEXCEPTION_RECORD ExceptionRecord);
extern "C" VOID NTAPI EmuRtlRaiseStatus(NTSTATUS Status);
extern "C" VOID NTAPI EmuRtlRip(PVOID ApiName, PVOID Expression, PVOID Message, PVOID Address);
extern "C" LONG NTAPI EmuRtlSnprintf(PCHAR Buffer, SIZE_T Count, const char *Format, ...);
extern "C" LONG NTAPI EmuRtlSprintf(PCHAR Buffer, const char *Format, ...);
extern "C" BOOLEAN NTAPI EmuRtlTimeFieldsToTime(const void *TimeFields, xboxkrnl::PLARGE_INTEGER Time);
extern "C" VOID NTAPI EmuRtlTimeToTimeFields(const xboxkrnl::LARGE_INTEGER *Time, void *TimeFields);
extern "C" BOOLEAN NTAPI EmuRtlTryEnterCriticalSection(xboxkrnl::PRTL_CRITICAL_SECTION CriticalSection);
extern "C" ULONG NTAPI EmuRtlUlongByteSwap(ULONG Source);
extern "C" NTSTATUS NTAPI EmuRtlUnicodeStringToInteger(xboxkrnl::PUNICODE_STRING String, ULONG Base, PULONG Value);
extern "C" NTSTATUS NTAPI EmuRtlUnicodeToMultiByteN(CHAR *MultiByteString, ULONG MaxBytesInMultiByteString, PULONG BytesInMultiByteString, const USHORT *UnicodeString, ULONG BytesInUnicodeString);
extern "C" NTSTATUS NTAPI EmuRtlUnicodeToMultiByteSize(PULONG BytesInMultiByteString, const USHORT *UnicodeString, ULONG BytesInUnicodeString);
extern "C" VOID NTAPI EmuRtlUnwind(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue);
extern "C" USHORT NTAPI EmuRtlUpcaseUnicodeChar(USHORT SourceCharacter);
extern "C" NTSTATUS NTAPI EmuRtlUpcaseUnicodeString(xboxkrnl::PUNICODE_STRING DestinationString, xboxkrnl::PUNICODE_STRING SourceString, BOOLEAN AllocateDestinationString);
extern "C" NTSTATUS NTAPI EmuRtlUpcaseUnicodeToMultiByteN(CHAR *MultiByteString, ULONG MaxBytesInMultiByteString, PULONG BytesInMultiByteString, const USHORT *UnicodeString, ULONG BytesInUnicodeString);
extern "C" CHAR NTAPI EmuRtlUpperChar(CHAR Character);
extern "C" VOID NTAPI EmuRtlUpperString(xboxkrnl::PSTRING DestinationString, const xboxkrnl::STRING *SourceString);
extern "C" USHORT NTAPI EmuRtlUshortByteSwap(USHORT Source);
extern "C" LONG NTAPI EmuRtlVsnprintf(PCHAR Buffer, SIZE_T Count, const char *Format, va_list Args);
extern "C" LONG NTAPI EmuRtlVsprintf(PCHAR Buffer, const char *Format, va_list Args);
extern "C" ULONG NTAPI EmuRtlWalkFrameChain(PVOID *Callers, ULONG Count, ULONG Flags);
extern "C" VOID NTAPI EmuRtlZeroMemory(PVOID Destination, SIZE_T Length);
extern "C" NTSTATUS NTAPI EmuXeLoadSection(PVOID Section);
extern "C" NTSTATUS NTAPI EmuXeUnloadSection(PVOID Section);
extern "C" VOID NTAPI EmuXcSHAInit(UCHAR *SHAContext);
extern "C" VOID NTAPI EmuXcSHAUpdate(UCHAR *SHAContext, UCHAR *Input, ULONG InputLength);
extern "C" VOID NTAPI EmuXcSHAFinal(UCHAR *SHAContext, UCHAR *Digest);
extern "C" VOID NTAPI EmuXcRC4Key(PUCHAR KeyStruct, ULONG KeyLength, PUCHAR Key);
extern "C" VOID NTAPI EmuXcRC4Crypt(PUCHAR KeyStruct, ULONG InputLength, PUCHAR Input);
extern "C" VOID NTAPI EmuXcHMAC(PUCHAR KeyMaterial, ULONG KeyMaterialLength, PUCHAR Data, ULONG DataLength, PUCHAR Data2, ULONG Data2Length, PUCHAR Digest);
extern "C" ULONG NTAPI EmuXcPKEncPublic(PUCHAR Key, PUCHAR Input, PUCHAR Output);
extern "C" ULONG NTAPI EmuXcPKDecPrivate(PUCHAR Key, PUCHAR Input, PUCHAR Output);
extern "C" ULONG NTAPI EmuXcPKGetKeyLen(PUCHAR Key);
extern "C" ULONG NTAPI EmuXcVerifyPKCS1Signature(PUCHAR Signature, PUCHAR Key, PUCHAR Digest);
extern "C" ULONG NTAPI EmuXcModExp(PULONG Output, PULONG Base, PULONG Exponent, PULONG Modulus, ULONG Count);
extern "C" VOID NTAPI EmuXcDESKeyParity(PUCHAR Key, ULONG KeyLength);
extern "C" VOID NTAPI EmuXcKeyTable(ULONG CipherSelector, PUCHAR KeyTable, PUCHAR Key);
extern "C" VOID NTAPI EmuXcBlockCrypt(ULONG CipherSelector, PUCHAR Output, PUCHAR Input, PUCHAR KeyTable, ULONG Encrypt);
extern "C" VOID NTAPI EmuXcBlockCryptCBC(ULONG CipherSelector, ULONG InputLength, PUCHAR Output, PUCHAR Input, PUCHAR KeyTable, ULONG Encrypt, PUCHAR Feedback);
extern "C" ULONG NTAPI EmuXcCryptService(ULONG Service, PVOID Buffer);
extern "C" ULONG NTAPI EmuXcUpdateCrypto(ULONG Unknown, PVOID Buffer);
extern "C" UCHAR g_EmuKdDebuggerEnabled;
extern "C" UCHAR g_EmuKdDebuggerNotPresent;
extern "C" UCHAR g_EmuXboxEEPROMKey[16];
extern "C" UCHAR g_EmuXboxHDKey[16];
extern "C" UCHAR g_EmuXboxLANKey[16];
extern "C" UCHAR g_EmuXboxAlternateSignatureKeys[32];
extern "C" UCHAR g_EmuXePublicKeyData[160];
extern "C" ULONG g_EmuIdexChannelObject[16];
struct EmuObjectType;
extern "C" EmuObjectType g_EmuExEventObjectType;
extern "C" EmuObjectType g_EmuExMutantObjectType;
extern "C" EmuObjectType g_EmuExSemaphoreObjectType;
extern "C" EmuObjectType g_EmuExTimerObjectType;
extern "C" EmuObjectType g_EmuIoCompletionObjectType;
extern "C" EmuObjectType g_EmuIoDeviceObjectType;
extern "C" EmuObjectType g_EmuIoFileObjectType;
extern "C" EmuObjectType g_EmuObDirectoryObjectType;
extern "C" EmuObjectType g_EmuObSymbolicLinkObjectType;
extern "C" EmuObjectType g_EmuPsThreadObjectType;
namespace xboxkrnl
{
    struct EmuXboxKernelVersion;
    extern EmuXboxKernelVersion EmuXboxKrnlVersion;
    extern STRING EmuXeImageFileName;
}

// ******************************************************************
// * NOTE:
// ******************************************************************
// *
// * Enable "#define PANIC(numb) numb" if you wish to find out what
// * kernel export the application is attempting to call. The app
// * will crash at the thunk number (i.e. PsCreateSystemThread:0xFF)
// *
// * For general use, you should probably just enable the other
// * option "#define PANIC(numb) cxbx_panic"
// *
// ******************************************************************
//#define PANIC(numb) EmuPanic

// ******************************************************************
// * Unimplemented-kernel-export warned trap.
// *
// * Historically an unwired ordinal stored its own number in the thunk
// * table (PANIC(numb) -> numb), so calling it jumped to a bogus low address
// * and crashed with no diagnostic. With CXBX_TRAP_UNIMPLEMENTED (default on)
// * each unwired ordinal instead points at a per-ordinal template stub that
// * logs which kernel export was called, and from where, then returns 0.
// *
// * Caveat: the Xbox kernel ABI is __stdcall (callee cleans arguments) and the
// * true argument count of an unimplemented export is unknown here, so for a
// * multi-argument export the stub cannot restore the caller's stack and the
// * title may still destabilise afterwards. The value is the diagnostic: you
// * learn which ordinal to implement next instead of chasing a crash at
// * address 0x000000NN. Build with CXBX_TRAP_UNIMPLEMENTED=0 to restore the
// * old crash-at-ordinal behaviour.
// ******************************************************************
#include "EmuKrnlLogging.h"

#ifndef CXBX_TRAP_UNIMPLEMENTED
#define CXBX_TRAP_UNIMPLEMENTED 1
#endif

#if CXBX_TRAP_UNIMPLEMENTED
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define CXBX_RETADDR() _ReturnAddress()
#else
#define CXBX_RETADDR() __builtin_return_address(0)
#endif

extern "C" void EmuUnimplementedKernelLog(int Ordinal, void *Caller)
{
    printf("KTRACE| UNIMPLEMENTED ordinal=%d (0x%03X) caller=%p\n",
           Ordinal, (unsigned)Ordinal, Caller);
    fflush(stdout);
}

template <int Ordinal>
static uint32 __stdcall CxbxUnimplementedStub(void)
{
    cxbx::trace::RecordFlight(cxbx::trace::Event::KernelBoundary, Ordinal);
    EmuUnimplementedKernelLog(Ordinal, CXBX_RETADDR());
    return 0;
}

#define PANIC(numb) &CxbxUnimplementedStub<numb>
#else
#define PANIC(numb) numb
#endif

// ******************************************************************
// * KernelThunkTable
// ******************************************************************
extern "C" CXBXKRNL_API uint32 KernelThunkTable[367] =
{
    (uint32)PANIC(0x0000),                          // 0x0000 (0)
    (uint32)&EmuAvGetSavedDataAddress,              // 0x0001 (1)
    (uint32)&EmuAvSendTVEncoderOption,              // 0x0002 (2)
    (uint32)&EmuAvSetDisplayMode,                   // 0x0003 (3)
    (uint32)&EmuAvSetSavedDataAddress,              // 0x0004 (4)
    (uint32)&EmuDbgBreakPoint,                      // 0x0005 (5)
    (uint32)&EmuDbgBreakPointWithStatus,            // 0x0006 (6)
    (uint32)PANIC(0x0007),                          // 0x0007 (7)
    (uint32)&EmuDbgPrint,                           // 0x0008 (8)
    (uint32)&EmuHalReadSMCTrayState,                // 0x0009 (9)
    (uint32)&EmuDbgPrompt,                          // 0x000A (10)
    (uint32)PANIC(0x000B),                          // 0x000B (11)
    (uint32)&EmuExAcquireReadWriteLockExclusive,    // 0x000C (12)
    (uint32)&EmuExAcquireReadWriteLockShared,       // 0x000D (13)
    (uint32)&xboxkrnl::ExAllocatePool,              // 0x000E (14)
    (uint32)&EmuExAllocatePoolWithTag,              // 0x000F (15)
    (uint32)&g_EmuExEventObjectType,                // 0x0010 (16)
    (uint32)&EmuExFreePool,                         // 0x0011 (17)
    (uint32)&EmuExInitializeReadWriteLock,          // 0x0012 (18)
    (uint32)&EmuExInterlockedAddLargeInteger,       // 0x0013 (19)
    (uint32)&EmuExInterlockedAddLargeStatistic,     // 0x0014 (20)
    (uint32)&EmuExInterlockedCompareExchange64,     // 0x0015 (21)
    (uint32)&g_EmuExMutantObjectType,               // 0x0016 (22)
    (uint32)&EmuExQueryPoolBlockSize,               // 0x0017 (23)
    (uint32)&xboxkrnl::ExQueryNonVolatileSetting,   // 0x0018 (24)
    (uint32)&EmuExReadWriteRefurbInfo,              // 0x0019 (25)
    (uint32)&EmuExRaiseException,                   // 0x001A (26)
    (uint32)&EmuExRaiseStatus,                      // 0x001B (27)
    (uint32)&EmuExReleaseReadWriteLock,             // 0x001C (28)
    (uint32)&EmuExSaveNonVolatileSetting,           // 0x001D (29)
    (uint32)&g_EmuExSemaphoreObjectType,            // 0x001E (30)
    (uint32)&g_EmuExTimerObjectType,                // 0x001F (31)
    (uint32)&EmuExfInterlockedInsertHeadList,       // 0x0020 (32)
    (uint32)&EmuExfInterlockedInsertTailList,       // 0x0021 (33)
    (uint32)&EmuExfInterlockedRemoveHeadList,       // 0x0022 (34)
    (uint32)&EmuFscGetCacheSize,                    // 0x0023 (35)
    (uint32)&EmuFscInvalidateIdleBlocks,            // 0x0024 (36)
    (uint32)&xboxkrnl::FscSetCacheSize,             // 0x0025 (37)
    (uint32)&EmuHalClearSoftwareInterrupt,          // 0x0026 (38)
    (uint32)&EmuHalDisableSystemInterrupt,          // 0x0027 (39)
    (uint32)&g_EmuHalDiskCachePartitionCount,       // 0x0028 (40)
    (uint32)&g_EmuHalDiskModelNumber,               // 0x0029 (41)
    (uint32)&g_EmuHalDiskSerialNumber,              // 0x002A (42)
    (uint32)&EmuHalEnableSystemInterrupt,           // 0x002B (43)
    (uint32)&EmuHalGetInterruptVector,              // 0x002C (44)
    (uint32)&EmuHalReadSMBusValue,                  // 0x002D (45)
    (uint32)&xboxkrnl::HalReadWritePCISpace,        // 0x002E (46)
    (uint32)&EmuHalRegisterShutdownNotification,    // 0x002F (47)
    (uint32)&EmuHalRequestSoftwareInterrupt,        // 0x0030 (48)
    (uint32)&xboxkrnl::HalReturnToFirmware,         // 0x0031 (49)
    (uint32)&xboxkrnl::HalWriteSMBusValue,          // 0x0032 (50)
    (uint32)&EmuInterlockedCompareExchange,         // 0x0033 (51)
    (uint32)&EmuInterlockedDecrement,               // 0x0034 (52)
    (uint32)&EmuInterlockedIncrement,               // 0x0035 (53)
    (uint32)&EmuInterlockedExchange,                // 0x0036 (54)
    (uint32)&EmuInterlockedExchangeAdd,             // 0x0037 (55)
    (uint32)&EmuInterlockedFlushSList,              // 0x0038 (56)
    (uint32)&EmuInterlockedPopEntrySList,           // 0x0039 (57)
    (uint32)&EmuInterlockedPushEntrySList,          // 0x003A (58)
    (uint32)&EmuIoAllocateIrp,                      // 0x003B (59)
    (uint32)&EmuIoBuildAsynchronousFsdRequest,      // 0x003C (60)
    (uint32)&EmuIoBuildDeviceIoControlRequest,      // 0x003D (61)
    (uint32)&EmuIoBuildSynchronousFsdRequest,       // 0x003E (62)
    (uint32)&EmuIoCheckShareAccess,                 // 0x003F (63)
    (uint32)&g_EmuIoCompletionObjectType,           // 0x0040 (64)
    (uint32)&EmuIoCreateDevice,                     // 0x0041 (65)
    (uint32)&xboxkrnl::IoCreateFile,                // 0x0042 (66)
    (uint32)&EmuIoCreateSymbolicLink,               // 0x0043 (67)
    (uint32)&EmuIoDeleteDevice,                     // 0x0044 (68)
    (uint32)&EmuIoDeleteSymbolicLink,               // 0x0045 (69)
    (uint32)&g_EmuIoDeviceObjectType,               // 0x0046 (70)
    (uint32)&g_EmuIoFileObjectType,                 // 0x0047 (71)
    (uint32)&EmuIoFreeIrp,                          // 0x0048 (72)
    (uint32)&EmuIoInitializeIrp,                    // 0x0049 (73)
    (uint32)&EmuIoInvalidDeviceRequest,             // 0x004A (74)
    (uint32)&EmuIoQueryFileInformation,             // 0x004B (75)
    (uint32)&EmuIoQueryVolumeInformation,           // 0x004C (76)
    (uint32)&EmuIoQueueThreadIrp,                   // 0x004D (77)
    (uint32)&EmuIoRemoveShareAccess,                // 0x004E (78)
    (uint32)&EmuIoSetIoCompletion,                  // 0x004F (79)
    (uint32)&EmuIoSetShareAccess,                   // 0x0050 (80)
    (uint32)&EmuIoStartNextPacket,                  // 0x0051 (81)
    (uint32)&EmuIoStartNextPacketByKey,             // 0x0052 (82)
    (uint32)&EmuIoStartPacket,                      // 0x0053 (83)
    (uint32)&EmuIoSynchronousDeviceIoControlRequest,// 0x0054 (84)
    (uint32)&EmuIoSynchronousFsdRequest,            // 0x0055 (85)
    (uint32)&EmuIofCallDriver,                      // 0x0056 (86)
    (uint32)&EmuIofCompleteRequest,                 // 0x0057 (87)
    (uint32)&g_EmuKdDebuggerEnabled,                // 0x0058 (88)
    (uint32)&g_EmuKdDebuggerNotPresent,             // 0x0059 (89)
    (uint32)&EmuIoDismountVolume,                   // 0x005A (90)
    (uint32)&EmuIoDismountVolumeByName,             // 0x005B (91)
    (uint32)&EmuKeAlertResumeThread,                // 0x005C (92)
    (uint32)&EmuKeAlertThread,                      // 0x005D (93)
    (uint32)&EmuKeBoostPriorityThread,              // 0x005E (94)
    (uint32)&EmuKeBugCheck,                         // 0x005F (95)
    (uint32)&EmuKeBugCheckEx,                       // 0x0060 (96)
    (uint32)&EmuKeCancelTimer,                      // 0x0061 (97)
    (uint32)&EmuKeConnectInterrupt,                 // 0x0062 (98)
    (uint32)&xboxkrnl::KeDelayExecutionThread,      // 0x0063 (99)
    (uint32)&EmuKeDisconnectInterrupt,              // 0x0064 (100)
    (uint32)&EmuKeEnterCriticalRegion,              // 0x0065 (101)
    (uint32)&g_EmuMmGlobalData,                     // 0x0066 (102)
    (uint32)&EmuKeGetCurrentIrql,                   // 0x0067 (103)
    (uint32)&EmuKeGetCurrentThread,                 // 0x0068 (104)
    (uint32)&EmuKeInitializeApc,                    // 0x0069 (105)
    (uint32)&EmuKeInitializeDeviceQueue,            // 0x006A (106)
    (uint32)&xboxkrnl::KeInitializeDpc,             // 0x006B (107)
    (uint32)&EmuKeInitializeEvent,                  // 0x006C (108)
    (uint32)&EmuKeInitializeInterrupt,              // 0x006D (109)
    (uint32)&EmuKeInitializeMutant,                 // 0x006E (110)
    (uint32)&EmuKeInitializeQueue,                  // 0x006F (111)
    (uint32)&EmuKeInitializeSemaphore,              // 0x0070 (112)
    (uint32)&xboxkrnl::KeInitializeTimerEx,         // 0x0071 (113)
    (uint32)&EmuKeInsertByKeyDeviceQueue,           // 0x0072 (114)
    (uint32)&EmuKeInsertDeviceQueue,                // 0x0073 (115)
    (uint32)&EmuKeInsertHeadQueue,                  // 0x0074 (116)
    (uint32)&EmuKeInsertQueue,                      // 0x0075 (117)
    (uint32)&EmuKeInsertQueueApc,                   // 0x0076 (118)
    (uint32)&EmuKeInsertQueueDpc,                   // 0x0077 (119)
    (uint32)&g_EmuKeInterruptTime,                  // 0x0078 (120)
    (uint32)&EmuKeIsExecutingDpc,                   // 0x0079 (121)
    (uint32)&EmuKeLeaveCriticalRegion,              // 0x007A (122)
    (uint32)&EmuKePulseEvent,                       // 0x007B (123)
    (uint32)&EmuKeQueryBasePriorityThread,          // 0x007C (124)
    (uint32)&EmuKeQueryInterruptTime,               // 0x007D (125)
    (uint32)&EmuKeQueryPerformanceCounter,          // 0x007E (126)
    (uint32)&EmuKeQueryPerformanceFrequency,        // 0x007F (127)
    (uint32)&xboxkrnl::KeQuerySystemTime,           // 0x0080 (128)
    (uint32)&EmuKeRaiseIrqlToDpcLevel,              // 0x0081 (129)
    (uint32)&EmuKeRaiseIrqlToSynchLevel,            // 0x0082 (130)
    (uint32)&EmuKeReleaseMutant,                    // 0x0083 (131)
    (uint32)&EmuKeReleaseSemaphore,                 // 0x0084 (132)
    (uint32)&EmuKeRemoveByKeyDeviceQueue,           // 0x0085 (133)
    (uint32)&EmuKeRemoveDeviceQueue,                // 0x0086 (134)
    (uint32)&EmuKeRemoveEntryDeviceQueue,           // 0x0087 (135)
    (uint32)&EmuKeRemoveQueue,                      // 0x0088 (136)
    (uint32)&EmuKeRemoveQueueDpc,                   // 0x0089 (137)
    (uint32)&EmuKeResetEvent,                       // 0x008A (138)
    (uint32)&EmuKeRestoreFloatingPointState,        // 0x008B (139)
    (uint32)&EmuKeResumeThread,                     // 0x008C (140)
    (uint32)&EmuKeRundownQueue,                     // 0x008D (141)
    (uint32)&EmuKeSaveFloatingPointState,           // 0x008E (142)
    (uint32)&EmuKeSetBasePriorityThread,            // 0x008F (143)
    (uint32)&EmuKeSetDisableBoostThread,            // 0x0090 (144)
    (uint32)&EmuKeSetEvent,                         // 0x0091 (145)
    (uint32)&EmuKeSetEventBoostPriority,            // 0x0092 (146)
    (uint32)&EmuKeSetPriorityProcess,               // 0x0093 (147)
    (uint32)&EmuKeSetPriorityThread,                // 0x0094 (148)
    (uint32)&xboxkrnl::KeSetTimer,                  // 0x0095 (149)
    (uint32)&EmuKeSetTimerEx,                       // 0x0096 (150)
    (uint32)&EmuKeStallExecutionProcessor,          // 0x0097 (151)
    (uint32)&EmuKeSuspendThread,                    // 0x0098 (152)
    (uint32)&EmuKeSynchronizeExecution,             // 0x0099 (153)
    (uint32)&g_EmuKeSystemTime,                     // 0x009A (154)
    (uint32)&EmuKeTestAlertThread,                  // 0x009B (155)
    (uint32)&xboxkrnl::KeTickCount,                 // 0x009C (156)
    (uint32)&g_EmuKeTimeIncrement,                  // 0x009D (157)
    (uint32)&EmuKeWaitForMultipleObjects,           // 0x009E (158)
    (uint32)&EmuKeWaitForSingleObject,              // 0x009F (159)
    (uint32)&EmuKfRaiseIrql,                        // 0x00A0 (160)
    (uint32)&EmuKfLowerIrql,                        // 0x00A1 (161)
    (uint32)&g_EmuKiBugCheckData,                   // 0x00A2 (162)
    (uint32)&EmuKiUnlockDispatcherDatabase,         // 0x00A3 (163)
    (uint32)&xboxkrnl::LaunchDataPage,              // 0x00A4 (164)
    (uint32)&xboxkrnl::MmAllocateContiguousMemory,  // 0x00A5 (165)
    (uint32)&xboxkrnl::MmAllocateContiguousMemoryEx,// 0x00A6 (166)
    (uint32)&xboxkrnl::MmAllocateSystemMemory,      // 0x00A7 (167)
    (uint32)&EmuMmClaimGpuInstanceMemory,           // 0x00A8 (168)
    (uint32)&EmuMmCreateKernelStack,                // 0x00A9 (169)
    (uint32)&EmuMmDeleteKernelStack,                // 0x00AA (170)
    (uint32)&xboxkrnl::MmFreeContiguousMemory,      // 0x00AB (171)
    (uint32)&xboxkrnl::MmFreeSystemMemory,          // 0x00AC (172)
    (uint32)&EmuMmGetPhysicalAddress,               // 0x00AD (173)
    (uint32)&EmuMmIsAddressValid,                   // 0x00AE (174)
    (uint32)&EmuMmLockUnlockBufferPages,            // 0x00AF (175)
    (uint32)&EmuMmLockUnlockPhysicalPage,           // 0x00B0 (176)
    (uint32)&EmuMmMapIoSpace,                       // 0x00B1 (177)
    (uint32)&xboxkrnl::MmPersistContiguousMemory,   // 0x00B2 (178)
    (uint32)&EmuMmQueryAddressProtect,              // 0x00B3 (179)
    (uint32)&EmuMmQueryAllocationSize,              // 0x00B4 (180)
    (uint32)&EmuMmQueryStatistics,                  // 0x00B5 (181)
    (uint32)&xboxkrnl::MmSetAddressProtect,         // 0x00B6 (182)
    (uint32)&EmuMmUnmapIoSpace,                     // 0x00B7 (183)
    (uint32)&xboxkrnl::NtAllocateVirtualMemory,     // 0x00B8 (184)
    (uint32)&EmuNtCancelTimer,                      // 0x00B9 (185)
    (uint32)&xboxkrnl::NtClearEvent,                // 0x00BA (186)
    (uint32)&xboxkrnl::NtClose,                     // 0x00BB (187)
    (uint32)&EmuNtCreateDirectoryObject,            // 0x00BC (188)
    (uint32)&xboxkrnl::NtCreateEvent,               // 0x00BD (189)
    (uint32)&xboxkrnl::NtCreateFile,                // 0x00BE (190)
    (uint32)&EmuNtCreateIoCompletion,               // 0x00BF (191)
    (uint32)&xboxkrnl::NtCreateMutant,              // 0x00C0 (192)
    (uint32)&EmuNtCreateSemaphore,                  // 0x00C1 (193)
    (uint32)&EmuNtCreateTimer,                      // 0x00C2 (194)
    (uint32)&EmuNtDeleteFile,                       // 0x00C3 (195)
    (uint32)&EmuNtDeviceIoControlFile,              // 0x00C4 (196)
    (uint32)&xboxkrnl::NtDuplicateObject,           // 0x00C5 (197)
    (uint32)&EmuNtFlushBuffersFile,                 // 0x00C6 (198)
    (uint32)&xboxkrnl::NtFreeVirtualMemory,         // 0x00C7 (199)
    (uint32)&EmuNtFsControlFile,                    // 0x00C8 (200)
    (uint32)&EmuNtOpenDirectoryObject,              // 0x00C9 (201)
    (uint32)&xboxkrnl::NtOpenFile,                  // 0x00CA (202)
    (uint32)&EmuNtOpenSymbolicLinkObject,           // 0x00CB (203)
    (uint32)&EmuNtProtectVirtualMemory,             // 0x00CC (204)
    (uint32)&EmuNtPulseEvent,                       // 0x00CD (205)
    (uint32)&EmuNtQueueApcThread,                   // 0x00CE (206)
    (uint32)&xboxkrnl::NtQueryDirectoryFile,        // 0x00CF (207)
    (uint32)&EmuNtQueryDirectoryObject,             // 0x00D0 (208)
    (uint32)&EmuNtQueryEvent,                       // 0x00D1 (209)
    (uint32)&xboxkrnl::NtQueryFullAttributesFile,   // 0x00D2 (210)
    (uint32)&xboxkrnl::NtQueryInformationFile,      // 0x00D3 (211)
    (uint32)&EmuNtQueryIoCompletion,                // 0x00D4 (212)
    (uint32)&EmuNtQueryMutant,                      // 0x00D5 (213)
    (uint32)&EmuNtQuerySemaphore,                   // 0x00D6 (214)
    (uint32)&EmuNtQuerySymbolicLinkObject,          // 0x00D7 (215)
    (uint32)&EmuNtQueryTimer,                       // 0x00D8 (216)
    (uint32)&EmuNtQueryVirtualMemory,               // 0x00D9 (217)
    (uint32)&xboxkrnl::NtQueryVolumeInformationFile,// 0x00DA (218)
    (uint32)&xboxkrnl::NtReadFile,                  // 0x00DB (219)
    (uint32)&EmuNtReadFileScatter,                  // 0x00DC (220)
    (uint32)&EmuNtReleaseMutant,                    // 0x00DD (221)
    (uint32)&EmuNtReleaseSemaphore,                 // 0x00DE (222)
    (uint32)&EmuNtRemoveIoCompletion,               // 0x00DF (223)
    (uint32)&xboxkrnl::NtResumeThread,              // 0x00E0 (224)
    (uint32)&xboxkrnl::NtSetEvent,                  // 0x00E1 (225)
    (uint32)&xboxkrnl::NtSetInformationFile,        // 0x00E2 (226)
    (uint32)&EmuNtSetIoCompletion,                  // 0x00E3 (227)
    (uint32)&EmuNtSetSystemTime,                    // 0x00E4 (228)
    (uint32)&EmuNtSetTimerEx,                       // 0x00E5 (229)
    (uint32)&EmuNtSignalAndWaitForSingleObjectEx,   // 0x00E6 (230)
    (uint32)&EmuNtSuspendThread,                    // 0x00E7 (231)
    (uint32)&xboxkrnl::NtUserIoApcDispatcher,       // 0x00E8 (232)
    (uint32)&xboxkrnl::NtWaitForSingleObject,       // 0x00E9 (233)
    (uint32)&xboxkrnl::NtWaitForSingleObjectEx,     // 0x00EA (234)
    (uint32)&EmuNtWaitForMultipleObjectsEx,         // 0x00EB (235)
    (uint32)&xboxkrnl::NtWriteFile,                 // 0x00EC (236)
    (uint32)&EmuNtWriteFileGather,                  // 0x00ED (237)
    (uint32)&xboxkrnl::NtYieldExecution,            // 0x00EE (238)
    (uint32)&EmuObCreateObject,                     // 0x00EF (239)
    (uint32)&g_EmuObDirectoryObjectType,            // 0x00F0 (240)
    (uint32)&EmuObInsertObject,                     // 0x00F1 (241)
    (uint32)&EmuObMakeTemporaryObject,              // 0x00F2 (242)
    (uint32)&EmuObOpenObjectByName,                 // 0x00F3 (243)
    (uint32)&EmuObOpenObjectByPointer,              // 0x00F4 (244)
    (uint32)&g_EmuObpObjectHandleTable,             // 0x00F5 (245)
    (uint32)&EmuObReferenceObjectByHandle,          // 0x00F6 (246)
    (uint32)&EmuObReferenceObjectByName,            // 0x00F7 (247)
    (uint32)&EmuObReferenceObjectByPointer,         // 0x00F8 (248)
    (uint32)&g_EmuObSymbolicLinkObjectType,         // 0x00F9 (249)
    (uint32)&EmuObfDereferenceObject,               // 0x00FA (250)
    (uint32)&EmuObfReferenceObject,                 // 0x00FB (251)
    (uint32)&EmuPhyGetLinkState,                    // 0x00FC (252)
    (uint32)&EmuPhyInitialize,                      // 0x00FD (253)
    (uint32)&EmuPsCreateSystemThread,               // 0x00FE (254)
    (uint32)&xboxkrnl::PsCreateSystemThreadEx,      // 0x00FF (255)
    (uint32)&EmuPsQueryStatistics,                  // 0x0100 (256)
    (uint32)&EmuPsSetCreateThreadNotifyRoutine,     // 0x0101 (257)
    (uint32)&xboxkrnl::PsTerminateSystemThread,     // 0x0102 (258)
    (uint32)&g_EmuPsThreadObjectType,               // 0x0103 (259)
    (uint32)&xboxkrnl::RtlAnsiStringToUnicodeString,// 0x0104 (260)
    (uint32)&EmuRtlAppendStringToString,            // 0x0105 (261)
    (uint32)&EmuRtlAppendUnicodeStringToString,     // 0x0106 (262)
    (uint32)&EmuRtlAppendUnicodeToString,           // 0x0107 (263)
    (uint32)&EmuRtlAssert,                          // 0x0108 (264)
    (uint32)&EmuRtlCaptureContext,                  // 0x0109 (265)
    (uint32)&EmuRtlCaptureStackBackTrace,           // 0x010A (266)
    (uint32)&EmuRtlCharToInteger,                   // 0x010B (267)
    (uint32)&EmuRtlCompareMemory,                   // 0x010C (268)
    (uint32)&EmuRtlCompareMemoryUlong,              // 0x010D (269)
    (uint32)&EmuRtlCompareString,                   // 0x010E (270)
    (uint32)&EmuRtlCompareUnicodeString,            // 0x010F (271)
    (uint32)&EmuRtlCopyString,                      // 0x0110 (272)
    (uint32)&EmuRtlCopyUnicodeString,               // 0x0111 (273)
    (uint32)&EmuRtlCreateUnicodeString,             // 0x0112 (274)
    (uint32)&EmuRtlDowncaseUnicodeChar,             // 0x0113 (275)
    (uint32)&EmuRtlDowncaseUnicodeString,           // 0x0114 (276)
    (uint32)&EmuRtlEnterCriticalSection,            // 0x0115 (277)
    (uint32)&EmuRtlEnterCriticalSectionAndRegion,   // 0x0116 (278)
    (uint32)&EmuRtlEqualString,                     // 0x0117 (279)
    (uint32)&EmuRtlEqualUnicodeString,              // 0x0118 (280)
    (uint32)&EmuRtlExtendedIntegerMultiply,         // 0x0119 (281)
    (uint32)&EmuRtlExtendedLargeIntegerDivide,      // 0x011A (282)
    (uint32)&EmuRtlExtendedMagicDivide,             // 0x011B (283)
    (uint32)&EmuRtlFillMemory,                      // 0x011C (284)
    (uint32)&EmuRtlFillMemoryUlong,                 // 0x011D (285)
    (uint32)&EmuRtlFreeAnsiString,                  // 0x011E (286)
    (uint32)&EmuRtlFreeUnicodeString,               // 0x011F (287)
    (uint32)&EmuRtlGetCallersAddress,               // 0x0120 (288)
    (uint32)&EmuRtlInitAnsiString,                  // 0x0121 (289)
    (uint32)&EmuRtlInitUnicodeString,               // 0x0122 (290)
    (uint32)&EmuRtlInitializeCriticalSection,       // 0x0123 (291)
    (uint32)&EmuRtlIntegerToChar,                   // 0x0124 (292)
    (uint32)&EmuRtlIntegerToUnicodeString,          // 0x0125 (293)
    (uint32)&EmuRtlLeaveCriticalSection,            // 0x0126 (294)
    (uint32)&EmuRtlLeaveCriticalSectionAndRegion,   // 0x0127 (295)
    (uint32)&EmuRtlLowerChar,                       // 0x0128 (296)
    (uint32)&EmuRtlMapGenericMask,                  // 0x0129 (297)
    (uint32)&EmuRtlMoveMemory,                      // 0x012A (298)
    (uint32)&EmuRtlMultiByteToUnicodeN,             // 0x012B (299)
    (uint32)&EmuRtlMultiByteToUnicodeSize,          // 0x012C (300)
    (uint32)&xboxkrnl::RtlNtStatusToDosError,       // 0x012D (301)
    (uint32)&EmuRtlRaiseException,                  // 0x012E (302)
    (uint32)&EmuRtlRaiseStatus,                     // 0x012F (303)
    (uint32)&EmuRtlTimeFieldsToTime,                // 0x0130 (304)
    (uint32)&EmuRtlTimeToTimeFields,                // 0x0131 (305)
    (uint32)&EmuRtlTryEnterCriticalSection,         // 0x0132 (306)
    (uint32)&EmuRtlUlongByteSwap,                   // 0x0133 (307)
    (uint32)&xboxkrnl::RtlUnicodeStringToAnsiString,// 0x0134 (308)
    (uint32)&EmuRtlUnicodeStringToInteger,          // 0x0135 (309)
    (uint32)&EmuRtlUnicodeToMultiByteN,             // 0x0136 (310)
    (uint32)&EmuRtlUnicodeToMultiByteSize,          // 0x0137 (311)
    (uint32)&EmuRtlUnwind,                          // 0x0138 (312)
    (uint32)&EmuRtlUpcaseUnicodeChar,               // 0x0139 (313)
    (uint32)&EmuRtlUpcaseUnicodeString,             // 0x013A (314)
    (uint32)&EmuRtlUpcaseUnicodeToMultiByteN,       // 0x013B (315)
    (uint32)&EmuRtlUpperChar,                       // 0x013C (316)
    (uint32)&EmuRtlUpperString,                     // 0x013D (317)
    (uint32)&EmuRtlUshortByteSwap,                  // 0x013E (318)
    (uint32)&EmuRtlWalkFrameChain,                  // 0x013F (319)
    (uint32)&EmuRtlZeroMemory,                      // 0x0140 (320)
    (uint32)&g_EmuXboxEEPROMKey,                    // 0x0141 (321)
    (uint32)&xboxkrnl::XboxHardwareInfo,            // 0x0142 (322)
    (uint32)&g_EmuXboxHDKey,                        // 0x0143 (323)
    (uint32)&xboxkrnl::EmuXboxKrnlVersion,          // 0x0144 (324)
    (uint32)&xboxkrnl::XboxSignatureKey,            // 0x0145 (325)
    (uint32)&xboxkrnl::EmuXeImageFileName,          // 0x0146 (326)
    (uint32)&EmuXeLoadSection,                      // 0x0147 (327)
    (uint32)&EmuXeUnloadSection,                    // 0x0148 (328)
    (uint32)&EmuREAD_PORT_BUFFER_UCHAR,             // 0x0149 (329)
    (uint32)&EmuREAD_PORT_BUFFER_USHORT,            // 0x014A (330)
    (uint32)&EmuREAD_PORT_BUFFER_ULONG,             // 0x014B (331)
    (uint32)&EmuWRITE_PORT_BUFFER_UCHAR,            // 0x014C (332)
    (uint32)&EmuWRITE_PORT_BUFFER_USHORT,           // 0x014D (333)
    (uint32)&EmuWRITE_PORT_BUFFER_ULONG,            // 0x014E (334)
    (uint32)&EmuXcSHAInit,                          // 0x014F (335)
    (uint32)&EmuXcSHAUpdate,                        // 0x0150 (336)
    (uint32)&EmuXcSHAFinal,                         // 0x0151 (337)
    (uint32)&EmuXcRC4Key,                           // 0x0152 (338)
    (uint32)&EmuXcRC4Crypt,                         // 0x0153 (339)
    (uint32)&EmuXcHMAC,                             // 0x0154 (340)
    (uint32)&EmuXcPKEncPublic,                      // 0x0155 (341)
    (uint32)&EmuXcPKDecPrivate,                     // 0x0156 (342)
    (uint32)&EmuXcPKGetKeyLen,                      // 0x0157 (343)
    (uint32)&EmuXcVerifyPKCS1Signature,             // 0x0158 (344)
    (uint32)&EmuXcModExp,                           // 0x0159 (345)
    (uint32)&EmuXcDESKeyParity,                     // 0x015A (346)
    (uint32)&EmuXcKeyTable,                         // 0x015B (347)
    (uint32)&EmuXcBlockCrypt,                       // 0x015C (348)
    (uint32)&EmuXcBlockCryptCBC,                    // 0x015D (349)
    (uint32)&EmuXcCryptService,                     // 0x015E (350)
    (uint32)&EmuXcUpdateCrypto,                     // 0x015F (351)
    (uint32)&EmuRtlRip,                             // 0x0160 (352)
    (uint32)&g_EmuXboxLANKey,                       // 0x0161 (353)
    (uint32)&g_EmuXboxAlternateSignatureKeys,       // 0x0162 (354)
    (uint32)&g_EmuXePublicKeyData,                  // 0x0163 (355)
    (uint32)&xboxkrnl::HalBootSMCVideoMode,         // 0x0164 (356)
    (uint32)&g_EmuIdexChannelObject,                // 0x0165 (357)
    (uint32)&EmuHalIsResetOrShutdownPending,        // 0x0166 (358)
    (uint32)&EmuIoMarkIrpMustComplete,              // 0x0167 (359)
    (uint32)&EmuHalInitiateShutdown,                // 0x0168 (360)
    (uint32)&EmuRtlSnprintf,                        // 0x0169 (361)
    (uint32)&EmuRtlSprintf,                         // 0x016A (362)
    (uint32)&EmuRtlVsnprintf,                       // 0x016B (363)
    (uint32)&EmuRtlVsprintf,                        // 0x016C (364)
    (uint32)&EmuHalEnableSecureTrayEject,           // 0x016D (365)
    (uint32)&EmuHalWriteSMCScratchRegister,         // 0x016E (366)
};
