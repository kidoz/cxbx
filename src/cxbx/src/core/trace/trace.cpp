// Runtime-owned structured text tracing.
#include "core/trace.h"
#include "core/trace_atomic_flight.h"
#include "core/trace_ring.h"

#include "../../platform/win32/trace_os.h"

#include <cinttypes>
#include <cstring>
#include <limits>

namespace cxbx::trace
{
namespace
{

constexpr std::size_t kLineCapacity = 512;
constexpr std::size_t kPayloadCapacity = 320;
constexpr std::uint32_t kFlushLineInterval = 256;
constexpr std::uint64_t kFlushTimeDivisor = 10;
constexpr std::size_t kFlightThreadCapacity = 64;
constexpr std::size_t kFlightRecordCapacity = 256;
constexpr std::size_t kBinaryRingCapacity = 1024;
constexpr std::size_t kBinaryDrainCapacity = 256;

struct TraceEventFileHeader
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

static_assert(sizeof(TraceEventFileHeader) == 32);

struct TraceState
{
    std::FILE* output = nullptr;
    std::uint64_t startTick = 0;
    std::uint64_t tickFrequency = 1;
    std::atomic<std::uint32_t> linesSinceFlush{ 0 };
    std::atomic<std::uint64_t> lastFlushTick{ 0 };
    std::atomic<bool> initialized{ false };
};

TraceState g_State{};
std::atomic<std::uint64_t> g_ClockOrigin{ 0 };

struct FlightThreadSlot
{
    std::atomic<std::uint32_t> osThreadId{ 0 };
    TraceConcurrentFlightRing<kFlightRecordCapacity> flight{};
    TraceSpscRing<kBinaryRingCapacity> binary{};
};

FlightThreadSlot g_FlightThreads[kFlightThreadCapacity]{};
TraceMpscFlightRing<kFlightRecordCapacity> g_FlightFallback{};
std::atomic<std::uint32_t> g_NextFlightThread{ 0 };
std::atomic<std::uint32_t> g_UnregisteredBinaryDropped{ 0 };
std::atomic_flag g_EmergencyDumping = ATOMIC_FLAG_INIT;

struct BinaryState
{
    std::FILE* output = nullptr;
    std::atomic<bool> accepting{ false };
    std::atomic<bool> stopRequested{ false };
    std::atomic<bool> writeFailed{ false };
};

BinaryState g_BinaryState{};

struct ThreadTraceState
{
    std::uint64_t localSequence = 0;
    std::uint16_t threadIndex = 0;
    FlightThreadSlot* flight = nullptr;
    bool usesFallback = false;
};

thread_local ThreadTraceState g_ThreadState{};

[[nodiscard]] std::uint64_t FlightTick() noexcept;

void AttachFlightThread() noexcept
{
    if(g_ThreadState.flight != nullptr || g_ThreadState.usesFallback)
    {
        return;
    }
    const std::uint32_t index = g_NextFlightThread.fetch_add(1, std::memory_order_relaxed);
    if(index < kFlightThreadCapacity)
    {
        g_ThreadState.flight = &g_FlightThreads[index];
        g_ThreadState.threadIndex = static_cast<std::uint16_t>(index + 1);
        g_ThreadState.flight->osThreadId.store(TraceOsThreadId(), std::memory_order_release);
    }
    else
    {
        g_ThreadState.usesFallback = true;
        g_ThreadState.threadIndex = UINT16_MAX;
    }
}

[[nodiscard]] TraceRecord MakeThreadRecord(Event event, std::uint32_t argument) noexcept
{
    return {
        FlightTick(),
        ++g_ThreadState.localSequence,
        static_cast<std::uint16_t>(event),
        g_ThreadState.threadIndex,
        argument,
    };
}

[[nodiscard]] std::uint64_t FlightTick() noexcept
{
    const std::uint64_t now = TraceOsNowTicks();
    const std::uint64_t origin = g_ClockOrigin.load(std::memory_order_relaxed);
    return origin != 0 && now >= origin ? now - origin : now;
}

[[nodiscard]] bool WriteBinaryRecords(const TraceRecord* records, std::size_t count) noexcept
{
    if(g_BinaryState.output == nullptr || records == nullptr || count == 0)
    {
        return false;
    }
    if(std::fwrite(records, sizeof(TraceRecord), count, g_BinaryState.output) == count)
    {
        return true;
    }
    g_BinaryState.writeFailed.store(true, std::memory_order_release);
    g_BinaryState.accepting.store(false, std::memory_order_release);
    return false;
}

[[nodiscard]] bool DrainBinaryOnce(bool* announcedThreads) noexcept
{
    bool wrote = false;
    const std::uint32_t unregisteredDropped =
        g_UnregisteredBinaryDropped.exchange(0, std::memory_order_acq_rel);
    if(unregisteredDropped != 0)
    {
        const TraceRecord marker{
            FlightTick(),
            0,
            static_cast<std::uint16_t>(Event::BinaryDropped),
            UINT16_MAX,
            unregisteredDropped,
        };
        wrote = WriteBinaryRecords(&marker, 1);
        if(g_BinaryState.writeFailed.load(std::memory_order_acquire))
        {
            return wrote;
        }
    }
    const std::uint32_t assigned = g_NextFlightThread.load(std::memory_order_acquire);
    const std::size_t count = assigned < kFlightThreadCapacity ? assigned
                                                               : kFlightThreadCapacity;
    for(std::size_t index = 0; index < count; ++index)
    {
        FlightThreadSlot& slot = g_FlightThreads[index];
        const std::uint16_t threadIndex = static_cast<std::uint16_t>(index + 1);
        if(!announcedThreads[index])
        {
            const std::uint32_t osThreadId = slot.osThreadId.load(std::memory_order_acquire);
            if(osThreadId != 0)
            {
                const TraceRecord attached{
                    FlightTick(),
                    0,
                    static_cast<std::uint16_t>(Event::ThreadAttach),
                    threadIndex,
                    osThreadId,
                };
                wrote |= WriteBinaryRecords(&attached, 1);
                if(g_BinaryState.writeFailed.load(std::memory_order_acquire))
                {
                    return wrote;
                }
                announcedThreads[index] = true;
            }
        }

        const std::uint64_t dropped = slot.binary.TakeDroppedEvents();
        if(dropped != 0)
        {
            const TraceRecord marker{
                FlightTick(),
                0,
                static_cast<std::uint16_t>(Event::BinaryDropped),
                threadIndex,
                dropped > std::numeric_limits<std::uint32_t>::max()
                    ? std::numeric_limits<std::uint32_t>::max()
                    : static_cast<std::uint32_t>(dropped),
            };
            wrote |= WriteBinaryRecords(&marker, 1);
            if(g_BinaryState.writeFailed.load(std::memory_order_acquire))
            {
                return wrote;
            }
        }

        TraceRecord records[kBinaryDrainCapacity]{};
        std::size_t drained = 0;
        do
        {
            drained = slot.binary.Pop(records, kBinaryDrainCapacity);
            if(drained != 0)
            {
                wrote |= WriteBinaryRecords(records, drained);
                if(g_BinaryState.writeFailed.load(std::memory_order_acquire))
                {
                    return wrote;
                }
            }
        } while(drained == kBinaryDrainCapacity);
    }
    return wrote;
}

void BinaryDrainer(void*) noexcept
{
    bool announcedThreads[kFlightThreadCapacity]{};
    while(!g_BinaryState.stopRequested.load(std::memory_order_acquire))
    {
        if(DrainBinaryOnce(announcedThreads))
        {
            static_cast<void>(std::fflush(g_BinaryState.output));
        }
        if(g_BinaryState.writeFailed.load(std::memory_order_acquire))
        {
            break;
        }
        TraceOsSleep(10);
    }
    if(DrainBinaryOnce(announcedThreads))
    {
        static_cast<void>(std::fflush(g_BinaryState.output));
    }
}

void StartBinarySink() noexcept
{
    char path[1024]{};
    if(!TraceOsReadEnvironment("CXBX_TRACE_EVT_FILE", path, sizeof(path)))
    {
        return;
    }

    std::FILE* output = nullptr;
    if(fopen_s(&output, path, "wb") != 0 || output == nullptr)
    {
        return;
    }
    const TraceEventFileHeader header{
        { 'C', 'X', 'B', 'X', 'E', 'V', 'T', '\0' },
        kTraceEventFileVersion,
        kTraceGrammarVersion,
        kTraceRecordSchemaVersion,
        static_cast<std::uint16_t>(sizeof(TraceEventFileHeader)),
        g_State.tickFrequency,
        static_cast<std::uint32_t>(sizeof(TraceRecord)),
        0,
    };
    if(std::fwrite(&header, sizeof(header), 1, output) != 1 || std::fflush(output) != 0)
    {
        std::fclose(output);
        return;
    }

    g_BinaryState.output = output;
    g_BinaryState.stopRequested.store(false, std::memory_order_relaxed);
    g_BinaryState.writeFailed.store(false, std::memory_order_relaxed);
    g_UnregisteredBinaryDropped.store(0, std::memory_order_relaxed);
    if(!TraceOsStartBackgroundThread(BinaryDrainer, nullptr))
    {
        std::fclose(output);
        g_BinaryState.output = nullptr;
        return;
    }
    g_BinaryState.accepting.store(true, std::memory_order_release);
}

void StopBinarySink() noexcept
{
    g_BinaryState.accepting.store(false, std::memory_order_release);
    if(g_BinaryState.output == nullptr)
    {
        return;
    }
    g_BinaryState.stopRequested.store(true, std::memory_order_release);
    TraceOsJoinBackgroundThread();
    static_cast<void>(std::fflush(g_BinaryState.output));
    std::fclose(g_BinaryState.output);
    g_BinaryState.output = nullptr;
}

struct EmergencyLine
{
    char data[160]{};
    std::size_t length = 0;
};

void AppendCharacter(EmergencyLine& line, char character) noexcept
{
    if(line.length < sizeof(line.data))
    {
        line.data[line.length++] = character;
    }
}

void AppendLiteral(EmergencyLine& line, const char* text) noexcept
{
    if(text == nullptr)
    {
        return;
    }
    while(*text != '\0')
    {
        AppendCharacter(line, *text++);
    }
}

void AppendDecimal(EmergencyLine& line, std::uint64_t value) noexcept
{
    char digits[20]{};
    std::size_t count = 0;
    do
    {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while(value != 0 && count < sizeof(digits));
    while(count > 0)
    {
        AppendCharacter(line, digits[--count]);
    }
}

void AppendHex8(EmergencyLine& line, std::uint32_t value) noexcept
{
    constexpr char digits[] = "0123456789ABCDEF";
    AppendLiteral(line, "0x");
    for(unsigned shift = 28; shift <= 28; shift -= 4)
    {
        AppendCharacter(line, digits[(value >> shift) & 0xFU]);
        if(shift == 0)
        {
            break;
        }
    }
}

void WriteEmergencyRecord(const TraceRecord& record, void*) noexcept
{
    EmergencyLine line{};
    AppendLiteral(line, "FLIGHT| tick=");
    AppendDecimal(line, record.tick);
    AppendLiteral(line, " tid_ix=");
    AppendDecimal(line, record.threadIndex);
    AppendLiteral(line, " lseq=");
    AppendDecimal(line, record.localSequence);
    AppendLiteral(line, " event=");
    AppendDecimal(line, record.event);
    AppendLiteral(line, " arg=");
    AppendHex8(line, record.argument);
    AppendCharacter(line, '\n');
    TraceOsEmergencyWrite(line.data, line.length);
}

[[nodiscard]] const ChannelDefinition& Info(Channel channel) noexcept
{
    return kChannelDefinitions[static_cast<std::size_t>(channel)];
}

[[nodiscard]] bool IsBinaryEventEnabled(Event event) noexcept
{
    const Channel channel = EventChannel(event);
    return channel != Channel::Count && IsEnabled(channel);
}

[[nodiscard]] std::uint32_t RecordLogicalEvent(
    Event event, const Event* continuations, const std::uint32_t* arguments,
    std::size_t argumentCount, std::size_t sequenceArgument) noexcept
{
    constexpr std::size_t kMaximumLogicalRecords = 4;
    if(arguments == nullptr || argumentCount == 0 ||
       argumentCount > kMaximumLogicalRecords ||
       (argumentCount > 1 && continuations == nullptr))
    {
        return 0;
    }

    AttachFlightThread();
    const std::uint64_t tick = FlightTick();
    const std::uint64_t localSequence = ++g_ThreadState.localSequence;
    TraceRecord records[kMaximumLogicalRecords]{};
    for(std::size_t index = 0; index < argumentCount; ++index)
    {
        const std::uint32_t argument = index == sequenceArgument
                                           ? static_cast<std::uint32_t>(localSequence)
                                           : arguments[index];
        records[index] = {
            tick,
            localSequence,
            static_cast<std::uint16_t>(index == 0 ? event : continuations[index - 1]),
            g_ThreadState.threadIndex,
            argument,
        };
    }

    if(g_ThreadState.flight == nullptr)
    {
        for(std::size_t index = 0; index < argumentCount; ++index)
        {
            g_FlightFallback.Record(records[index]);
        }
        if(IsBinaryEventEnabled(event) &&
           g_BinaryState.accepting.load(std::memory_order_acquire))
        {
            g_UnregisteredBinaryDropped.fetch_add(1, std::memory_order_relaxed);
        }
        return static_cast<std::uint32_t>(localSequence);
    }

    for(std::size_t index = 0; index < argumentCount; ++index)
    {
        g_ThreadState.flight->flight.Record(records[index]);
    }
    if(IsBinaryEventEnabled(event) &&
       g_BinaryState.accepting.load(std::memory_order_acquire))
    {
        static_cast<void>(g_ThreadState.flight->binary.Push(records, argumentCount));
    }
    return static_cast<std::uint32_t>(localSequence);
}

[[nodiscard]] char AsciiLower(char character) noexcept
{
    if(character >= 'A' && character <= 'Z')
    {
        return static_cast<char>(character - 'A' + 'a');
    }
    return character;
}

[[nodiscard]] bool KeyEquals(const char* token, std::size_t length,
                             const char* expected) noexcept
{
    while(length > 0 && (*token == ' ' || *token == '\t'))
    {
        ++token;
        --length;
    }
    while(length > 0 && (token[length - 1] == ' ' || token[length - 1] == '\t'))
    {
        --length;
    }
    if(std::strlen(expected) != length)
    {
        return false;
    }
    for(std::size_t index = 0; index < length; ++index)
    {
        if(AsciiLower(token[index]) != expected[index])
        {
            return false;
        }
    }
    return true;
}

void EnableConfiguredChannels() noexcept
{
    std::uint32_t mask = 0;
    for(const ChannelDefinition& info : kChannelDefinitions)
    {
        if(info.available && info.environmentAlias != nullptr &&
           TraceOsEnvironmentExists(info.environmentAlias))
        {
            mask |= ChannelBit(info.channel);
        }
    }

    char configured[1024]{};
    if(TraceOsReadEnvironment("CXBX_TRACE", configured, sizeof(configured)))
    {
        std::size_t offset = 0;
        const std::size_t listLength = std::strlen(configured);
        while(offset <= listLength)
        {
            const char* token = configured + offset;
            const char* comma = std::strchr(token, ',');
            const std::size_t count = comma == nullptr
                                          ? listLength - offset
                                          : static_cast<std::size_t>(comma - token);
            for(const ChannelDefinition& info : kChannelDefinitions)
            {
                if(info.available &&
                   (KeyEquals(token, count, "all") || KeyEquals(token, count, info.key)))
                {
                    mask |= ChannelBit(info.channel);
                }
            }
            if(comma == nullptr)
            {
                break;
            }
            offset = static_cast<std::size_t>(comma - configured) + 1;
        }
    }
    g_TraceMask.store(mask, std::memory_order_relaxed);
}

void WriteRaw(const char* line, std::size_t length) noexcept
{
    if(g_State.output == nullptr)
    {
        return;
    }
    static_cast<void>(std::fwrite(line, 1, length, g_State.output));
}

void MaybeFlush(std::uint64_t now) noexcept
{
    const std::uint32_t lines = g_State.linesSinceFlush.fetch_add(1, std::memory_order_relaxed) + 1;
    const std::uint64_t last = g_State.lastFlushTick.load(std::memory_order_relaxed);
    const std::uint64_t timeInterval = g_State.tickFrequency / kFlushTimeDivisor;
    if(lines < kFlushLineInterval && (now - last) < timeInterval)
    {
        return;
    }

    g_State.linesSinceFlush.store(0, std::memory_order_relaxed);
    g_State.lastFlushTick.store(now, std::memory_order_relaxed);
    static_cast<void>(std::fflush(g_State.output));
}

void Emit(Channel channel, const char* verb, const char* format, std::va_list arguments,
          bool flushImmediately) noexcept
{
    if(!g_State.initialized.load(std::memory_order_acquire) || !IsAvailable(channel) ||
       g_State.output == nullptr || format == nullptr)
    {
        return;
    }

    char payload[kPayloadCapacity]{};
    const int payloadLength = std::vsnprintf(payload, sizeof(payload), format, arguments);
    if(payloadLength < 0)
    {
        std::memcpy(payload, "format-error", sizeof("format-error"));
    }
    else if(static_cast<std::size_t>(payloadLength) >= sizeof(payload))
    {
        constexpr char marker[] = "...!";
        std::memcpy(payload + sizeof(payload) - sizeof(marker), marker, sizeof(marker));
    }

    const std::uint64_t now = TraceOsNowTicks();
    const std::uint64_t origin = g_ClockOrigin.load(std::memory_order_relaxed);
    const std::uint64_t tick = origin != 0 && now >= origin ? now - origin : now;
    const std::uint64_t localSequence = ++g_ThreadState.localSequence;
    const ChannelDefinition& info = Info(channel);

    char line[kLineCapacity]{};
    const char* safeVerb = verb == nullptr ? "" : verb;
    const char* separator = safeVerb[0] == '\0' ? "" : " ";
    const int lineLength = std::snprintf(
        line, sizeof(line), "%s| %s%stick=%" PRIu64 " tid=0x%08" PRIX32 " lseq=%" PRIu64 " %s\n",
        info.prefix, safeVerb, separator, tick, TraceOsThreadId(), localSequence, payload);
    if(lineLength <= 0)
    {
        return;
    }
    const std::size_t writeLength = static_cast<std::size_t>(lineLength) < sizeof(line)
                                        ? static_cast<std::size_t>(lineLength)
                                        : sizeof(line) - 1;
    WriteRaw(line, writeLength);

    if(flushImmediately)
    {
        Flush();
    }
    else
    {
        MaybeFlush(now);
    }
}

} // namespace

void Initialize(std::FILE* output) noexcept
{
    if(output == nullptr || g_State.initialized.load(std::memory_order_acquire))
    {
        return;
    }

    g_State.output = output;
    TraceOsEmergencyInitialize(output);
    g_State.tickFrequency = TraceOsTickFrequency();
    g_State.startTick = TraceOsNowTicks();
    g_ClockOrigin.store(g_State.startTick, std::memory_order_relaxed);
    g_State.lastFlushTick.store(g_State.startTick, std::memory_order_relaxed);
    g_State.linesSinceFlush.store(0, std::memory_order_relaxed);
    StartBinarySink();
    EnableConfiguredChannels();
    g_State.initialized.store(true, std::memory_order_release);

    char banner[128]{};
    const int length = std::snprintf(banner, sizeof(banner),
                                     "TRACE| v=2 start qpc_hz=%" PRIu64 "\n",
                                     g_State.tickFrequency);
    if(length > 0)
    {
        WriteRaw(banner, static_cast<std::size_t>(length));
    }
    for(const ChannelDefinition& info : kChannelDefinitions)
    {
        char channelLine[192]{};
        const bool enabled = (g_TraceMask.load(std::memory_order_relaxed) &
                              ChannelBit(info.channel)) != 0;
        const int channelLength = std::snprintf(
            channelLine, sizeof(channelLine),
            "TRACE| v=2 channel=%s state=%s tier=%s\n", info.key,
            info.available ? (enabled ? "enabled" : "disabled") : "unavailable",
            info.defaultTier);
        if(channelLength > 0)
        {
            WriteRaw(channelLine, static_cast<std::size_t>(channelLength));
        }
    }
    Flush();
}

void Shutdown() noexcept
{
    if(!g_State.initialized.exchange(false, std::memory_order_acq_rel))
    {
        return;
    }
    StopBinarySink();
    if(g_State.output != nullptr)
    {
        static_cast<void>(std::fflush(g_State.output));
    }
    g_State.output = nullptr;
    TraceOsEmergencyShutdown();
    g_TraceMask.store(0, std::memory_order_relaxed);
}

void Flush() noexcept
{
    if(g_State.output == nullptr)
    {
        return;
    }
    static_cast<void>(std::fflush(g_State.output));
    g_State.linesSinceFlush.store(0, std::memory_order_relaxed);
    g_State.lastFlushTick.store(TraceOsNowTicks(), std::memory_order_relaxed);
}

void SetEnabled(Channel channel, bool enabled) noexcept
{
    if(!IsAvailable(channel))
    {
        return;
    }
    if(enabled)
    {
        g_TraceMask.fetch_or(ChannelBit(channel), std::memory_order_relaxed);
    }
    else
    {
        g_TraceMask.fetch_and(~ChannelBit(channel), std::memory_order_relaxed);
    }
}

bool IsAvailable(Channel channel) noexcept
{
    const auto index = static_cast<std::size_t>(channel);
    return index < static_cast<std::size_t>(Channel::Count) &&
           kChannelDefinitions[index].available;
}

void RecordFlight(Event event, std::uint32_t argument) noexcept
{
    AttachFlightThread();
    const TraceRecord record = MakeThreadRecord(event, argument);
    if(g_ThreadState.flight != nullptr)
    {
        g_ThreadState.flight->flight.Record(record);
    }
    else
    {
        g_FlightFallback.Record(record);
    }
}

void RecordBinary(Event event, std::uint32_t argument) noexcept
{
    static_cast<void>(RecordLogicalEvent(event, nullptr, &argument, 1, SIZE_MAX));
}

std::uint32_t RecordD3dCall(D3dApi api, std::uint32_t flags,
                            std::uint32_t marker) noexcept
{
    constexpr Event continuations[]{ Event::Continuation0, Event::Continuation1,
                                     Event::Continuation2 };
    const std::uint32_t arguments[]{ static_cast<std::uint32_t>(api), 0, flags, marker };
    return RecordLogicalEvent(Event::D3dCall, continuations, arguments, 4, 1);
}

void RecordD3dReturn(D3dApi api, std::uint32_t sequence, std::uint32_t result) noexcept
{
    constexpr Event continuations[]{ Event::Continuation0, Event::Continuation1 };
    const std::uint32_t arguments[]{ static_cast<std::uint32_t>(api), sequence, result };
    static_cast<void>(RecordLogicalEvent(Event::D3dReturn, continuations, arguments, 3,
                                         SIZE_MAX));
}

void RecordD3dWait(D3dWaitReason reason, std::uint32_t sequence, std::uint32_t handle,
                   bool pending) noexcept
{
    constexpr Event continuations[]{ Event::Continuation0, Event::Continuation1,
                                     Event::Continuation2 };
    const std::uint32_t arguments[]{ static_cast<std::uint32_t>(reason), sequence, handle,
                                     pending ? 1U : 0U };
    static_cast<void>(RecordLogicalEvent(Event::D3dWait, continuations, arguments, 4,
                                         sequence == 0 ? 1 : SIZE_MAX));
}

void RecordNv2aRegister(bool write, std::uint32_t offset, std::uint32_t value) noexcept
{
    if(!IsEnabled(Channel::Nv2a))
    {
        return;
    }
    constexpr Event continuations[]{ Event::Continuation0 };
    const std::uint32_t arguments[]{ offset, value };
    static_cast<void>(RecordLogicalEvent(write ? Event::Nv2aWrite : Event::Nv2aRead,
                                         continuations, arguments, 2, SIZE_MAX));
}

void RecordNv2aPush(std::uint32_t word) noexcept
{
    static_cast<void>(RecordLogicalEvent(Event::Nv2aPush, nullptr, &word, 1, SIZE_MAX));
}

void RecordNv2aMethod(std::uint32_t objectClass, std::uint32_t method,
                      std::uint32_t data) noexcept
{
    constexpr Event continuations[]{ Event::Continuation0, Event::Continuation1 };
    const std::uint32_t arguments[]{ objectClass, method, data };
    static_cast<void>(RecordLogicalEvent(Event::Nv2aMethod, continuations, arguments, 3,
                                         SIZE_MAX));
}

void RecordNv2aRamht(std::uint32_t handle, std::uint32_t instance,
                     std::uint32_t objectClass) noexcept
{
    constexpr Event continuations[]{ Event::Continuation0, Event::Continuation1 };
    const std::uint32_t arguments[]{ handle, instance, objectClass };
    static_cast<void>(RecordLogicalEvent(Event::Nv2aRamht, continuations, arguments, 3,
                                         SIZE_MAX));
}

void RecordSingleStep(std::uint32_t instructionPointer, std::uint32_t stackPointer) noexcept
{
    constexpr Event continuations[]{ Event::Continuation0 };
    const std::uint32_t arguments[]{ instructionPointer, stackPointer };
    static_cast<void>(RecordLogicalEvent(Event::SingleStepInstruction, continuations,
                                         arguments, 2, SIZE_MAX));
}

void RecordFlightFallback(Event event, std::uint32_t argument) noexcept
{
    const TraceRecord record{
        FlightTick(),
        0,
        static_cast<std::uint16_t>(event),
        UINT16_MAX,
        argument,
    };
    g_FlightFallback.Record(record);
}

std::size_t CopyCurrentFlight(TraceRecord* records, std::size_t capacity) noexcept
{
    AttachFlightThread();
    if(g_ThreadState.flight != nullptr)
    {
        return g_ThreadState.flight->flight.Copy(records, capacity);
    }
    return g_FlightFallback.Copy(records, capacity);
}

void VisitCurrentFlight(FlightVisitor visitor, void* context) noexcept
{
    if(visitor == nullptr)
    {
        return;
    }
    AttachFlightThread();
    if(g_ThreadState.flight != nullptr)
    {
        g_ThreadState.flight->flight.Visit([visitor, context](const TraceRecord& record) noexcept
                                           { visitor(record, context); });
    }
    else
    {
        g_FlightFallback.Visit([visitor, context](const TraceRecord& record) noexcept
                               { visitor(record, context); });
    }
}

void VisitAllFlight(FlightVisitor visitor, void* context) noexcept
{
    if(visitor == nullptr)
    {
        return;
    }
    const std::uint32_t assigned = g_NextFlightThread.load(std::memory_order_acquire);
    const std::size_t count = assigned < kFlightThreadCapacity ? assigned
                                                               : kFlightThreadCapacity;
    for(std::size_t index = 0; index < count; ++index)
    {
        g_FlightThreads[index].flight.Visit(
            [visitor, context](const TraceRecord& record) noexcept
            { visitor(record, context); });
    }
    g_FlightFallback.Visit([visitor, context](const TraceRecord& record) noexcept
                           { visitor(record, context); });
}

std::uint64_t FlightFallbackOverwritten() noexcept
{
    return g_FlightFallback.Overwritten();
}

void DumpFlightEmergency() noexcept
{
    if(g_EmergencyDumping.test_and_set(std::memory_order_acquire))
    {
        return;
    }
    VisitAllFlight(WriteEmergencyRecord, nullptr);

    EmergencyLine summary{};
    AppendLiteral(summary, "FLIGHT| fallback_dropped=");
    AppendDecimal(summary, FlightFallbackOverwritten());
    AppendCharacter(summary, '\n');
    TraceOsEmergencyWrite(summary.data, summary.length);
    g_EmergencyDumping.clear(std::memory_order_release);
}

void Text(Channel channel, const char* verb, const char* format, ...) noexcept
{
    std::va_list arguments;
    va_start(arguments, format);
    Emit(channel, verb, format, arguments, false);
    va_end(arguments);
}

void Warn(Channel channel, const char* verb, const char* format, ...) noexcept
{
    std::va_list arguments;
    va_start(arguments, format);
    Emit(channel, verb, format, arguments, true);
    va_end(arguments);
}

} // namespace cxbx::trace
