// XDK 5659 uses the same input implementations as XDK 5849, but its
// performance-counter leaf functions must also be intercepted. Leaving the
// native RDTSC body in place makes guest time advance at the host TSC rate
// while QueryPerformanceFrequency still reports the Xbox's 733 MHz clock.

// _USBD_Init@8 (xapilib.lib, 121 bytes). XInitDevices is a relocation-only
// thunk in this XDK, so intercept its uniquely identifiable implementation.
SOOVPA<12> USBD_Init_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x0A, 0x00 },
        { 0x15, 0x78 },
        { 0x20, 0xBE },
        { 0x2B, 0x8B },
        { 0x36, 0x50 },
        { 0x41, 0xBC },
        { 0x4B, 0xE8 },
        { 0x57, 0x45 },
        { 0x64, 0xC6 },
        { 0x6B, 0xE8 },
        { 0x78, 0x00 }
    }
};

// _QueryPerformanceCounter@4 (xapilib.lib, 17 bytes)
SOOVPA<8> QueryPerformanceCounter_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x02, 0x24 },
        { 0x04, 0x0F },
        { 0x06, 0x89 },
        { 0x09, 0x51 },
        { 0x0B, 0x33 },
        { 0x0D, 0x40 },
        { 0x10, 0x00 }
    }
};

// _QueryPerformanceFrequency@4 (xapilib.lib, 20 bytes)
SOOVPA<8> QueryPerformanceFrequency_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x02, 0x24 },
        { 0x05, 0x60 },
        { 0x08, 0xC7 },
        { 0x0A, 0x55 },
        { 0x0D, 0x2B },
        { 0x10, 0x40 },
        { 0x13, 0x00 }
    }
};

// _XapiThreadStartup@8 (xapilib.lib, 152 bytes)
SOOVPA<8> XapiThreadStartup_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x6A },
        { 0x15, 0x00 },
        { 0x2B, 0x8B },
        { 0x40, 0xA5 },
        { 0x56, 0x13 },
        { 0x6C, 0xFF },
        { 0x81, 0xE8 },
        { 0x97, 0xCC }
    }
};

OOVPATable XAPI_1_0_5659[] =
{
    {
        (OOVPA*)&USBD_Init_1_0_5659,
        XTL::EmuXInitDevices,
        #ifdef _DEBUG_TRACE
        "EmuXInitDevices"
        #endif
    },
    {
        (OOVPA*)&QueryPerformanceCounter_1_0_5659,
        XTL::EmuQueryPerformanceCounter,
        #ifdef _DEBUG_TRACE
        "EmuQueryPerformanceCounter"
        #endif
    },
    {
        (OOVPA*)&QueryPerformanceFrequency_1_0_5659,
        XTL::EmuQueryPerformanceFrequency,
        #ifdef _DEBUG_TRACE
        "EmuQueryPerformanceFrequency"
        #endif
    },
    {
        (OOVPA*)&XapiThreadStartup_1_0_5659,
        XTL::EmuXapiThreadStartup,
        #ifdef _DEBUG_TRACE
        "EmuXapiThreadStartup"
        #endif
    },
    {
        (OOVPA*)&XGetDevices_1_0_5849,
        XTL::EmuXGetDevices,
        #ifdef _DEBUG_TRACE
        "EmuXGetDevices"
        #endif
    },
    {
        (OOVPA*)&XGetDeviceChanges_1_0_5849,
        XTL::EmuXGetDeviceChanges,
        #ifdef _DEBUG_TRACE
        "EmuXGetDeviceChanges"
        #endif
    },
    {
        (OOVPA*)&XInputOpen_1_0_5849,
        XTL::EmuXInputOpen,
        #ifdef _DEBUG_TRACE
        "EmuXInputOpen"
        #endif
    },
    {
        (OOVPA*)&XInputGetCapabilities_1_0_5849,
        XTL::EmuXInputGetCapabilities,
        #ifdef _DEBUG_TRACE
        "EmuXInputGetCapabilities"
        #endif
    },
    {
        (OOVPA*)&XInputGetState_1_0_5849,
        XTL::EmuXInputGetState,
        #ifdef _DEBUG_TRACE
        "EmuXInputGetState"
        #endif
    },
    {
        (OOVPA*)&XInputSetState_1_0_5849,
        XTL::EmuXInputSetState,
        #ifdef _DEBUG_TRACE
        "EmuXInputSetState"
        #endif
    },
    {
        (OOVPA*)&XInputCloseInternal_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "XID_fCloseDevice (XREF)"
        #endif
    },
    {
        (OOVPA*)&XInputClose_1_0_5849,
        XTL::EmuXInputClose,
        #ifdef _DEBUG_TRACE
        "EmuXInputClose"
        #endif
    },
};

uint32 XAPI_1_0_5659_SIZE = sizeof(XAPI_1_0_5659);
