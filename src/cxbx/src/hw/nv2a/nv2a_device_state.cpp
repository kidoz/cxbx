#include "hw/nv2a_device_state.h"

namespace cxbx::nv2a
{

bool DeviceState::IsRaminOffset(std::uint32_t offset) const noexcept
{
    return offset >= RaminBase && offset < RaminBase + RaminSize;
}

std::uint32_t DeviceState::ReadRamin32(std::uint32_t offset) const noexcept
{
    if(!IsRaminOffset(offset))
    {
        return 0;
    }

    const std::uint32_t raminOffset = offset - RaminBase;
    if(raminOffset > RaminSize - sizeof(std::uint32_t))
    {
        return 0;
    }

    return ramin_[raminOffset / sizeof(std::uint32_t)];
}

void DeviceState::WriteRamin32(std::uint32_t offset,
                               std::uint32_t value) noexcept
{
    if(!IsRaminOffset(offset))
    {
        return;
    }

    const std::uint32_t raminOffset = offset - RaminBase;
    if(raminOffset > RaminSize - sizeof(std::uint32_t))
    {
        return;
    }

    ramin_[raminOffset / sizeof(std::uint32_t)] = value;
}

const void* DeviceState::RaminData() const noexcept
{
    return ramin_.data();
}

} // namespace cxbx::nv2a
