// XACT 5849 sound-bank metadata lookup and object lifetime.

#include "xdk_xtrace.h"
#include <xact.h>

static const DWORD kSoundBankSize = 0x80;
static const DWORD kCueTableOffset = 0x3C;
static const DWORD kCueEntrySize = 0x14;
static const DWORD kAlphaNameOffset = 0x68;
static const DWORD kBetaNameOffset = 0x71;

static void WriteWord(BYTE* data, DWORD offset, WORD value)
{
    CopyMemory(data + offset, &value, sizeof(value));
}

static void WriteDword(BYTE* data, DWORD offset, DWORD value)
{
    CopyMemory(data + offset, &value, sizeof(value));
}

static void InitializeProbeSoundBank(BYTE* bank)
{
    ZeroMemory(bank, kSoundBankSize);

    WriteDword(bank, 0x00, 0x4B424453); // "SDBK" in guest memory.
    WriteWord(bank, 0x04, 11);
    WriteDword(bank, 0x14, kSoundBankSize);
    WriteDword(bank, 0x18, 0);
    WriteWord(bank, 0x1A, 2);
    WriteWord(bank, 0x1E, 2);
    WriteDword(bank, kCueTableOffset, kAlphaNameOffset);
    WriteDword(bank, kCueTableOffset + kCueEntrySize, kBetaNameOffset);
    CopyMemory(bank + kAlphaNameOffset, "AlphaCue", 9);
    CopyMemory(bank + kBetaNameOffset, "BetaCue", 8);
}

void __cdecl main()
{
    xt_begin("xact_soundbank");

    const int create_patched =
        xt_is_hle_patched((const void*)IXACTEngine_CreateSoundBank);
    const int lookup_patched = xt_is_hle_patched(
        (const void*)IXACTSoundBank_GetSoundCueIndexFromFriendlyName);
    const int addref_patched =
        xt_is_hle_patched((const void*)IXACTSoundBank_AddRef);
    const int release_patched =
        xt_is_hle_patched((const void*)IXACTSoundBank_Release);
    xt_chk("xact.soundbank_create_hle", 1, create_patched);
    xt_chk("xact.soundbank_lookup_hle", 1, lookup_patched);
    xt_chk("xact.soundbank_addref_hle", 1, addref_patched);
    xt_chk("xact.soundbank_release_hle", 1, release_patched);
    if(!create_patched || !lookup_patched || !addref_patched ||
       !release_patched)
    {
        xt_emit("NOTE XACTENG 1.0.5849 sound-bank metadata is not fully HLE-patched");
        xt_end_and_exit();
    }

    XACT_RUNTIME_PARAMETERS params;
    ZeroMemory(&params, sizeof(params));
    params.dwMax2DHwVoices = 64;
    params.dwMax3DHwVoices = 32;
    params.dwMaxConcurrentStreams = 4;

    IXACTEngine* engine = NULL;
    HRESULT result = XACTEngineCreate(&params, &engine);
    xt_chk("xact.soundbank_engine_create", 1,
           SUCCEEDED(result) && engine != NULL);
    if(FAILED(result) || engine == NULL)
    {
        xt_end_and_exit();
    }

    BYTE bank[kSoundBankSize];
    InitializeProbeSoundBank(bank);

    IXACTSoundBank* sound_bank = NULL;
    DWORD signature = 0;
    CopyMemory(&signature, bank, sizeof(signature));
    WriteDword(bank, 0x00, 0);
    result = IXACTEngine_CreateSoundBank(
        engine, bank, sizeof(bank), &sound_bank);
    xt_chk("xact.soundbank_bad_signature_rejected", 1,
           FAILED(result) && sound_bank == NULL);
    WriteDword(bank, 0x00, signature);

    WriteDword(bank, kCueTableOffset, kSoundBankSize);
    result = IXACTEngine_CreateSoundBank(
        engine, bank, sizeof(bank), &sound_bank);
    xt_chk("xact.soundbank_bad_name_offset_rejected", 1,
           FAILED(result) && sound_bank == NULL);
    WriteDword(bank, kCueTableOffset, kAlphaNameOffset);

    result = IXACTEngine_CreateSoundBank(
        engine, bank, sizeof(bank), &sound_bank);
    xt_chk("xact.soundbank_create_ok", 1,
           SUCCEEDED(result) && sound_bank != NULL);
    if(FAILED(result) || sound_bank == NULL)
    {
        IXACTEngine_Release(engine);
        xt_end_and_exit();
    }

    xt_chk_u32("xact.soundbank_addref_count", 2,
               IXACTSoundBank_AddRef(sound_bank));

    DWORD cue_index = 0xFFFFFFFF;
    result = IXACTSoundBank_GetSoundCueIndexFromFriendlyName(
        sound_bank, "AlphaCue", &cue_index);
    xt_chk("xact.soundbank_lookup_alpha", 1,
           SUCCEEDED(result) && cue_index == 0);

    cue_index = 0xFFFFFFFF;
    result = IXACTSoundBank_GetSoundCueIndexFromFriendlyName(
        sound_bank, "BetaCue", &cue_index);
    xt_chk("xact.soundbank_lookup_beta", 1,
           SUCCEEDED(result) && cue_index == 1);

    cue_index = 7;
    result = IXACTSoundBank_GetSoundCueIndexFromFriendlyName(
        sound_bank, "MissingCue", &cue_index);
    xt_chk("xact.soundbank_lookup_missing", 1,
           FAILED(result) && cue_index == 0xFFFFFFFF);

    cue_index = 7;
    result = IXACTSoundBank_GetSoundCueIndexFromFriendlyName(
        sound_bank, "alphacue", &cue_index);
    xt_chk("xact.soundbank_lookup_case_sensitive", 1,
           FAILED(result) && cue_index == 0xFFFFFFFF);

    xt_chk_u32("xact.soundbank_release_count", 1,
               IXACTSoundBank_Release(sound_bank));
    xt_chk_u32("xact.soundbank_engine_child_reference", 1,
               IXACTEngine_Release(engine));
    xt_chk_u32("xact.soundbank_final_release", 0,
               IXACTSoundBank_Release(sound_bank));

    xt_end_and_exit();
}
