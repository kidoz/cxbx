// XapiInitProcess is image-derived from Soul Calibur II's XDK 5455 XAPILIB
// body. The established XbSymbolDatabase 5028 signature matches the same
// prologue, and this wider signature is unique in every title XBE in-repo.
//
// XDK 5455 performs retail hard-disk and XBE validation in the guest
// implementation before entering the title. Those checks assume a complete
// Xbox boot environment and route failures to the dashboard. The host wrapper
// provides the process-heap initialization required by user-mode HLE.
SOOVPA<14> XapiInitProcess_1_0_5455 =
{
    0, 14, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x0F, 0x68 },
        { 0x27, 0x8D },
        { 0x3A, 0xFF },
        { 0x49, 0xE8 },
        { 0x62, 0x57 },
        { 0x7B, 0x14 },
        { 0x86, 0xE8 },
        { 0x9A, 0x68 },
        { 0xAE, 0x3D },
        { 0xC4, 0xA1 },
        { 0xD8, 0x16 },
        { 0xEB, 0x00 },
        { 0xFF, 0x50 }
    }
};

// These XInput/device bodies are byte-compatible with XDK 5849. Each
// signature resolves exactly once in Soul Calibur II's 5455 image and remains
// unique across the title corpus.
SOOVPA<8> XGetDevices_1_0_5455 =
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

SOOVPA<8> XInputOpen_1_0_5455 =
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

SOOVPA<8> XInputGetCapabilities_1_0_5455 =
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

SOOVPA<8> XInputGetState_1_0_5455 =
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

SOOVPA<8> XInputSetState_1_0_5455 =
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

OOVPATable XAPI_1_0_5455[] =
{
    {
        (OOVPA*)&XapiInitProcess_1_0_5455,
        XTL::EmuXapiInitProcess,
        #ifdef _DEBUG_TRACE
        "EmuXapiInitProcess"
        #endif
    },
    {
        (OOVPA*)&XGetDevices_1_0_5455,
        XTL::EmuXGetDevices,
        #ifdef _DEBUG_TRACE
        "EmuXGetDevices"
        #endif
    },
    {
        (OOVPA*)&XInputOpen_1_0_5455,
        XTL::EmuXInputOpen,
        #ifdef _DEBUG_TRACE
        "EmuXInputOpen"
        #endif
    },
    {
        (OOVPA*)&XInputGetCapabilities_1_0_5455,
        XTL::EmuXInputGetCapabilities,
        #ifdef _DEBUG_TRACE
        "EmuXInputGetCapabilities"
        #endif
    },
    {
        (OOVPA*)&XInputGetState_1_0_5455,
        XTL::EmuXInputGetState,
        #ifdef _DEBUG_TRACE
        "EmuXInputGetState"
        #endif
    },
    {
        (OOVPA*)&XInputSetState_1_0_5455,
        XTL::EmuXInputSetState,
        #ifdef _DEBUG_TRACE
        "EmuXInputSetState"
        #endif
    },
};

uint32 XAPI_1_0_5455_SIZE = sizeof(XAPI_1_0_5455);
