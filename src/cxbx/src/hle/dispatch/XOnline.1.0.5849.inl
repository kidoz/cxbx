// _XNetStartup@4 (xonlines.lib 5849, 23 bytes)
SOOVPA<13> XNetStartup_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x33 },
        { 0x01, 0xC0 },
        { 0x02, 0x50 },
        { 0x03, 0x50 },
        { 0x04, 0x50 },
        { 0x05, 0xFF },
        { 0x06, 0x74 },
        { 0x07, 0x24 },
        { 0x09, 0x50 },
        { 0x0A, 0x68 },
        { 0x0F, 0xE8 },
        { 0x14, 0xC2 },
        { 0x16, 0x00 }
    }
};

// _WSAStartup@8 (xonlines.lib 5849, 27 bytes)
SOOVPA<13> WSAStartup_1_0_5849 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0xFF },
        { 0x01, 0x74 },
        { 0x02, 0x24 },
        { 0x03, 0x08 },
        { 0x04, 0xFF },
        { 0x06, 0x24 },
        { 0x08, 0x6A },
        { 0x0A, 0x6A },
        { 0x0D, 0x00 },
        { 0x0E, 0x68 },
        { 0x13, 0xE8 },
        { 0x18, 0xC2 },
        { 0x1A, 0x00 }
    }
};

OOVPATable XOnline_1_0_5849[] =
{
    {
        (OOVPA*)&XNetStartup_1_0_5849,
        XTL::EmuXNetStartup,
        #ifdef _DEBUG_TRACE
        "EmuXNetStartup"
        #endif
    },
    {
        (OOVPA*)&WSAStartup_1_0_5849,
        XTL::EmuWSAStartup,
        #ifdef _DEBUG_TRACE
        "EmuWSAStartup"
        #endif
    },
};

uint32 XOnline_1_0_5849_SIZE = sizeof(XOnline_1_0_5849);
