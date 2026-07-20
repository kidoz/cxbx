// XACT 5849 engine creation, reference counting, work, and teardown.

#include "xdk_xtrace.h"
#include <xact.h>

void __cdecl main()
{
    xt_begin("xact_engine");

    const int create_patched = xt_is_hle_patched((const void*)XACTEngineCreate);
    const int addref_patched =
        xt_is_hle_patched((const void*)IXACTEngine_AddRef);
    const int release_patched =
        xt_is_hle_patched((const void*)IXACTEngine_Release);
    const int work_patched =
        xt_is_hle_patched((const void*)XACTEngineDoWork);
    xt_chk("xact.engine_create_hle", 1, create_patched);
    xt_chk("xact.engine_addref_hle", 1, addref_patched);
    xt_chk("xact.engine_release_hle", 1, release_patched);
    xt_chk("xact.engine_work_hle", 1, work_patched);
    if(!create_patched || !addref_patched || !release_patched || !work_patched)
    {
        xt_emit("NOTE XACTENG 1.0.5849 engine lifecycle is not fully HLE-patched");
        xt_end_and_exit();
    }

    XACT_RUNTIME_PARAMETERS params;
    ZeroMemory(&params, sizeof(params));
    params.dwMax2DHwVoices = 64;
    params.dwMax3DHwVoices = 32;
    params.dwMaxConcurrentStreams = 4;

    IXACTEngine* engine = NULL;
    HRESULT result = XACTEngineCreate(&params, &engine);
    xt_chk("xact.engine_create_ok", 1, SUCCEEDED(result) && engine != NULL);
    if(FAILED(result) || engine == NULL)
    {
        xt_end_and_exit();
    }

    xt_chk_u32("xact.engine_addref_count", 2, IXACTEngine_AddRef(engine));
    XACTEngineDoWork();
    xt_chk("xact.engine_work_survived", 1, 1);
    xt_chk_u32("xact.engine_release_count", 1, IXACTEngine_Release(engine));
    xt_chk_u32("xact.engine_final_release", 0, IXACTEngine_Release(engine));

    engine = NULL;
    result = XACTEngineCreate(&params, &engine);
    xt_chk("xact.engine_recreate_ok", 1,
           SUCCEEDED(result) && engine != NULL);
    if(engine != NULL)
    {
        xt_chk_u32("xact.engine_recreate_release", 0,
                   IXACTEngine_Release(engine));
    }

    xt_end_and_exit();
}
