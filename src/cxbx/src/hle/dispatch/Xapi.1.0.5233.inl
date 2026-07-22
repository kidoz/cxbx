// Generated from the nearest available XDK xapilib.lib with
// tools/oovpa/gen_oovpa.py. Every new signature is unique in the XDK 5233
// Big Mutha Truckers image. Intercepting USBD_Init keeps the title out of the
// native OHCI path and routes controller discovery through Cxbx's XInput HLE.

// _USBD_Init@8 (xapilib.lib, 121 bytes)
SOOVPA<12> USBD_Init_1_0_5233 =
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
        { 0x64, 0x80 },
        { 0x6B, 0xE8 },
        { 0x78, 0x00 }
    }
};

// _XGetDevices@4 (xapilib.lib, 34 bytes)
SOOVPA<8> XGetDevices_1_0_5233 =
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
SOOVPA<8> XGetDeviceChanges_1_0_5233 =
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
SOOVPA<8> XInputOpen_1_0_5233 =
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

// _XInputGetCapabilities@8 (xapilib.lib, 478 bytes)
SOOVPA<12> XInputGetCapabilities_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x17, 0xFF },
        { 0x2E, 0x00 },
        { 0x45, 0x40 },
        { 0x5C, 0x80 },
        { 0x73, 0x4D },
        { 0x8B, 0x30 },
        { 0xA2, 0xC6 },
        { 0xB9, 0x83 },
        { 0xD0, 0x4D },
        { 0xE7, 0xD3 },
        { 0xFF, 0x00 }
    }
};

// _XInputGetState@8 (xapilib.lib, 108 bytes)
SOOVPA<12> XInputGetState_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x53 },
        { 0x0A, 0x8B },
        { 0x13, 0x00 },
        { 0x1D, 0xEB },
        { 0x26, 0x41 },
        { 0x30, 0x8B },
        { 0x3A, 0x8B },
        { 0x44, 0xB6 },
        { 0x4D, 0x04 },
        { 0x57, 0x03 },
        { 0x60, 0x15 },
        { 0x6B, 0x00 }
    }
};

// _XInputSetState@8 (xapilib.lib, 51 bytes)
SOOVPA<12> XInputSetState_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x04, 0x8D },
        { 0x09, 0x00 },
        { 0x0D, 0x42 },
        { 0x12, 0x6A },
        { 0x16, 0x19 },
        { 0x1B, 0x80 },
        { 0x1F, 0x8B },
        { 0x24, 0x8A },
        { 0x28, 0x88 },
        { 0x2B, 0xE8 },
        { 0x32, 0x00 }
    }
};

// ?XID_fCloseDevice@@YIXPAU_XID_OPEN_DEVICE@@@Z (xapilib.lib, 151 bytes)
SOOVPA<12> XInputCloseInternal_1_0_5233 =
{
    0, 12, XREF_XAPI5233_XINPUTCLOSE, 0,
    {
        { 0x00, 0x55 },
        { 0x0B, 0x15 },
        { 0x1B, 0x1C },
        { 0x28, 0x74 },
        { 0x36, 0xF8 },
        { 0x44, 0xEC },
        { 0x51, 0x00 },
        { 0x60, 0x53 },
        { 0x6E, 0xEB },
        { 0x7A, 0x86 },
        { 0x88, 0x86 },
        { 0x96, 0xC3 }
    }
};

// _XInputClose@4 (xapilib.lib, 12 bytes)
SOOVPA<9> XInputClose_1_0_5233 =
{
    0, 9, -1, 1,
    {
        { 0x05, XREF_XAPI5233_XINPUTCLOSE },
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

#ifdef _DEBUG_TRACE
#define XAPI_5233_TRACE_NAME(Name) , #Name
#else
#define XAPI_5233_TRACE_NAME(Name)
#endif

OOVPATable XAPI_1_0_5233[] =
{
    { (OOVPA*)&USBD_Init_1_0_5233, XTL::EmuXInitDevices XAPI_5233_TRACE_NAME(EmuXInitDevices) },
    { (OOVPA*)&XGetDevices_1_0_5233, XTL::EmuXGetDevices XAPI_5233_TRACE_NAME(EmuXGetDevices) },
    { (OOVPA*)&XGetDeviceChanges_1_0_5233, XTL::EmuXGetDeviceChanges XAPI_5233_TRACE_NAME(EmuXGetDeviceChanges) },
    { (OOVPA*)&XInputOpen_1_0_5233, XTL::EmuXInputOpen XAPI_5233_TRACE_NAME(EmuXInputOpen) },
    { (OOVPA*)&XInputGetCapabilities_1_0_5233, XTL::EmuXInputGetCapabilities XAPI_5233_TRACE_NAME(EmuXInputGetCapabilities) },
    { (OOVPA*)&XInputGetState_1_0_5233, XTL::EmuXInputGetState XAPI_5233_TRACE_NAME(EmuXInputGetState) },
    { (OOVPA*)&XInputSetState_1_0_5233, XTL::EmuXInputSetState XAPI_5233_TRACE_NAME(EmuXInputSetState) },
    { (OOVPA*)&XInputCloseInternal_1_0_5233, 0 XAPI_5233_TRACE_NAME(XInputCloseInternal) },
    { (OOVPA*)&XInputClose_1_0_5233, XTL::EmuXInputClose XAPI_5233_TRACE_NAME(EmuXInputClose) }
};

#undef XAPI_5233_TRACE_NAME

uint32 XAPI_1_0_5233_SIZE = sizeof(XAPI_1_0_5233);
