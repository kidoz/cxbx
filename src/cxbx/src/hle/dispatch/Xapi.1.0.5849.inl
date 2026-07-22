// Generated from XDK 5849 xapilib.lib (tools/oovpa/gen_oovpa.py); each
// signature verified unique across the hle_resolve probe image and every
// title XBE in-repo. Covers the XInput/device family -- with the
// CXBX_INPUT_STATE injection hook in EmuXInputGetState this enables headless
// controller input for 5849 titles. XInitDevices is deliberately absent (its
// wrapper is almost entirely relocations, no verifiable signature; the
// un-HLE'd guest init is harmless -- one-time OHCI register reads against
// the USB0 model).

// _XGetDevices@4 (xapilib.lib, 34 bytes)
SOOVPA<8> XGetDevices_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x02, 0x15 },
        { 0x09, 0x24 },
        { 0x0E, 0x62 },
        { 0x12, 0xC8 },
        { 0x17, 0x15 },
        { 0x1C, 0x8B },
        { 0x21, 0x00 }
    }
};

// _XGetDeviceChanges@12 (xapilib.lib, 109 bytes)
SOOVPA<8> XGetDeviceChanges_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x0F, 0x4D },
        { 0x1D, 0x15 },
        { 0x2E, 0x8B },
        { 0x3D, 0x23 },
        { 0x4D, 0x00 },
        { 0x5C, 0x4D },
        { 0x6C, 0x00 }
    }
};

// _XInputOpen@16 (xapilib.lib, 86 bytes)
SOOVPA<8> XInputOpen_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x0B, 0xE8 },
        { 0x16, 0xE8 },
        { 0x24, 0xF6 },
        { 0x30, 0x0C },
        { 0x3C, 0xC8 },
        { 0x48, 0x06 },
        { 0x55, 0x00 }
    }
};

// _XInputGetCapabilities@8 (xapilib.lib, 472 bytes)
SOOVPA<8> XInputGetCapabilities_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x24, 0x00 },
        { 0x48, 0x74 },
        { 0x6D, 0x8D },
        { 0x91, 0x89 },
        { 0xB6, 0x83 },
        { 0xDA, 0x50 },
        { 0xFF, 0x39 }
    }
};

// _XInputGetState@8 (xapilib.lib, 115 bytes)
SOOVPA<8> XInputGetState_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x53 },
        { 0x10, 0xA3 },
        { 0x20, 0x0A },
        { 0x30, 0x8B },
        { 0x41, 0x8B },
        { 0x51, 0xD1 },
        { 0x61, 0x8B },
        { 0x72, 0x00 }
    }
};

// _XInputSetState@8 (xapilib.lib, 51 bytes)
SOOVPA<8> XInputSetState_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x07, 0x00 },
        { 0x0E, 0x28 },
        { 0x15, 0xEB },
        { 0x1C, 0x42 },
        { 0x23, 0x0C },
        { 0x2A, 0x41 },
        { 0x32, 0x00 }
    }
};

// ?XID_fCloseDevice@@YIXPAU_XID_OPEN_DEVICE@@@Z (xapilib.lib, 148 bytes)
// XRef-save signature: locates the internal so the 12-byte XInputClose thin
// wrapper can be matched by its call target.
SOOVPA<8> XInputCloseInternal_1_0_5849 =
{
    0, 8, XREF_XAPI5849_XINPUTCLOSE, 0,
    {
        { 0x00, 0x55 },
        { 0x15, 0xA3 },
        { 0x2A, 0x80 },
        { 0x3F, 0x88 },
        { 0x54, 0x8A },
        { 0x6B, 0xEB },
        { 0x7E, 0x01 },
        { 0x93, 0xC3 }
    }
};

// _XInputClose@4 (xapilib.lib, 12 bytes; call@0x05 -> XREF_XAPI5849_XINPUTCLOSE)
SOOVPA<9> XInputClose_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x05, XREF_XAPI5849_XINPUTCLOSE },
        { 0x00, 0x8B },
        { 0x01, 0x4C },
        { 0x02, 0x24 },
        { 0x03, 0x04 },
        { 0x04, 0xE8 },
        { 0x09, 0xC2 },
        { 0x0A, 0x04 },
        { 0x0B, 0x00 }
    }
};

// _QueryPerformanceCounter@4 (xapilib.lib, 17 bytes)
SOOVPA<13> QueryPerformanceCounter_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x8B }, { 0x01, 0x4C }, { 0x02, 0x24 },
        { 0x04, 0x0F }, { 0x05, 0x31 }, { 0x06, 0x89 },
        { 0x08, 0x89 }, { 0x09, 0x51 }, { 0x0A, 0x04 },
        { 0x0C, 0xC0 }, { 0x0D, 0x40 }, { 0x0E, 0xC2 },
        { 0x10, 0x00 }
    }
};

// _QueryPerformanceFrequency@4 (xapilib.lib, 20 bytes)
SOOVPA<13> QueryPerformanceFrequency_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x8B }, { 0x01, 0x44 }, { 0x03, 0x04 },
        { 0x04, 0x83 }, { 0x06, 0x04 }, { 0x07, 0x00 },
        { 0x09, 0x00 }, { 0x0B, 0xC7 }, { 0x0C, 0xB5 },
        { 0x0E, 0x33 }, { 0x0F, 0xC0 }, { 0x11, 0xC2 },
        { 0x13, 0x00 }
    }
};

// _XMountUtilityDrive@4 (xapilib.lib, 261 bytes)
SOOVPA<13> XMountUtilityDrive_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x55 }, { 0x15, 0x75 }, { 0x2A, 0x08 },
        { 0x3F, 0x24 }, { 0x55, 0x00 }, { 0x6A, 0xC4 },
        { 0x7F, 0x50 }, { 0x94, 0xE8 }, { 0xAA, 0xD8 },
        { 0xBF, 0x85 }, { 0xD4, 0x45 }, { 0xE9, 0x9D },
        { 0xFF, 0x5E }
    }
};

OOVPATable XAPI_1_0_5849[] =
{
    // XGetDevices
    {
        (OOVPA*)&XGetDevices_1_0_5849,
        XTL::EmuXGetDevices,
        #ifdef _DEBUG_TRACE
        "EmuXGetDevices"
        #endif
    },
    // XGetDeviceChanges
    {
        (OOVPA*)&XGetDeviceChanges_1_0_5849,
        XTL::EmuXGetDeviceChanges,
        #ifdef _DEBUG_TRACE
        "EmuXGetDeviceChanges"
        #endif
    },
    // XInputOpen
    {
        (OOVPA*)&XInputOpen_1_0_5849,
        XTL::EmuXInputOpen,
        #ifdef _DEBUG_TRACE
        "EmuXInputOpen"
        #endif
    },
    // XInputGetCapabilities
    {
        (OOVPA*)&XInputGetCapabilities_1_0_5849,
        XTL::EmuXInputGetCapabilities,
        #ifdef _DEBUG_TRACE
        "EmuXInputGetCapabilities"
        #endif
    },
    // XInputGetState
    {
        (OOVPA*)&XInputGetState_1_0_5849,
        XTL::EmuXInputGetState,
        #ifdef _DEBUG_TRACE
        "EmuXInputGetState"
        #endif
    },
    // XInputSetState
    {
        (OOVPA*)&XInputSetState_1_0_5849,
        XTL::EmuXInputSetState,
        #ifdef _DEBUG_TRACE
        "EmuXInputSetState"
        #endif
    },
    // XID_fCloseDevice (XREF save)
    {
        (OOVPA*)&XInputCloseInternal_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "XID_fCloseDevice (XREF)"
        #endif
    },
    // XInputClose
    {
        (OOVPA*)&XInputClose_1_0_5849,
        XTL::EmuXInputClose,
        #ifdef _DEBUG_TRACE
        "EmuXInputClose"
        #endif
    },
    // QueryPerformanceCounter
    {
        (OOVPA*)&QueryPerformanceCounter_1_0_5849,
        XTL::EmuQueryPerformanceCounter,
        #ifdef _DEBUG_TRACE
        "EmuQueryPerformanceCounter"
        #endif
    },
    // QueryPerformanceFrequency
    {
        (OOVPA*)&QueryPerformanceFrequency_1_0_5849,
        XTL::EmuQueryPerformanceFrequency,
        #ifdef _DEBUG_TRACE
        "EmuQueryPerformanceFrequency"
        #endif
    },
    // XMountUtilityDrive
    {
        (OOVPA*)&XMountUtilityDrive_1_0_5849,
        XTL::EmuXMountUtilityDrive,
        #ifdef _DEBUG_TRACE
        "EmuXMountUtilityDrive"
        #endif
    },
};

uint32 XAPI_1_0_5849_SIZE = sizeof(XAPI_1_0_5849);
