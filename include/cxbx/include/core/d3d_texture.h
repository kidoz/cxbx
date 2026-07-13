#pragma once

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
    constexpr std::uint32_t XboxRamMask = (64u * 1024u * 1024u) - 1u;
    return static_cast<std::uint32_t>(address) & XboxRamMask;
}

} // namespace cxbx::d3d
