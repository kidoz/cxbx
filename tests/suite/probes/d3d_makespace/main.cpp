// d3d_makespace -- validate the writable push-buffer cursor return ABI.
#include "xdk_xtrace.h"

#if defined(CXBX_MAKESPACE_CPP)
namespace D3D
{
volatile DWORD* WINAPI MakeSpace();
}
#else
extern DWORD* WINAPI D3DDevice_MakeSpace();
#endif

static DWORD* make_space()
{
#if defined(CXBX_MAKESPACE_CPP)
    return const_cast<DWORD*>(D3D::MakeSpace());
#else
    return D3DDevice_MakeSpace();
#endif
}

static const void* make_space_entry()
{
#if defined(CXBX_MAKESPACE_CPP)
    return reinterpret_cast<const void*>(D3D::MakeSpace);
#else
    return reinterpret_cast<const void*>(D3DDevice_MakeSpace);
#endif
}

void __cdecl main()
{
    xt_begin("d3d_makespace");
    xt_emit("EV   library=d3d8 feature=make-space-return-abi");

    const int makeSpaceHle = xt_is_hle_patched(make_space_entry());
    xt_chk("makespace.hle", 1, makeSpaceHle);
    if(!makeSpaceHle)
    {
        xt_end_and_exit();
    }

    LPDIRECT3D8 d3d = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, d3d != NULL);
    if(d3d == NULL)
    {
        xt_end_and_exit();
    }

    D3DPRESENT_PARAMETERS parameters;
    ZeroMemory(&parameters, sizeof(parameters));
    parameters.BackBufferWidth = 640;
    parameters.BackBufferHeight = 480;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice* device = NULL;
    const HRESULT result = d3d->CreateDevice(
        0, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &parameters, &device);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(result) && device != NULL);
    if(FAILED(result) || device == NULL)
    {
        xt_end_and_exit();
    }

    DWORD* const pushBuffer = make_space();
    xt_chk("makespace.pointer_nonnull", 1, pushBuffer != NULL);

    int writable = 0;
    if(pushBuffer != NULL)
    {
        __try
        {
            DWORD saved[8];
            for(int index = 0; index < 8; ++index)
            {
                saved[index] = pushBuffer[index];
                pushBuffer[index] = 0xC0DE0000u + static_cast<DWORD>(index);
            }
            writable = 1;
            for(int index = 0; index < 8; ++index)
            {
                writable = writable &&
                           pushBuffer[index] == 0xC0DE0000u + static_cast<DWORD>(index);
                pushBuffer[index] = saved[index];
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            writable = 0;
        }
    }
    xt_chk("makespace.packet_writable", 1, writable);
    xt_emitf("EV   packet_dwords=8 writable=%d", writable);

    xt_end_and_exit();
}
