#pragma once

#include <cstddef>
#include <cstdint>

namespace cxbx
{

inline constexpr std::uint32_t XboxPhysicalAddress(std::uintptr_t address) noexcept
{
    constexpr std::uint32_t XboxRamMask = (64u * 1024u * 1024u) - 1u;
    return static_cast<std::uint32_t>(address) & XboxRamMask;
}

inline constexpr bool XboxPhysicalSpanOffset(
    std::uint32_t physicalAddress, std::uint32_t allocationPhysicalAddress,
    std::size_t allocationSize, std::size_t byteCount, std::size_t& offset) noexcept
{
    constexpr std::uint32_t XboxRamMask = (64u * 1024u * 1024u) - 1u;
    offset = (physicalAddress - allocationPhysicalAddress) & XboxRamMask;
    return byteCount != 0 && offset < allocationSize &&
           byteCount <= allocationSize - offset;
}

} // namespace cxbx
