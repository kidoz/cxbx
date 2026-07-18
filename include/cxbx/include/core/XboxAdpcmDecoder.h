// Xbox IMA ADPCM decoding helpers.

#pragma once

#include <cstddef>
#include <cstdint>

namespace cxbx::audio
{
constexpr std::size_t kXboxAdpcmSamplesPerBlock = 64;

[[nodiscard]] std::size_t XboxAdpcmDecodedBytes(std::size_t sourceBytes,
                                                std::size_t channels) noexcept;

[[nodiscard]] bool DecodeXboxAdpcm(const std::uint8_t* source,
                                   std::size_t sourceBytes,
                                   std::size_t channels,
                                   std::uint8_t* destination,
                                   std::size_t destinationBytes) noexcept;
} // namespace cxbx::audio
