#pragma once

#include <cstdint>

namespace cxbx::nvnet
{

inline constexpr std::uint32_t TxRxControl = 0x00000144u;
inline constexpr std::uint32_t TxRxControlBit2 = 0x00000004u;
inline constexpr std::uint32_t TxRxControlIdle = 0x00000008u;

inline constexpr std::uint32_t RegisterValueAfterWrite(
    std::uint32_t offset, std::uint32_t value) noexcept
{
    if(offset == TxRxControl && (value & TxRxControlBit2) != 0)
    {
        return TxRxControlIdle;
    }
    return value;
}

} // namespace cxbx::nvnet
