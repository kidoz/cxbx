// D3D8 debug-library signatures generated from the XDK 5849 d3d8d.lib.
// Keep this table separate from D3D8: the debug wrappers contain additional
// validation and profiling code, so retail OOVPAs do not match them.

// _Direct3D_CreateDevice@24 (d3d8d.lib, 384 bytes)
LOOVPA<8> Direct3D_CreateDevice_1_0_5849_D =
{
    1, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x36, 0x83 },
        { 0x6D, 0x1C },
        { 0xA4, 0x46 },
        { 0xDA, 0x75 },
        { 0x112, 0xC7 },
        { 0x148, 0x06 },
        { 0x17F, 0x00 }
    }
};

// _D3DDevice_Clear@24 (d3d8d.lib, 1544 bytes)
LOOVPA<8> D3DDevice_Clear_1_0_5849_D =
{
    1, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x38, 0x83 },
        { 0x6D, 0xD9 },
        { 0xA6, 0x80 },
        { 0xDB, 0x00 },
        { 0x112, 0x0F },
        { 0x149, 0x8B },
        { 0x180, 0xF8 }
    }
};

// _D3DDevice_Swap@4 (d3d8d.lib, 413 bytes)
LOOVPA<8> D3DDevice_Swap_1_0_5849_D =
{
    1, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x37, 0x01 },
        { 0x6D, 0xE8 },
        { 0xA3, 0xE8 },
        { 0xDB, 0x85 },
        { 0x112, 0x74 },
        { 0x14A, 0xE8 },
        { 0x180, 0x0A }
    }
};

// _D3DDevice_SetDebugMarker@4 (d3d8d.lib, 28 bytes)
SOOVPA<8> D3DDevice_SetDebugMarker_1_0_5849_D =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x03, 0x8B },
        { 0x09, 0x8B },
        { 0x0B, 0x08 },
        { 0x0F, 0x09 },
        { 0x13, 0x91 },
        { 0x17, 0x00 },
        { 0x1B, 0x00 }
    }
};

// _D3DDevice_GetDebugMarker@0 (d3d8d.lib, 12 bytes)
SOOVPA<8> D3DDevice_GetDebugMarker_1_0_5849_D =
{
    0, 8, -1, 0,
    {
        { 0x00, 0xA1 },
        { 0x05, 0x8B },
        { 0x06, 0x80 },
        { 0x07, 0x34 },
        { 0x08, 0x09 },
        { 0x09, 0x00 },
        { 0x0A, 0x00 },
        { 0x0B, 0xC3 }
    }
};

// _D3DPERF_Reset@0 (d3d8d.lib, 420 bytes)
LOOVPA<8> D3DPERF_Reset_1_0_5849_D =
{
    1, 8, -1, 0,
    {
        { 0x00, 0xA1 },
        { 0x38, 0xF3 },
        { 0x6E, 0x64 },
        { 0xA2, 0x1D },
        { 0xDB, 0x00 },
        { 0x110, 0x1D },
        { 0x149, 0x00 },
        { 0x180, 0x02 }
    }
};

OOVPATable D3D8D_1_0_5849[] =
{
    {
        (OOVPA*)&Direct3D_CreateDevice_1_0_5849_D,
        XTL::EmuIDirect3D8_CreateDevice,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3D8_CreateDevice"
        #endif
    },
    {
        (OOVPA*)&D3DDevice_Clear_1_0_5849_D,
        XTL::EmuIDirect3DDevice8_Clear,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_Clear"
        #endif
    },
    {
        (OOVPA*)&D3DDevice_Swap_1_0_5849_D,
        XTL::EmuIDirect3DDevice8_Swap,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_Swap"
        #endif
    },
    {
        (OOVPA*)&D3DDevice_SetDebugMarker_1_0_5849_D,
        XTL::EmuIDirect3DDevice8_SetDebugMarker,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetDebugMarker"
        #endif
    },
    {
        (OOVPA*)&D3DDevice_GetDebugMarker_1_0_5849_D,
        XTL::EmuIDirect3DDevice8_GetDebugMarker,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetDebugMarker"
        #endif
    },
    {
        (OOVPA*)&D3DPERF_Reset_1_0_5849_D,
        XTL::EmuD3DPERF_Reset,
        #ifdef _DEBUG_TRACE
        "EmuD3DPERF_Reset"
        #endif
    }
};

uint32 D3D8D_1_0_5849_SIZE = sizeof(D3D8D_1_0_5849);
