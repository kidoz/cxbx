#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cxbx::nv2a
{

class DeviceState final
{
  public:
    static constexpr std::uint32_t RaminBase = 0x00700000u;
    static constexpr std::uint32_t RaminSize = 0x00100000u;

    [[nodiscard]] bool IsRaminOffset(std::uint32_t offset) const noexcept;
    [[nodiscard]] std::uint32_t ReadRamin32(std::uint32_t offset) const noexcept;
    void WriteRamin32(std::uint32_t offset, std::uint32_t value) noexcept;

    [[nodiscard]] const void* RaminData() const noexcept;
    [[nodiscard]] constexpr std::uint32_t RaminByteSize() const noexcept
    {
        return RaminSize;
    }

  private:
    static constexpr std::size_t RaminDwordCount =
        RaminSize / sizeof(std::uint32_t);

    std::array<std::uint32_t, RaminDwordCount> ramin_{};
};

} // namespace cxbx::nv2a
