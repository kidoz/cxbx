// Generated from XDK 5933 dsound.lib with tools/oovpa/gen_oovpa.py and
// verified unique in the NestopiaX 1.3 image. This table intentionally covers
// the coherent static-buffer lifecycle used by the emulator's PCM ring.

// _DirectSoundCreate@12 (dsound.lib, 71 bytes)
SOOVPA<12> DirectSoundCreate_1_0_5933 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x55 }, { 0x06, 0xE8 }, { 0x0C, 0xB6 },
        { 0x12, 0xE8 }, { 0x19, 0x85 }, { 0x1F, 0xFC },
        { 0x26, 0xD9 }, { 0x2C, 0x45 }, { 0x32, 0x74 },
        { 0x39, 0xFF }, { 0x3F, 0x8B }, { 0x46, 0x00 }
    }
};

// _IDirectSoundBuffer_Lock@32 (dsound.lib, 48 bytes)
SOOVPA<12> IDirectSoundBuffer8_Lock_1_0_5933 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x55 }, { 0x04, 0x75 }, { 0x08, 0x08 },
        { 0x0C, 0x8B }, { 0x11, 0x83 }, { 0x15, 0x75 },
        { 0x19, 0xFF }, { 0x1D, 0xC9 }, { 0x22, 0xC8 },
        { 0x26, 0x51 }, { 0x2C, 0x5D }, { 0x2F, 0x00 }
    }
};

// _IDirectSoundBuffer_Stop@4 (dsound.lib, 24 bytes)
SOOVPA<12> IDirectSoundBuffer8_Stop_1_0_5933 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x8B }, { 0x01, 0x44 }, { 0x02, 0x24 },
        { 0x04, 0x8B }, { 0x06, 0x83 }, { 0x08, 0xE4 },
        { 0x0A, 0xD9 }, { 0x0C, 0xC9 }, { 0x0E, 0xC8 },
        { 0x10, 0xE8 }, { 0x15, 0xC2 }, { 0x17, 0x00 }
    }
};

// _DirectSoundDoWork@0 (dsound.lib, 41 bytes)
SOOVPA<12> DirectSoundDoWork_1_0_5933 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x56 }, { 0x01, 0xE8 }, { 0x07, 0xB6 },
        { 0x09, 0xA1 }, { 0x0E, 0x85 }, { 0x12, 0x50 },
        { 0x13, 0xE8 }, { 0x19, 0xF6 }, { 0x1D, 0x68 },
        { 0x22, 0xFF }, { 0x23, 0x15 }, { 0x28, 0xC3 }
    }
};

// ?CreateSoundBuffer@CDirectSound@DirectSound@@QAGJPBU_DSBUFFERDESC@@PAPAUIDirectSoundBuffer@@PAUIUnknown@@@Z
SOOVPA<12> CDirectSound_CreateSoundBuffer_1_0_5933 =
{
    0, 12, XREF_DS5933_CREATESOUNDBUFFER, 0,
    {
        { 0x00, 0x53 }, { 0x0F, 0x0F }, { 0x1D, 0xFF },
        { 0x2A, 0x6A }, { 0x38, 0x24 }, { 0x46, 0xF7 },
        { 0x54, 0x07 }, { 0x62, 0x8B }, { 0x70, 0x1C },
        { 0x7E, 0x06 }, { 0x8E, 0xFF }, { 0x9B, 0x00 }
    }
};

// _DirectSoundCreateBuffer@8 (dsound.lib, 87 bytes;
// call@0x2E -> XREF_DS5933_CREATESOUNDBUFFER)
SOOVPA<16> DirectSoundCreateBuffer_1_0_5933 =
{
    0, 16, -1, 1,
    {
        { 0x2F, XREF_DS5933_CREATESOUNDBUFFER },
        { 0x00, 0x55 }, { 0x05, 0x65 }, { 0x0B, 0xE8 },
        { 0x11, 0xB6 }, { 0x16, 0x50 }, { 0x1C, 0x8B },
        { 0x22, 0xDB }, { 0x28, 0x75 }, { 0x2D, 0x56 },
        { 0x33, 0x8B }, { 0x39, 0x8B }, { 0x3F, 0x85 },
        { 0x43, 0x68 }, { 0x49, 0x15 }, { 0x56, 0x00 }
    }
};

// _IDirectSound_CreateSoundBuffer@16
SOOVPA<13> IDirectSound8_CreateSoundBuffer_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x1D, XREF_DS5933_CREATESOUNDBUFFER },
        { 0x00, 0xFF }, { 0x03, 0x10 }, { 0x06, 0x24 },
        { 0x09, 0x74 }, { 0x0C, 0x8B }, { 0x0F, 0x74 },
        { 0x13, 0xC0 }, { 0x16, 0xD9 }, { 0x19, 0x23 },
        { 0x1C, 0xE8 }, { 0x21, 0xC2 }, { 0x23, 0x00 }
    }
};

// ?SetMixBins@CDirectSoundVoice@DirectSound@@QAGJPBU_DSMIXBINS@@@Z
SOOVPA<12> CDirectSoundBuffer_SetMixBinsT_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_SETMIXBINS_T, 0,
    {
        { 0x00, 0x56 }, { 0x01, 0x8B }, { 0x02, 0x74 },
        { 0x03, 0x24 }, { 0x05, 0xFF }, { 0x07, 0x24 },
        { 0x0A, 0x4E }, { 0x0C, 0xE8 }, { 0x11, 0x8B },
        { 0x14, 0xE8 }, { 0x19, 0x5E }, { 0x1C, 0x00 }
    }
};

// ?SetMixBins@CDirectSoundBuffer@DirectSound@@QAGJPBU_DSMIXBINS@@@Z
SOOVPA<13> CDirectSoundBuffer_SetMixBins_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_SETMIXBINS, 1,
    {
        { 0x32, XREF_DS5933_BUF_SETMIXBINS_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x0E, 0xB6 },
        { 0x15, 0x0B }, { 0x1C, 0x15 }, { 0x23, 0x40 },
        { 0x2A, 0x74 }, { 0x31, 0xE8 }, { 0x38, 0x8B },
        { 0x41, 0xFF }, { 0x47, 0x8B }, { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetMixBins@8
SOOVPA<13> IDirectSoundBuffer8_SetMixBins_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5933_BUF_SETMIXBINS },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x04, 0xFF },
        { 0x07, 0x08 }, { 0x09, 0xC8 }, { 0x0C, 0xE4 },
        { 0x0E, 0xD9 }, { 0x11, 0x23 }, { 0x13, 0x51 },
        { 0x14, 0xE8 }, { 0x19, 0xC2 }, { 0x1B, 0x00 }
    }
};

// ?Play@CMcpxBuffer@DirectSound@@QAEJK@Z
SOOVPA<12> CDirectSoundBuffer_PlayT_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_PLAY_T, 0,
    {
        { 0x00, 0x53 }, { 0x0F, 0x20 }, { 0x1F, 0xCE },
        { 0x2F, 0x04 }, { 0x41, 0x8B }, { 0x4F, 0x09 },
        { 0x5F, 0x75 }, { 0x6F, 0xEB }, { 0x7F, 0x08 },
        { 0x8F, 0x80 }, { 0x9F, 0x57 }, { 0xAF, 0x00 }
    }
};

// ?Play@CDirectSoundBuffer@DirectSound@@QAGJKKK@Z
SOOVPA<13> CDirectSoundBuffer_Play_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_PLAY, 1,
    {
        { 0x35, XREF_DS5933_BUF_PLAY_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x0E, 0xB6 },
        { 0x15, 0x0B }, { 0x1C, 0x15 }, { 0x24, 0x00 },
        { 0x2B, 0x08 }, { 0x32, 0x24 }, { 0x3A, 0xF6 },
        { 0x3F, 0x68 }, { 0x4A, 0x8B }, { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_Play@16
SOOVPA<13> IDirectSoundBuffer8_Play_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x1D, XREF_DS5933_BUF_PLAY },
        { 0x00, 0xFF }, { 0x03, 0x10 }, { 0x06, 0x24 },
        { 0x09, 0x74 }, { 0x0C, 0x8B }, { 0x0F, 0x74 },
        { 0x13, 0xC0 }, { 0x16, 0xD9 }, { 0x19, 0x23 },
        { 0x1C, 0xE8 }, { 0x21, 0xC2 }, { 0x23, 0x00 }
    }
};

// ?SetVolume@CDirectSoundVoice@DirectSound@@QAGJJ@Z
SOOVPA<12> CDirectSoundBuffer_SetVolumeT_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_SETVOLUME_T, 0,
    {
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x04, 0x8B },
        { 0x07, 0x8B }, { 0x09, 0x24 }, { 0x0C, 0x50 },
        { 0x0E, 0x89 }, { 0x11, 0x8B }, { 0x13, 0x0C },
        { 0x14, 0xE8 }, { 0x19, 0xC2 }, { 0x1B, 0x00 }
    }
};

// ?SetVolume@CDirectSoundBuffer@DirectSound@@QAGJJ@Z
SOOVPA<13> CDirectSoundBuffer_SetVolume_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_SETVOLUME, 1,
    {
        { 0x32, XREF_DS5933_BUF_SETVOLUME_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x0E, 0xB6 },
        { 0x15, 0x0B }, { 0x1C, 0x15 }, { 0x23, 0x40 },
        { 0x2A, 0x74 }, { 0x31, 0xE8 }, { 0x38, 0x8B },
        { 0x41, 0xFF }, { 0x47, 0x8B }, { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetVolume@8
SOOVPA<13> IDirectSoundBuffer8_SetVolume_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5933_BUF_SETVOLUME },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x04, 0xFF },
        { 0x07, 0x08 }, { 0x09, 0xC8 }, { 0x0C, 0xE4 },
        { 0x0E, 0xD9 }, { 0x11, 0x23 }, { 0x13, 0x51 },
        { 0x14, 0xE8 }, { 0x19, 0xC2 }, { 0x1B, 0x00 }
    }
};

// ?GetCurrentPosition@CDirectSoundBuffer@DirectSound@@QAGJPAK0@Z
SOOVPA<12> CDirectSoundBuffer_GetCurrentPosition_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_GETCURRENTPOSITION, 0,
    {
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x0F, 0xF0 },
        { 0x16, 0x68 }, { 0x1C, 0x15 }, { 0x26, 0xEB },
        { 0x2D, 0x48 }, { 0x35, 0x74 }, { 0x3D, 0x85 },
        { 0x43, 0x68 }, { 0x4E, 0x8B }, { 0x54, 0x00 }
    }
};

// _IDirectSoundBuffer_GetCurrentPosition@12
SOOVPA<13> IDirectSoundBuffer8_GetCurrentPosition_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x19, XREF_DS5933_BUF_GETCURRENTPOSITION },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x05, 0x74 },
        { 0x08, 0x8B }, { 0x0B, 0x74 }, { 0x0E, 0x83 },
        { 0x10, 0xE4 }, { 0x13, 0x1B }, { 0x16, 0xC8 },
        { 0x18, 0xE8 }, { 0x1D, 0xC2 }, { 0x1F, 0x00 }
    }
};

// ?GetStatus@CMcpxBuffer@DirectSound@@QAEJPAK@Z
SOOVPA<12> CDirectSoundBuffer_GetStatusT_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_GETSTATUS_T, 0,
    {
        { 0x00, 0x0F }, { 0x05, 0xC8 }, { 0x0B, 0x03 },
        { 0x11, 0x04 }, { 0x17, 0x00 }, { 0x1D, 0xF6 },
        { 0x23, 0x19 }, { 0x29, 0xB9 }, { 0x2F, 0xC1 },
        { 0x35, 0x04 }, { 0x3B, 0x89 }, { 0x41, 0x00 }
    }
};

// ?GetStatus@CDirectSoundBuffer@DirectSound@@QAGJPAK@Z
SOOVPA<13> CDirectSoundBuffer_GetStatus_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_GETSTATUS, 1,
    {
        { 0x35, XREF_DS5933_BUF_GETSTATUS_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x0E, 0xB6 },
        { 0x15, 0x0B }, { 0x1C, 0x15 }, { 0x24, 0x00 },
        { 0x2B, 0x08 }, { 0x32, 0x24 }, { 0x3A, 0xF6 },
        { 0x3F, 0x68 }, { 0x4A, 0x8B }, { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_GetStatus@8
SOOVPA<13> IDirectSoundBuffer8_GetStatus_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5933_BUF_GETSTATUS },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x04, 0xFF },
        { 0x07, 0x08 }, { 0x09, 0xC8 }, { 0x0C, 0xE4 },
        { 0x0E, 0xD9 }, { 0x11, 0x23 }, { 0x13, 0x51 },
        { 0x14, 0xE8 }, { 0x19, 0xC2 }, { 0x1B, 0x00 }
    }
};

// ?SetBufferData@CDirectSoundBuffer@DirectSound@@QAGJPAXK@Z
SOOVPA<12> CDirectSoundBuffer_SetBufferData_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_SETBUFFERDATA, 0,
    {
        { 0x00, 0x55 }, { 0x0F, 0x39 }, { 0x1E, 0xE8 },
        { 0x2F, 0x74 }, { 0x40, 0xB8 }, { 0x4E, 0x3B },
        { 0x5E, 0x00 }, { 0x6E, 0x85 }, { 0x7E, 0x8B },
        { 0x8D, 0xE8 }, { 0x9B, 0x68 }, { 0xAD, 0x00 }
    }
};

// _IDirectSoundBuffer_SetBufferData@12
SOOVPA<13> IDirectSoundBuffer8_SetBufferData_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x19, XREF_DS5933_BUF_SETBUFFERDATA },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x05, 0x74 },
        { 0x08, 0x8B }, { 0x0B, 0x74 }, { 0x0E, 0x83 },
        { 0x10, 0xE4 }, { 0x13, 0x1B }, { 0x16, 0xC8 },
        { 0x18, 0xE8 }, { 0x1D, 0xC2 }, { 0x1F, 0x00 }
    }
};

// ?SetCurrentPosition@CMcpxBuffer@DirectSound@@QAEJK@Z
SOOVPA<12> CDirectSoundBuffer_SetCurrentPositionT_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_SETCURRENTPOSITION_T, 0,
    {
        { 0x00, 0x55 }, { 0x16, 0x3C }, { 0x2D, 0xC1 },
        { 0x42, 0x03 }, { 0x59, 0x00 }, { 0x6F, 0x7C },
        { 0x86, 0x0F }, { 0x9B, 0xF3 }, { 0xB2, 0x82 },
        { 0xC8, 0x0F }, { 0xDE, 0x8B }, { 0xF5, 0x00 }
    }
};

// ?SetCurrentPosition@CDirectSoundBuffer@DirectSound@@QAGJK@Z
SOOVPA<13> CDirectSoundBuffer_SetCurrentPosition_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_SETCURRENTPOSITION, 1,
    {
        { 0x35, XREF_DS5933_BUF_SETCURRENTPOSITION_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x0E, 0xB6 },
        { 0x15, 0x0B }, { 0x1C, 0x15 }, { 0x24, 0x00 },
        { 0x2B, 0x08 }, { 0x32, 0x24 }, { 0x3A, 0xF6 },
        { 0x3F, 0x68 }, { 0x4A, 0x8B }, { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_SetCurrentPosition@8
SOOVPA<13> IDirectSoundBuffer8_SetCurrentPosition_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5933_BUF_SETCURRENTPOSITION },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x04, 0xFF },
        { 0x07, 0x08 }, { 0x09, 0xC8 }, { 0x0C, 0xE4 },
        { 0x0E, 0xD9 }, { 0x11, 0x23 }, { 0x13, 0x51 },
        { 0x14, 0xE8 }, { 0x19, 0xC2 }, { 0x1B, 0x00 }
    }
};

// ?SetPlayRegion@CDirectSoundBuffer@DirectSound@@QAGJKK@Z
SOOVPA<12> CDirectSoundBuffer_SetPlayRegion_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_SETPLAYREGION, 0,
    {
        { 0x00, 0x55 }, { 0x0A, 0x3D }, { 0x17, 0x74 },
        { 0x24, 0xB8 }, { 0x2E, 0x85 }, { 0x39, 0x8B },
        { 0x45, 0x10 }, { 0x50, 0x00 }, { 0x5A, 0xE8 },
        { 0x67, 0x8B }, { 0x73, 0x15 }, { 0x7F, 0x00 }
    }
};

// _IDirectSoundBuffer_SetPlayRegion@12
SOOVPA<13> IDirectSoundBuffer8_SetPlayRegion_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x19, XREF_DS5933_BUF_SETPLAYREGION },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x05, 0x74 },
        { 0x08, 0x8B }, { 0x0B, 0x74 }, { 0x0E, 0x83 },
        { 0x10, 0xE4 }, { 0x13, 0x1B }, { 0x16, 0xC8 },
        { 0x18, 0xE8 }, { 0x1D, 0xC2 }, { 0x1F, 0x00 }
    }
};

// ?SetLoopRegion@CDirectSoundBuffer@DirectSound@@QAGJKK@Z
SOOVPA<12> CDirectSoundBuffer_SetLoopRegion_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_SETLOOPREGION, 0,
    {
        { 0x00, 0x55 }, { 0x0A, 0x3D }, { 0x18, 0x0B },
        { 0x24, 0xB8 }, { 0x30, 0x8B }, { 0x3C, 0x53 },
        { 0x48, 0x0F }, { 0x54, 0x00 }, { 0x60, 0xD0 },
        { 0x6C, 0x8B }, { 0x78, 0x15 }, { 0x84, 0x00 }
    }
};

// _IDirectSoundBuffer_SetLoopRegion@12
SOOVPA<13> IDirectSoundBuffer8_SetLoopRegion_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x19, XREF_DS5933_BUF_SETLOOPREGION },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x05, 0x74 },
        { 0x08, 0x8B }, { 0x0B, 0x74 }, { 0x0E, 0x83 },
        { 0x10, 0xE4 }, { 0x13, 0x1B }, { 0x16, 0xC8 },
        { 0x18, 0xE8 }, { 0x1D, 0xC2 }, { 0x1F, 0x00 }
    }
};

// ?Stop@CMcpxBuffer@DirectSound@@QAEJK@Z
SOOVPA<12> CDirectSoundBuffer_StopExTT_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_STOPEX_T_T, 0,
    {
        { 0x00, 0x53 }, { 0x0D, 0xDB }, { 0x1A, 0x44 },
        { 0x27, 0x02 }, { 0x34, 0x8B }, { 0x41, 0xEB },
        { 0x4E, 0x18 }, { 0x5B, 0xCE }, { 0x69, 0x8B },
        { 0x75, 0x00 }, { 0x82, 0x01 }, { 0x8F, 0x00 }
    }
};

// ?Stop@CMcpxBuffer@DirectSound@@QAEJ_JK@Z
SOOVPA<13> CDirectSoundBuffer_StopExT_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_STOPEX_T, 1,
    {
        { 0x2B, XREF_DS5933_BUF_STOPEX_T_T },
        { 0x00, 0x55 }, { 0x05, 0x08 }, { 0x0A, 0x0B },
        { 0x0F, 0x74 }, { 0x14, 0x8B }, { 0x19, 0xFF },
        { 0x1E, 0xFF }, { 0x23, 0x75 }, { 0x28, 0x8B },
        { 0x2F, 0x8B }, { 0x32, 0xC7 }, { 0x38, 0x00 }
    }
};

// ?StopEx@CDirectSoundBuffer@DirectSound@@QAGJ_JK@Z
SOOVPA<13> CDirectSoundBuffer_StopEx_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_STOPEX, 1,
    {
        { 0x3D, XREF_DS5933_BUF_STOPEX_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x10, 0x74 },
        { 0x16, 0x68 }, { 0x21, 0xB8 }, { 0x28, 0x8B },
        { 0x30, 0xFF }, { 0x38, 0xFF }, { 0x41, 0x85 },
        { 0x47, 0x68 }, { 0x52, 0x8B }, { 0x58, 0x00 }
    }
};

// _IDirectSoundBuffer_StopEx@16
SOOVPA<13> IDirectSoundBuffer8_StopEx_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x1D, XREF_DS5933_BUF_STOPEX },
        { 0x00, 0xFF }, { 0x03, 0x10 }, { 0x06, 0x24 },
        { 0x09, 0x74 }, { 0x0C, 0x8B }, { 0x0F, 0x74 },
        { 0x13, 0xC0 }, { 0x16, 0xD9 }, { 0x19, 0x23 },
        { 0x1C, 0xE8 }, { 0x21, 0xC2 }, { 0x23, 0x00 }
    }
};

// ?Play@CMcpxBuffer@DirectSound@@QAEJ_JK@Z
SOOVPA<13> CDirectSoundBuffer_PlayExT_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_PLAYEX_T, 1,
    {
        { 0x2B, XREF_DS5933_BUF_PLAY_T },
        { 0x00, 0x55 }, { 0x05, 0x08 }, { 0x0A, 0x0B },
        { 0x0F, 0x74 }, { 0x14, 0x8B }, { 0x19, 0xFF },
        { 0x1E, 0xFF }, { 0x23, 0x75 }, { 0x28, 0x8B },
        { 0x2F, 0x8B }, { 0x32, 0xC7 }, { 0x38, 0x00 }
    }
};

// ?PlayEx@CDirectSoundBuffer@DirectSound@@QAGJ_JK@Z
SOOVPA<13> CDirectSoundBuffer_PlayEx_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_PLAYEX, 1,
    {
        { 0x3D, XREF_DS5933_BUF_PLAYEX_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x10, 0x74 },
        { 0x16, 0x68 }, { 0x21, 0xB8 }, { 0x28, 0x8B },
        { 0x30, 0xFF }, { 0x38, 0xFF }, { 0x41, 0x85 },
        { 0x47, 0x68 }, { 0x52, 0x8B }, { 0x58, 0x00 }
    }
};

// _IDirectSoundBuffer_PlayEx@16
SOOVPA<13> IDirectSoundBuffer8_PlayEx_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x1D, XREF_DS5933_BUF_PLAYEX },
        { 0x00, 0xFF }, { 0x03, 0x10 }, { 0x06, 0x24 },
        { 0x09, 0x74 }, { 0x0C, 0x8B }, { 0x0F, 0x74 },
        { 0x13, 0xC0 }, { 0x16, 0xD9 }, { 0x19, 0x23 },
        { 0x1C, 0xE8 }, { 0x21, 0xC2 }, { 0x23, 0x00 }
    }
};

// ?SetFormat@CDirectSoundVoice@DirectSound@@QAGJPBUtWAVEFORMATEX@@@Z
SOOVPA<12> CDirectSoundBuffer_SetFormatT_1_0_5933 =
{
    0, 12, XREF_DS5933_BUF_SETFORMAT_T, 0,
    {
        { 0x00, 0x56 }, { 0x06, 0x4E }, { 0x0C, 0x24 },
        { 0x13, 0x85 }, { 0x18, 0x4E }, { 0x20, 0x8B },
        { 0x24, 0x01 }, { 0x2A, 0x7C }, { 0x2F, 0xE8 },
        { 0x36, 0x7C }, { 0x3B, 0xE8 }, { 0x43, 0x00 }
    }
};

// ?SetFormat@CDirectSoundBuffer@DirectSound@@QAGJPBUtWAVEFORMATEX@@@Z
SOOVPA<13> CDirectSoundBuffer_SetFormat_1_0_5933 =
{
    0, 13, XREF_DS5933_BUF_SETFORMAT, 1,
    {
        { 0x32, XREF_DS5933_BUF_SETFORMAT_T },
        { 0x00, 0x56 }, { 0x07, 0x3D }, { 0x0E, 0xB6 },
        { 0x15, 0x0B }, { 0x1C, 0x15 }, { 0x23, 0x40 },
        { 0x2A, 0x74 }, { 0x31, 0xE8 }, { 0x38, 0x8B },
        { 0x41, 0xFF }, { 0x47, 0x8B }, { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetFormat@8
SOOVPA<13> IDirectSoundBuffer8_SetFormat_1_0_5933 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5933_BUF_SETFORMAT },
        { 0x00, 0x8B }, { 0x02, 0x24 }, { 0x04, 0xFF },
        { 0x07, 0x08 }, { 0x09, 0xC8 }, { 0x0C, 0xE4 },
        { 0x0E, 0xD9 }, { 0x11, 0x23 }, { 0x13, 0x51 },
        { 0x14, 0xE8 }, { 0x19, 0xC2 }, { 0x1B, 0x00 }
    }
};

OOVPATable DSound_1_0_5933[] =
{
    { (OOVPA*)&DirectSoundCreate_1_0_5933, XTL::EmuDirectSoundCreate,
      #ifdef _DEBUG_TRACE
      "EmuDirectSoundCreate"
      #endif
    },
    { (OOVPA*)&IDirectSound_Release_1_0_5849, XTL::EmuIDirectSound8_Release,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSound8_Release"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer_Release_1_0_4627, XTL::EmuIDirectSoundBuffer8_Release,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_Release"
      #endif
    },
    { (OOVPA*)&CDirectSound_CreateSoundBuffer_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSound::CreateSoundBuffer (XREF)"
      #endif
    },
    { (OOVPA*)&DirectSoundCreateBuffer_1_0_5933, XTL::EmuDirectSoundCreateBuffer,
      #ifdef _DEBUG_TRACE
      "EmuDirectSoundCreateBuffer"
      #endif
    },
    { (OOVPA*)&IDirectSound8_CreateSoundBuffer_1_0_5933, XTL::EmuIDirectSound8_CreateSoundBuffer,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSound8_CreateSoundBuffer"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_PlayT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::Play leaf (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_Play_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::Play (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_Play_1_0_5933, XTL::EmuIDirectSoundBuffer8_Play,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_Play"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_Stop_1_0_5933, XTL::EmuIDirectSoundBuffer8_Stop,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_Stop"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetVolumeT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetVolume leaf (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetVolume_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetVolume (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_SetVolume_1_0_5933, XTL::EmuIDirectSoundBuffer8_SetVolume,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_SetVolume"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_GetCurrentPosition_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::GetCurrentPosition (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_GetCurrentPosition_1_0_5933, XTL::EmuIDirectSoundBuffer8_GetCurrentPosition,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_GetCurrentPosition"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_Lock_1_0_5933, XTL::EmuIDirectSoundBuffer8_Lock,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_Lock"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetMixBinsT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetMixBins leaf (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetMixBins_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetMixBins (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_SetMixBins_1_0_5933, XTL::EmuIDirectSoundBuffer8_SetMixBins,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_SetMixBins"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_GetStatusT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::GetStatus leaf (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_GetStatus_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::GetStatus (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_GetStatus_1_0_5933, XTL::EmuIDirectSoundBuffer8_GetStatus,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_GetStatus"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetBufferData_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetBufferData (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_SetBufferData_1_0_5933, XTL::EmuIDirectSoundBuffer8_SetBufferData,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_SetBufferData"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetCurrentPositionT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetCurrentPosition leaf (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetCurrentPosition_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetCurrentPosition (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_SetCurrentPosition_1_0_5933, XTL::EmuIDirectSoundBuffer8_SetCurrentPosition,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_SetCurrentPosition"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetPlayRegion_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetPlayRegion (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_SetPlayRegion_1_0_5933, XTL::EmuIDirectSoundBuffer8_SetPlayRegion,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_SetPlayRegion"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetLoopRegion_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetLoopRegion (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_SetLoopRegion_1_0_5933, XTL::EmuIDirectSoundBuffer8_SetLoopRegion,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_SetLoopRegion"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_StopExTT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CMcpxBuffer::Stop (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_StopExT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CMcpxBuffer::StopEx (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_StopEx_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::StopEx (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_StopEx_1_0_5933, XTL::EmuIDirectSoundBuffer8_StopEx,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_StopEx"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_PlayExT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CMcpxBuffer::PlayEx (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_PlayEx_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::PlayEx (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_PlayEx_1_0_5933, XTL::EmuIDirectSoundBuffer8_PlayEx,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_PlayEx"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetFormatT_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundVoice::SetFormat (XREF)"
      #endif
    },
    { (OOVPA*)&CDirectSoundBuffer_SetFormat_1_0_5933, 0,
      #ifdef _DEBUG_TRACE
      "CDirectSoundBuffer::SetFormat (XREF)"
      #endif
    },
    { (OOVPA*)&IDirectSoundBuffer8_SetFormat_1_0_5933, XTL::EmuIDirectSoundBuffer8_SetFormat,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSoundBuffer8_SetFormat"
      #endif
    },
    { (OOVPA*)&DirectSoundDoWork_1_0_5933, XTL::EmuDirectSoundDoWork,
      #ifdef _DEBUG_TRACE
      "EmuDirectSoundDoWork"
      #endif
    },
    { (OOVPA*)&IDirectSound_SynchPlayback_1_0_5849, XTL::EmuIDirectSound8_SynchPlayback,
      #ifdef _DEBUG_TRACE
      "EmuIDirectSound8_SynchPlayback"
      #endif
    },
    { (OOVPA*)&XAudioDownloadEffectsImage_1_0_5849, XTL::EmuXAudioDownloadEffectsImage,
      #ifdef _DEBUG_TRACE
      "EmuXAudioDownloadEffectsImage"
      #endif
    },
    { (OOVPA*)&DirectSoundUseFullHRTF_1_0_5849, XTL::EmuDirectSoundUseFullHRTF,
      #ifdef _DEBUG_TRACE
      "EmuDirectSoundUseFullHRTF"
      #endif
    },
};

uint32 DSound_1_0_5933_SIZE = sizeof(DSound_1_0_5933);
