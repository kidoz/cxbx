// SPDX-License-Identifier: MIT
//
// xtrace implementation. See xtrace.h and tests/suite/README.md.

#include "xtrace.h"

#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>   // DbgPrint (kernel ordinal 8)
#include <hal/debug.h>           // debugPrint (optional screen channel)
#include <hal/video.h>           // XVideoSetMode (optional screen channel)
#include <stdio.h>
#include <string.h>

#define XT_LINEBUF 1024

static HANDLE s_file    = INVALID_HANDLE_VALUE;
static int    s_checks  = 0;
static int    s_fails   = 0;
static int    s_screen  = 0;
static char   s_probe[64];

// Write one finished line to every active channel.
static void xt_write_line(const char *line)
{
    if (s_file != INVALID_HANDLE_VALUE) {
        DWORD wrote;
        WriteFile(s_file, line, (DWORD)strlen(line), &wrote, NULL);
        WriteFile(s_file, "\r\n", 2, &wrote, NULL);
    }

    // Kernel debug channel. The "XT| " prefix lets the host runner extract
    // trace lines from an otherwise noisy emulator log.
    DbgPrint("XT| %s\n", line);

    if (s_screen) {
        debugPrint("%s\n", line);
    }
}

static void xt_emit(const char *fmt, ...)
{
    char buf[XT_LINEBUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[XT_LINEBUF - 1] = '\0';
    xt_write_line(buf);
}

void xt_begin(const char *suite_version, const char *probe_name)
{
    char path[80];

    s_checks = 0;
    s_fails  = 0;
    strncpy(s_probe, probe_name, sizeof(s_probe) - 1);
    s_probe[sizeof(s_probe) - 1] = '\0';

    // D: is auto-mounted to the XBE directory by nxdk's automount_d CRT init.
    snprintf(path, sizeof(path), "D:\\%s.trace", probe_name);
    s_file = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    // If the file channel is unavailable the DbgPrint channel still carries
    // the full trace, so we do not treat a failed open as fatal.

    xt_emit("#suite xbox-conformance %s", suite_version);
    xt_emit("#probe %s", s_probe);
}

void xt_ev(const char *fmt, ...)
{
    char body[XT_LINEBUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    body[XT_LINEBUF - 1] = '\0';
    xt_emit("EV %s", body);
}

void xt_note(const char *fmt, ...)
{
    char body[XT_LINEBUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    body[XT_LINEBUF - 1] = '\0';
    xt_emit("NOTE %s", body);
}

int xt_check(const char *name, int passed, const char *detail_fmt, ...)
{
    char detail[XT_LINEBUF];
    va_list ap;

    va_start(ap, detail_fmt);
    vsnprintf(detail, sizeof(detail), detail_fmt, ap);
    va_end(ap);
    detail[XT_LINEBUF - 1] = '\0';

    s_checks++;
    if (!passed) {
        s_fails++;
    }
    xt_emit("CHK %s %s %s", name, detail, passed ? "PASS" : "FAIL");
    return passed ? 1 : 0;
}

int xt_check_u32(const char *name, uint32_t expect, uint32_t got)
{
    return xt_check(name, expect == got,
                    "expect=0x%08lX got=0x%08lX",
                    (unsigned long)expect, (unsigned long)got);
}

int xt_check_u64(const char *name, uint64_t expect, uint64_t got)
{
    return xt_check(name, expect == got,
                    "expect=0x%016llX got=0x%016llX",
                    (unsigned long long)expect, (unsigned long long)got);
}

int xt_check_bool(const char *name, int expect, int got)
{
    return xt_check(name, (!!expect) == (!!got),
                    "expect=%d got=%d", !!expect, !!got);
}

int xt_check_str(const char *name, const char *expect, const char *got)
{
    if (expect == NULL) expect = "(null)";
    if (got == NULL)    got    = "(null)";
    return xt_check(name, strcmp(expect, got) == 0,
                    "expect='%s' got='%s'", expect, got);
}

int xt_end(void)
{
    xt_emit("#result %s verdict=%s checks=%d fail=%d",
            s_probe, s_fails == 0 ? "PASS" : "FAIL", s_checks, s_fails);
    xt_emit("#end");

    if (s_file != INVALID_HANDLE_VALUE) {
        // CloseHandle flushes the OS write buffer; the live DbgPrint mirror
        // already covers the case where a probe crashes before this point.
        CloseHandle(s_file);
        s_file = INVALID_HANDLE_VALUE;
    }
    return s_fails;
}

int xt_fail_count(void)  { return s_fails; }
int xt_check_count(void) { return s_checks; }

void xt_enable_screen(void)
{
    if (!s_screen) {
        XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
        s_screen = 1;
    }
}
