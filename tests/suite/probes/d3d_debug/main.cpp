// d3d_debug -- deterministic checks for the Xbox SDK debug Direct3D library.
//
// Unlike the retail D3D probes, this image links d3d8d.lib and defines _DEBUG.
// That makes the SDK's debug marker entry points real functions and enables
// D3D__SingleStepPusher, which inserts a BlockUntilIdle after D3D API calls.
#include "xdk_xtrace.h"

// The SDK sample helper carries the same packing workaround around this file.
#pragma pack(push, 8)
#include <d3d8perf.h>
#pragma pack(pop)

static const DWORD MARKER_A = 0xD3D0BEEF;
static const DWORD MARKER_B = 0xD3D01234;

void __cdecl main()
{
    xt_begin("d3d_debug");
    xt_emit("EV   library=d3d8d mode=debug");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    int create_hle = xt_is_hle_patched((const void*)Direct3D_CreateDevice);
    int clear_hle = xt_is_hle_patched((const void*)D3DDevice_Clear);
    int swap_hle = xt_is_hle_patched((const void*)D3DDevice_Swap);
    xt_chk("d3d.create_device_hle", 1, create_hle);
    xt_chk("d3d.clear_hle", 1, clear_hle);
    xt_chk("d3d.swap_hle", 1, swap_hle);
    if(!create_hle || !clear_hle || !swap_hle)
    {
        xt_emit("NOTE d3d8d core entry points are not HLE-patched");
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
    xt_chk("debug.perf_stats_ok", 1, perf != NULL);
    if(perf == NULL)
        xt_end_and_exit();

    // Debug markers are state, not merely annotations: Set returns the old
    // value and Swap resets the current marker to zero.
    xt_chk_u32("debug.marker_initial", 0, D3DDevice_GetDebugMarker());
    xt_chk_u32("debug.marker_set_old", 0,
               D3DDevice_SetDebugMarker(MARKER_A));
    xt_chk_u32("debug.marker_get_a", MARKER_A,
               D3DDevice_GetDebugMarker());
    xt_chk_u32("debug.marker_replace_old", MARKER_A,
               D3DDevice_SetDebugMarker(MARKER_B));
    xt_chk_u32("debug.marker_get_b", MARKER_B,
               D3DDevice_GetDebugMarker());

    // Single-step mode is intended to localize a GPU hang to one API call.
    // The instrumented counter makes its implicit BlockUntilIdle observable.
    D3D__SingleStepPusher = FALSE;
    D3DPERF_Reset();
    DWORD idle_before = perf->m_APICounters[API_D3DDEVICE_BLOCKUNTILIDLE];
    DWORD clear_before = perf->m_APICounters[API_D3DDEVICE_CLEAR];
    D3D__SingleStepPusher = TRUE;
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, 0xFF203040, 1.0f, 0);
    DWORD idle_after = perf->m_APICounters[API_D3DDEVICE_BLOCKUNTILIDLE];
    DWORD clear_after = perf->m_APICounters[API_D3DDEVICE_CLEAR];
    xt_chk("debug.single_step_enabled", 1, D3D__SingleStepPusher == TRUE);
    xt_chk("debug.clear_counted", 1, clear_after == clear_before + 1);
    xt_chk("debug.single_step_blocked", 1, idle_after > idle_before);
    xt_emitf("EV   single_step clear_delta=%u idle_delta=%u",
             clear_after - clear_before, idle_after - idle_before);
    D3D__SingleStepPusher = FALSE;

    xt_chk_u32("debug.swap_hr", 0, D3DDevice_Swap(D3DSWAP_DEFAULT));
    xt_chk_u32("debug.marker_reset_on_swap", 0,
               D3DDevice_GetDebugMarker());

    xt_end_and_exit();
}
