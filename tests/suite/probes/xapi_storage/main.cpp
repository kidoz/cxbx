// XAPI 5849 timing and utility-drive regression coverage.

#include "xdk_xtrace.h"

void __cdecl main()
{
    xt_begin("xapi_storage");

    const int counter_patched =
        xt_is_hle_patched((const void*)QueryPerformanceCounter);
    const int frequency_patched =
        xt_is_hle_patched((const void*)QueryPerformanceFrequency);
    const int mount_patched =
        xt_is_hle_patched((const void*)XMountUtilityDrive);
    xt_chk("xapi.counter_hle", 1, counter_patched);
    xt_chk("xapi.frequency_hle", 1, frequency_patched);
    xt_chk("xapi.mount_utility_hle", 1, mount_patched);
    if(!counter_patched || !frequency_patched || !mount_patched)
    {
        xt_emit("NOTE XAPI 1.0.5849 timing/storage entry points are not HLE-patched");
        xt_end_and_exit();
    }

    LARGE_INTEGER frequency;
    LARGE_INTEGER before;
    LARGE_INTEGER after;
    ZeroMemory(&frequency, sizeof(frequency));
    ZeroMemory(&before, sizeof(before));
    ZeroMemory(&after, sizeof(after));
    xt_chk("xapi.frequency_ok", 1,
           QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0);
    QueryPerformanceCounter(&before);
    QueryPerformanceCounter(&after);
    xt_chk("xapi.counter_monotonic", 1, after.QuadPart >= before.QuadPart);
    xt_chk("xapi.mount_utility_ok", 1, XMountUtilityDrive(FALSE));

    const char* path = "Z:\\xapi_probe.tmp";
    HANDLE file = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    xt_chk("storage.utility_create", 1, file != INVALID_HANDLE_VALUE);
    if(file != INVALID_HANDLE_VALUE)
    {
        const DWORD expected = 0x58495841;
        DWORD actual = 0;
        DWORD transferred = 0;
        const BOOL wrote = WriteFile(file, &expected, sizeof(expected),
                                     &transferred, NULL);
        xt_chk("storage.utility_write", 1,
               wrote && transferred == sizeof(expected));
        SetFilePointer(file, 0, NULL, FILE_BEGIN);
        transferred = 0;
        const BOOL read = ReadFile(file, &actual, sizeof(actual),
                                   &transferred, NULL);
        xt_chk("storage.utility_readback", 1,
               read && transferred == sizeof(actual) && actual == expected);
        CloseHandle(file);
        xt_chk("storage.utility_cleanup", 1, DeleteFile(path));
    }

    xt_end_and_exit();
}
