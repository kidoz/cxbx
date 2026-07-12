#include "core/trace.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct StaticFlightProbe
{
    StaticFlightProbe() noexcept
    {
        cxbx::trace::RecordFlight(cxbx::trace::Event::KernelBoundary, 0xCAFEU);
    }
};

StaticFlightProbe g_StaticFlightProbe{};

[[noreturn]] void Fail(const char* message)
{
    std::fprintf(stderr, "trace_core_test: %s\n", message);
    std::exit(1);
}

std::string ReadAll(std::FILE* file)
{
    if(std::fflush(file) != 0 || std::fseek(file, 0, SEEK_END) != 0)
    {
        Fail("could not seek trace output");
    }
    const long length = std::ftell(file);
    if(length < 0 || std::fseek(file, 0, SEEK_SET) != 0)
    {
        Fail("could not measure trace output");
    }
    std::string text(static_cast<std::size_t>(length), '\0');
    if(!text.empty() && std::fread(text.data(), 1, text.size(), file) != text.size())
    {
        Fail("could not read trace output");
    }
    return text;
}

struct FlightSummary
{
    bool staticRecord = false;
    bool childRecord = false;
    bool fallbackRecord = false;
};

void CollectFlight(const cxbx::trace::TraceRecord& record, void* context) noexcept
{
    auto& summary = *static_cast<FlightSummary*>(context);
    summary.staticRecord |= record.argument == 0xCAFEU;
    summary.childRecord |= record.argument == 0xBEEFU;
    summary.fallbackRecord |= record.argument == 0xF00DU;
}

struct EventFileHeader
{
    char magic[8];
    std::uint16_t fileVersion;
    std::uint16_t grammarVersion;
    std::uint16_t recordSchemaVersion;
    std::uint16_t headerSize;
    std::uint64_t tickFrequency;
    std::uint32_t recordSize;
    std::uint32_t reserved;
};

static_assert(sizeof(EventFileHeader) == 32);

void CheckEventFile(const char* path)
{
    std::FILE* file = nullptr;
    if(fopen_s(&file, path, "rb") != 0 || file == nullptr)
    {
        Fail("could not open binary trace output");
    }
    EventFileHeader header{};
    if(std::fread(&header, sizeof(header), 1, file) != 1 ||
       std::memcmp(header.magic, "CXBXEVT", 7) != 0 || header.fileVersion != 1 ||
       header.grammarVersion != cxbx::trace::kTraceGrammarVersion ||
       header.recordSchemaVersion != cxbx::trace::kTraceRecordSchemaVersion ||
       header.headerSize != sizeof(header) || header.recordSize != sizeof(cxbx::trace::TraceRecord) ||
       header.tickFrequency == 0)
    {
        std::fclose(file);
        Fail("binary trace header mismatch");
    }

    std::vector<cxbx::trace::TraceRecord> records;
    cxbx::trace::TraceRecord record{};
    while(std::fread(&record, sizeof(record), 1, file) == 1)
    {
        records.push_back(record);
    }
    std::fclose(file);

    bool d3d = false;
    bool mmio = false;
    bool attached = false;
    bool d3dCall = false;
    bool d3dWait = false;
    bool d3dReturn = false;
    bool nv2aMethod = false;
    bool singleStep = false;
    bool mediaUpdate = false;
    bool mediaPresent = false;
    bool audioQueued = false;
    bool audioCompleted = false;
    bool audioStarved = false;
    bool waitSatisfied = false;
    bool waitTimeout = false;
    for(std::size_t index = 0; index < records.size(); ++index)
    {
        const cxbx::trace::TraceRecord& item = records[index];
        d3d |= item.event == static_cast<std::uint16_t>(cxbx::trace::Event::D3dBoundary) &&
               item.argument == 0x1234U;
        mmio |= item.event == static_cast<std::uint16_t>(cxbx::trace::Event::MmioAccess) &&
                item.argument == 0x5678U;
        attached |= item.event ==
                    static_cast<std::uint16_t>(cxbx::trace::Event::ThreadAttach);
        if(item.event == static_cast<std::uint16_t>(cxbx::trace::Event::D3dCall) &&
           index + 3 < records.size())
        {
            d3dCall = item.argument == static_cast<std::uint32_t>(cxbx::trace::D3dApi::Clear) &&
                      records[index + 1].argument == item.localSequence &&
                      records[index + 2].argument == 1 &&
                      records[index + 3].argument == 0xD3D0BEEFU;
        }
        if(item.event == static_cast<std::uint16_t>(cxbx::trace::Event::D3dWait) &&
           index + 3 < records.size())
        {
            d3dWait =
                item.argument ==
                    static_cast<std::uint32_t>(cxbx::trace::D3dWaitReason::SingleStep) &&
                records[index + 2].argument == 7 && records[index + 3].argument == 1;
        }
        if(item.event == static_cast<std::uint16_t>(cxbx::trace::Event::D3dReturn) &&
           index + 2 < records.size())
        {
            d3dReturn =
                item.argument == static_cast<std::uint32_t>(cxbx::trace::D3dApi::Clear) &&
                records[index + 2].argument == 0x8876086CU;
        }
        if(item.event == static_cast<std::uint16_t>(cxbx::trace::Event::Nv2aMethod) &&
           index + 2 < records.size())
        {
            nv2aMethod = item.argument == 0x97 && records[index + 1].argument == 0x300 &&
                         records[index + 2].argument == 0xA5A5A5A5U;
        }
        if(item.event ==
               static_cast<std::uint16_t>(cxbx::trace::Event::SingleStepInstruction) &&
           index + 1 < records.size())
        {
            singleStep = item.argument == 0x1000 && records[index + 1].argument == 0x2000;
        }
        mediaUpdate |=
            item.event ==
                static_cast<std::uint16_t>(cxbx::trace::Event::MediaOverlayUpdate) &&
            item.argument == 41;
        mediaPresent |=
            item.event == static_cast<std::uint16_t>(cxbx::trace::Event::MediaPresent) &&
            item.argument == 41;
        audioQueued |=
            item.event ==
                static_cast<std::uint16_t>(cxbx::trace::Event::AudioPacketQueued) &&
            item.argument == 4096;
        audioCompleted |=
            item.event ==
                static_cast<std::uint16_t>(cxbx::trace::Event::AudioPacketCompleted) &&
            item.argument == 4096;
        audioStarved |=
            item.event == static_cast<std::uint16_t>(cxbx::trace::Event::AudioStarved) &&
            item.argument == 8192;
        waitSatisfied |=
            item.event ==
                static_cast<std::uint16_t>(cxbx::trace::Event::SyncWaitSatisfied) &&
            item.argument == 2;
        waitTimeout |=
            item.event ==
                static_cast<std::uint16_t>(cxbx::trace::Event::SyncWaitTimeout) &&
            item.argument == 16;
    }
    if(!d3d || !mmio || !attached || !d3dCall || !d3dWait || !d3dReturn ||
       !nv2aMethod || !singleStep || !mediaUpdate || !mediaPresent || !audioQueued ||
       !audioCompleted || !audioStarved || !waitSatisfied || !waitTimeout)
    {
        Fail("binary trace records are incomplete");
    }
}

} // namespace

int Run()
{
    constexpr char eventPath[] = "trace_core_test.evt";
    std::remove(eventPath);
    if(_putenv_s("CXBX_TRACE_EVT_FILE", eventPath) != 0)
    {
        Fail("could not configure binary trace output");
    }

    cxbx::trace::TraceRecord initialFlight[2]{};
    if(cxbx::trace::CopyCurrentFlight(initialFlight, 2) != 1 ||
       initialFlight[0].event !=
           static_cast<std::uint16_t>(cxbx::trace::Event::KernelBoundary) ||
       initialFlight[0].argument != 0xCAFEU || initialFlight[0].localSequence != 1)
    {
        Fail("flight recorder is not safe during static initialization");
    }

    std::FILE* output = nullptr;
    if(tmpfile_s(&output) != 0 || output == nullptr)
    {
        Fail("tmpfile failed");
    }

    cxbx::trace::Initialize(output);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::D3d, true);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::Mmio, true);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::Nv2a, true);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::SingleStep, true);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::Media, true);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::Audio, true);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::Sync, true);
    cxbx::trace::RecordBinary(cxbx::trace::Event::D3dBoundary, 0x1234U);
    const std::uint32_t d3dSequence =
        cxbx::trace::RecordD3dCall(cxbx::trace::D3dApi::Clear, 1, 0xD3D0BEEFU);
    cxbx::trace::RecordD3dWait(cxbx::trace::D3dWaitReason::SingleStep, d3dSequence, 7,
                               true);
    cxbx::trace::RecordD3dReturn(cxbx::trace::D3dApi::Clear, d3dSequence, 0x8876086CU);
    cxbx::trace::RecordNv2aMethod(0x97, 0x300, 0xA5A5A5A5U);
    cxbx::trace::RecordSingleStep(0x1000, 0x2000);

    std::thread child([]()
                      {
        cxbx::trace::RecordFlight(cxbx::trace::Event::D3dBoundary, 0xBEEFU);
        cxbx::trace::RecordBinary(cxbx::trace::Event::MmioAccess, 0x5678U); });
    child.join();
    cxbx::trace::RecordFlightFallback(cxbx::trace::Event::MmioAccess, 0xF00DU);
    FlightSummary flightSummary{};
    cxbx::trace::VisitAllFlight(CollectFlight, &flightSummary);
    if(!flightSummary.staticRecord || !flightSummary.childRecord ||
       !flightSummary.fallbackRecord)
    {
        Fail("all-thread flight visitation missed a persistent record");
    }

    int evaluated = 0;
    CXBX_TRACE_TEXT(cxbx::trace::Channel::Kernel, "call", "value=%d", ++evaluated);
    if(evaluated != 0)
    {
        Fail("disabled channel evaluated macro arguments");
    }

    CXBX_TRACE_TEXT(cxbx::trace::Channel::D3d, "call", "api=Clear flags=0x%08X", 1U);
    cxbx::trace::Warn(cxbx::trace::Channel::D3d, "ret", "api=Clear hr=0x%08X", 0U);
    const std::string longPayload(400, 'x');
    CXBX_TRACE_TEXT(cxbx::trace::Channel::D3d, "call", "payload=%s", longPayload.c_str());
    cxbx::trace::RecordBinary(cxbx::trace::Event::MediaOverlayUpdate, 41);
    cxbx::trace::RecordBinary(cxbx::trace::Event::MediaPresent, 41);
    cxbx::trace::RecordBinary(cxbx::trace::Event::AudioPacketQueued, 4096);
    cxbx::trace::RecordBinary(cxbx::trace::Event::AudioPacketCompleted, 4096);
    cxbx::trace::RecordBinary(cxbx::trace::Event::AudioStarved, 8192);
    cxbx::trace::RecordBinary(cxbx::trace::Event::SyncWaitSatisfied, 2);
    cxbx::trace::RecordBinary(cxbx::trace::Event::SyncWaitTimeout, 16);
    cxbx::trace::Flush();
    cxbx::trace::DumpFlightEmergency();
    cxbx::trace::Shutdown();
    if(_putenv_s("CXBX_TRACE_EVT_FILE", "") != 0)
    {
        Fail("could not clear binary trace output configuration");
    }

    const std::string text = ReadAll(output);
    std::fclose(output);

    if(text.find("TRACE| v=2 start qpc_hz=") != 0)
    {
        Fail("missing eager version banner");
    }
    if(text.find("D3D| call tick=") == std::string::npos ||
       text.find(" lseq=8 api=Clear flags=0x00000001") == std::string::npos)
    {
        Fail("canonical call envelope mismatch");
    }
    if(text.find("D3D| ret tick=") == std::string::npos ||
       text.find(" lseq=9 api=Clear hr=0x00000000") == std::string::npos)
    {
        Fail("canonical return envelope mismatch");
    }
    if(text.find(" lseq=10 payload=") == std::string::npos ||
       text.find("...!\n") == std::string::npos)
    {
        Fail("truncated payload marker mismatch");
    }
    if(text.find("FLIGHT| tick=") == std::string::npos ||
       text.find(" arg=0x0000CAFE\n") == std::string::npos ||
       text.find(" arg=0x0000BEEF\n") == std::string::npos ||
       text.find(" arg=0x0000F00D\n") == std::string::npos ||
       text.find("FLIGHT| fallback_dropped=0\n") == std::string::npos)
    {
        Fail("emergency flight output is incomplete");
    }

    CheckEventFile(eventPath);
    std::remove(eventPath);

    return 0;
}

int main() noexcept
{
    try
    {
        return Run();
    }
    catch(...)
    {
        std::fprintf(stderr, "trace_core_test: unexpected exception\n");
        return 1;
    }
}
