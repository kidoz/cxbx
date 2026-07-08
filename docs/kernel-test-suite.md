# Kernel Test And Conformance

## Conformance Runner

Use the maintained conformance runner when possible:

```powershell
cd tools/xtest
python xtest.py list
python xtest.py run --emulator cxbx
python xtest.py run --emulator cxbx --probe kernel_cov
python xtest.py run --emulator cxbx --probe kernel_trap
```

See [tests/suite/README.md](../tests/suite/README.md) and
[tools/xtest/README.md](../tools/xtest/README.md) for the current probe suite.

## Kernel Test Asset

The prebuilt kernel test XBE can still be run manually after building the
emulator:

```powershell
meson setup build-min --cross-file cross/i686-windows-clang.ini
meson compile -C build-min
build-min\src\cxbx\cxbx.exe --run tests\xbes\kernel_test_suite.xbe --log build-min\kernel-test-suite.log
```

In batch mode, the runner opens the XBE, converts it to:

```text
%TEMP%\kernel_test_suite.exe
```

It also copies the runtime DLL beside the generated executable:

```text
%TEMP%\Cxbx.dll
```

## Last Recorded Status

- The XBE parsed successfully.
- The generated EXE was written successfully.
- The generated EXE loaded `Cxbx.dll` and reached `EmuInit`.
- Runtime detected the test as an OpenXDK application.
- The next blocker was process exit code `0xE06D7363` after OpenXDK startup.
