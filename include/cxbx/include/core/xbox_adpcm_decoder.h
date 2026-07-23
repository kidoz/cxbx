// Xbox IMA ADPCM decoding helpers.

#pragma once

#include <cstddef>
#include <cstdint>

namespace cxbx::audio
{
constexpr std::size_t kXboxAdpcmEncodedBytesPerChannelBlock = 36;
constexpr std::size_t kXboxAdpcmSamplesPerBlock = 65;

[[nodiscard]] std::size_t XboxAdpcmEncodedBlockBytes(
    std::size_t channels) noexcept;

[[nodiscard]] std::size_t XboxAdpcmDecodedBlockBytes(
    std::size_t channels) noexcept;

[[nodiscard]] std::size_t XboxAdpcmGuestToPcmBytes(
    std::size_t guestBytes, std::size_t channels) noexcept;

[[nodiscard]] std::size_t XboxAdpcmPcmToGuestBytes(
    std::size_t pcmBytes, std::size_t channels) noexcept;

[[nodiscard]] std::size_t XboxAdpcmDecodedBytes(std::size_t sourceBytes,
                                                std::size_t channels) noexcept;

[[nodiscard]] bool DecodeXboxAdpcm(const std::uint8_t* source,
                                   std::size_t sourceBytes,
                                   std::size_t channels,
                                   std::uint8_t* destination,
                                   std::size_t destinationBytes) noexcept;
} // namespace cxbx::audio
