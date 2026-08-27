#!/usr/bin/env python3
"""External thread sampler for a running Cxbx emulator process.

Suspends each thread of the target process every --interval ms, records EIP,
and resumes. Prints a per-thread EIP histogram (module + RVA) suitable for
symbolization with llvm-symbolizer against the Cxbx.dll PDB.

Usage: python tools/sampler.py <pid> [--seconds 25] [--interval-ms 10]
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import collections
import time

TH32CS_SNAP_THREAD = 0x4
TH32CS_SNAP_MODULE = 0x8
CONTEXT_i386 = 0x10000
CONTEXT_CONTROL = CONTEXT_i386 | 0x1  # CONTEXT_CONTROL on x86


class THREADENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wt.DWORD),
        ("cntUsage", wt.DWORD),
        ("th32ThreadID", wt.DWORD),
        ("th32OwnerProcessID", wt.DWORD),
        ("tpBasePri", wt.LONG),
        ("tpDeltaPri", wt.LONG),
        ("dwFlags", wt.DWORD),
    ]


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wt.DWORD),
        ("th32ModuleID", wt.DWORD),
        ("th32ProcessID", wt.DWORD),
        ("GlblcntUsage", wt.DWORD),
        ("ProccntUsage", wt.DWORD),
        ("modBaseAddr", ctypes.POINTER(wt.BYTE)),
        ("modBaseSize", wt.DWORD),
        ("szModule", ctypes.c_char * 256),
        ("szExePath", ctypes.c_char * 260),
    ]


class MODULEINFO(ctypes.Structure):
    _fields_ = [
        ("lpBaseOfDll", ctypes.c_void_p),
        ("SizeOfImage", wt.DWORD),
        ("EntryPoint", ctypes.c_void_p),
    ]


class CONTEXT86(ctypes.Structure):
    _fields_ = [
        ("ContextFlags", wt.DWORD),
        ("Dr0", wt.DWORD), ("Dr1", wt.DWORD), ("Dr2", wt.DWORD),
        ("Dr3", wt.DWORD), ("Dr6", wt.DWORD), ("Dr7", wt.DWORD),
        ("FloatSave", ctypes.c_byte * 112),
        ("SegGs", wt.DWORD), ("SegFs", wt.DWORD), ("SegEs", wt.DWORD),
        ("SegDs", wt.DWORD),
        ("Edi", wt.DWORD), ("Esi", wt.DWORD), ("Ebx", wt.DWORD),
        ("Edx", wt.DWORD), ("Ecx", wt.DWORD), ("Eax", wt.DWORD),
        ("Ebp", wt.DWORD), ("Eip", wt.DWORD), ("SegCs", wt.DWORD),
        ("EFlags", wt.DWORD), ("Esp", wt.DWORD), ("SegSs", wt.DWORD),
        ("ExtendedRegisters", ctypes.c_byte * 512),
    ]


k32 = ctypes.WinDLL("kernel32", use_last_error=True)

# A 64-bit sampler controlling a 32-bit (WOW64) target must use the Wow64
# variants; plain SuspendThread/GetThreadContext report the 64-bit halves.
HAVE_WOW64 = hasattr(k32, "Wow64GetThreadContext")


def list_threads(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAP_THREAD, 0)
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(entry)
    threads = []
    ok = k32.Thread32First(snap, ctypes.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            threads.append(entry.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(entry))
    k32.CloseHandle(snap)
    return threads


def list_modules(process, pid):
    # Toolhelp module snapshots fail cross-bitness (x64 tool -> WOW64 target);
    # psapi's EnumProcessModulesEx with LIST_MODULES_32BIT handles it.
    psapi = ctypes.WinDLL("psapi")
    LIST_MODULES_32BIT = 0x01
    needed = wt.DWORD(0)
    if not psapi.EnumProcessModulesEx(process, None, 0, ctypes.byref(needed),
                                      LIST_MODULES_32BIT):
        return []
    count = needed.value // ctypes.sizeof(wt.HMODULE)
    if count == 0:
        return []
    array = (wt.HMODULE * count)()
    if not psapi.EnumProcessModulesEx(process, ctypes.byref(array),
                                      ctypes.sizeof(array),
                                      ctypes.byref(needed),
                                      LIST_MODULES_32BIT):
        return []
    modules = []
    for index in range(count):
        name = ctypes.create_unicode_buffer(260)
        info = MODULEINFO()
        psapi.GetModuleInformation(process, array[index],
                                   ctypes.byref(info), ctypes.sizeof(info))
        psapi.GetModuleBaseNameW(process, array[index], name, 260)
        modules.append((name.value, ctypes.cast(info.lpBaseOfDll,
                                                ctypes.c_void_p).value,
                        info.SizeOfImage))
    return modules


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pid", type=int)
    ap.add_argument("--seconds", type=float, default=25.0)
    ap.add_argument("--interval-ms", type=float, default=10.0)
    args = ap.parse_args()

    process = k32.OpenProcess(0x041F, False, args.pid)  # query + suspend/resume + vm-read
    if not process:
        raise SystemExit(f"OpenProcess({args.pid}) failed: {ctypes.get_last_error()}")

    modules = list_modules(process, args.pid)
    print(f"# modules: {[(n, hex(b), s) for n, b, s in modules]}")

    # per-thread counters of (module, rva); waiting-in-ntdll samples tracked
    # separately so idle threads do not drown the hot ones.
    per_thread = collections.defaultdict(collections.Counter)
    deadline = time.time() + args.seconds
    samples = 0
    ctx = CONTEXT86()
    ctx.ContextFlags = CONTEXT_CONTROL
    while time.time() < deadline:
        for tid in list_threads(args.pid):
            handle = k32.OpenThread(0x0002 | 0x0008 | 0x0010, False, tid)  # suspend|resume|getcontext
            if not handle:
                continue
            if k32.Wow64SuspendThread(handle) == 0xFFFFFFFF:
                k32.CloseHandle(handle)
                continue
            ctx.ContextFlags = CONTEXT_CONTROL
            got = (k32.Wow64GetThreadContext(handle, ctypes.byref(ctx))
                   if HAVE_WOW64 else
                   k32.GetThreadContext(handle, ctypes.byref(ctx)))
            k32.ResumeThread(handle)
            k32.CloseHandle(handle)
            if not got:
                continue
            eip = ctx.Eip
            owner = "unknown"
            for name, base, size in modules:
                if base is not None and base <= eip < base + size:
                    owner = name
                    eip = eip - base
                    break
            per_thread[tid][(owner, eip)] += 1
            samples += 1
        time.sleep(args.interval_ms / 1000.0)

    print(f"# samples={samples} over {args.seconds}s")
    ranked = sorted(per_thread.items(),
                    key=lambda kv: -sum(v for (m, e), v in kv[1].items()
                                        if m not in ("ntdll.dll", "KERNEL32.DLL",
                                                     "KERNELBASE.dll")))
    for tid, counter in ranked:
        busy = sum(v for (m, e), v in counter.items()
                   if m not in ("ntdll.dll", "KERNEL32.DLL", "KERNELBASE.dll"))
        total = sum(counter.values())
        if busy < 10:
            continue
        print(f"\n# tid {tid}: {total} samples, {busy} outside waits")
        for (owner, eip), count in counter.most_common(20):
            tag = "" if owner not in ("ntdll.dll", "KERNEL32.DLL",
                                      "KERNELBASE.dll") else "  (wait)"
            print(f"  {owner}+0x{eip:06X}  {count:5d}  {count/total*100:5.1f}%{tag}")


if __name__ == "__main__":
    main()
