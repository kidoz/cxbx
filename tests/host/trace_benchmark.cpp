// Optimized caller-path benchmark for structured tracing primitives.
#include "core/trace.h"
#include "core/trace_ring.h"
#include "../../src/cxbx/src/platform/win32/trace_os.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{

using Clock = std::chrono::steady_clock;
volatile std::uint64_t g_BenchmarkSink = 0;

#ifndef CXBX_TRACE_BENCH_OPTIMIZATION
#define CXBX_TRACE_BENCH_OPTIMIZATION "unknown"
#endif

std::uint64_t Iterations() noexcept
{
    constexpr std::uint64_t defaultIterations = 10'000'000;
    char* configured = nullptr;
    std::size_t length = 0;
    if(_dupenv_s(&configured, &length, "CXBX_TRACE_BENCH_ITERATIONS") != 0 ||
       configured == nullptr)
    {
        return defaultIterations;
    }
    const unsigned long long value = std::strtoull(configured, nullptr, 10);
    std::free(configured);
    return value >= 1000 ? static_cast<std::uint64_t>(value) : defaultIterations;
}

template <typename Operation>
double Measure(std::uint64_t iterations, Operation operation)
{
    const auto emptyStart = Clock::now();
    for(std::uint64_t index = 0; index < iterations; ++index)
    {
        g_BenchmarkSink += index & 1U;
    }
    const auto emptyEnd = Clock::now();

    const auto start = Clock::now();
    for(std::uint64_t index = 0; index < iterations; ++index)
    {
        operation(index);
        g_BenchmarkSink += index & 1U;
    }
    const auto end = Clock::now();
    const double elapsed =
        std::chrono::duration<double, std::nano>(end - start).count();
    const double empty =
        std::chrono::duration<double, std::nano>(emptyEnd - emptyStart).count();
    return std::max(0.0, elapsed - empty) / static_cast<double>(iterations);
}

double MeasureEmpty(std::uint64_t iterations)
{
    const auto start = Clock::now();
    for(std::uint64_t index = 0; index < iterations; ++index)
    {
        g_BenchmarkSink += index & 1U;
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::nano>(end - start).count();
}

template <std::size_t GroupSize>
double MeasureBinaryRing(std::uint64_t iterations)
{
    cxbx::trace::TraceSpscRing<1024> ring;
    std::array<cxbx::trace::TraceRecord, GroupSize> records{};
    std::array<cxbx::trace::TraceRecord, 1024> output{};
    const std::size_t groupsPerBatch = 1024 / GroupSize;
    std::uint64_t completed = 0;
    double elapsed = 0.0;
    while(completed < iterations)
    {
        const std::uint64_t batch =
            std::min<std::uint64_t>(groupsPerBatch, iterations - completed);
        const auto start = Clock::now();
        for(std::uint64_t group = 0; group < batch; ++group)
        {
            const std::uint64_t sequence = completed + group + 1;
            const std::uint64_t tick = cxbx::trace::TraceOsNowTicks();
            for(std::size_t word = 0; word < GroupSize; ++word)
            {
                records[word] = { tick, sequence,
                                  static_cast<std::uint16_t>(word + 1), 1,
                                  static_cast<std::uint32_t>(sequence + word) };
            }
            if(!ring.Push(records.data(), records.size()))
            {
                std::abort();
            }
        }
        const auto end = Clock::now();
        elapsed += std::chrono::duration<double, std::nano>(end - start).count();
        if(ring.Pop(output.data(), output.size()) != batch * GroupSize)
        {
            std::abort();
        }
        completed += batch;
    }
    return std::max(0.0, elapsed - MeasureEmpty(iterations)) /
           static_cast<double>(iterations);
}

double MeasureFlushedText(std::uint64_t iterations)
{
    const std::uint64_t textIterations = std::min<std::uint64_t>(iterations, 100'000);
    std::FILE* output = nullptr;
    if(tmpfile_s(&output) != 0 || output == nullptr)
    {
        std::abort();
    }
    const auto start = Clock::now();
    for(std::uint64_t index = 0; index < textIterations; ++index)
    {
        if(std::fprintf(output, "NV2A| mthd class=0x97 method=0x0300 data=0x%08X\n",
                        static_cast<unsigned>(index)) < 0 ||
           std::fflush(output) != 0)
        {
            std::abort();
        }
    }
    const auto end = Clock::now();
    if(std::fclose(output) != 0)
    {
        std::abort();
    }
    return std::chrono::duration<double, std::nano>(end - start).count() /
           static_cast<double>(textIterations);
}

} // namespace

int main()
{
    const std::uint64_t iterations = Iterations();
    cxbx::trace::SetEnabled(cxbx::trace::Channel::D3d, false);
    const double optionalGate = Measure(iterations, [](std::uint64_t)
                                        { g_BenchmarkSink += cxbx::trace::IsEnabled(
                                              cxbx::trace::Channel::D3d); });
    const double flight = Measure(iterations, [](std::uint64_t index)
                                  { cxbx::trace::RecordFlight(
                                        cxbx::trace::Event::D3dBoundary,
                                        static_cast<std::uint32_t>(index)); });
    const double binaryOne = MeasureBinaryRing<1>(iterations);
    const double binaryFour = MeasureBinaryRing<4>(iterations);
    const double flushedText = MeasureFlushedText(iterations);

    std::printf(
        "{\"iterations\":%llu,\"compiler\":\"clang-%d.%d.%d\","
        "\"optimization\":\"%s\","
        "\"optional_gate_ns\":%.3f,\"flight_ns\":%.3f,"
        "\"binary_one_ns\":%.3f,\"binary_four_ns\":%.3f,"
        "\"flushed_text_ns\":%.3f}\n",
        static_cast<unsigned long long>(iterations), __clang_major__, __clang_minor__,
        __clang_patchlevel__, CXBX_TRACE_BENCH_OPTIMIZATION, optionalGate, flight,
        binaryOne, binaryFour, flushedText);
    return binaryOne < flushedText && binaryFour < flushedText ? 0 : 1;
}
