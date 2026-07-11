// DSound Version 1.0.3911 OOVPA table.
// Generated from XDK 3911 dsound.lib via reuse-then-generate.
// 2 reused, 2 fresh, 23 skipped.

// Fresh signature definitions
SOOVPA<8> DirectSoundCreate_1_0_3911 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x17, 0x3B },
        { 0x2C, 0x8B },
        { 0x41, 0xE8 },
        { 0x59, 0x80 },
        { 0x70, 0x83 },
        { 0x86, 0x85 },
        { 0x9D, 0x00 }
    }
};


SOOVPA<8> IDirectSoundStream_SetHeadroom_1_0_3911 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0xFF },
        { 0x02, 0x24 },
        { 0x05, 0x44 },
        { 0x08, 0x83 },
        { 0x0A, 0x04 },
        { 0x0C, 0xE8 },
        { 0x11, 0xC2 },
        { 0x13, 0x00 }
    }
};


OOVPATable DSound_1_0_3911[] =
{
    // DirectSoundCreate
    {
        (OOVPA*)&DirectSoundCreate_1_0_3911,
        XTL::EmuDirectSoundCreate,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreate"
        #endif
    },
    // IDirectSoundStream::SetHeadroom
    {
        (OOVPA*)&IDirectSoundStream_SetHeadroom_1_0_3911,
        XTL::EmuIDirectSoundStream_SetHeadroom,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_SetHeadroom"
        #endif
    },
    // IDirectSound8::SetOrientation
    {
        (OOVPA*)&IDirectSound8_SetOrientation_1_0_5849,
        XTL::EmuIDirectSound8_SetOrientation,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetOrientation"
        #endif
    },
    // IDirectSound8::DownloadEffectsImage
    {
        (OOVPA*)&IDirectSound8_DownloadEffectsImage_1_0_5849,
        XTL::EmuIDirectSound8_DownloadEffectsImage,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_DownloadEffectsImage"
        #endif
    },
};

uint32 DSound_1_0_3911_SIZE = sizeof(DSound_1_0_3911);
