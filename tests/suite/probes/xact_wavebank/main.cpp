// XACT 5849 in-memory wave-bank registration and teardown.

#include "xdk_xtrace.h"
#include <stddef.h>
#include <xact.h>
#include <xactwb.h>

struct ProbeWaveBank
{
    WAVEBANKHEADER Header;
    WAVEBANKDATA Data;
    WAVEBANKENTRY Entry;
    BYTE Samples[4];
};

static void InitializeProbeWaveBank(ProbeWaveBank* bank)
{
    ZeroMemory(bank, sizeof(*bank));

    bank->Header.dwSignature = WAVEBANK_HEADER_SIGNATURE;
    bank->Header.dwVersion = WAVEBANK_HEADER_VERSION;
    bank->Header.Segments[WAVEBANK_SEGIDX_BANKDATA].dwOffset =
        offsetof(ProbeWaveBank, Data);
    bank->Header.Segments[WAVEBANK_SEGIDX_BANKDATA].dwLength =
        sizeof(bank->Data);
    bank->Header.Segments[WAVEBANK_SEGIDX_ENTRYMETADATA].dwOffset =
        offsetof(ProbeWaveBank, Entry);
    bank->Header.Segments[WAVEBANK_SEGIDX_ENTRYMETADATA].dwLength =
        sizeof(bank->Entry);
    bank->Header.Segments[WAVEBANK_SEGIDX_ENTRYNAMES].dwOffset =
        offsetof(ProbeWaveBank, Samples);
    bank->Header.Segments[WAVEBANK_SEGIDX_ENTRYWAVEDATA].dwOffset =
        offsetof(ProbeWaveBank, Samples);
    bank->Header.Segments[WAVEBANK_SEGIDX_ENTRYWAVEDATA].dwLength =
        sizeof(bank->Samples);

    bank->Data.dwFlags = WAVEBANK_TYPE_BUFFER;
    bank->Data.dwEntryCount = 1;
    lstrcpynA(bank->Data.szBankName, "probe", WAVEBANK_BANKNAME_LENGTH);
    bank->Data.dwEntryMetaDataElementSize = sizeof(bank->Entry);
    bank->Data.dwAlignment = WAVEBANK_ALIGNMENT_MIN;

    bank->Entry.Format.wFormatTag = WAVEBANKMINIFORMAT_TAG_PCM;
    bank->Entry.Format.nChannels = 1;
    bank->Entry.Format.nSamplesPerSec = 8000;
    bank->Entry.Format.wBitsPerSample = WAVEBANKMINIFORMAT_BITDEPTH_8;
    bank->Entry.PlayRegion.dwLength = sizeof(bank->Samples);

    bank->Samples[0] = 0x10;
    bank->Samples[1] = 0x40;
    bank->Samples[2] = 0x80;
    bank->Samples[3] = 0xF0;
}

void __cdecl main()
{
    xt_begin("xact_wavebank");

    const int register_patched =
        xt_is_hle_patched((const void*)IXACTEngine_RegisterWaveBank);
    const int unregister_patched =
        xt_is_hle_patched((const void*)IXACTEngine_UnRegisterWaveBank);
    xt_chk("xact.wavebank_register_hle", 1, register_patched);
    xt_chk("xact.wavebank_unregister_hle", 1, unregister_patched);
    if(!register_patched || !unregister_patched)
    {
        xt_emit("NOTE XACTENG 1.0.5849 wave-bank lifecycle is not fully HLE-patched");
        xt_end_and_exit();
    }

    XACT_RUNTIME_PARAMETERS params;
    ZeroMemory(&params, sizeof(params));
    params.dwMax2DHwVoices = 64;
    params.dwMax3DHwVoices = 32;
    params.dwMaxConcurrentStreams = 4;

    IXACTEngine* engine = NULL;
    HRESULT result = XACTEngineCreate(&params, &engine);
    xt_chk("xact.wavebank_engine_create", 1,
           SUCCEEDED(result) && engine != NULL);
    if(FAILED(result) || engine == NULL)
    {
        xt_end_and_exit();
    }

    ProbeWaveBank bank;
    InitializeProbeWaveBank(&bank);

    IXACTWaveBank* wave_bank = NULL;
    const DWORD signature = bank.Header.dwSignature;
    bank.Header.dwSignature = 0;
    result = IXACTEngine_RegisterWaveBank(
        engine, &bank, sizeof(bank), &wave_bank);
    xt_chk("xact.wavebank_bad_signature_rejected", 1,
           FAILED(result) && wave_bank == NULL);
    bank.Header.dwSignature = signature;

    bank.Header.Segments[WAVEBANK_SEGIDX_ENTRYWAVEDATA].dwLength =
        sizeof(bank.Samples) + 1;
    result = IXACTEngine_RegisterWaveBank(
        engine, &bank, sizeof(bank), &wave_bank);
    xt_chk("xact.wavebank_bad_region_rejected", 1,
           FAILED(result) && wave_bank == NULL);
    bank.Header.Segments[WAVEBANK_SEGIDX_ENTRYWAVEDATA].dwLength =
        sizeof(bank.Samples);

    result = IXACTEngine_RegisterWaveBank(
        engine, &bank, sizeof(bank), &wave_bank);
    xt_chk("xact.wavebank_register_ok", 1,
           SUCCEEDED(result) && wave_bank != NULL);
    DWORD sample_word = 0;
    CopyMemory(&sample_word, bank.Samples, sizeof(sample_word));
    xt_chk_u32("xact.wavebank_data_preserved", 0xF0804010, sample_word);
    if(SUCCEEDED(result) && wave_bank != NULL)
    {
        xt_chk("xact.wavebank_unregister_ok", 1,
               SUCCEEDED(IXACTEngine_UnRegisterWaveBank(engine, wave_bank)));
    }

    wave_bank = NULL;
    result = IXACTEngine_RegisterWaveBank(
        engine, &bank, sizeof(bank), &wave_bank);
    xt_chk("xact.wavebank_reregister_ok", 1,
           SUCCEEDED(result) && wave_bank != NULL);
    if(SUCCEEDED(result) && wave_bank != NULL)
    {
        xt_chk("xact.wavebank_reregister_cleanup", 1,
               SUCCEEDED(IXACTEngine_UnRegisterWaveBank(engine, wave_bank)));
    }

    wave_bank = NULL;
    result = IXACTEngine_RegisterWaveBank(
        engine, &bank, sizeof(bank), &wave_bank);
    xt_chk("xact.wavebank_engine_owned_register", 1,
           SUCCEEDED(result) && wave_bank != NULL);

    xt_chk_u32("xact.wavebank_engine_child_reference", 1,
               IXACTEngine_Release(engine));
    xt_chk("xact.wavebank_engine_owned_cleanup", 1,
           SUCCEEDED(IXACTEngine_UnRegisterWaveBank(engine, wave_bank)));
    xt_end_and_exit();
}
