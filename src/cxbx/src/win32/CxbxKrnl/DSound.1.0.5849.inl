// Generated from XDK 5849 dsound.lib (tools/oovpa/gen_oovpa.py); each
// signature verified unique across the hle_resolve probe image and every
// title XBE in-repo. This v1 table carries only the functions whose library
// code is DISTINCT: the thin per-setter wrappers (SetDistanceFactor /
// SetRolloffFactor / SetDopplerFactor / SetPosition / SetVelocity /
// SetI3DL2Listener / SetMixBinHeadroom / CommitDeferredSettings, and the
// DirectSoundCreateBuffer / DirectSoundCreateStream pair) are byte-identical
// to each other outside their relocated call target, so plain (offset, value)
// signatures cannot tell them apart -- distinguishing them needs XRef
// signatures (locate the distinct internal CDirectSound_* the wrapper calls,
// then match the wrapper's rel32 against it), the mechanism the 4627 table
// uses. The hle_resolve probe documents exactly which functions resolve.

// _DirectSoundCreate@12 (dsound.lib 5849, 71 bytes)
SOOVPA<8> DirectSoundCreate_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x0B, 0x0F },
        { 0x12, 0xE8 },
        { 0x1E, 0x45 },
        { 0x28, 0xC9 },
        { 0x32, 0x74 },
        { 0x3A, 0x15 },
        { 0x46, 0x00 }
    }
};

// _IDirectSound_CreateSoundBuffer@16 (dsound.lib 5849, 36 bytes)
SOOVPA<8> IDirectSound8_CreateSoundBuffer_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0x74 },
        { 0x14, 0xF8 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// _IDirectSound_SetOrientation@32 (dsound.lib 5849, 74 bytes)
SOOVPA<8> IDirectSound8_SetOrientation_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x0A, 0x45 },
        { 0x14, 0xC8 },
        { 0x1F, 0xF7 },
        { 0x29, 0x0C },
        { 0x34, 0x45 },
        { 0x3E, 0x1C },
        { 0x49, 0x00 }
    }
};

// _IDirectSound_DownloadEffectsImage@20 (dsound.lib 5849, 39 bytes)
SOOVPA<8> IDirectSound8_DownloadEffectsImage_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x05, 0x18 },
        { 0x0A, 0x75 },
        { 0x10, 0x10 },
        { 0x15, 0x75 },
        { 0x1B, 0x23 },
        { 0x1E, 0xE8 },
        { 0x26, 0x00 }
    }
};

OOVPATable DSound_1_0_5849[] =
{
    // DirectSoundCreate
    {
        (OOVPA*)&DirectSoundCreate_1_0_5849,
        XTL::EmuDirectSoundCreate,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreate"
        #endif
    },
    // IDirectSound8::CreateSoundBuffer
    {
        (OOVPA*)&IDirectSound8_CreateSoundBuffer_1_0_5849,
        XTL::EmuIDirectSound8_CreateSoundBuffer,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_CreateSoundBuffer"
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

uint32 DSound_1_0_5849_SIZE = sizeof(DSound_1_0_5849);
