// d3d_pushbuffer -- Xbox SDK push-buffer recording and GPU/CPU fence checks.
//
// PIX correlates its API timing stream with a separately captured push-buffer
// stream. This probe exercises the deterministic core of that mechanism:
// record commands, observe the recording offset, replay them, insert a fence,
// wait for it, and verify the resulting pixels.
#include "xdk_xtrace.h"

static const D3DCOLOR CLEAR_COLOR = 0xFF2874B8;

static DWORD read_pixel(void* bits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)bits + y * pitch + x * 4)) & 0x00FFFFFF;
}

void __cdecl main()
{
    xt_begin("d3d_pushbuffer");
    xt_emit("EV   library=d3d8 mode=retail feature=pushbuffer+fence");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    // Fail before entering native Xbox GPU code when an HLE target has not yet
    // added a wrapper/signature. Each check names one implementation boundary.
    int create_device_hle = xt_is_hle_patched((const void*)Direct3D_CreateDevice);
    int create_pb_hle = xt_is_hle_patched((const void*)D3DDevice_CreatePushBuffer2);
    int begin_pb_hle = xt_is_hle_patched((const void*)D3DDevice_BeginPushBuffer);
    int end_pb_hle = xt_is_hle_patched((const void*)D3DDevice_EndPushBuffer);
    int run_pb_hle = xt_is_hle_patched((const void*)D3DDevice_RunPushBuffer);
    int offset_hle = xt_is_hle_patched((const void*)D3DDevice_GetPushBufferOffset);
    int insert_fence_hle = xt_is_hle_patched((const void*)D3DDevice_InsertFence);
    int pending_hle = xt_is_hle_patched((const void*)D3DDevice_IsFencePending);
    int block_fence_hle = xt_is_hle_patched((const void*)D3DDevice_BlockOnFence);
    int kick_hle = xt_is_hle_patched((const void*)D3DDevice_KickPushBuffer);
    int idle_hle = xt_is_hle_patched((const void*)D3DDevice_BlockUntilIdle);
    int field_hle = xt_is_hle_patched((const void*)D3DDevice_GetDisplayFieldStatus);

    xt_chk("d3d.create_device_hle", 1, create_device_hle);
    xt_chk("push.create_hle", 1, create_pb_hle);
    xt_chk("push.begin_hle", 1, begin_pb_hle);
    xt_chk("push.end_hle", 1, end_pb_hle);
    xt_chk("push.run_hle", 1, run_pb_hle);
    xt_chk("push.offset_hle", 1, offset_hle);
    xt_chk("fence.insert_hle", 1, insert_fence_hle);
    xt_chk("fence.pending_hle", 1, pending_hle);
    xt_chk("fence.block_hle", 1, block_fence_hle);
    xt_chk("push.kick_hle", 1, kick_hle);
    xt_chk("push.idle_hle", 1, idle_hle);
    xt_chk("display.field_hle", 1, field_hle);

    if(!create_device_hle || !create_pb_hle || !begin_pb_hle || !end_pb_hle || !run_pb_hle || !offset_hle || !insert_fence_hle || !pending_hle || !block_fence_hle || !kick_hle || !idle_hle || !field_hle)
    {
        xt_emit("NOTE retail d3d8 push-buffer/fence entry points are not fully HLE-patched");
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

    D3DPushBuffer* pb = D3DDevice_CreatePushBuffer2(64 * 1024, TRUE);
    xt_chk("push.object_ok", 1, pb != NULL);
    if(pb == NULL)
        xt_end_and_exit();

    DWORD offset_before = 0;
    DWORD offset_after = 0;
    D3DDevice_BeginPushBuffer(pb);
    D3DDevice_GetPushBufferOffset(&offset_before);
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, CLEAR_COLOR, 1.0f, 0);
    D3DDevice_GetPushBufferOffset(&offset_after);
    hr = D3DDevice_EndPushBuffer();
    xt_chk_u32("push.end_hr", 0, (DWORD)hr);
    xt_chk("push.offset_grew", 1, offset_after > offset_before);
    xt_emitf("EV   recorded offset_before=%u offset_after=%u bytes=%u",
             offset_before, offset_after, offset_after - offset_before);

    D3DDevice_RunPushBuffer(pb, NULL);
    D3DDevice_KickPushBuffer();
    DWORD fence = D3DDevice_InsertFence();
    xt_chk("fence.handle_nonzero", 1, fence != 0);
    BOOL pending_before = D3DDevice_IsFencePending(fence);
    D3DDevice_BlockOnFence(fence);
    BOOL pending_after = D3DDevice_IsFencePending(fence);
    xt_chk("fence.completed_after_block", 0, pending_after);
    xt_emitf("EV   fence handle=%u pending_before=%d pending_after=%d",
             fence, pending_before, pending_after);
    D3DDevice_BlockUntilIdle();

    D3DFIELD_STATUS field;
    ZeroMemory(&field, sizeof(field));
    D3DDevice_GetDisplayFieldStatus(&field);
    xt_chk("display.field_valid", 1,
           field.Field == D3DFIELD_ODD || field.Field == D3DFIELD_EVEN || field.Field == D3DFIELD_PROGRESSIVE);
    xt_emitf("EV   display field=%u vblank=%u", field.Field, field.VBlankCount);

    D3DPushBuffer_Release(pb);

    // Read back only after all GPU work. The HLE LockRect path keeps the host
    // surface locked, so no drawing or synchronization may follow this point.
    D3DSurface* backbuffer = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, backbuffer != NULL);
    if(backbuffer != NULL)
    {
        D3DLOCKED_RECT lr;
        ZeroMemory(&lr, sizeof(lr));
        D3DSurface_LockRect(backbuffer, &lr, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
        if(lr.pBits != NULL)
        {
            DWORD expected = CLEAR_COLOR & 0x00FFFFFF;
            xt_chk_u32("d3d.pixel_center", expected,
                       read_pixel(lr.pBits, lr.Pitch, 320, 240));
        }
    }

    xt_end_and_exit();
}
