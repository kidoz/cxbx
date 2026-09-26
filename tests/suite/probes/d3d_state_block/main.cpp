// d3d_state_block -- pixel-exact verification of the state-block path at the
// HLE/host-backend boundary.
//
// Flavor A (recorded block) is the restore-semantics gate: a Begin/End block
// records COLORWRITEENABLE=all, the state is then broken to writes-disabled,
// and ApplyStateBlock must re-enable writes. The post-apply green draw covers
// only the LEFT half of the region, so:
//   left half green  -> the apply restored the recorded state;
//   right half red   -> the writes-disabled draw was a genuine no-op.
//
// Flavor B (captured block) verifies CaptureStateBlock + ApplyStateBlock is
// a non-corrupting roundtrip: capture the live state, draw with writes
// enabled. (Applying a captured block after breaking the state cannot be
// gated here: the DXVK d3d8 staging does not restore captured state, while
// the native Vulkan backend does -- that difference is covered by the
// host-vulkan-state_block renderer scenario and documented in the spec.)
//
// All rendering happens before the single readback at the end (the HLE
// LockRect leaves the host surface locked).
#include "xdk_xtrace.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF; // blue
static const D3DCOLOR COL_RED = 0xFFFF0000;
static const D3DCOLOR COL_GREEN = 0xFF00FF00;

struct VERTEX
{
    float x, y, z, rhw;
    D3DCOLOR color;
};
#define FVF_VERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static DWORD read_pixel(void* pBits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static void draw_quad(float x0, float y0, float x1, float y1, D3DCOLOR color)
{
    VERTEX quad[6] = {
        { x0, y0, 0.5f, 1.0f, color }, { x1, y0, 0.5f, 1.0f, color },
        { x0, y1, 0.5f, 1.0f, color }, { x1, y0, 0.5f, 1.0f, color },
        { x1, y1, 0.5f, 1.0f, color }, { x0, y1, 0.5f, 1.0f, color },
    };
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, quad, sizeof(VERTEX));
}

// Xbox COLORWRITEENABLE bit layout: R=bit16, G=bit8, B=bit0, A=bit24.
static void set_color_write(DWORD mask)
{
    D3DDevice_SetRenderState(D3DRS_COLORWRITEENABLE, mask);
}

void __cdecl main()
{
    xt_begin("d3d_state_block");

    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.BackBufferWidth = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount = 1;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice* pDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &d3dpp, &pDevice);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && pDevice != NULL);
    if(FAILED(hr) || pDevice == NULL)
        xt_end_and_exit();

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    D3DDevice_SetRenderState(D3DRS_ZENABLE, FALSE);
    D3DDevice_SetVertexShader(FVF_VERTEX);

    // --- Flavor A: recorded block (restore semantics) -----------------
    draw_quad(32.0f, 32.0f, 160.0f, 160.0f, COL_RED);

    D3DDevice_BeginStateBlock();
    set_color_write(0x01010101u); // the recorded delta
    DWORD tokenA = 0;
    hr = D3DDevice_EndStateBlock(&tokenA);
    xt_chk("d3d.sb_end_hr", 1, SUCCEEDED(hr));
    xt_chk("d3d.sb_token_valid", 1, SUCCEEDED(hr) && tokenA != 0);

    set_color_write(0x00000000u); // break the recorded state
    draw_quad(32.0f, 32.0f, 160.0f, 160.0f, COL_GREEN); // must be a no-op

    D3DDevice_ApplyStateBlock(tokenA);
    draw_quad(32.0f, 32.0f, 96.0f, 160.0f, COL_GREEN); // left half only

    // --- Flavor B: captured block (roundtrip) --------------------------
    set_color_write(0x01010101u);
    draw_quad(320.0f, 32.0f, 448.0f, 160.0f, COL_RED);

    D3DDevice_BeginStateBlock();
    DWORD tokenB = 0;
    hr = D3DDevice_EndStateBlock(&tokenB);
    xt_chk("d3d.sb2_end_hr", 1, SUCCEEDED(hr));
    xt_chk("d3d.sb2_token_valid", 1, SUCCEEDED(hr) && tokenB != 0);

    // Capture the LIVE state into the token, then draw normally.
    D3DDevice_CaptureStateBlock(tokenB);
    D3DDevice_ApplyStateBlock(tokenB);
    draw_quad(320.0f, 32.0f, 448.0f, 160.0f, COL_GREEN);

    // --- Single readback ----------------------------------------------
    D3DSurface* pBB = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, pBB != NULL);
    if(pBB != NULL)
    {
        D3DLOCKED_RECT lr;
        lr.pBits = NULL;
        D3DSurface_LockRect(pBB, &lr, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
        if(lr.pBits != NULL)
        {
            // Region A: the applied block re-enabled writes (left half
            // green) and the writes-disabled draw never painted (right
            // half still the red base).
            xt_chk_u32("d3d.sb_recorded_applied", COL_GREEN & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 64, 64));
            xt_chk_u32("d3d.sb_noop_kept_red", COL_RED & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 128, 64));
            // Region B: the capture/apply roundtrip preserved a drawable
            // device (the green draw paints).
            xt_chk_u32("d3d.sb2_roundtrip_draws", COL_GREEN & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 384, 64));
            // Untouched clear between the regions.
            xt_chk_u32("d3d.sb_clear", COL_CLEAR & 0xFFFFFF,
                       read_pixel(lr.pBits, lr.Pitch, 240, 240));
        }
    }

    xt_end_and_exit();
}
