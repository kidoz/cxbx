// Win32 services used by the platform-neutral trace core.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cxbx::trace
{

using TraceOsThreadRoutine = void (*)(void* context) noexcept;

[[nodiscard]] std::uint32_t TraceOsThreadId() noexcept;
[[nodiscard]] std::uint64_t TraceOsNowTicks() noexcept;
[[nodiscard]] std::uint64_t TraceOsTickFrequency() noexcept;
[[nodiscard]] bool TraceOsEnvironmentExists(const char* name) noexcept;
[[nodiscard]] bool TraceOsReadEnvironment(const char* name, char* buffer,
                                          std::size_t capacity) noexcept;
void TraceOsEmergencyInitialize(std::FILE* output) noexcept;
void TraceOsEmergencyShutdown() noexcept;
void TraceOsEmergencyWrite(const char* data, std::size_t length) noexcept;
[[nodiscard]] bool TraceOsStartBackgroundThread(TraceOsThreadRoutine routine,
                                                void* context) noexcept;
void TraceOsJoinBackgroundThread() noexcept;
void TraceOsSleep(std::uint32_t milliseconds) noexcept;

} // namespace cxbx::trace
