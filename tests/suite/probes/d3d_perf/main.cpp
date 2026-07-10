// d3d_perf -- deterministic checks for the Xbox SDK instrumented Direct3D
// library (d3d8i.lib): API counters, reset behavior, event nesting, and
// push-buffer accounting.
#include "xdk_xtrace.h"

#pragma pack(push, 8)
#include <d3d8perf.h>
#pragma pack(pop)

void __cdecl main()
{
    xt_begin("d3d_perf");
    xt_emit("EV   library=d3d8i mode=profile");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    int create_hle = xt_is_hle_patched((const void*)Direct3D_CreateDevice);
    int clear_hle = xt_is_hle_patched((const void*)D3DDevice_Clear);
    xt_chk("d3d.create_device_hle", 1, create_hle);
    xt_chk("d3d.clear_hle", 1, clear_hle);
    if(!create_hle || !clear_hle)
    {
        xt_emit("NOTE d3d8i core entry points are not HLE-patched");
        xt_end_and_exit();
    }

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice* pDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &pp, &pDevice);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && pDevice != NULL);
    if(FAILED(hr) || pDevice == NULL)
        xt_end_and_exit();

    D3DPERF* perf = D3DPERF_GetStatistics();
    D3DPERFNAMES* names = D3DPERF_GetNames();
    xt_chk("perf.stats_ok", 1, perf != NULL);
    xt_chk("perf.names_ok", 1, names != NULL);
    if(perf == NULL)
        xt_end_and_exit();

    D3DPERF_Reset();
    xt_chk_u32("perf.clear_reset", 0,
               perf->m_APICounters[API_D3DDEVICE_CLEAR]);

    D3DPUSHBUFFERINFO before;
    D3DPUSHBUFFERINFO after;
    ZeroMemory(&before, sizeof(before));
    ZeroMemory(&after, sizeof(after));
    D3DPERF_GetPushBufferInfo(&before);

    // PIX-style annotations are also part of d3d8i. The nesting calls must
    // balance within a frame; exact timing and event-buffer contents are not
    // deterministic enough for a cross-emulator check.
    INT level = D3DPERF_BeginEvent(0xFF4080C0, "xtest.d3d_perf");
    xt_chk("perf.begin_event_ok", 1, level >= 0);
    D3DPERF_SetMarker(0xFFC08040, "before_clear");
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0);
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, 0xFF304050, 1.0f, 0);
    INT end_level = D3DPERF_EndEvent();
    xt_chk("perf.end_event_ok", 1, end_level >= 0);

    D3DPERF_GetPushBufferInfo(&after);
    DWORD clear_count = perf->m_APICounters[API_D3DDEVICE_CLEAR];
    xt_chk_u32("perf.clear_count", 2, clear_count);
    xt_chk("perf.push_size_nonzero", 1, after.PushBufferSize != 0);
    xt_chk("perf.push_range_valid", 1,
           after.pPushBase != NULL && after.pPushLimit > after.pPushBase);
    xt_chk("perf.push_bytes_grew", 1,
           after.PushBufferBytesWritten > before.PushBufferBytesWritten);
    xt_emitf("EV   counters clear=%u push_size=%u push_bytes_before=%I64u push_bytes_after=%I64u",
             clear_count, after.PushBufferSize,
             before.PushBufferBytesWritten, after.PushBufferBytesWritten);

    D3DPERF_Reset();
    xt_chk_u32("perf.clear_reset_again", 0,
               perf->m_APICounters[API_D3DDEVICE_CLEAR]);

    xt_end_and_exit();
}
