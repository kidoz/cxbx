// XACT engine lifecycle signatures generated from XDK 5849 xacteng.lib.

// _XACTEngineDoWork@0 (xacteng.lib, 50 bytes)
SOOVPA<13> XACTEngineDoWork_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x56 }, { 0x02, 0xE8 }, { 0x08, 0x35 },
        { 0x0D, 0x85 }, { 0x10, 0xB6 }, { 0x14, 0xE8 },
        { 0x19, 0x8B }, { 0x1B, 0xE8 }, { 0x20, 0x85 },
        { 0x24, 0x68 }, { 0x29, 0xFF }, { 0x2A, 0x15 },
        { 0x31, 0xC3 }
    }
};

// _XACTEngineCreate@8 (xacteng.lib, 165 bytes)
SOOVPA<13> XACTEngineCreate_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x53 }, { 0x0C, 0xA1 }, { 0x1B, 0x08 },
        { 0x29, 0xEB }, { 0x36, 0xE8 }, { 0x46, 0x8B },
        { 0x52, 0x81 }, { 0x5F, 0x24 }, { 0x6D, 0x85 },
        { 0x7B, 0xD8 }, { 0x88, 0x57 }, { 0x98, 0xFF },
        { 0xA4, 0x00 }
    }
};

// _IXACTEngine_AddRef@4 (xacteng.lib, 55 bytes)
SOOVPA<13> IXACTEngine_AddRef_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x56 }, { 0x02, 0xE8 }, { 0x09, 0xF0 },
        { 0x0D, 0x0C }, { 0x12, 0xF8 }, { 0x16, 0xC9 },
        { 0x1A, 0xE8 }, { 0x1F, 0x85 }, { 0x24, 0x0B },
        { 0x2A, 0xFF }, { 0x2B, 0x15 }, { 0x31, 0xC7 },
        { 0x36, 0x00 }
    }
};

// ?Release@CEngine@XACT@@QAGKXZ (xacteng.lib, 47 bytes)
SOOVPA<13> XACTEngine_ReleaseInternal_1_0_5849 =
{
    0, 13, XREF_XACT5849_ENGINE_RELEASE, 0,
    {
        { 0x00, 0x56 }, { 0x02, 0xBE }, { 0x07, 0x56 },
        { 0x09, 0x15 }, { 0x0F, 0x4C }, { 0x13, 0x49 },
        { 0x17, 0x08 }, { 0x1A, 0x6A }, { 0x1C, 0xE8 },
        { 0x22, 0xFF }, { 0x28, 0x8B }, { 0x2A, 0x5F },
        { 0x2E, 0x00 }
    }
};

// _IXACTEngine_Release@4 (call@0x11 -> XREF_XACT5849_ENGINE_RELEASE)
SOOVPA<14> IXACTEngine_Release_1_0_5849 =
{
    0, 14, -1, 1,
    {
        { 0x11, XREF_XACT5849_ENGINE_RELEASE },
        { 0x00, 0x8B }, { 0x01, 0x44 }, { 0x02, 0x24 },
        { 0x03, 0x04 }, { 0x05, 0xC8 }, { 0x07, 0xC0 },
        { 0x09, 0xF7 }, { 0x0B, 0x1B }, { 0x0D, 0x23 },
        { 0x0F, 0x51 }, { 0x10, 0xE8 }, { 0x15, 0xC2 },
        { 0x17, 0x00 }
    }
};

// _IXACTEngine_RegisterWaveBank@16 (xacteng.lib, 67 bytes)
SOOVPA<13> IXACTEngine_RegisterWaveBank_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x56 }, { 0x07, 0xFF }, { 0x0B, 0x0F },
        { 0x10, 0x24 }, { 0x16, 0x8B }, { 0x1B, 0x18 },
        { 0x21, 0x1B }, { 0x26, 0xE8 }, { 0x2C, 0xF6 },
        { 0x31, 0x68 }, { 0x37, 0x15 }, { 0x3C, 0x8B },
        { 0x42, 0x00 }
    }
};

// _IXACTEngine_UnRegisterWaveBank@8 (xacteng.lib, 59 bytes)
SOOVPA<13> IXACTEngine_UnRegisterWaveBank_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x56 }, { 0x02, 0xE8 }, { 0x09, 0x24 },
        { 0x0E, 0x8B }, { 0x13, 0xC8 }, { 0x18, 0xD9 },
        { 0x1D, 0x51 }, { 0x23, 0x85 }, { 0x26, 0xF8 },
        { 0x29, 0x68 }, { 0x2F, 0x15 }, { 0x35, 0xC7 },
        { 0x3A, 0x00 }
    }
};

OOVPATable XACTENG_1_0_5849[] =
{
    {
        (OOVPA*)&XACTEngineDoWork_1_0_5849,
        XTL::EmuXACTEngineDoWork,
        #ifdef _DEBUG_TRACE
        "EmuXACTEngineDoWork"
        #endif
    },
    {
        (OOVPA*)&XACTEngineCreate_1_0_5849,
        XTL::EmuXACTEngineCreate,
        #ifdef _DEBUG_TRACE
        "EmuXACTEngineCreate"
        #endif
    },
    {
        (OOVPA*)&IXACTEngine_AddRef_1_0_5849,
        XTL::EmuIXACTEngine_AddRef,
        #ifdef _DEBUG_TRACE
        "EmuIXACTEngine_AddRef"
        #endif
    },
    {
        (OOVPA*)&XACTEngine_ReleaseInternal_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "XACT::CEngine::Release (XREF)"
        #endif
    },
    {
        (OOVPA*)&IXACTEngine_Release_1_0_5849,
        XTL::EmuIXACTEngine_Release,
        #ifdef _DEBUG_TRACE
        "EmuIXACTEngine_Release"
        #endif
    },
    {
        (OOVPA*)&IXACTEngine_RegisterWaveBank_1_0_5849,
        XTL::EmuIXACTEngine_RegisterWaveBank,
        #ifdef _DEBUG_TRACE
        "EmuIXACTEngine_RegisterWaveBank"
        #endif
    },
    {
        (OOVPA*)&IXACTEngine_UnRegisterWaveBank_1_0_5849,
        XTL::EmuIXACTEngine_UnRegisterWaveBank,
        #ifdef _DEBUG_TRACE
        "EmuIXACTEngine_UnRegisterWaveBank"
        #endif
    }
};

uint32 XACTENG_1_0_5849_SIZE = sizeof(XACTENG_1_0_5849);
