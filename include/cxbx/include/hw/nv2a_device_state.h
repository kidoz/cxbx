#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cxbx::nv2a
{

struct RamhtLookupResult
{
    std::uint32_t instance = 0;
    // Raw RAMIN class byte; callers own compatibility fallback policy.
    std::uint32_t objectClass = 0;
};

class DeviceState final
{
  public:
    static constexpr std::uint32_t RaminBase = 0x00700000u;
    static constexpr std::uint32_t RaminSize = 0x00100000u;

    [[nodiscard]] bool IsRaminOffset(std::uint32_t offset) const noexcept;
    [[nodiscard]] std::uint32_t ReadRamin32(std::uint32_t offset) const noexcept;
    void WriteRamin32(std::uint32_t offset, std::uint32_t value) noexcept;
    [[nodiscard]] std::optional<RamhtLookupResult> LookupRamht(
        std::uint32_t handle, std::uint32_t ramht,
        std::uint32_t channelId) const noexcept;

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
