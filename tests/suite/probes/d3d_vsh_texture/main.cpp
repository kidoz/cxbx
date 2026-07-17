// Verifies that CXBX's CPU vertex-shader bridge preserves all four components
// of an Xbox texture output and keeps a successfully translated pixel shader.
#include "xdk_xtrace.h"
#include "cpu_bridge.h"

static const D3DCOLOR COL_CLEAR = 0xFF0000FF;
static const D3DCOLOR COL_RED   = 0xFFFF0000;
static const D3DCOLOR COL_GREEN = 0xFF00FF00;
static const D3DCOLOR COL_BLUE  = 0xFF0000FF;
static const D3DCOLOR COL_WHITE = 0xFFFFFFFF;

struct VERTEX
{
    float position[4];
    D3DCOLOR color;
    float texCoord[4];
};

static DWORD read_pixel(void* bits, INT pitch, int x, int y)
{
    return (*(DWORD*)((BYTE*)bits + y * pitch + x * 4)) & 0x00FFFFFF;
}

static void fill_texture(D3DTexture* texture)
{
    D3DLOCKED_RECT locked;
    locked.pBits = NULL;
    D3DTexture_LockRect(texture, 0, &locked, NULL, 0);
    xt_chk("texture.lock_ok", 1, locked.pBits != NULL);
    if(locked.pBits == NULL)
        return;

    for(int y = 0; y < 8; ++y)
    {
        DWORD* row = (DWORD*)((BYTE*)locked.pBits + y * locked.Pitch);
        for(int x = 0; x < 8; ++x)
        {
            if(x < 4 && y < 4)
                row[x] = COL_RED;
            else if(x >= 4 && y < 4)
                row[x] = COL_GREEN;
            else if(x < 4)
                row[x] = COL_BLUE;
            else
                row[x] = COL_WHITE;
        }
    }
}

static void build_pixel_shader(D3DPIXELSHADERDEF* shader)
{
    memset(shader, 0, sizeof(*shader));
    shader->PSCombinerCount = PS_COMBINERCOUNT(1, 0);
    shader->PSTextureModes = PS_TEXTUREMODES_PROJECT2D;
    shader->PSRGBInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T0 | PS_INPUTMAPPING_UNSIGNED_IDENTITY | PS_CHANNEL_RGB,
        PS_REGISTER_V0 | PS_INPUTMAPPING_UNSIGNED_IDENTITY | PS_CHANNEL_RGB,
        PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    shader->PSRGBOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
    shader->PSAlphaInputs[0] = PS_COMBINERINPUTS(
        PS_REGISTER_T0 | PS_INPUTMAPPING_UNSIGNED_IDENTITY | PS_CHANNEL_ALPHA,
        PS_REGISTER_V0 | PS_INPUTMAPPING_UNSIGNED_IDENTITY | PS_CHANNEL_ALPHA,
        PS_REGISTER_ZERO, PS_REGISTER_ZERO);
    shader->PSAlphaOutputs[0] = PS_COMBINEROUTPUTS(
        PS_REGISTER_DISCARD, PS_REGISTER_DISCARD, PS_REGISTER_R0, 0);
}

void __cdecl main()
{
    xt_begin("d3d_vsh_texture");

    LPDIRECT3D8 d3d = Direct3DCreate8(D3D_SDK_VERSION);
    xt_chk("d3d.object_ok", 1, d3d != NULL);

    D3DPRESENT_PARAMETERS present;
    ZeroMemory(&present, sizeof(present));
    present.BackBufferWidth = 640;
    present.BackBufferHeight = 480;
    present.BackBufferFormat = D3DFMT_X8R8G8B8;
    present.BackBufferCount = 1;
    present.SwapEffect = D3DSWAPEFFECT_DISCARD;

    D3DDevice* device = NULL;
    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                   &present, &device);
    xt_chk("d3d.device_ok", 1, SUCCEEDED(hr) && device != NULL);
    if(FAILED(hr) || device == NULL)
        xt_end_and_exit();

    D3DTexture* texture = D3DDevice_CreateTexture2(
        8, 8, 1, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DRTYPE_TEXTURE);
    xt_chk("texture.create_ok", 1, texture != NULL);
    if(texture == NULL)
        xt_end_and_exit();
    fill_texture(texture);

    const DWORD declaration[] = {
        D3DVSD_STREAM(0),
        D3DVSD_REG(0, D3DVSDT_FLOAT4),
        D3DVSD_REG(1, D3DVSDT_D3DCOLOR),
        D3DVSD_REG(2, D3DVSDT_FLOAT4),
        D3DVSD_END()
    };
    DWORD vertexShader = 0;
    hr = D3DDevice_CreateVertexShader(declaration, g_CpuBridgeShader,
                                      &vertexShader, 0);
    xt_chk("vsh.create_ok", 1, SUCCEEDED(hr) && vertexShader != 0);

    D3DPIXELSHADERDEF pixelDefinition;
    build_pixel_shader(&pixelDefinition);
    DWORD pixelShader = 0;
    D3DDevice_CreatePixelShader(&pixelDefinition, &pixelShader);
    xt_chk("psh.create_ok", 1, pixelShader != 0);

    // After projection by q=3, every vertex has (u,v)=(0.25,0.25), selecting
    // the red quadrant. A two-component bridge instead samples (0.75,0.75),
    // selecting white and making the regression unambiguous.
    const VERTEX quad[6] = {
        {{-0.75f,  0.75f, 0.5f, 1.0f}, COL_WHITE, {0.75f, 0.75f, 0.0f, 3.0f}},
        {{-0.25f,  0.75f, 0.5f, 1.0f}, COL_WHITE, {0.75f, 0.75f, 0.0f, 3.0f}},
        {{-0.75f,  0.25f, 0.5f, 1.0f}, COL_WHITE, {0.75f, 0.75f, 0.0f, 3.0f}},
        {{-0.25f,  0.75f, 0.5f, 1.0f}, COL_WHITE, {0.75f, 0.75f, 0.0f, 3.0f}},
        {{-0.25f,  0.25f, 0.5f, 1.0f}, COL_WHITE, {0.75f, 0.75f, 0.0f, 3.0f}},
        {{-0.75f,  0.25f, 0.5f, 1.0f}, COL_WHITE, {0.75f, 0.75f, 0.0f, 3.0f}}
    };

    D3DDevice_Clear(0, NULL, D3DCLEAR_TARGET, COL_CLEAR, 1.0f, 0);
    D3DDevice_SetRenderState_CullMode(D3DCULL_NONE);
    D3DDevice_SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    D3DDevice_SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    D3DDevice_SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    D3DDevice_SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    D3DDevice_SetTexture(0, texture);
    D3DDevice_SetVertexShader(vertexShader);
    D3DDevice_SetPixelShader(pixelShader);
    D3DDevice_DrawVerticesUP(D3DPT_TRIANGLELIST, 6, quad, sizeof(VERTEX));
    D3DDevice_SetPixelShader(0);

    D3DSurface* backBuffer = D3DDevice_GetBackBuffer2(0);
    xt_chk("d3d.backbuffer_ok", 1, backBuffer != NULL);
    if(backBuffer != NULL)
    {
        D3DLOCKED_RECT locked;
        locked.pBits = NULL;
        D3DSurface_LockRect(backBuffer, &locked, NULL, D3DLOCK_READONLY);
        xt_chk("d3d.lock_ok", 1, locked.pBits != NULL);
        if(locked.pBits != NULL)
        {
            xt_chk_u32("vsh.projected_texture", COL_RED & 0x00FFFFFF,
                       read_pixel(locked.pBits, locked.Pitch, 160, 120));
            xt_chk_u32("vsh.clear", COL_CLEAR & 0x00FFFFFF,
                       read_pixel(locked.pBits, locked.Pitch, 480, 360));
        }
    }

    xt_end_and_exit();
}
