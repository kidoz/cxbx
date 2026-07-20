// D3D8 profile-library signatures generated from the XDK 5849 d3d8i.lib.
// Keep this table separate from retail and debug D3D8 variants.

// _Direct3D_CreateDevice@24 (d3d8i.lib, 183 bytes)
SOOVPA<13> Direct3D_CreateDevice_1_0_5849_I =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x0F, 0x85 },
        { 0x1F, 0x00 },
        { 0x2D, 0x05 },
        { 0x3C, 0x56 },
        { 0x4C, 0x0B },
        { 0x5D, 0x01 },
        { 0x6C, 0x8B },
        { 0x79, 0x8B },
        { 0x88, 0xB9 },
        { 0x98, 0x00 },
        { 0xA6, 0x8B },
        { 0xB6, 0x00 }
    }
};

// _D3DDevice_Clear@24 (d3d8i.lib, 1004 bytes)
SOOVPA<13> D3DDevice_Clear_1_0_5849_I =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x15, 0x33 },
        { 0x2A, 0xB6 },
        { 0x3F, 0x7D },
        { 0x55, 0x89 },
        { 0x6A, 0x00 },
        { 0x7F, 0xF0 },
        { 0x95, 0xFF },
        { 0xAA, 0x00 },
        { 0xBF, 0x03 },
        { 0xD4, 0xF8 },
        { 0xE9, 0x89 },
        { 0xFF, 0x24 }
    }
};

// _D3DPERF_Reset@0 (d3d8i.lib, 389 bytes)
SOOVPA<13> D3DPERF_Reset_1_0_5849_I =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x53 },
        { 0x15, 0x00 },
        { 0x2A, 0x20 },
        { 0x3F, 0x0D },
        { 0x55, 0x00 },
        { 0x6C, 0xA3 },
        { 0x7E, 0x1D },
        { 0x95, 0x89 },
        { 0xAA, 0x04 },
        { 0xBF, 0xE0 },
        { 0xD4, 0x8B },
        { 0xE9, 0x00 },
        { 0xFF, 0xE8 }
    }
};

// _D3DPERF_GetPushBufferInfo@4 (d3d8i.lib, 75 bytes)
SOOVPA<13> D3DPERF_GetPushBufferInfo_1_0_5849_I =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x55 }, { 0x08, 0x56 }, { 0x0C, 0x89 },
        { 0x14, 0x89 }, { 0x18, 0x15 }, { 0x1E, 0x42 },
        { 0x25, 0x0D }, { 0x2B, 0x51 }, { 0x30, 0xA1 },
        { 0x37, 0x28 }, { 0x3B, 0xE8 }, { 0x43, 0x89 },
        { 0x4A, 0x00 }
    }
};

// _D3DPERF_SetMarker (d3d8i.lib, 57 bytes)
SOOVPA<13> D3DPERF_SetMarker_1_0_5849_I =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x55 }, { 0x04, 0x8B }, { 0x0A, 0x8D },
        { 0x0E, 0x45 }, { 0x12, 0xD8 }, { 0x17, 0xC0 },
        { 0x1C, 0x0C }, { 0x20, 0x8D }, { 0x25, 0x51 },
        { 0x2A, 0x00 }, { 0x30, 0xE8 }, { 0x35, 0x8B },
        { 0x38, 0xC3 }
    }
};

// _D3DPERF_BeginEvent (d3d8i.lib, 68 bytes)
SOOVPA<13> D3DPERF_BeginEvent_1_0_5849_I =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x55 }, { 0x05, 0x0D }, { 0x0B, 0x45 },
        { 0x10, 0x8A }, { 0x16, 0x84 }, { 0x1B, 0x45 },
        { 0x21, 0x55 }, { 0x27, 0x2A }, { 0x2B, 0xB9 },
        { 0x30, 0xE8 }, { 0x35, 0xA1 }, { 0x3B, 0xA3 },
        { 0x43, 0xC3 }
    }
};

// _D3DPERF_EndEvent@0 (d3d8i.lib, 26 bytes)
SOOVPA<13> D3DPERF_EndEvent_1_0_5849_I =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x68 }, { 0x01, 0x2B }, { 0x02, 0xE0 },
        { 0x03, 0x00 }, { 0x04, 0x00 }, { 0x05, 0xE8 },
        { 0x0A, 0x8B }, { 0x0B, 0x0D }, { 0x10, 0x8B },
        { 0x11, 0xC1 }, { 0x12, 0x49 }, { 0x14, 0x0D },
        { 0x19, 0xC3 }
    }
};

OOVPATable D3D8I_1_0_5849[] =
{
    {
        (OOVPA*)&Direct3D_CreateDevice_1_0_5849_I,
        XTL::EmuIDirect3D8_CreateDevice,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3D8_CreateDevice"
        #endif
    },
    {
        (OOVPA*)&D3DDevice_Clear_1_0_5849_I,
        XTL::EmuIDirect3DDevice8_Clear,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_Clear"
        #endif
    },
    {
        (OOVPA*)&D3DPERF_Reset_1_0_5849_I,
        XTL::EmuD3DPERF_Reset,
        #ifdef _DEBUG_TRACE
        "EmuD3DPERF_Reset"
        #endif
    },
    {
        (OOVPA*)&D3DPERF_GetPushBufferInfo_1_0_5849_I,
        XTL::EmuD3DPERF_GetPushBufferInfo,
        #ifdef _DEBUG_TRACE
        "EmuD3DPERF_GetPushBufferInfo"
        #endif
    },
    {
        (OOVPA*)&D3DPERF_SetMarker_1_0_5849_I,
        XTL::EmuD3DPERF_SetMarker,
        #ifdef _DEBUG_TRACE
        "EmuD3DPERF_SetMarker"
        #endif
    },
    {
        (OOVPA*)&D3DPERF_BeginEvent_1_0_5849_I,
        XTL::EmuD3DPERF_BeginEvent,
        #ifdef _DEBUG_TRACE
        "EmuD3DPERF_BeginEvent"
        #endif
    },
    {
        (OOVPA*)&D3DPERF_EndEvent_1_0_5849_I,
        XTL::EmuD3DPERF_EndEvent,
        #ifdef _DEBUG_TRACE
        "EmuD3DPERF_EndEvent"
        #endif
    }
};

uint32 D3D8I_1_0_5849_SIZE = sizeof(D3D8I_1_0_5849);
