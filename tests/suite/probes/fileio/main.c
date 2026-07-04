// SPDX-License-Identifier: MIT
//
// fileio - FATX / file-system round-trips on the title's D: drive: create,
// write, reopen, size, sequential read, seek+read, delete, absence check.
// This exercises the same NtCreateFile/NtWriteFile path the trace harness uses.

#include "xtrace.h"
#include <windows.h>
#include <string.h>

int main(void)
{
    const char *path = "D:\\xtest_io.tmp";
    const char *msg  = "xbox-conformance-fileio-0123456789";
    DWORD len = (DWORD)strlen(msg);

    xt_begin("v1", "fileio");

    // --- create + write ------------------------------------------------------
    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    xt_check_bool("io.create_write", 1, h != INVALID_HANDLE_VALUE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wrote = 0;
        BOOL wok = WriteFile(h, msg, len, &wrote, NULL);
        xt_check_bool("io.write_ok", 1, wok);
        xt_check_u32("io.write_count", len, wrote);
        CloseHandle(h);
    }

    // --- reopen, size, read --------------------------------------------------
    HANDLE r = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    xt_check_bool("io.open_read", 1, r != INVALID_HANDLE_VALUE);
    if (r != INVALID_HANDLE_VALUE) {
        DWORD sz = SetFilePointer(r, 0, NULL, FILE_END);
        xt_check_u32("io.size", len, sz);
        SetFilePointer(r, 0, NULL, FILE_BEGIN);

        char buf[64];
        memset(buf, 0, sizeof(buf));
        DWORD got = 0;
        BOOL rok = ReadFile(r, buf, len, &got, NULL);
        xt_check_bool("io.read_ok", 1, rok);
        xt_check_u32("io.read_count", len, got);
        xt_check_str("io.content", msg, buf);

        // Seek to offset 5 and read a single byte.
        SetFilePointer(r, 5, NULL, FILE_BEGIN);
        char c = 0;
        DWORD g2 = 0;
        ReadFile(r, &c, 1, &g2, NULL);
        xt_check_u32("io.seek_char", (uint32_t)(unsigned char)msg[5],
                     (uint32_t)(unsigned char)c);
        CloseHandle(r);
    }

    // --- delete + confirm gone ----------------------------------------------
    BOOL dok = DeleteFile(path);
    xt_check_bool("io.delete", 1, dok);

    HANDLE g = CreateFile(path, GENERIC_READ, 0, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    xt_check_bool("io.deleted_absent", 1, g == INVALID_HANDLE_VALUE);
    if (g != INVALID_HANDLE_VALUE) CloseHandle(g);

    return xt_end();
}
