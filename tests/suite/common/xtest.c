// SPDX-License-Identifier: MIT

#include "xtest.h"
#include <stdio.h>
#include <string.h>

int xt_check_flags(const char *name, uint32_t mask, uint32_t expect, uint32_t got)
{
    static const struct { uint32_t bit; const char *n; } tbl[] = {
        { XT_CF, "CF" }, { XT_PF, "PF" }, { XT_AF, "AF" },
        { XT_ZF, "ZF" }, { XT_SF, "SF" }, { XT_OF, "OF" },
    };
    char full[96];
    int all_ok = 1;
    unsigned i;

    for (i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        if (mask & tbl[i].bit) {
            int e = (expect & tbl[i].bit) ? 1 : 0;
            int g = (got & tbl[i].bit) ? 1 : 0;
            snprintf(full, sizeof(full), "%s.%s", name, tbl[i].n);
            if (!xt_check_bool(full, e, g)) {
                all_ok = 0;
            }
        }
    }
    return all_ok;
}
