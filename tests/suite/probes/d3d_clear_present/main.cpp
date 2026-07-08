// d3d_clear_present -- end-to-end D3D8 HLE pipeline: device creation through
// the OOVPA-patched CreateDevice, Clear on the HOST GPU, Swap (host Present),
// and a pixel-exact readback of the backbuffer through the HLE
// GetBackBuffer2 + Surface LockRect path.
//
// Built with the real XDK 5849 toolchain, so every D3D call below runs the
// genuine d3d8.lib code until the HLE patch at the function's prologue
// redirects it to the host implementation -- exactly what a real title does.
//
// Readback discipline: the HLE LockRect leaves the host surface locked (Xbox
// has no UnlockRect in the HLE table), so ALL rendering happens before the
// single readback at the end -- nothing draws or presents on a locked
// backbuffer.
#include "xdk_xtrace.h"

static const D3DCOLOR CLEAR_A = 0xFF206080; // first clear (swapped away)
static const D3DCOLOR CLEAR_B = 0xFFC03050; // final clear (verified)

static DWORD read_pixel(void *pBits, INT pitch, int x, int y)
{
    return (*(DWORD *)((BYTE *)pBits + y * pitch + x * 4)) & 0x00FFFFFF;
}

void __cdecl main()
{
    xt_begin("d3d_clear_present");

    // Direct3DCreate8 is un-HLE'd guest code (returns the library's global
    // D3D object); CreateDevice is where the HLE takes over.
    LPDIRECT3D8 pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, pD3D != NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.BackBufferWidth  = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount  = 1;
    d3dpp.SwapEffect       = D3DSWAPEFFECT_DISCARD;

    D3DDevice *pDevice = NULL;
    HRESULT hr = pD3D->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                    &d3dpp, &pDevice);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && pDevice != NULL);
    if (FAILED(hr) || pDevice == NULL)
        xt_end_and_exit();

    // Clear + Swap: the host Present must accept the converted parameters.
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, CLEAR_A, 1.0f, 0);
    xt_chk_u32("d3d.swap_hr", 0, D3DDevice_Swap(D3DSWAP_DEFAULT));

    // Final frame: clear to a distinct color, then verify actual pixels.
    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, CLEAR_B, 1.0f, 0);

    D3DSurface *pBB = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, pBB != NULL);
    if (pBB != NULL) {
        D3DLOCKED_RECT lr;
        lr.pBits = NULL;
        D3DSurface_LockRect(pBB, &lr, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, lr.pBits != NULL);
        if (lr.pBits != NULL) {
            DWORD want = CLEAR_B & 0x00FFFFFF;
            xt_chk_u32("d3d.px_topleft", want, read_pixel(lr.pBits, lr.Pitch, 10, 10));
            xt_chk_u32("d3d.px_center", want, read_pixel(lr.pBits, lr.Pitch, 320, 240));
            xt_chk_u32("d3d.px_bottomright", want, read_pixel(lr.pBits, lr.Pitch, 630, 470));
        }
    }

    xt_end_and_exit();
}
