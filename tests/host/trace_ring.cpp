#include "core/trace_ring.h"
#include "core/trace_flight.h"
#include "core/trace_atomic_flight.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace
{

[[noreturn]] void Fail(const char* message)
{
    std::fprintf(stderr, "trace_ring_test: %s\n", message);
    std::exit(1);
}

cxbx::trace::TraceRecord Record(std::uint64_t sequence, std::uint32_t argument)
{
    return { sequence * 10, sequence, 7, 3, argument };
}

void TestWholeEventOverflow()
{
    cxbx::trace::TraceSpscRing<4> ring;
    const std::array first{ Record(1, 11), Record(1, 12), Record(1, 13) };
    const std::array second{ Record(2, 21), Record(2, 22) };
    if(!ring.Push(first.data(), first.size()) || ring.Push(second.data(), second.size()))
    {
        Fail("whole-event overflow policy mismatch");
    }
    if(ring.Size() != first.size() || ring.TakeDroppedEvents() != 1)
    {
        Fail("overflow accounting mismatch");
    }

    std::array<cxbx::trace::TraceRecord, 2> shortOutput{};
    if(ring.Pop(shortOutput.data(), shortOutput.size()) != 0)
    {
        Fail("consumer observed a partial logical event");
    }

    std::array<cxbx::trace::TraceRecord, 4> output{};
    const std::size_t count = ring.Pop(output.data(), output.size());
    if(count != first.size())
    {
        Fail("overflow exposed a partial event");
    }
    for(std::size_t index = 0; index < count; ++index)
    {
        if(output[index].argument != first[index].argument)
        {
            Fail("overflow corrupted a committed event");
        }
    }
}

void TestWrap()
{
    cxbx::trace::TraceSpscRing<8> ring;
    for(std::uint64_t sequence = 1; sequence <= 1000; ++sequence)
    {
        const auto input = Record(sequence, static_cast<std::uint32_t>(sequence));
        if(!ring.Push(&input, 1))
        {
            Fail("single-record push failed during wrap test");
        }
        cxbx::trace::TraceRecord output{};
        if(ring.Pop(&output, 1) != 1 || output.localSequence != sequence)
        {
            Fail("ring wrap changed record order");
        }
    }
}

void TestFlightOverwrite()
{
    cxbx::trace::TraceFlightRing<4> ring;
    for(std::uint64_t sequence = 1; sequence <= 6; ++sequence)
    {
        ring.Record(Record(sequence, static_cast<std::uint32_t>(sequence)));
    }

    std::array<cxbx::trace::TraceRecord, 4> output{};
    if(ring.Copy(output.data(), output.size()) != output.size())
    {
        Fail("flight snapshot size mismatch");
    }
    for(std::size_t index = 0; index < output.size(); ++index)
    {
        if(output[index].localSequence != index + 3)
        {
            Fail("flight snapshot is not ordered oldest-first");
        }
    }
}

void TestConcurrentPublication()
{
    constexpr std::uint64_t recordCount = 100000;
    cxbx::trace::TraceSpscRing<1024> ring;
    std::atomic<bool> producerDone{ false };
    std::atomic<bool> failed{ false };

    std::thread producer([&]()
                         {
        for(std::uint64_t sequence = 1; sequence <= recordCount; ++sequence)
        {
            const auto record = Record(sequence, static_cast<std::uint32_t>(sequence));
            while(ring.Size() == 1024)
            {
                std::this_thread::yield();
            }
            if(!ring.Push(&record, 1))
            {
                failed.store(true, std::memory_order_relaxed);
                break;
            }
        }
        producerDone.store(true, std::memory_order_release); });

    std::uint64_t expected = 1;
    while(!producerDone.load(std::memory_order_acquire) || ring.Size() != 0)
    {
        std::array<cxbx::trace::TraceRecord, 64> output{};
        const std::size_t count = ring.Pop(output.data(), output.size());
        for(std::size_t index = 0; index < count; ++index)
        {
            if(output[index].localSequence != expected ||
               output[index].argument != static_cast<std::uint32_t>(expected))
            {
                failed.store(true, std::memory_order_relaxed);
            }
            ++expected;
        }
        if(count == 0)
        {
            std::this_thread::yield();
        }
    }
    producer.join();

    if(failed.load(std::memory_order_relaxed) || expected != recordCount + 1)
    {
        Fail("concurrent publication exposed malformed or reordered records");
    }
}

void TestConcurrentFlightSnapshot()
{
    constexpr std::uint64_t recordCount = 100000;
    cxbx::trace::TraceConcurrentFlightRing<64> ring;
    std::atomic<bool> producerDone{ false };
    std::atomic<bool> failed{ false };

    std::thread producer([&]()
                         {
        for(std::uint64_t sequence = 1; sequence <= recordCount; ++sequence)
        {
            ring.Record(Record(sequence, static_cast<std::uint32_t>(sequence)));
        }
        producerDone.store(true, std::memory_order_release); });

    while(!producerDone.load(std::memory_order_acquire))
    {
        std::array<cxbx::trace::TraceRecord, 64> output{};
        const std::size_t count = ring.Copy(output.data(), output.size());
        for(std::size_t index = 0; index < count; ++index)
        {
            if(output[index].tick != output[index].localSequence * 10 ||
               output[index].argument !=
                   static_cast<std::uint32_t>(output[index].localSequence))
            {
                failed.store(true, std::memory_order_relaxed);
            }
        }
    }
    producer.join();

    if(failed.load(std::memory_order_relaxed))
    {
        Fail("concurrent flight snapshot observed a torn record");
    }
}

void TestMpscFlightOverwrite()
{
    constexpr std::uint32_t writerCount = 4;
    constexpr std::uint32_t recordsPerWriter = 10000;
    constexpr std::size_t capacity = 128;
    cxbx::trace::TraceMpscFlightRing<capacity> ring;
    std::array<std::thread, writerCount> writers;

    for(std::uint32_t writer = 0; writer < writerCount; ++writer)
    {
        writers[writer] = std::thread([&, writer]()
                                      {
            for(std::uint32_t index = 0; index < recordsPerWriter; ++index)
            {
                const std::uint32_t value = writer * recordsPerWriter + index + 1;
                ring.Record({ value, 0, 7, UINT16_MAX, value });
            } });
    }
    for(std::thread& writer : writers)
    {
        writer.join();
    }

    std::array<cxbx::trace::TraceRecord, capacity> output{};
    const std::size_t count = ring.Copy(output.data(), output.size());
    if(count == 0 ||
       ring.Overwritten() != writerCount * recordsPerWriter - capacity)
    {
        Fail("MPSC flight overwrite accounting mismatch");
    }
    for(std::size_t index = 0; index < count; ++index)
    {
        if(output[index].tick != output[index].argument || output[index].event != 7 ||
           output[index].threadIndex != UINT16_MAX || output[index].localSequence == 0)
        {
            Fail("MPSC flight snapshot observed a torn record");
        }
    }
}

} // namespace

int Run()
{
    TestWholeEventOverflow();
    TestWrap();
    TestFlightOverwrite();
    TestConcurrentPublication();
    TestConcurrentFlightSnapshot();
    TestMpscFlightOverwrite();
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
        std::fprintf(stderr, "trace_ring_test: unexpected exception\n");
        return 1;
    }
}
