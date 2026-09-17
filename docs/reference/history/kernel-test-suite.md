# Legacy kernel-test record

[Historical records](README.md)

This undated record describes a previous manual run of a prebuilt OpenXDK
kernel-test XBE. It does not establish the status of the current conformance
suite. For current procedures, use [Run kernel conformance probes](../../how-to/run-kernel-tests.md).

## Conversion behavior recorded

The batch runner converted the input to `%TEMP%\kernel_test_suite.exe` and
copied `Cxbx.dll` beside it.

## Last recorded status

- The XBE parsed successfully.
- The generated EXE was written successfully.
- The generated EXE loaded `Cxbx.dll` and reached `EmuInit`.
- Runtime detected the test as an OpenXDK application.
- The next blocker was process exit code `0xE06D7363` after OpenXDK startup.
