#pragma once

#include "core/xbox_memory.h"

#include <cstddef>
#include <cstdint>

namespace cxbx::d3d
{

inline constexpr std::size_t CompressedTextureLevelSize(
    std::size_t width, std::size_t height, std::size_t bytesPerBlock) noexcept
{
    if(width == 0 || height == 0)
    {
        return 0;
    }

    const std::size_t blockWidth = (width + 3) / 4;
    const std::size_t blockHeight = (height + 3) / 4;
    return blockWidth * blockHeight * bytesPerBlock;
}

inline constexpr std::size_t CompressedTextureMipChainSize(
    std::size_t width, std::size_t height, std::size_t bytesPerBlock,
    std::size_t levelCount) noexcept
{
    std::size_t size = 0;
    for(std::size_t level = 0; level < levelCount; ++level)
    {
        size += CompressedTextureLevelSize(width, height, bytesPerBlock);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return size;
}

inline constexpr std::uint32_t XboxPhysicalAddress(std::uintptr_t address) noexcept
{
    return cxbx::XboxPhysicalAddress(address);
}

inline constexpr std::uint32_t XboxResourceDataAddress(
    std::uintptr_t baseAddress, std::uint32_t dataOffset,
    bool isPushBuffer) noexcept
{
    const std::uint32_t resolvedAddress =
        static_cast<std::uint32_t>(baseAddress) + dataOffset;
    constexpr std::uint32_t ResourceAddressMask = 0x0FFFFFFFu;
    return isPushBuffer ? resolvedAddress : resolvedAddress & ResourceAddressMask;
}

inline constexpr std::uint32_t XboxResourceHostDataAddress(
    std::uintptr_t baseAddress, std::uint32_t dataOffset,
    bool hasTrackedUnmaskedBacking) noexcept
{
    const std::uint32_t unmaskedAddress =
        static_cast<std::uint32_t>(baseAddress) + dataOffset;
    return hasTrackedUnmaskedBacking
               ? unmaskedAddress
               : XboxResourceDataAddress(baseAddress, dataOffset, false);
}

} // namespace cxbx::d3d
