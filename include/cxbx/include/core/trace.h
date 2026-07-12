// Process-owned structured tracing for the emulator runtime.
#pragma once

#include "core/trace_registry.gen.h"
#include "core/trace_record.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace cxbx::trace
{

inline std::atomic<std::uint32_t> g_TraceMask{ 0 };

[[nodiscard]] inline bool IsEnabled(Channel channel) noexcept
{
    return (g_TraceMask.load(std::memory_order_relaxed) & ChannelBit(channel)) != 0;
}

void Initialize(std::FILE* output) noexcept;
void Shutdown() noexcept;
void Flush() noexcept;
void SetEnabled(Channel channel, bool enabled) noexcept;
[[nodiscard]] bool IsAvailable(Channel channel) noexcept;

using FlightVisitor = void (*)(const TraceRecord& record, void* context) noexcept;
void RecordFlight(Event event, std::uint32_t argument) noexcept;
void RecordFlightFallback(Event event, std::uint32_t argument) noexcept;
void RecordBinary(Event event, std::uint32_t argument) noexcept;
[[nodiscard]] std::uint32_t RecordD3dCall(D3dApi api, std::uint32_t flags,
                                          std::uint32_t marker) noexcept;
void RecordD3dReturn(D3dApi api, std::uint32_t sequence, std::uint32_t result) noexcept;
void RecordD3dWait(D3dWaitReason reason, std::uint32_t sequence, std::uint32_t handle,
                   bool pending) noexcept;
void RecordNv2aRegister(bool write, std::uint32_t offset, std::uint32_t value) noexcept;
void RecordNv2aPush(std::uint32_t word) noexcept;
void RecordNv2aMethod(std::uint32_t objectClass, std::uint32_t method,
                      std::uint32_t data) noexcept;
void RecordNv2aRamht(std::uint32_t handle, std::uint32_t instance,
                     std::uint32_t objectClass) noexcept;
void RecordSingleStep(std::uint32_t instructionPointer, std::uint32_t stackPointer) noexcept;
[[nodiscard]] std::size_t CopyCurrentFlight(TraceRecord* records,
                                            std::size_t capacity) noexcept;
void VisitCurrentFlight(FlightVisitor visitor, void* context) noexcept;
void VisitAllFlight(FlightVisitor visitor, void* context) noexcept;
[[nodiscard]] std::uint64_t FlightFallbackOverwritten() noexcept;
void DumpFlightEmergency() noexcept;

#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
void Text(Channel channel, const char* verb, const char* format, ...) noexcept;

#if defined(__clang__) || defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
void Warn(Channel channel, const char* verb, const char* format, ...) noexcept;

} // namespace cxbx::trace

#define CXBX_TRACE_FLIGHT(event, argument) \
    ::cxbx::trace::RecordFlight((event), static_cast<std::uint32_t>(argument))

#define CXBX_TRACE_BINARY(event, argument) \
    ::cxbx::trace::RecordBinary((event), static_cast<std::uint32_t>(argument))

#define CXBX_TRACE_TEXT(channel, ...)                  \
    do                                                 \
    {                                                  \
        if(::cxbx::trace::IsEnabled(channel))          \
        {                                              \
            ::cxbx::trace::Text(channel, __VA_ARGS__); \
        }                                              \
    } while(false)

#define CXBX_TRACE_WARN(channel, ...)                  \
    do                                                 \
    {                                                  \
        if(::cxbx::trace::IsEnabled(channel))          \
        {                                              \
            ::cxbx::trace::Warn(channel, __VA_ARGS__); \
        }                                              \
    } while(false)
