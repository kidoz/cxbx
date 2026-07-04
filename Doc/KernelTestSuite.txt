Kernel test suite run
=====================

Build:

    meson setup build-min --cross-file cross/i686-windows-clang.ini
    meson compile -C build-min

Run:

    build-min\src\cxbx\cxbx.exe --run tests\xbes\kernel_test_suite.xbe --log build-min\kernel-test-suite.log

The batch runner opens the XBE, converts it to:

    %TEMP%\kernel_test_suite.exe

It also copies the runtime DLL beside the generated executable:

    %TEMP%\Cxbx.dll

Current status:

- The XBE parses successfully.
- The generated EXE is written successfully.
- The generated EXE loads Cxbx.dll and reaches EmuInit.
- Runtime detects the test as an OpenXDK application.
- The next blocker is process exit code 0xE06D7363 after OpenXDK startup.
