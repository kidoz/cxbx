// SPDX-License-Identifier: MIT
//
// xtrace - deterministic, self-checking trace harness for Xbox-Original
// conformance probes. See tests/suite/README.md for the trace protocol.
//
// Every probe follows the same shape:
//
//     #include "xtrace.h"
//     int main(void) {
//         xt_begin("v1", "cpu_flags");
//         ...
//         xt_check_u32("add8.res", 0x80, observed);
//         ...
//         return xt_end();          // exit code == number of failed checks
//     }
//
// Output goes to TWO machine-readable channels at once, so a trace is
// captured no matter how far along a target emulator is:
//   1. D:\<probe>.trace  - a clean file on the title's D: drive (mapped to
//      the host directory by every Xbox emulator). Primary channel.
//   2. DbgPrint (kernel ordinal 8) - each line mirrored with an "XT| " prefix,
//      captured in the emulator's debug log even before its file I/O works.
// An optional on-screen channel (xt_enable_screen) is off by default so probes
// stay portable to emulators without working video.

#ifndef XTRACE_H
#define XTRACE_H

#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

// Begin a probe: opens D:\<probe_name>.trace, resets counters, emits the
// "#suite"/"#probe" header lines. suite_version is a short tag, e.g. "v1".
void xt_begin(const char *suite_version, const char *probe_name);

// Emit an observed-data event line (not a pass/fail check):
//   EV <formatted text>
void xt_ev(const char *fmt, ...);

// Emit a check line and record the verdict:
//   CHK <name> <detail> PASS|FAIL
// Returns 1 on pass, 0 on fail.
int xt_check(const char *name, int passed, const char *detail_fmt, ...);
int xt_check_u32(const char *name, uint32_t expect, uint32_t got);
int xt_check_u64(const char *name, uint64_t expect, uint64_t got);
int xt_check_bool(const char *name, int expect, int got);
int xt_check_str(const char *name, const char *expect, const char *got);

// Emit a note line (informational, ignored by golden diff):
//   NOTE <formatted text>
void xt_note(const char *fmt, ...);

// Finish the probe: emits "#result"/"#end", flushes and closes the trace.
// Returns the number of failed checks (use as main()'s return value).
int xt_end(void);

int xt_fail_count(void);
int xt_check_count(void);

// Opt in to mirroring every trace line to the screen via nxdk debugPrint.
// Sets a video mode as a side effect. Off by default; use only when you want
// visible output on real hardware or a GPU-emulating target.
void xt_enable_screen(void);

#ifdef __cplusplus
}
#endif

#endif // XTRACE_H
