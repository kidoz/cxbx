// SPDX-License-Identifier: GPL-2.0-or-later
//
// kernel_logging.h - standardized, parseable kernel-HLE trace helpers.
//
// KTRACE() unifies the ad-hoc "#ifdef _DEBUG_TRACE printf(...)" pattern used
// throughout kernel_emulation.cpp into a single line with a stable "KTRACE|" prefix, so
// host-side tooling (e.g. tools/xtest) can extract kernel traces from an
// emulator log the same way it extracts guest "XT|" trace lines.
//
//   KTRACE("IoCreateSymbolicLink", "link=%s dev=%s", link, dev);
//     -> KTRACE| IoCreateSymbolicLink link=\Device\... dev=\Device\...

#ifndef KERNEL_LOGGING_H
#define KERNEL_LOGGING_H

#include <cstdio>

#ifdef _DEBUG_TRACE
#define KTRACE(name, fmt, ...) \
    do { printf("KTRACE| %s " fmt "\n", (name), ##__VA_ARGS__); fflush(stdout); } while (0)
#else
#define KTRACE(name, fmt, ...) do { } while (0)
#endif

// Logs a call into an unimplemented kernel export. Defined in KernelThunk.cpp
// (where the per-ordinal trap stubs live). Always emits, so a developer sees
// exactly which export a title needs even in a non-_DEBUG build.
extern "C" void EmuUnimplementedKernelLog(int Ordinal, void *Caller);

#endif // KERNEL_LOGGING_H
