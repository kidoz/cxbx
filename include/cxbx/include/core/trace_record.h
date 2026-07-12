// Fixed-width in-memory event record shared by binary and crash-context tracing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cxbx::trace
{

struct TraceRecord
{
    std::uint64_t tick;
    std::uint64_t localSequence;
    std::uint16_t event;
    std::uint16_t threadIndex;
    std::uint32_t argument;
};

constexpr std::uint16_t kTraceRecordSchemaVersion = 1;
constexpr std::uint16_t kTraceEventFileVersion = 1;
constexpr std::size_t kTraceRecordSize = 24;

static_assert(std::is_trivially_copyable_v<TraceRecord>);
static_assert(std::is_standard_layout_v<TraceRecord>);
static_assert(sizeof(TraceRecord) == kTraceRecordSize);
static_assert(offsetof(TraceRecord, tick) == 0);
static_assert(offsetof(TraceRecord, localSequence) == 8);
static_assert(offsetof(TraceRecord, event) == 16);
static_assert(offsetof(TraceRecord, threadIndex) == 18);
static_assert(offsetof(TraceRecord, argument) == 20);

} // namespace cxbx::trace
