// SPDX-License-Identifier: MIT
//
// kernel_thread - kernel threading / dispatcher-object HLE conformance. v1
// self-checks the Ke*/Nt* dispatcher-object exports that CXBX patches via the
// kernel thunk table: KeGetCurrentThread, KeInitializeEvent, KeSetEvent,
// KeResetEvent, KeWaitForSingleObject, and KeDelayExecutionThread.
//
// This is a kernel-HLE probe (like kernel_cov), so it is untagged and runs
// everywhere -- no GPU, no XDK libraries. Ground truth is encoded as checks
// against dispatcher-object semantics that are observable through the kernel
// API:
//
//   * waits consume synchronization-event signals, retain notification-event
//     signals, and return STATUS_TIMEOUT for an elapsed deadline.
//   * KeSetEvent/KeResetEvent return the previous SignalState; KeSetEvent
//     ignores its Increment/Wait arguments for behavior.
//
// KNOWN GAP -- thread SPAWN (PsCreateSystemThread / NtSuspendThread /
// NtResumeThread) is deliberately NOT exercised here. On this Cxbx build
// spawning a guest thread does not yield a runnable thread: the new thread gets
// no Xbox FS selector (the LDT setup fails with STATUS_OBJECT_NAME_COLLISION),
// so it faults on entry and the host process hangs within the run timeout.
// PsCreateSystemThread itself returns STATUS_SUCCESS + a handle, but the thread
// cannot run. This is exactly the kind of subsystem gap the suite exists to
// surface; it is tracked here as a note rather than an executed check so the
// probe stays deterministic and terminates cleanly (a known hang would stall
// the whole suite). When the FS/LDT path for spawned threads is repaired, add a
// spawn + suspend/resume round-trip block and a golden for it.

#include "xtest.h"
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_TIMEOUT
#define STATUS_TIMEOUT ((NTSTATUS)0x00000102L)
#endif

int main(void)
{
    xt_begin("v1", "kernel_thread");
    xt_note("v1: Ke* dispatcher-object HLE self-checks");
    xt_note("thread SPAWN (PsCreateSystemThread) is a known gap on Cxbx: not exercised");

    // --- KeGetCurrentThread: returns a non-NULL PKTHREAD --------------------
    PKTHREAD cur = KeGetCurrentThread();
    xt_ev("KeGetCurrentThread = %p", (void*)cur);
    xt_check_bool("ke.getcurrent_nonzero", 1, cur != NULL);

    // --- Event lifecycle: KeInitializeEvent / KeSetEvent / KeResetEvent ------
    // On this HLE an event is just a DISPATCHER_HEADER: SignalState toggles
    // between 0 and 1; KeSetEvent/KeResetEvent return the previous state.
    KEVENT ev;
    KeInitializeEvent(&ev, SynchronizationEvent, FALSE);
    LONG ss_init = ev.Header.SignalState;
    xt_ev("init SignalState=%ld (SynchronizationEvent, FALSE)", (long)ss_init);
    xt_check_u32("ke.event_init_zero", 0u, (uint32_t)ss_init);

    LONG prev_set = KeSetEvent(&ev, 0, FALSE);
    LONG ss_after_set = ev.Header.SignalState;
    xt_ev("KeSetEvent prev=%ld SignalState=%ld", (long)prev_set, (long)ss_after_set);
    xt_check_u32("ke.setevent_prev_zero", 0u, (uint32_t)prev_set);
    xt_check_u32("ke.setevent_sets_one", 1u, (uint32_t)ss_after_set);

    LONG prev_reset = KeResetEvent(&ev);
    LONG ss_after_reset = ev.Header.SignalState;
    xt_ev("KeResetEvent prev=%ld SignalState=%ld", (long)prev_reset, (long)ss_after_reset);
    // KeResetEvent returns the PREVIOUS state, which was 1 (KeSetEvent signaled
    // it above), and then clears it to 0.
    xt_check_u32("ke.resetevent_prev_one", 1u, (uint32_t)prev_reset);
    xt_check_u32("ke.resetevent_sets_zero", 0u, (uint32_t)ss_after_reset);

    // --- KeWaitForSingleObject dispatcher semantics -------------------------
    KEVENT ev2;
    KeInitializeEvent(&ev2, SynchronizationEvent, TRUE);
    NTSTATUS wait_st = KeWaitForSingleObject(&ev2, Executive, KernelMode, FALSE, NULL);
    xt_ev("KeWaitForSingleObject = 0x%08lX", (unsigned long)wait_st);
    xt_check_u32("ke.wait_success", (uint32_t)STATUS_SUCCESS, (uint32_t)wait_st);
    xt_check_u32("ke.wait_sync_consumed", 0u, (uint32_t)ev2.Header.SignalState);

    KEVENT notification;
    KeInitializeEvent(&notification, NotificationEvent, TRUE);
    NTSTATUS notification_st =
        KeWaitForSingleObject(&notification, Executive, KernelMode, FALSE, NULL);
    xt_check_u32("ke.wait_notification_success", (uint32_t)STATUS_SUCCESS,
                 (uint32_t)notification_st);
    xt_check_u32("ke.wait_notification_retained", 1u,
                 (uint32_t)notification.Header.SignalState);

    KEVENT unsignaled;
    LARGE_INTEGER zero_timeout;
    KeInitializeEvent(&unsignaled, SynchronizationEvent, FALSE);
    zero_timeout.QuadPart = 0;
    NTSTATUS timeout_st =
        KeWaitForSingleObject(&unsignaled, Executive, KernelMode, FALSE, &zero_timeout);
    xt_check_u32("ke.wait_zero_timeout", (uint32_t)STATUS_TIMEOUT,
                 (uint32_t)timeout_st);

    // --- KeDelayExecutionThread: short relative wait -> STATUS_SUCCESS ------
    // A negative Interval is relative; -100000 (100ns units) is ~10 ms.
    LARGE_INTEGER delay;
    delay.QuadPart = -100000LL;
    NTSTATUS delay_st = KeDelayExecutionThread(KernelMode, FALSE, &delay);
    xt_ev("KeDelayExecutionThread = 0x%08lX", (unsigned long)delay_st);
    xt_check_u32("ke.delay_success", (uint32_t)STATUS_SUCCESS, (uint32_t)delay_st);

    return xt_end();
}
