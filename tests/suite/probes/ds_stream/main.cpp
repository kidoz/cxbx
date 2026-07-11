// DirectSound stream packet lifecycle: queue, play, complete, flush, release.
#include "xdk_xtrace.h"

#define PACKET_BYTES 17640u

static short g_pcm[PACKET_BYTES / sizeof(short)];
static volatile DWORD g_callback_count = 0;
static volatile DWORD g_callback_status[2] = { 0, 0 };
static volatile DWORD g_callback_context[2] = { 0, 0 };

static VOID CALLBACK stream_callback(LPVOID stream_context, LPVOID packet_context, DWORD status)
{
    DWORD index = g_callback_count;
    if(index < 2)
    {
        g_callback_status[index] = status;
        g_callback_context[index] = (DWORD)packet_context;
    }
    g_callback_count = index + 1;
    (void)stream_context;
}

void __cdecl main()
{
    xt_begin("ds_stream");

    LPDIRECTSOUND direct_sound = NULL;
    HRESULT result = DirectSoundCreate(NULL, &direct_sound, NULL);
    xt_chk("stream.device_created", 1, SUCCEEDED(result) && direct_sound != NULL);
    if(FAILED(result) || direct_sound == NULL)
    {
        xt_end_and_exit();
    }

    WAVEFORMATEX format;
    ZeroMemory(&format, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 44100;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 176400;

    DSSTREAMDESC descriptor;
    ZeroMemory(&descriptor, sizeof(descriptor));
    descriptor.dwMaxAttachedPackets = 2;
    descriptor.lpwfxFormat = &format;
    descriptor.lpfnCallback = stream_callback;
    descriptor.lpvContext = (LPVOID)0x5354524Du;

    LPDIRECTSOUNDSTREAM stream = NULL;
    result = DirectSoundCreateStream(&descriptor, &stream);
    xt_chk("stream.created", 1, SUCCEEDED(result) && stream != NULL);
    if(FAILED(result) || stream == NULL)
    {
        xt_end_and_exit();
    }

    DWORD stream_status = 0;
    result = stream->GetStatus(&stream_status);
    xt_chk("stream.status_ready", 1,
           SUCCEEDED(result) && (stream_status & DSSTREAMSTATUS_READY) != 0);

    for(DWORD sample = 0; sample < PACKET_BYTES / sizeof(short); sample++)
    {
        g_pcm[sample] = ((sample / 50) & 1) != 0 ? 5000 : -5000;
    }

    DWORD completed = 0xFFFFFFFF;
    DWORD packet_status = 0;
    XMEDIAPACKET packet;
    ZeroMemory(&packet, sizeof(packet));
    packet.pvBuffer = g_pcm;
    packet.dwMaxSize = PACKET_BYTES;
    packet.pdwCompletedSize = &completed;
    packet.pdwStatus = &packet_status;
    packet.pContext = (LPVOID)0x11111111u;

    result = stream->Process(&packet, NULL);
    xt_chk("stream.process_accepted", 1, SUCCEEDED(result));
    xt_chk("stream.packet_pending", 1,
           packet_status == (DWORD)XMEDIAPACKET_STATUS_PENDING && completed == 0);

    for(DWORD attempt = 0; attempt < 100 && g_callback_count == 0; attempt++)
    {
        DirectSoundDoWork();
        Sleep(10);
    }
    DirectSoundDoWork();

    xt_chk_u32("stream.packet_success", (DWORD)XMEDIAPACKET_STATUS_SUCCESS, packet_status);
    xt_chk_u32("stream.completed_size", PACKET_BYTES, completed);
    xt_chk_u32("stream.callback_count", 1, g_callback_count);
    xt_chk_u32("stream.callback_status", (DWORD)XMEDIAPACKET_STATUS_SUCCESS, g_callback_status[0]);
    xt_chk_u32("stream.callback_context", 0x11111111u, g_callback_context[0]);

    completed = 0xFFFFFFFF;
    packet_status = 0;
    packet.pContext = (LPVOID)0x22222222u;
    result = stream->Process(&packet, NULL);
    xt_chk("stream.flush_packet_accepted", 1, SUCCEEDED(result));
    result = stream->Flush();
    xt_chk("stream.flush_succeeded", 1, SUCCEEDED(result));
    xt_chk_u32("stream.flush_status", (DWORD)XMEDIAPACKET_STATUS_FLUSHED, packet_status);
    xt_chk_u32("stream.flush_completed_size", 0, completed);
    xt_chk_u32("stream.flush_callback_count", 2, g_callback_count);
    xt_chk_u32("stream.flush_callback_status", (DWORD)XMEDIAPACKET_STATUS_FLUSHED, g_callback_status[1]);
    xt_chk_u32("stream.flush_callback_context", 0x22222222u, g_callback_context[1]);

    ULONG references = stream->Release();
    xt_chk_u32("stream.release_zero", 0, references);

    direct_sound->Release();
    xt_end_and_exit();
}
