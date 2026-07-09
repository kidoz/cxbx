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

// ?SetBufferData@CDirectSoundBuffer@DirectSound@@QAGJPAXK@Z (dsound.lib, 174 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_SetBufferData_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_SETBUFFERDATA, 0,
    {
        { 0x00, 0x55 },
        { 0x18, 0x45 },
        { 0x31, 0x3B },
        { 0x4A, 0x08 },
        { 0x62, 0x75 },
        { 0x79, 0xE8 },
        { 0x94, 0x83 },
        { 0xAD, 0x00 }
    }
};

// _IDirectSoundBuffer_SetBufferData@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS5849_BUF_SETBUFFERDATA)
SOOVPA<9> IDirectSoundBuffer8_SetBufferData_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS5849_BUF_SETBUFFERDATA },
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

// ?Play@CMcpxBuffer@DirectSound@@QAEJK@Z (dsound.lib, 176 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_PlayT_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_PLAY_T, 0,
    {
        { 0x00, 0x53 },
        { 0x19, 0x04 },
        { 0x32, 0xF6 },
        { 0x4B, 0x00 },
        { 0x64, 0x12 },
        { 0x7D, 0x53 },
        { 0x96, 0x00 },
        { 0xAF, 0x00 }
    }
};

// ?Play@CDirectSoundBuffer@DirectSound@@QAGJKKK@Z (dsound.lib, 81 bytes; call@0x35 ?Play@CMcpxBuffer@DirectSound@@QAEJK@Z -> XREF_DS5849_BUF_PLAY_T)
// XRef chain level: saved to XREF_DS5849_BUF_PLAY, discriminated by callee.
SOOVPA<9> CDirectSoundBuffer_Play_1_0_5849 =
{
    0, 9, XREF_DS5849_BUF_PLAY, 1,
    {
        { 0x35, XREF_DS5849_BUF_PLAY_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x22, 0x05 },
        { 0x2D, 0x48 },
        { 0x39, 0x85 },
        { 0x44, 0xFF },
        { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_Play@16 (dsound.lib, 36 bytes; call@0x1D -> XREF_DS5849_BUF_PLAY)
SOOVPA<9> IDirectSoundBuffer8_Play_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x1D, XREF_DS5849_BUF_PLAY },
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0x74 },
        { 0x14, 0xE4 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// ?Stop@CDirectSoundBuffer@DirectSound@@QAGJXZ (dsound.lib, 79 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_Stop_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_STOP, 0,
    {
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x8B },
        { 0x37, 0x85 },
        { 0x42, 0xFF },
        { 0x4E, 0x00 }
    }
};

// _IDirectSoundBuffer_Stop@4 (dsound.lib, 24 bytes; call@0x11 -> XREF_DS5849_BUF_STOP)
SOOVPA<9> IDirectSoundBuffer8_Stop_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x11, XREF_DS5849_BUF_STOP },
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

// ?SetPlayRegion@CDirectSoundBuffer@DirectSound@@QAGJKK@Z (dsound.lib, 128 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_SetPlayRegion_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_SETPLAYREGION, 0,
    {
        { 0x00, 0x55 },
        { 0x12, 0xF8 },
        { 0x24, 0xB8 },
        { 0x36, 0x56 },
        { 0x48, 0x00 },
        { 0x5A, 0xE8 },
        { 0x6C, 0x0B },
        { 0x7F, 0x00 }
    }
};

// _IDirectSoundBuffer_SetPlayRegion@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS5849_BUF_SETPLAYREGION)
SOOVPA<9> IDirectSoundBuffer8_SetPlayRegion_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS5849_BUF_SETPLAYREGION },
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

// ?SetLoopRegion@CDirectSoundBuffer@DirectSound@@QAGJKK@Z (dsound.lib, 133 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_SetLoopRegion_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_SETLOOPREGION, 0,
    {
        { 0x00, 0x55 },
        { 0x12, 0xF8 },
        { 0x25, 0x05 },
        { 0x38, 0x75 },
        { 0x4B, 0x00 },
        { 0x5E, 0x89 },
        { 0x71, 0x0B },
        { 0x84, 0x00 }
    }
};

// _IDirectSoundBuffer_SetLoopRegion@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS5849_BUF_SETLOOPREGION)
SOOVPA<9> IDirectSoundBuffer8_SetLoopRegion_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS5849_BUF_SETLOOPREGION },
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

// ?SetVolume@CDirectSoundVoice@DirectSound@@QAGJJ@Z (dsound.lib, 28 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_SetVolumeT_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_SETVOLUME_T, 0,
    {
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x8B },
        { 0x0B, 0x2B },
        { 0x0F, 0x50 },
        { 0x13, 0x0C },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetVolume@CDirectSoundBuffer@DirectSound@@QAGJJ@Z (dsound.lib, 78 bytes; call@0x32 ?SetVolume@CDirectSoundVoice@DirectSound@@QAGJJ@Z -> XREF_DS5849_BUF_SETVOLUME_T)
// XRef chain level: saved to XREF_DS5849_BUF_SETVOLUME, discriminated by callee.
SOOVPA<9> CDirectSoundBuffer_SetVolume_1_0_5849 =
{
    0, 9, XREF_DS5849_BUF_SETVOLUME, 1,
    {
        { 0x32, XREF_DS5849_BUF_SETVOLUME_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x10 },
        { 0x37, 0xF6 },
        { 0x42, 0x15 },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetVolume@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS5849_BUF_SETVOLUME)
SOOVPA<9> IDirectSoundBuffer8_SetVolume_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS5849_BUF_SETVOLUME },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetCurrentPosition@CMcpxBuffer@DirectSound@@QAEJK@Z (dsound.lib, 246 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_SetCurrentPositionT_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_SETCURRENTPOS_T, 0,
    {
        { 0x00, 0x55 },
        { 0x23, 0xB7 },
        { 0x46, 0x03 },
        { 0x6A, 0x85 },
        { 0x8C, 0xC1 },
        { 0xAF, 0x05 },
        { 0xD2, 0xD2 },
        { 0xF5, 0x00 }
    }
};

// ?SetCurrentPosition@CDirectSoundBuffer@DirectSound@@QAGJK@Z (dsound.lib, 81 bytes; call@0x35 ?SetCurrentPosition@CMcpxBuffer@DirectSound@@QAEJK@Z -> XREF_DS5849_BUF_SETCURRENTPOS_T)
// XRef chain level: saved to XREF_DS5849_BUF_SETCURRENTPOS, discriminated by callee.
SOOVPA<9> CDirectSoundBuffer_SetCurrentPosition_1_0_5849 =
{
    0, 9, XREF_DS5849_BUF_SETCURRENTPOS, 1,
    {
        { 0x35, XREF_DS5849_BUF_SETCURRENTPOS_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x22, 0x05 },
        { 0x2D, 0x48 },
        { 0x39, 0x85 },
        { 0x44, 0xFF },
        { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_SetCurrentPosition@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS5849_BUF_SETCURRENTPOS)
SOOVPA<9> IDirectSoundBuffer8_SetCurrentPosition_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS5849_BUF_SETCURRENTPOS },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?GetCurrentPosition@CDirectSoundBuffer@DirectSound@@QAGJPAK0@Z (dsound.lib, 85 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_GetCurrentPosition_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_GETCURRENTPOS, 0,
    {
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x24, 0x00 },
        { 0x30, 0xFF },
        { 0x3D, 0x85 },
        { 0x48, 0xFF },
        { 0x54, 0x00 }
    }
};

// _IDirectSoundBuffer_GetCurrentPosition@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS5849_BUF_GETCURRENTPOS)
SOOVPA<9> IDirectSoundBuffer8_GetCurrentPosition_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS5849_BUF_GETCURRENTPOS },
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

// ?Lock@CDirectSoundBuffer@DirectSound@@QAGJKKPAPAXPAK01K@Z (dsound.lib, 226 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_Lock_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_LOCK, 0,
    {
        { 0x00, 0x55 },
        { 0x20, 0x74 },
        { 0x40, 0x12 },
        { 0x60, 0x00 },
        { 0x80, 0x00 },
        { 0xA0, 0x18 },
        { 0xC0, 0x06 },
        { 0xE1, 0x00 }
    }
};

// _IDirectSoundBuffer_Lock@32 (dsound.lib, 48 bytes; call@0x28 -> XREF_DS5849_BUF_LOCK)
SOOVPA<9> IDirectSoundBuffer8_Lock_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x28, XREF_DS5849_BUF_LOCK },
        { 0x00, 0x55 },
        { 0x06, 0x8B },
        { 0x0D, 0xC8 },
        { 0x14, 0xFF },
        { 0x1A, 0x75 },
        { 0x21, 0x23 },
        { 0x27, 0xE8 },
        { 0x2F, 0x00 }
    }
};

// ?SetMixBins@CDirectSoundVoice@DirectSound@@QAGJPBU_DSMIXBINS@@@Z (dsound.lib, 29 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_SetMixBinsT_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_SETMIXBINS_T, 0,
    {
        { 0x00, 0x56 },
        { 0x04, 0x08 },
        { 0x08, 0x0C },
        { 0x0C, 0xE8 },
        { 0x11, 0x8B },
        { 0x14, 0xE8 },
        { 0x19, 0x5E },
        { 0x1C, 0x00 }
    }
};

// ?SetMixBins@CDirectSoundBuffer@DirectSound@@QAGJPBU_DSMIXBINS@@@Z (dsound.lib, 78 bytes; call@0x32 ?SetMixBins@CDirectSoundVoice@DirectSound@@QAGJPBU_DSMIXBINS@@@Z -> XREF_DS5849_BUF_SETMIXBINS_T)
// XRef chain level: saved to XREF_DS5849_BUF_SETMIXBINS, discriminated by callee.
SOOVPA<9> CDirectSoundBuffer_SetMixBins_1_0_5849 =
{
    0, 9, XREF_DS5849_BUF_SETMIXBINS, 1,
    {
        { 0x32, XREF_DS5849_BUF_SETMIXBINS_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x10 },
        { 0x37, 0xF6 },
        { 0x42, 0x15 },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetMixBins@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS5849_BUF_SETMIXBINS)
SOOVPA<9> IDirectSoundBuffer8_SetMixBins_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS5849_BUF_SETMIXBINS },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?GetStatus@CMcpxBuffer@DirectSound@@QAEJPAK@Z (dsound.lib, 66 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundBuffer_GetStatusT_1_0_5849 =
{
    0, 8, XREF_DS5849_BUF_GETSTATUS_T, 0,
    {
        { 0x00, 0x0F },
        { 0x09, 0x80 },
        { 0x12, 0x66 },
        { 0x1B, 0xC2 },
        { 0x25, 0x09 },
        { 0x2E, 0x23 },
        { 0x37, 0xD8 },
        { 0x41, 0x00 }
    }
};

// ?GetStatus@CDirectSoundBuffer@DirectSound@@QAGJPAK@Z (dsound.lib, 81 bytes; call@0x35 ?GetStatus@CMcpxBuffer@DirectSound@@QAEJPAK@Z -> XREF_DS5849_BUF_GETSTATUS_T)
// XRef chain level: saved to XREF_DS5849_BUF_GETSTATUS, discriminated by callee.
SOOVPA<9> CDirectSoundBuffer_GetStatus_1_0_5849 =
{
    0, 9, XREF_DS5849_BUF_GETSTATUS, 1,
    {
        { 0x35, XREF_DS5849_BUF_GETSTATUS_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x22, 0x05 },
        { 0x2D, 0x48 },
        { 0x39, 0x85 },
        { 0x44, 0xFF },
        { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_GetStatus@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS5849_BUF_GETSTATUS)
SOOVPA<9> IDirectSoundBuffer8_GetStatus_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS5849_BUF_GETSTATUS },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?DoWork@CDirectSound@DirectSound@@QAGXXZ (dsound.lib, 49 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSound_DoWork_1_0_5849 =
{
    0, 8, XREF_DS5849_DOWORK, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x14, 0x24 },
        { 0x19, 0xE8 },
        { 0x22, 0x68 },
        { 0x28, 0x15 },
        { 0x30, 0x00 }
    }
};

// _DirectSoundDoWork@0 (dsound.lib, 41 bytes; call@0x14 -> XREF_DS5849_DOWORK)
SOOVPA<9> DirectSoundDoWork_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x14, XREF_DS5849_DOWORK },
        { 0x00, 0x56 },
        { 0x06, 0x0F },
        { 0x09, 0xA1 },
        { 0x11, 0x06 },
        { 0x18, 0x85 },
        { 0x1C, 0x0B },
        { 0x22, 0xFF },
        { 0x28, 0xC3 }
    }
};

// ?FlushEx@CDirectSoundStream@DirectSound@@QAGJ_JK@Z (dsound.lib, 106 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CDirectSoundStream_FlushExI_1_0_5849 =
{
    0, 8, XREF_DS5849_STR_FLUSHEX, 0,
    {
        { 0x00, 0x55 },
        { 0x0F, 0x00 },
        { 0x1E, 0xFF },
        { 0x2D, 0x14 },
        { 0x3C, 0x75 },
        { 0x4B, 0x00 },
        { 0x5C, 0xFF },
        { 0x69, 0x00 }
    }
};

// _IDirectSoundStream_FlushEx@16 (dsound.lib, 24 bytes; call@0x11 -> XREF_DS5849_STR_FLUSHEX)
SOOVPA<9> IDirectSoundStream_FlushEx_1_0_5849 =
{
    0, 9, -1, 1,
    {
        { 0x11, XREF_DS5849_STR_FLUSHEX },
        { 0x00, 0xFF },
        { 0x03, 0x10 },
        { 0x06, 0x24 },
        { 0x09, 0x74 },
        { 0x0D, 0x74 },
        { 0x10, 0xE8 },
        { 0x15, 0xC2 },
        { 0x17, 0x00 }
    }
};

// _XAudioDownloadEffectsImage@16 (dsound.lib, 514 bytes)
SOOVPA<8> XAudioDownloadEffectsImage_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x24, 0x0A },
        { 0x48, 0xFF },
        { 0x6D, 0xF3 },
        { 0x90, 0xE8 },
        { 0xB6, 0xFC },
        { 0xDA, 0x00 },
        { 0xFF, 0x85 }
    }
};
// _DirectSoundUseFullHRTF@0 (dsound.lib, 31 bytes)
SOOVPA<8> DirectSoundUseFullHRTF_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x0F },
        { 0x08, 0xF0 },
        { 0x0E, 0x85 },
        { 0x11, 0x74 },
        { 0x13, 0x68 },
        { 0x19, 0x15 },
        { 0x1E, 0xC3 }
    }
};
// _IDirectSound_Release@4 (dsound.lib 5849, 22 bytes). The Xbox interface
// thunk adjusts the object by -8 and dispatches Release through the native
// vtable; unpatched it runs against the raw host IDirectSound8 Cxbx hands out
// and faults. XMV video decode releases a DirectSound every frame. This -8
// this-offset is unique to IDirectSound (buffers use -0x1C), so the byte
// pattern locates it unambiguously.
SOOVPA<8> IDirectSound_Release_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x04, 0x8D },
        { 0x06, 0xF8 },
        { 0x07, 0xF7 },
        { 0x0B, 0x23 },
        { 0x10, 0xFF },
        { 0x12, 0x08 },
        { 0x13, 0xC2 }
    }
};

// _IDirectSound_SynchPlayback@4 (dsound.lib 5849, 24 bytes). Waits for the APU
// voice queue to reach a sync point; unpatched it calls a native helper that
// walks the Xbox object internals and faults on the host object. XMV calls it
// every frame for A/V pacing -- a no-op is correct for the host mixer.
SOOVPA<8> IDirectSound_SynchPlayback_1_0_5849 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x04, 0x8B },
        { 0x05, 0xC8 },
        { 0x06, 0x83 },
        { 0x08, 0xF8 },
        { 0x09, 0xF7 },
        { 0x0F, 0x51 },
        { 0x10, 0xE8 }
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
    // IDirectSound8::Release
    {
        (OOVPA*)&IDirectSound_Release_1_0_5849,
        XTL::EmuIDirectSound8_Release,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_Release"
        #endif
    },
    // IDirectSound8::SynchPlayback
    {
        (OOVPA*)&IDirectSound_SynchPlayback_1_0_5849,
        XTL::EmuIDirectSound8_SynchPlayback,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SynchPlayback"
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
    // CDirectSoundBuffer::SetBufferData (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetBufferData_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetBufferData (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetBufferData
    {
        (OOVPA*)&IDirectSoundBuffer8_SetBufferData_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetBufferData,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetBufferData"
        #endif
    },
    // CDirectSoundBuffer::Play chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_PlayT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Play leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::Play (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_Play_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Play (XREF)"
        #endif
    },
    // IDirectSoundBuffer::Play
    {
        (OOVPA*)&IDirectSoundBuffer8_Play_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_Play,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Play"
        #endif
    },
    // CDirectSoundBuffer::Stop (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_Stop_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Stop (XREF)"
        #endif
    },
    // IDirectSoundBuffer::Stop
    {
        (OOVPA*)&IDirectSoundBuffer8_Stop_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_Stop,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Stop"
        #endif
    },
    // CDirectSoundBuffer::SetPlayRegion (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetPlayRegion_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetPlayRegion (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetPlayRegion
    {
        (OOVPA*)&IDirectSoundBuffer8_SetPlayRegion_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetPlayRegion,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetPlayRegion"
        #endif
    },
    // CDirectSoundBuffer::SetLoopRegion (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetLoopRegion_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetLoopRegion (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetLoopRegion
    {
        (OOVPA*)&IDirectSoundBuffer8_SetLoopRegion_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetLoopRegion,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetLoopRegion"
        #endif
    },
    // CDirectSoundBuffer::SetVolume chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetVolumeT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetVolume leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetVolume (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetVolume_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetVolume (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetVolume
    {
        (OOVPA*)&IDirectSoundBuffer8_SetVolume_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetVolume,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetVolume"
        #endif
    },
    // CDirectSoundBuffer::SetCurrentPosition chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetCurrentPositionT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetCurrentPosition leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetCurrentPosition (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetCurrentPosition_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetCurrentPosition (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetCurrentPosition
    {
        (OOVPA*)&IDirectSoundBuffer8_SetCurrentPosition_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetCurrentPosition,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetCurrentPosition"
        #endif
    },
    // CDirectSoundBuffer::GetCurrentPosition (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_GetCurrentPosition_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::GetCurrentPosition (XREF)"
        #endif
    },
    // IDirectSoundBuffer::GetCurrentPosition
    {
        (OOVPA*)&IDirectSoundBuffer8_GetCurrentPosition_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_GetCurrentPosition,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_GetCurrentPosition"
        #endif
    },
    // CDirectSoundBuffer::Lock (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_Lock_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Lock (XREF)"
        #endif
    },
    // IDirectSoundBuffer::Lock
    {
        (OOVPA*)&IDirectSoundBuffer8_Lock_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_Lock,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Lock"
        #endif
    },
    // CDirectSoundBuffer::SetMixBins chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetMixBinsT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetMixBins leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetMixBins (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetMixBins_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetMixBins (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetMixBins
    {
        (OOVPA*)&IDirectSoundBuffer8_SetMixBins_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_SetMixBins,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetMixBins"
        #endif
    },
    // CDirectSoundBuffer::GetStatus chain leaf (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_GetStatusT_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::GetStatus leaf (XREF)"
        #endif
    },
    // CDirectSoundBuffer::GetStatus (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_GetStatus_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::GetStatus (XREF)"
        #endif
    },
    // IDirectSoundBuffer::GetStatus
    {
        (OOVPA*)&IDirectSoundBuffer8_GetStatus_1_0_5849,
        XTL::EmuIDirectSoundBuffer8_GetStatus,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_GetStatus"
        #endif
    },
    // CDirectSound::DoWork (XREF save)
    {
        (OOVPA*)&CDirectSound_DoWork_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::DoWork (XREF)"
        #endif
    },
    // DirectSoundDoWork
    {
        (OOVPA*)&DirectSoundDoWork_1_0_5849,
        XTL::EmuDirectSoundDoWork,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundDoWork"
        #endif
    },
    // CDirectSoundStream::FlushEx (XREF save)
    {
        (OOVPA*)&CDirectSoundStream_FlushExI_1_0_5849, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundStream::FlushEx (XREF)"
        #endif
    },
    // IDirectSoundStream::FlushEx
    {
        (OOVPA*)&IDirectSoundStream_FlushEx_1_0_5849,
        XTL::EmuIDirectSoundStream_FlushEx,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_FlushEx"
        #endif
    },
    // XAudioDownloadEffectsImage
    {
        (OOVPA*)&XAudioDownloadEffectsImage_1_0_5849,
        XTL::EmuXAudioDownloadEffectsImage,
        #ifdef _DEBUG_TRACE
        "EmuXAudioDownloadEffectsImage"
        #endif
    },
    // DirectSoundUseFullHRTF
    {
        (OOVPA*)&DirectSoundUseFullHRTF_1_0_5849,
        XTL::EmuDirectSoundUseFullHRTF,
        #ifdef _DEBUG_TRACE
        "EmuDirectSoundUseFullHRTF"
        #endif
    },
};

uint32 DSound_1_0_5849_SIZE = sizeof(DSound_1_0_5849);
