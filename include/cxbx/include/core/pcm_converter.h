// PCM channel conversion helpers shared by audio backends and host tests.

#pragma once

#include <cstddef>
#include <cstdint>

namespace cxbx::audio
{
[[nodiscard]] bool CanDownmixPcm16ToStereo(std::size_t sourceChannels) noexcept;

void DownmixPcm16ToStereo(const std::uint8_t* source,
                          std::size_t frameCount,
                          std::size_t sourceChannels,
                          std::uint8_t* destination) noexcept;
} // namespace cxbx::audio
