// xdk_smoke -- XDK-toolchain pipeline spike.
//
// Unlike every other probe (built with nxdk/clang), this one is compiled with
// the real Xbox XDK 5849 toolchain (xbox\bin\vc71 CL/Link + imagebld) and
// links the genuine XDK static libraries (xapilib/xboxkrnl). Its image
// therefore contains real XDK library code -- the prerequisite for probing the
// OOVPA/HLE layer (EmuD3D8/EmuDSound/EmuXapi) instead of raw hardware.
//
// Scope is deliberately minimal: boot through the XAPI runtime, write the
// standard trace to D:\xdk_smoke.trace (mirrored via DbgPrint "XT| "), do one
// file write-readback through the XAPI file layer, and exit cleanly.
#include <xtl.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

extern "C" ULONG __cdecl DbgPrint(const char *Format, ...);
extern "C" VOID __stdcall HalReturnToFirmware(ULONG Routine);

static HANDLE g_trace = INVALID_HANDLE_VALUE;
static int g_checks = 0;
static int g_fails = 0;

static void emit(const char *line)
{
    if (g_trace != INVALID_HANDLE_VALUE) {
        DWORD cb;
        WriteFile(g_trace, line, (DWORD)strlen(line), &cb, NULL);
        WriteFile(g_trace, "\n", 1, &cb, NULL);
    }
    DbgPrint("XT| %s\n", line);
}

static void emitf(const char *fmt, ...)
{
    char line[480];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    line[sizeof(line) - 1] = 0;
    va_end(ap);
    emit(line);
}

static void chk_bool(const char *name, int expect, int got)
{
    g_checks++;
    if (expect != got)
        g_fails++;
    emitf("CHK  %s expect=%d got=%d %s", name, expect, got,
          (expect == got) ? "PASS" : "FAIL");
}

void __cdecl main()
{
    g_trace = CreateFile("D:\\xdk_smoke.trace", GENERIC_WRITE, 0, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    emit("#suite xbox-conformance v1");
    emit("#probe xdk_smoke");
    emitf("EV   boot toolchain=xdk5849 trace=%s",
          (g_trace != INVALID_HANDLE_VALUE) ? "file" : "dbgprint-only");

    chk_bool("xdk.trace_open", 1, g_trace != INVALID_HANDLE_VALUE);

    // XAPI liveness: the tick counter must run.
    DWORD tick = GetTickCount();
    chk_bool("xdk.tick_nonzero", 1, tick != 0);

    // File write-readback through the XAPI CreateFile/ReadFile layer.
    static const char payload[] = "xdk-smoke-payload";
    char readback[sizeof(payload)] = {0};
    DWORD cb = 0;
    BOOL wrote = FALSE, read = FALSE;
    HANDLE h = CreateFile("D:\\xdk_smoke.dat", GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        wrote = WriteFile(h, payload, sizeof(payload), &cb, NULL) &&
                cb == sizeof(payload);
        CloseHandle(h);
    }
    h = CreateFile("D:\\xdk_smoke.dat", GENERIC_READ, 0, NULL,
                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        read = ReadFile(h, readback, sizeof(readback), &cb, NULL) &&
               cb == sizeof(payload);
        CloseHandle(h);
    }
    chk_bool("xdk.file_write", 1, wrote);
    chk_bool("xdk.file_readback", 1, read && memcmp(payload, readback,
                                                    sizeof(payload)) == 0);

    emitf("#result xdk_smoke verdict=%s checks=%d fail=%d",
          g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    emit("#end");

    if (g_trace != INVALID_HANDLE_VALUE)
        CloseHandle(g_trace);

    // Returning from main() would hand control to the XAPI runtime's
    // dashboard relaunch, which parks under emulation. Exit explicitly:
    // the emulator treats QuickReboot (2) as clean title exit.
    HalReturnToFirmware(2);
}
