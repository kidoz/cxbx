// First XACT 5849 integration boundary: engine creation and release.

#include "xdk_xtrace.h"
#include <xact.h>

void __cdecl main()
{
    xt_begin("xact_engine");

    const int create_patched =
        xt_is_hle_patched((const void*)XACTEngineCreate);
    xt_chk("xact.engine_create_hle", 1, create_patched);
    if(!create_patched)
    {
        xt_emit("NOTE XACTENG 1.0.5849 engine lifecycle is the next HLE slice");
        xt_end_and_exit();
    }

    xt_emit("NOTE add create/release semantics when the lifecycle pair lands");
    xt_end_and_exit();
}
