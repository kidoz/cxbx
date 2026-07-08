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

// ?CreateSoundBuffer@CDirectSound@DirectSound@@QAGJPBU_DSBUFFERDESC@@PAPAUIDirectSoundBuffer@@PAUIUnknown@@@Z (dsound.lib, 156 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_CreateSoundBuffer_1_0_5849 =
{
    0, 8, XREF_DS5849_CREATESOUNDBUFFER, 0,
    {
        { 0x00, 0x53 },
        { 0x16, 0x74 },
        { 0x2C, 0xE8 },
        { 0x42, 0xF8 },
        { 0x58, 0xFF },
        { 0x6E, 0x83 },
        { 0x84, 0x08 },
        { 0x9B, 0x00 }
    }
};

// _DirectSoundCreateBuffer@8 (dsound.lib, 87 bytes; call@0x2F -> XREF_DS5849_CREATESOUNDBUFFER)
SOOVPA<9> DirectSoundCreateBuffer_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x2F, XREF_DS5849_CREATESOUNDBUFFER },
        { 0x00, 0x55 },
        { 0x0B, 0xE8 },
        { 0x17, 0xE8 },
        { 0x24, 0x10 },
        { 0x33, 0x8B },
        { 0x3D, 0x50 },
        { 0x49, 0x15 },
        { 0x56, 0x00 }
    }
};

// ?CreateSoundStream@CDirectSound@DirectSound@@QAGJPBU_DSSTREAMDESC@@PAPAUIDirectSoundStream@@PAUIUnknown@@@Z (dsound.lib, 145 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_CreateSoundStream_1_0_5849 =
{
    0, 8, XREF_DS5849_CREATESOUNDSTREAM, 0,
    {
        { 0x00, 0x53 },
        { 0x14, 0x3B },
        { 0x29, 0x62 },
        { 0x3C, 0xE8 },
        { 0x52, 0x0E },
        { 0x66, 0x7C },
        { 0x7B, 0xDB },
        { 0x90, 0x00 }
    }
};

// _DirectSoundCreateStream@8 (dsound.lib, 87 bytes; call@0x2F -> XREF_DS5849_CREATESOUNDSTREAM)
SOOVPA<9> DirectSoundCreateStream_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x2F, XREF_DS5849_CREATESOUNDSTREAM },
        { 0x00, 0x55 },
        { 0x0B, 0xE8 },
        { 0x17, 0xE8 },
        { 0x24, 0x10 },
        { 0x33, 0x8B },
        { 0x3D, 0x50 },
        { 0x49, 0x15 },
        { 0x56, 0x00 }
    }
};

// ?SetI3DL2Listener@CDirectSound@DirectSound@@QAGJPBU_DSI3DL2LISTENER@@K@Z (dsound.lib, 264 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_SetI3DL2Listener_1_0_5849 =
{
    0, 8, XREF_DS5849_SETI3DL2LISTENER, 0,
    {
        { 0x00, 0x56 },
        { 0x24, 0x05 },
        { 0x48, 0x4C },
        { 0x6D, 0x59 },
        { 0x91, 0x59 },
        { 0xB6, 0x24 },
        { 0xDA, 0x08 },
        { 0xFC, 0x15 }
    }
};

// _IDirectSound_SetI3DL2Listener@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS5849_SETI3DL2LISTENER)
SOOVPA<9> IDirectSound8_SetI3DL2Listener_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS5849_SETI3DL2LISTENER },
        { 0x00, 0x8B },
        { 0x04, 0xFF },
        { 0x08, 0x8B },
        { 0x0D, 0x0C },
        { 0x11, 0xF7 },
        { 0x16, 0xC8 },
        { 0x18, 0xE8 },
        { 0x1F, 0x00 }
    }
};

// ?SetMixBinHeadroom@CDirectSound@DirectSound@@QAGJKK@Z (dsound.lib, 95 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_SetMixBinHeadroom_1_0_5849 =
{
    0, 8, XREF_DS5849_SETMIXBINHEADROOM, 0,
    {
        { 0x00, 0x56 },
        { 0x0D, 0x0F },
        { 0x1B, 0xFF },
        { 0x28, 0x8B },
        { 0x35, 0x5C },
        { 0x41, 0xE8 },
        { 0x51, 0xFF },
        { 0x5E, 0x00 }
    }
};

// _IDirectSound_SetMixBinHeadroom@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS5849_SETMIXBINHEADROOM)
SOOVPA<9> IDirectSound8_SetMixBinHeadroom_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS5849_SETMIXBINHEADROOM },
        { 0x00, 0x8B },
        { 0x04, 0xFF },
        { 0x08, 0x8B },
        { 0x0D, 0x0C },
        { 0x11, 0xF7 },
        { 0x16, 0xC8 },
        { 0x18, 0xE8 },
        { 0x1F, 0x00 }
    }
};

// ?SetDistanceFactor@CDirectSound@DirectSound@@QAGJMK@Z (dsound.lib, 95 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_SetDistanceFactor_1_0_5849 =
{
    0, 8, XREF_DS5849_SETDISTANCEFACTOR, 0,
    {
        { 0x00, 0x56 },
        { 0x0D, 0x0F },
        { 0x1B, 0xFF },
        { 0x28, 0x8B },
        { 0x35, 0x68 },
        { 0x43, 0x06 },
        { 0x4E, 0x68 },
        { 0x5E, 0x00 }
    }
};

// _IDirectSound_SetDistanceFactor@12 (dsound.lib, 36 bytes; call@0x1D -> XREF_DS5849_SETDISTANCEFACTOR)
SOOVPA<9> IDirectSound8_SetDistanceFactor_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x1D, XREF_DS5849_SETDISTANCEFACTOR },
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0xD9 },
        { 0x14, 0xF8 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// ?SetRolloffFactor@CDirectSound@DirectSound@@QAGJMK@Z (dsound.lib, 95 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_SetRolloffFactor_1_0_5849 =
{
    0, 8, XREF_DS5849_SETROLLOFFFACTOR, 0,
    {
        { 0x00, 0x56 },
        { 0x0D, 0x0F },
        { 0x1B, 0xFF },
        { 0x28, 0x8B },
        { 0x35, 0x6C },
        { 0x43, 0x06 },
        { 0x4E, 0x68 },
        { 0x5E, 0x00 }
    }
};

// _IDirectSound_SetRolloffFactor@12 (dsound.lib, 36 bytes; call@0x1D -> XREF_DS5849_SETROLLOFFFACTOR)
SOOVPA<9> IDirectSound8_SetRolloffFactor_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x1D, XREF_DS5849_SETROLLOFFFACTOR },
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0xD9 },
        { 0x14, 0xF8 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// ?SetDopplerFactor@CDirectSound@DirectSound@@QAGJMK@Z (dsound.lib, 95 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_SetDopplerFactor_1_0_5849 =
{
    0, 8, XREF_DS5849_SETDOPPLERFACTOR, 0,
    {
        { 0x00, 0x56 },
        { 0x0D, 0x0F },
        { 0x1B, 0xFF },
        { 0x28, 0x8B },
        { 0x35, 0x70 },
        { 0x43, 0x06 },
        { 0x4E, 0x68 },
        { 0x5E, 0x00 }
    }
};

// _IDirectSound_SetDopplerFactor@12 (dsound.lib, 36 bytes; call@0x1D -> XREF_DS5849_SETDOPPLERFACTOR)
SOOVPA<9> IDirectSound8_SetDopplerFactor_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x1D, XREF_DS5849_SETDOPPLERFACTOR },
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0xD9 },
        { 0x14, 0xF8 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// ?SetPosition@CDirectSound@DirectSound@@QAGJMMMK@Z (dsound.lib, 115 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_SetPosition_1_0_5849 =
{
    0, 8, XREF_DS5849_SETPOSITION, 0,
    {
        { 0x00, 0x55 },
        { 0x10, 0x0F },
        { 0x1F, 0x15 },
        { 0x30, 0x08 },
        { 0x41, 0x3C },
        { 0x51, 0x45 },
        { 0x61, 0x68 },
        { 0x72, 0x00 }
    }
};

// _IDirectSound_SetPosition@20 (dsound.lib, 53 bytes; call@0x2D -> XREF_DS5849_SETPOSITION)
SOOVPA<9> IDirectSound8_SetPosition_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x2D, XREF_DS5849_SETPOSITION },
        { 0x00, 0x55 },
        { 0x07, 0x45 },
        { 0x0E, 0x0C },
        { 0x16, 0x45 },
        { 0x1D, 0xD9 },
        { 0x25, 0xC9 },
        { 0x2C, 0xE8 },
        { 0x34, 0x00 }
    }
};

// ?SetVelocity@CDirectSound@DirectSound@@QAGJMMMK@Z (dsound.lib, 115 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_SetVelocity_1_0_5849 =
{
    0, 8, XREF_DS5849_SETVELOCITY, 0,
    {
        { 0x00, 0x55 },
        { 0x10, 0x0F },
        { 0x1F, 0x15 },
        { 0x30, 0x08 },
        { 0x41, 0x48 },
        { 0x51, 0x45 },
        { 0x61, 0x68 },
        { 0x72, 0x00 }
    }
};

// _IDirectSound_SetVelocity@20 (dsound.lib, 53 bytes; call@0x2D -> XREF_DS5849_SETVELOCITY)
SOOVPA<9> IDirectSound8_SetVelocity_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x2D, XREF_DS5849_SETVELOCITY },
        { 0x00, 0x55 },
        { 0x07, 0x45 },
        { 0x0E, 0x0C },
        { 0x16, 0x45 },
        { 0x1D, 0xD9 },
        { 0x25, 0xC9 },
        { 0x2C, 0xE8 },
        { 0x34, 0x00 }
    }
};

// ?CommitDeferredSettings@CDirectSound@DirectSound@@QAGJXZ (dsound.lib, 154 bytes)
// XRef-save signature: locates the internal so the identical
// thin wrappers below can be told apart by their call target.
SOOVPA<8> CDirectSound_CommitDeferredSettingsI_1_0_5849 =
{
    0, 8, XREF_DS5849_COMMITDEFERRED, 0,
    {
        { 0x00, 0x55 },
        { 0x15, 0xFC },
        { 0x2B, 0x80 },
        { 0x41, 0x83 },
        { 0x57, 0xEB },
        { 0x6D, 0x89 },
        { 0x83, 0xFC },
        { 0x99, 0x00 }
    }
};

// _IDirectSound_CommitDeferredSettings@4 (dsound.lib, 24 bytes; call@0x11 -> XREF_DS5849_COMMITDEFERRED)
SOOVPA<9> CDirectSound_CommitDeferredSettings_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x11, XREF_DS5849_COMMITDEFERRED },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x06, 0x83 },
        { 0x09, 0xF7 },
        { 0x0D, 0x23 },
        { 0x10, 0xE8 },
        { 0x15, 0xC2 },
        { 0x17, 0x00 }
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
    // CDirectSound::CreateSoundBuffer (XREF save)
    {
        (OOVPA*)&CDirectSound_CreateSoundBuffer_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::CreateSoundBuffer (XREF)"
        #endif
    },
    // DirectSoundCreateBuffer
    {
        (OOVPA*)&DirectSoundCreateBuffer_1_0_5849,
        XTL::EmuDirectSoundCreateBuffer,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreateBuffer"
        #endif
    },
    // CDirectSound::CreateSoundStream (XREF save)
    {
        (OOVPA*)&CDirectSound_CreateSoundStream_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::CreateSoundStream (XREF)"
        #endif
    },
    // DirectSoundCreateStream
    {
        (OOVPA*)&DirectSoundCreateStream_1_0_5849,
        XTL::EmuDirectSoundCreateStream,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreateStream"
        #endif
    },
    // CDirectSound::SetI3DL2Listener (XREF save)
    {
        (OOVPA*)&CDirectSound_SetI3DL2Listener_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetI3DL2Listener (XREF)"
        #endif
    },
    // IDirectSound8::SetI3DL2Listener
    {
        (OOVPA*)&IDirectSound8_SetI3DL2Listener_1_0_5849,
        XTL::EmuIDirectSound8_SetI3DL2Listener,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetI3DL2Listener"
        #endif
    },
    // CDirectSound::SetMixBinHeadroom (XREF save)
    {
        (OOVPA*)&CDirectSound_SetMixBinHeadroom_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetMixBinHeadroom (XREF)"
        #endif
    },
    // IDirectSound8::SetMixBinHeadroom
    {
        (OOVPA*)&IDirectSound8_SetMixBinHeadroom_1_0_5849,
        XTL::EmuIDirectSound8_SetMixBinHeadroom,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetMixBinHeadroom"
        #endif
    },
    // CDirectSound::SetDistanceFactor (XREF save)
    {
        (OOVPA*)&CDirectSound_SetDistanceFactor_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetDistanceFactor (XREF)"
        #endif
    },
    // IDirectSound8::SetDistanceFactor
    {
        (OOVPA*)&IDirectSound8_SetDistanceFactor_1_0_5849,
        XTL::EmuIDirectSound8_SetDistanceFactor,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetDistanceFactor"
        #endif
    },
    // CDirectSound::SetRolloffFactor (XREF save)
    {
        (OOVPA*)&CDirectSound_SetRolloffFactor_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetRolloffFactor (XREF)"
        #endif
    },
    // IDirectSound8::SetRolloffFactor
    {
        (OOVPA*)&IDirectSound8_SetRolloffFactor_1_0_5849,
        XTL::EmuIDirectSound8_SetRolloffFactor,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetRolloffFactor"
        #endif
    },
    // CDirectSound::SetDopplerFactor (XREF save)
    {
        (OOVPA*)&CDirectSound_SetDopplerFactor_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetDopplerFactor (XREF)"
        #endif
    },
    // IDirectSound8::SetDopplerFactor
    {
        (OOVPA*)&IDirectSound8_SetDopplerFactor_1_0_5849,
        XTL::EmuIDirectSound8_SetDopplerFactor,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetDopplerFactor"
        #endif
    },
    // CDirectSound::SetPosition (XREF save)
    {
        (OOVPA*)&CDirectSound_SetPosition_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetPosition (XREF)"
        #endif
    },
    // IDirectSound8::SetPosition
    {
        (OOVPA*)&IDirectSound8_SetPosition_1_0_5849,
        XTL::EmuIDirectSound8_SetPosition,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetPosition"
        #endif
    },
    // CDirectSound::SetVelocity (XREF save)
    {
        (OOVPA*)&CDirectSound_SetVelocity_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SetVelocity (XREF)"
        #endif
    },
    // IDirectSound8::SetVelocity
    {
        (OOVPA*)&IDirectSound8_SetVelocity_1_0_5849,
        XTL::EmuIDirectSound8_SetVelocity,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetVelocity"
        #endif
    },
    // CDirectSound::CommitDeferredSettings internal (XREF save)
    {
        (OOVPA*)&CDirectSound_CommitDeferredSettingsI_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::CommitDeferredSettings (XREF)"
        #endif
    },
    // IDirectSound::CommitDeferredSettings
    {
        (OOVPA*)&CDirectSound_CommitDeferredSettings_1_0_5849,
        XTL::EmuCDirectSound_CommitDeferredSettings,
        #ifdef _DEBUG_TRACE
        "EmuCDirectSound_CommitDeferredSettings"
        #endif
    },
};

uint32 DSound_1_0_5849_SIZE = sizeof(DSound_1_0_5849);
