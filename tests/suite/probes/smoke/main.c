// SPDX-License-Identifier: MIT
//
// smoke - the minimal probe. Proves the harness + trace channels work
// end-to-end on a target: it should produce D:\smoke.trace on the host, mirror
// every line via DbgPrint into the emulator log, and exit with code 0.

#include "xtrace.h"

int main(void)
{
    xt_begin("v1", "smoke");
    xt_note("harness self-test: file(D:) + DbgPrint channels");

    xt_check_u32("const.answer", 42, 42);
    xt_check_bool("logic.true", 1, 1);
    xt_check_str("string.eq", "xbox", "xbox");

    xt_ev("marker value=0xdeadbeef");

    return xt_end();
}
