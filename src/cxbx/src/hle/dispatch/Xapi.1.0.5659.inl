// XDK 5659 uses the same input implementations as XDK 5849, but its
// performance-counter leaf functions must also be intercepted. Leaving the
// native RDTSC body in place makes guest time advance at the host TSC rate
// while QueryPerformanceFrequency still reports the Xbox's 733 MHz clock.

// _RtlCreateHeap@24 (xapilib.lib, 1060 bytes)
SOOVPA<12> RtlCreateHeap_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x6A },
        { 0x17, 0xC0 },
        { 0x2E, 0x50 },
        { 0x45, 0x00 },
        { 0x5C, 0xDE },
        { 0x73, 0x75 },
        { 0x8B, 0x75 },
        { 0xA2, 0x45 },
        { 0xB9, 0xF0 },
        { 0xD0, 0x10 },
        { 0xE7, 0x00 },
        { 0xFF, 0x45 }
    }
};

// _RtlAllocateHeap@12 (xapilib.lib, 1915 bytes)
SOOVPA<12> RtlAllocateHeap_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x68 },
        { 0x17, 0xFF },
        { 0x2E, 0x40 },
        { 0x45, 0xC1 },
        { 0x5C, 0x00 },
        { 0x73, 0x84 },
        { 0x8B, 0xE0 },
        { 0xA2, 0xB7 },
        { 0xB9, 0x10 },
        { 0xD0, 0x33 },
        { 0xE7, 0x50 },
        { 0xFF, 0xD1 }
    }
};

// _RtlReAllocateHeap@16 (xapilib.lib, 1838 bytes)
SOOVPA<12> RtlReAllocateHeap_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x68 },
        { 0x17, 0xE3 },
        { 0x2E, 0xFF },
        { 0x45, 0x23 },
        { 0x5C, 0x06 },
        { 0x73, 0xFF },
        { 0x8B, 0x00 },
        { 0xA2, 0xE1 },
        { 0xB9, 0xD0 },
        { 0xD0, 0x81 },
        { 0xE7, 0x8B },
        { 0xFF, 0x03 }
    }
};

// _RtlFreeHeap@12 (xapilib.lib, 500 bytes)
SOOVPA<12> RtlFreeHeap_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x6A },
        { 0x17, 0x00 },
        { 0x2E, 0x65 },
        { 0x45, 0x8D },
        { 0x5C, 0x01 },
        { 0x73, 0x0F },
        { 0x8C, 0x89 },
        { 0xA2, 0x88 },
        { 0xB9, 0x08 },
        { 0xD0, 0x60 },
        { 0xE7, 0xBC },
        { 0xFF, 0x00 }
    }
};

// _RtlSizeHeap@12 (xapilib.lib, 47 bytes)
SOOVPA<12> RtlSizeHeap_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x04, 0x8A },
        { 0x08, 0x01 },
        { 0x0C, 0xC8 },
        { 0x10, 0xA8 },
        { 0x14, 0x0F },
        { 0x19, 0x41 },
        { 0x1D, 0xEB },
        { 0x21, 0x41 },
        { 0x25, 0x49 },
        { 0x29, 0x04 },
        { 0x2E, 0x00 }
    }
};

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
        (OOVPA*)&RtlCreateHeap_1_0_5659,
        XTL::EmuRtlCreateHeap,
        #ifdef _DEBUG_TRACE
        "EmuRtlCreateHeap"
        #endif
    },
    {
        (OOVPA*)&RtlAllocateHeap_1_0_5659,
        XTL::EmuRtlAllocateHeap,
        #ifdef _DEBUG_TRACE
        "EmuRtlAllocateHeap"
        #endif
    },
    {
        (OOVPA*)&RtlReAllocateHeap_1_0_5659,
        XTL::EmuRtlReAllocateHeap,
        #ifdef _DEBUG_TRACE
        "EmuRtlReAllocateHeap"
        #endif
    },
    {
        (OOVPA*)&RtlFreeHeap_1_0_5659,
        XTL::EmuRtlFreeHeap,
        #ifdef _DEBUG_TRACE
        "EmuRtlFreeHeap"
        #endif
    },
    {
        (OOVPA*)&RtlSizeHeap_1_0_5659,
        XTL::EmuRtlSizeHeap,
        #ifdef _DEBUG_TRACE
        "EmuRtlSizeHeap"
        #endif
    },
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
