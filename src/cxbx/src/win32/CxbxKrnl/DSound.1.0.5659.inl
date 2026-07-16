// DSound Version 1.0.5659 OOVPA table.
// Mirrors the 5849 DSound registration array: the 5849 signatures (both plain
// and XRef-consuming) are version-agnostic at the registration level -- the
// runtime scanner matches what it can against this version's linked dsound.lib.

// _IDirectSound_CreateSoundBuffer@16 (dsound.lib 5659, 36 bytes;
// call@0x1D -> XREF_DS5849_CREATESOUNDBUFFER)
SOOVPA<13> IDirectSound8_CreateSoundBuffer_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x1D, XREF_DS5849_CREATESOUNDBUFFER },
        { 0x00, 0xFF },
        { 0x03, 0x10 },
        { 0x06, 0x24 },
        { 0x09, 0x74 },
        { 0x0C, 0x8B },
        { 0x0F, 0x74 },
        { 0x13, 0xC0 },
        { 0x16, 0xD9 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x21, 0xC2 },
        { 0x23, 0x00 }
    }
};

// _IDirectSound_CreateSoundStream@16 (Arx Fatalis 5659 image, 36 bytes;
// call@0x1D -> XREF_DS5849_CREATESOUNDSTREAM). Same this-adjusting thin
// wrapper family as IDirectSound_CreateSoundBuffer above; only the XRef
// call target tells them apart. XACTENG reaches DSOUND streams through
// this COM method rather than the global DirectSoundCreateStream.
SOOVPA<10> IDirectSound8_CreateSoundStream_1_0_5659 =
{
    0, 10, -1, 1,
    {
        { 0x1D, XREF_DS5849_CREATESOUNDSTREAM },
        { 0x00, 0xFF },
        { 0x04, 0x8B },
        { 0x07, 0x08 },
        { 0x0C, 0x8B },
        { 0x12, 0x83 },
        { 0x15, 0xF7 },
        { 0x19, 0x23 },
        { 0x1B, 0x51 },
        { 0x22, 0x10 }
    }
};

// The four IDirectSoundStream_* setter APIs XACTENG calls (Arx Fatalis 5659
// image). All share one 64-byte template (lock preamble, error check,
// this+4 voice adjust, call the shared voice leaf, ret 8) and differ ONLY in
// the rel32 leaf target at +0x36, so each signature is the same byte set
// plus a discriminating XRef pair against the leaf's save enum.
// _IDirectSoundStream_SetVolume@8 (call@0x36 -> XREF_DS5849_BUF_SETVOLUME_T)
SOOVPA<11> IDirectSoundStream_SetVolume_1_0_5659 =
{
    0, 11, -1, 1,
    {
        { 0x36, XREF_DS5849_BUF_SETVOLUME_T },
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x12, 0x85 },
        { 0x21, 0xB8 },
        { 0x28, 0x8B },
        { 0x31, 0x83 },
        { 0x33, 0x04 },
        { 0x3A, 0x85 },
        { 0x3C, 0x8B }
    }
};

// _IDirectSoundStream_SetMixBins@8 (call@0x36 -> XREF_DS5849_BUF_SETMIXBINS_T)
SOOVPA<11> IDirectSoundStream_SetMixBins_1_0_5659 =
{
    0, 11, -1, 1,
    {
        { 0x36, XREF_DS5849_BUF_SETMIXBINS_T },
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x12, 0x85 },
        { 0x21, 0xB8 },
        { 0x28, 0x8B },
        { 0x31, 0x83 },
        { 0x33, 0x04 },
        { 0x3A, 0x85 },
        { 0x3C, 0x8B }
    }
};

// _IDirectSoundStream_SetOutputBuffer@8 (call@0x36 -> XREF_DS5659_BUFFER_SETOUTPUTBUFFER_T)
SOOVPA<11> IDirectSoundStream_SetOutputBuffer_1_0_5659 =
{
    0, 11, -1, 1,
    {
        { 0x36, XREF_DS5659_BUFFER_SETOUTPUTBUFFER_T },
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x12, 0x85 },
        { 0x21, 0xB8 },
        { 0x28, 0x8B },
        { 0x31, 0x83 },
        { 0x33, 0x04 },
        { 0x3A, 0x85 },
        { 0x3C, 0x8B }
    }
};

// _IDirectSoundStream_SetFormat@8 (call@0x36 -> XREF_DS5659_BUFFER_SETFORMAT_T)
SOOVPA<11> IDirectSoundStream_SetFormat_1_0_5659 =
{
    0, 11, -1, 1,
    {
        { 0x36, XREF_DS5659_BUFFER_SETFORMAT_T },
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x12, 0x85 },
        { 0x21, 0xB8 },
        { 0x28, 0x8B },
        { 0x31, 0x83 },
        { 0x33, 0x04 },
        { 0x3A, 0x85 },
        { 0x3C, 0x8B }
    }
};

// Setter twin-family sweep signatures (Arx Fatalis 5659 image). Every
// remaining unhooked public DSOUND API is a 2-arg setter built from one of
// three compiler templates (identified against dsound.lib 5659: the
// SetPitch/SetLFO/SetEG/SetFilter/SetHeadroom class for buffers and
// streams, plus stream Use3DVoiceData). Their raw bodies poke fields of the
// fake HLE voice objects (fatal), so a PATCH_ALL fallback maps every
// still-unpatched member to the accept-and-ignore impl. Specifically-hooked
// members (SetVolume/SetMixBins/SetOutputBuffer/SetFormat...) are E9-patched
// before the sweep runs and are skipped via the offset-0 byte mismatch.
// Template A: lock preamble / this+4 voice adjust / call leaf / ret 8.
SOOVPA<12> IDirectSoundVoice_SetterAdjusted_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x12, 0x85 },
        { 0x21, 0xB8 },
        { 0x26, 0xEB },
        { 0x27, 0x26 },
        { 0x28, 0x8B },
        { 0x31, 0x83 },
        { 0x33, 0x04 },
        { 0x3A, 0x85 },
        { 0x3C, 0x8B }
    }
};

// Template B: lock preamble / arguments passed through / call leaf / ret 8.
SOOVPA<13> IDirectSoundVoice_SetterPassthrough_1_0_5659 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x12, 0x85 },
        { 0x21, 0xB8 },
        { 0x26, 0xEB },
        { 0x27, 0x22 },
        { 0x28, 0x57 },
        { 0x29, 0xFF },
        { 0x2D, 0xFF },
        { 0x31, 0xE8 },
        { 0x36, 0x85 },
        { 0x38, 0x8B }
    }
};

// Template C: register variant (ecx result, esi saved late) — stream
// Use3DVoiceData in the Arx image.
SOOVPA<13> IDirectSoundVoice_SetterRegVariant_1_0_5659 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0xE8 },
        { 0x05, 0x83 },
        { 0x0C, 0x0F },
        { 0x0F, 0x74 },
        { 0x11, 0x85 },
        { 0x20, 0xB8 },
        { 0x25, 0xEB },
        { 0x27, 0x8B },
        { 0x2B, 0x56 },
        { 0x30, 0x83 },
        { 0x32, 0x04 },
        { 0x39, 0x85 },
        { 0x3B, 0x8B }
    }
};

// ?Pause@CMcpxStream@DirectSound@@QAEJK@Z (dsound.lib 5659, 142 bytes)
SOOVPA<12> CMcpxStream_Pause_1_0_5659 =
{
    0, 12, XREF_DS5659_STREAM_PAUSE, 0,
    {
        { 0x00, 0x8B },
        { 0x0C, 0x8D },
        { 0x19, 0xDF },
        { 0x26, 0x83 },
        { 0x33, 0x56 },
        { 0x40, 0x83 },
        { 0x4C, 0x46 },
        { 0x59, 0x75 },
        { 0x66, 0x46 },
        { 0x73, 0xEB },
        { 0x80, 0x12 },
        { 0x8D, 0x00 }
    }
};

// ?Pause@CDirectSoundStream@DirectSound@@QAGJK@Z (dsound.lib 5659,
// 81 bytes; call@0x35 -> XREF_DS5659_STREAM_PAUSE)
SOOVPA<13> CDirectSoundStream_Pause_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x35, XREF_DS5659_STREAM_PAUSE },
        { 0x00, 0x56 },
        { 0x07, 0x3D },
        { 0x0E, 0xB6 },
        { 0x15, 0x0B },
        { 0x1C, 0x15 },
        { 0x24, 0x00 },
        { 0x2B, 0x08 },
        { 0x32, 0x24 },
        { 0x3A, 0xF6 },
        { 0x3F, 0x68 },
        { 0x4A, 0x8B },
        { 0x50, 0x00 }
    }
};

// ?SynchPlayback@CDirectSound@DirectSound@@QAGJXZ
// (dsound.lib 5659, 77 bytes)
SOOVPA<12> CDirectSound_SynchPlayback_1_0_5659 =
{
    0, 12, XREF_DS5659_SYNCHPLAYBACK, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x83 },
        { 0x0D, 0x0F },
        { 0x14, 0x74 },
        { 0x1B, 0xFF },
        { 0x22, 0x05 },
        { 0x29, 0x44 },
        { 0x30, 0xE8 },
        { 0x37, 0x8B },
        { 0x40, 0xFF },
        { 0x46, 0x8B },
        { 0x4C, 0x00 }
    }
};

// _IDirectSound_SynchPlayback@4 (dsound.lib 5659, 24 bytes;
// call@0x11 -> XREF_DS5659_SYNCHPLAYBACK)
SOOVPA<13> IDirectSound_SynchPlayback_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x11, XREF_DS5659_SYNCHPLAYBACK },
        { 0x00, 0x8B },
        { 0x01, 0x44 },
        { 0x02, 0x24 },
        { 0x04, 0x8B },
        { 0x06, 0x83 },
        { 0x08, 0xF8 },
        { 0x0A, 0xD9 },
        { 0x0C, 0xC9 },
        { 0x0E, 0xC8 },
        { 0x10, 0xE8 },
        { 0x15, 0xC2 },
        { 0x17, 0x00 }
    }
};

// ?GetOutputLevels@CDirectSound@DirectSound@@QAGJPAU_DSOUTPUTLEVELS@@H@Z
// (dsound.lib 5659, 70 bytes)
SOOVPA<12> CDirectSound_GetOutputLevels_1_0_5659 =
{
    0, 12, XREF_DS5659_GETOUTPUTLEVELS, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x6A },
        { 0x0C, 0xFE },
        { 0x12, 0x33 },
        { 0x19, 0x5E },
        { 0x1F, 0xB0 },
        { 0x25, 0xA6 },
        { 0x2B, 0x85 },
        { 0x32, 0xA3 },
        { 0x38, 0xCC },
        { 0x3E, 0xA6 },
        { 0x45, 0x00 }
    }
};

// _IDirectSound_GetOutputLevels@12 (dsound.lib 5659, 32 bytes;
// call@0x19 -> CDirectSound::GetOutputLevels)
SOOVPA<13> IDirectSound_GetOutputLevels_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x19, XREF_DS5659_GETOUTPUTLEVELS },
        { 0x00, 0x8B },
        { 0x02, 0x24 },
        { 0x05, 0x74 },
        { 0x08, 0x8B },
        { 0x0B, 0x74 },
        { 0x0E, 0x83 },
        { 0x10, 0xF8 },
        { 0x13, 0x1B },
        { 0x16, 0xC8 },
        { 0x18, 0xE8 },
        { 0x1D, 0xC2 },
        { 0x1F, 0x00 }
    }
};

// ?SetOutputBuffer@CDirectSoundVoice@DirectSound@@QAGJPAUIDirectSoundBuffer@@@Z
// (dsound.lib 5659, 84 bytes)
SOOVPA<12> CDirectSoundBuffer_SetOutputBufferT_1_0_5659 =
{
    0, 12, XREF_DS5659_BUFFER_SETOUTPUTBUFFER_T, 0,
    {
        { 0x00, 0x53 },
        { 0x07, 0x46 },
        { 0x0F, 0x7C },
        { 0x16, 0x10 },
        { 0x1E, 0xDB },
        { 0x25, 0x74 },
        { 0x2F, 0x8B },
        { 0x34, 0x17 },
        { 0x3E, 0x85 },
        { 0x43, 0x4F },
        { 0x4B, 0xD8 },
        { 0x53, 0x00 }
    }
};

// ?SetOutputBuffer@CDirectSoundBuffer@DirectSound@@QAGJPAUIDirectSoundBuffer@@@Z
// (dsound.lib 5659, 78 bytes; call@0x32 -> voice SetOutputBuffer)
SOOVPA<13> CDirectSoundBuffer_SetOutputBuffer_1_0_5659 =
{
    0, 13, XREF_DS5659_BUFFER_SETOUTPUTBUFFER, 1,
    {
        { 0x32, XREF_DS5659_BUFFER_SETOUTPUTBUFFER_T },
        { 0x00, 0x56 },
        { 0x07, 0x3D },
        { 0x0E, 0xB6 },
        { 0x15, 0x0B },
        { 0x1C, 0x15 },
        { 0x23, 0x40 },
        { 0x2A, 0x74 },
        { 0x31, 0xE8 },
        { 0x38, 0x8B },
        { 0x41, 0xFF },
        { 0x47, 0x8B },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetOutputBuffer@8 (dsound.lib 5659, 28 bytes;
// call@0x15 -> CDirectSoundBuffer::SetOutputBuffer)
SOOVPA<13> IDirectSoundBuffer_SetOutputBuffer_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5659_BUFFER_SETOUTPUTBUFFER },
        { 0x00, 0x8B },
        { 0x02, 0x24 },
        { 0x04, 0xFF },
        { 0x07, 0x08 },
        { 0x09, 0xC8 },
        { 0x0C, 0xE4 },
        { 0x0E, 0xD9 },
        { 0x11, 0x23 },
        { 0x13, 0x51 },
        { 0x14, 0xE8 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?Use3DVoiceData@CDirectSoundBuffer@DirectSound@@QAGJH@Z
// (dsound.lib 5659, 76 bytes)
SOOVPA<12> CDirectSoundBuffer_Use3DVoiceData_1_0_5659 =
{
    0, 12, XREF_DS5659_BUFFER_USE3DVOICEDATA, 0,
    {
        { 0x00, 0xE8 },
        { 0x06, 0x3D },
        { 0x0D, 0xB6 },
        { 0x14, 0x0B },
        { 0x1B, 0x15 },
        { 0x22, 0x40 },
        { 0x28, 0xFF },
        { 0x2F, 0x0C },
        { 0x36, 0xC9 },
        { 0x3B, 0x68 },
        { 0x46, 0x8B },
        { 0x4B, 0x00 }
    }
};

// _IDirectSoundBuffer_Use3DVoiceData@8 (dsound.lib 5659, 28 bytes;
// call@0x15 -> CDirectSoundBuffer::Use3DVoiceData)
SOOVPA<13> IDirectSoundBuffer_Use3DVoiceData_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5659_BUFFER_USE3DVOICEDATA },
        { 0x00, 0x8B },
        { 0x02, 0x24 },
        { 0x04, 0xFF },
        { 0x07, 0x08 },
        { 0x09, 0xC8 },
        { 0x0C, 0xE4 },
        { 0x0E, 0xD9 },
        { 0x11, 0x23 },
        { 0x13, 0x51 },
        { 0x14, 0xE8 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetFormat@CDirectSoundVoice@DirectSound@@QAGJPBUtWAVEFORMATEX@@@Z
// (dsound.lib 5659, 68 bytes)
SOOVPA<12> CDirectSoundBuffer_SetFormatT_1_0_5659 =
{
    0, 12, XREF_DS5659_BUFFER_SETFORMAT_T, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x4E },
        { 0x0C, 0x24 },
        { 0x13, 0x85 },
        { 0x18, 0x4E },
        { 0x20, 0x8B },
        { 0x24, 0x01 },
        { 0x2A, 0x7C },
        { 0x2F, 0xE8 },
        { 0x36, 0x7C },
        { 0x3B, 0xE8 },
        { 0x43, 0x00 }
    }
};

// ?SetFormat@CDirectSoundBuffer@DirectSound@@QAGJPBUtWAVEFORMATEX@@@Z
// (dsound.lib 5659, 78 bytes; call@0x32 -> voice SetFormat)
SOOVPA<13> CDirectSoundBuffer_SetFormat_1_0_5659 =
{
    0, 13, XREF_DS5659_BUFFER_SETFORMAT, 1,
    {
        { 0x32, XREF_DS5659_BUFFER_SETFORMAT_T },
        { 0x00, 0x56 },
        { 0x07, 0x3D },
        { 0x0E, 0xB6 },
        { 0x15, 0x0B },
        { 0x1C, 0x15 },
        { 0x23, 0x40 },
        { 0x2A, 0x74 },
        { 0x31, 0xE8 },
        { 0x38, 0x8B },
        { 0x41, 0xFF },
        { 0x47, 0x8B },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetFormat@8 (dsound.lib 5659, 28 bytes;
// call@0x15 -> CDirectSoundBuffer::SetFormat)
SOOVPA<13> IDirectSoundBuffer_SetFormat_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x15, XREF_DS5659_BUFFER_SETFORMAT },
        { 0x00, 0x8B },
        { 0x02, 0x24 },
        { 0x04, 0xFF },
        { 0x07, 0x08 },
        { 0x09, 0xC8 },
        { 0x0C, 0xE4 },
        { 0x0E, 0xD9 },
        { 0x11, 0x23 },
        { 0x13, 0x51 },
        { 0x14, 0xE8 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?StopEx@CDirectSoundBuffer@DirectSound@@QAGJ_JK@Z
// (dsound.lib 5659, 89 bytes)
SOOVPA<12> CDirectSoundBuffer_StopEx_1_0_5659 =
{
    0, 12, XREF_DS5659_BUFFER_STOPEX, 0,
    {
        { 0x00, 0x56 },
        { 0x07, 0x3D },
        { 0x10, 0x74 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x28, 0x8B },
        { 0x30, 0xFF },
        { 0x38, 0xFF },
        { 0x41, 0x85 },
        { 0x47, 0x68 },
        { 0x52, 0x8B },
        { 0x58, 0x00 }
    }
};

// _IDirectSoundBuffer_StopEx@16 (dsound.lib 5659, 36 bytes;
// call@0x1D -> CDirectSoundBuffer::StopEx)
SOOVPA<13> IDirectSoundBuffer_StopEx_1_0_5659 =
{
    0, 13, -1, 1,
    {
        { 0x1D, XREF_DS5659_BUFFER_STOPEX },
        { 0x00, 0xFF },
        { 0x03, 0x10 },
        { 0x06, 0x24 },
        { 0x09, 0x74 },
        { 0x0C, 0x8B },
        { 0x0F, 0x74 },
        { 0x13, 0xC0 },
        { 0x16, 0xD9 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x21, 0xC2 },
        { 0x23, 0x00 }
    }
};

OOVPATable DSound_1_0_5659[] =
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
    // IDirectSoundBuffer8::Release (* unchanged since 4627 *)
    {
        (OOVPA*)&IDirectSoundBuffer_Release_1_0_4627,
        XTL::EmuIDirectSoundBuffer8_Release,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Release"
        #endif
    },
    // CDirectSound::SynchPlayback (XREF save)
    {
        (OOVPA*)&CDirectSound_SynchPlayback_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::SynchPlayback (XREF)"
        #endif
    },
    // IDirectSound8::SynchPlayback
    {
        (OOVPA*)&IDirectSound_SynchPlayback_1_0_5659,
        XTL::EmuIDirectSound8_SynchPlayback,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SynchPlayback"
        #endif
    },
    // CDirectSound::GetOutputLevels (XREF save)
    {
        (OOVPA*)&CDirectSound_GetOutputLevels_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSound::GetOutputLevels (XREF)"
        #endif
    },
    // IDirectSound8::GetOutputLevels
    {
        (OOVPA*)&IDirectSound_GetOutputLevels_1_0_5659,
        XTL::EmuIDirectSound8_GetOutputLevels,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_GetOutputLevels"
        #endif
    },
    // IDirectSound8::CreateSoundBuffer
    {
        (OOVPA*)&IDirectSound8_CreateSoundBuffer_1_0_5659,
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
    // IDirectSound8::CreateSoundStream (COM method wrapper; XACTENG path)
    {
        (OOVPA*)&IDirectSound8_CreateSoundStream_1_0_5659,
        XTL::EmuIDirectSound8_CreateSoundStream,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_CreateSoundStream"
        #endif
    },
    // IDirectSoundStream_SetVolume (XACTENG path)
    {
        (OOVPA*)&IDirectSoundStream_SetVolume_1_0_5659,
        XTL::EmuCDirectSoundStream_SetVolume,
        #ifdef _DEBUG_TRACE
        "EmuCDirectSoundStream_SetVolume"
        #endif
    },
    // IDirectSoundStream_SetMixBins (XACTENG path)
    {
        (OOVPA*)&IDirectSoundStream_SetMixBins_1_0_5659,
        XTL::EmuIDirectSoundStream_SetMixBinsS,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_SetMixBinsS"
        #endif
    },
    // IDirectSoundStream_SetOutputBuffer (XACTENG path)
    {
        (OOVPA*)&IDirectSoundStream_SetOutputBuffer_1_0_5659,
        XTL::EmuIDirectSoundStream_SetOutputBuffer,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_SetOutputBuffer"
        #endif
    },
    // IDirectSoundStream_SetFormat (XACTENG path)
    {
        (OOVPA*)&IDirectSoundStream_SetFormat_1_0_5659,
        XTL::EmuIDirectSoundStream_SetFormat,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_SetFormat"
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
    // CDirectSoundVoice::SetOutputBuffer (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetOutputBufferT_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundVoice::SetOutputBuffer (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetOutputBuffer (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetOutputBuffer_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetOutputBuffer (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetOutputBuffer
    {
        (OOVPA*)&IDirectSoundBuffer_SetOutputBuffer_1_0_5659,
        XTL::EmuIDirectSoundBuffer8_SetOutputBuffer,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetOutputBuffer"
        #endif
    },
    // CDirectSoundBuffer::Use3DVoiceData (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_Use3DVoiceData_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::Use3DVoiceData (XREF)"
        #endif
    },
    // IDirectSoundBuffer::Use3DVoiceData
    {
        (OOVPA*)&IDirectSoundBuffer_Use3DVoiceData_1_0_5659,
        XTL::EmuIDirectSoundBuffer8_Use3DVoiceData,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Use3DVoiceData"
        #endif
    },
    // CDirectSoundVoice::SetFormat (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetFormatT_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundVoice::SetFormat (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetFormat (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_SetFormat_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::SetFormat (XREF)"
        #endif
    },
    // IDirectSoundBuffer::SetFormat
    {
        (OOVPA*)&IDirectSoundBuffer_SetFormat_1_0_5659,
        XTL::EmuIDirectSoundBuffer8_SetFormat,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetFormat"
        #endif
    },
    // CDirectSoundBuffer::StopEx (XREF save)
    {
        (OOVPA*)&CDirectSoundBuffer_StopEx_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer::StopEx (XREF)"
        #endif
    },
    // IDirectSoundBuffer::StopEx
    {
        (OOVPA*)&IDirectSoundBuffer_StopEx_1_0_5659,
        XTL::EmuIDirectSoundBuffer8_StopEx,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_StopEx"
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
    // CMcpxStream::Pause (XREF save)
    {
        (OOVPA*)&CMcpxStream_Pause_1_0_5659, 0,
        #ifdef _DEBUG_TRACE
        "CMcpxStream::Pause (XREF)"
        #endif
    },
    // CDirectSoundStream::Pause
    {
        (OOVPA*)&CDirectSoundStream_Pause_1_0_5659,
        XTL::EmuCDirectSoundStream_Pause,
        #ifdef _DEBUG_TRACE
        "EmuCDirectSoundStream_Pause"
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
    // Setter twin-family sweeps (patch-all FALLBACK; must stay the LAST
    // DSOUND entries so every specifically-hooked member is already
    // E9-patched and skipped)
    {
        (OOVPA*)&IDirectSoundVoice_SetterAdjusted_1_0_5659,
        XTL::EmuIDirectSoundStream_SetMixBinsS,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_SetMixBinsS (setter sweep A)",
        #endif
        OOVPA_FLAG_PATCH_ALL
    },
    {
        (OOVPA*)&IDirectSoundVoice_SetterPassthrough_1_0_5659,
        XTL::EmuIDirectSoundStream_SetMixBinsS,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_SetMixBinsS (setter sweep B)",
        #endif
        OOVPA_FLAG_PATCH_ALL
    },
    {
        (OOVPA*)&IDirectSoundVoice_SetterRegVariant_1_0_5659,
        XTL::EmuIDirectSoundStream_SetMixBinsS,
        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundStream_SetMixBinsS (setter sweep C)",
        #endif
        OOVPA_FLAG_PATCH_ALL
    },
};

uint32 DSound_1_0_5659_SIZE = sizeof(DSound_1_0_5659);
