// xdk_xtrace.h -- minimal trace harness for XDK-toolchain probes (the
// counterpart of common/xtrace.h for nxdk probes). Same wire protocol:
// D:\<probe>.trace file + DbgPrint "XT| " mirror, CHK/#result grammar parsed
// by tools/xtest/xtest.py. Include from a probe built with the real XDK
// compiler (see probes/xdk_smoke/build.ps1 for the pipeline).
#ifndef XDK_XTRACE_H
#define XDK_XTRACE_H

#include <xtl.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern "C" ULONG __cdecl DbgPrint(const char* Format, ...);
extern "C" VOID __stdcall HalReturnToFirmware(ULONG Routine);

static HANDLE xt_trace = INVALID_HANDLE_VALUE;
static const char* xt_probe = "";
static int xt_checks = 0;
static int xt_fails = 0;

// CXBX HLE probes use this to distinguish an unsupported XDK library variant
// from a semantic failure inside a host wrapper. Retail/debug/instrumented D3D
// libraries contain different code, so each variant must be patched explicitly.
#define XT_GUEST_TOP 0x04000000UL
#define XT_XREF_SENTINEL 0x7FFFFFFFUL

static DWORD xt_jump_target(const unsigned char* p)
{
    return (DWORD)(p + 5) + *(const DWORD*)(p + 1);
}

static int xt_is_hle_patched(const void* fn)
{
    const unsigned char* p = (const unsigned char*)fn;
    if((DWORD)p >= XT_GUEST_TOP || (DWORD)p == XT_XREF_SENTINEL)
        return 0;
    if(p[0] != 0xE9)
        return 0;
    DWORD target = xt_jump_target(p);
    if(target == XT_XREF_SENTINEL)
        return 0;
    if(target >= XT_GUEST_TOP)
        return 1;
    // Some library exports first jump through a guest-local linker thunk.
    p = (const unsigned char*)target;
    if(p[0] != 0xE9)
        return 0;
    target = xt_jump_target(p);
    return target != XT_XREF_SENTINEL && target >= XT_GUEST_TOP;
}

static void xt_emit(const char* line)
{
    if(xt_trace != INVALID_HANDLE_VALUE)
    {
        DWORD cb;
        WriteFile(xt_trace, line, (DWORD)strlen(line), &cb, NULL);
        WriteFile(xt_trace, "\n", 1, &cb, NULL);
    }
    DbgPrint("XT| %s\n", line);
}

static void xt_emitf(const char* fmt, ...)
{
    char line[480];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    line[sizeof(line) - 1] = 0;
    va_end(ap);
    xt_emit(line);
}

// Probe name MUST match the probe directory name (runner contract).
static void xt_begin(const char* probe)
{
    char path[64];
    xt_probe = probe;
    _snprintf(path, sizeof(path) - 1, "D:\\%s.trace", probe);
    path[sizeof(path) - 1] = 0;
    xt_trace = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    xt_emit("#suite xbox-conformance v1");
    xt_emitf("#probe %s", probe);
}

static void xt_chk(const char* name, int expect, int got)
{
    xt_checks++;
    if(expect != got)
        xt_fails++;
    xt_emitf("CHK  %s expect=%d got=%d %s", name, expect, got,
             (expect == got) ? "PASS" : "FAIL");
}

static void xt_chk_u32(const char* name, DWORD expect, DWORD got)
{
    xt_checks++;
    if(expect != got)
        xt_fails++;
    xt_emitf("CHK  %s expect=0x%08X got=0x%08X %s", name, expect, got,
             (expect == got) ? "PASS" : "FAIL");
}

// Writes #result/#end, closes the trace, and exits the title. QuickReboot(2)
// is the clean-exit path under emulation; returning from main() instead
// parks forever in the XAPI dashboard-relaunch path.
static void xt_end_and_exit(void)
{
    xt_emitf("#result %s verdict=%s checks=%d fail=%d",
             xt_probe, xt_fails ? "FAIL" : "PASS", xt_checks, xt_fails);
    xt_emit("#end");
    if(xt_trace != INVALID_HANDLE_VALUE)
        CloseHandle(xt_trace);
    HalReturnToFirmware(2);
}

#endif // XDK_XTRACE_H
