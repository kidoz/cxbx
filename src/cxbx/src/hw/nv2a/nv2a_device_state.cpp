#include "hw/nv2a_device_state.h"

namespace cxbx::nv2a
{

namespace
{

constexpr std::uint32_t RamhtBaseMask = 0x000001F0u;
constexpr std::uint32_t RamhtSizeMask = 0x00030000u;
constexpr std::uint32_t RamhtEntryValid = 0x80000000u;
constexpr std::uint32_t RamhtInstanceMask = 0x0000FFFFu;
constexpr std::uint32_t RamhtObjectClassMask = 0x000000FFu;
constexpr std::uint32_t RamhtChannelMask = 0x0000001Fu;
constexpr std::uint32_t RamhtEntrySize = 8;
constexpr std::uint32_t RamhtProbeLimit = 16;
constexpr std::uint32_t RamhtBaseFieldShift = 4;
constexpr std::uint32_t RamhtSizeFieldShift = 16;
constexpr std::uint32_t RamhtMinimumHashBits = 11;
constexpr std::uint32_t RamhtMaximumHashBits = 15;
constexpr std::uint32_t RamhtChannelHashBias = 4;
constexpr std::uint32_t RaminPageShift = 12;
constexpr std::uint32_t RamhtInstanceShift = 4;
constexpr std::uint32_t DmaObjectWordSize = sizeof(std::uint32_t);
constexpr std::uint32_t DmaObjectWordCount = 3;
constexpr std::uint32_t DmaObjectByteSize =
    DmaObjectWordSize * DmaObjectWordCount;

} // namespace

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

std::optional<RamhtLookupResult> DeviceState::LookupRamht(
    std::uint32_t handle, std::uint32_t ramht,
    std::uint32_t channelId) const noexcept
{
    const std::uint32_t tableOffset =
        ((ramht & RamhtBaseMask) >> RamhtBaseFieldShift) << RaminPageShift;
    const std::uint32_t tableSize =
        std::uint32_t{ 1 }
        << (((ramht & RamhtSizeMask) >> RamhtSizeFieldShift) + RaminPageShift);
    if(tableOffset > RaminSize || tableSize > RaminSize - tableOffset ||
       tableSize < RamhtEntrySize || tableSize % RamhtEntrySize != 0)
    {
        return std::nullopt;
    }

    std::uint32_t hashBits = RamhtMinimumHashBits;
    while(hashBits < RamhtMaximumHashBits &&
          (std::uint32_t{ 1 } << (hashBits + 1)) < tableSize)
    {
        ++hashBits;
    }

    std::uint32_t hash = 0;
    std::uint32_t remainingHandle = handle;
    while(remainingHandle != 0)
    {
        hash ^= remainingHandle & ((std::uint32_t{ 1 } << hashBits) - 1);
        remainingHandle >>= hashBits;
    }

    if(hashBits > RamhtChannelHashBias)
    {
        hash ^= (channelId & RamhtChannelMask)
                << (hashBits - RamhtChannelHashBias);
    }

    const std::uint32_t entryCount = tableSize / RamhtEntrySize;
    hash &= entryCount - 1;

    for(std::uint32_t probe = 0;
        probe < RamhtProbeLimit && probe < entryCount; ++probe)
    {
        const std::uint32_t entryIndex = (hash + probe) & (entryCount - 1);
        const std::uint32_t entryOffset =
            RaminBase + tableOffset + entryIndex * RamhtEntrySize;
        const std::uint32_t entryHandle = ReadRamin32(entryOffset);
        const std::uint32_t entryContext = ReadRamin32(entryOffset + 4);

        if(entryHandle != handle || (entryContext & RamhtEntryValid) == 0)
        {
            continue;
        }

        const std::uint32_t objectInstance =
            (entryContext & RamhtInstanceMask) << RamhtInstanceShift;
        const std::uint32_t objectClass =
            ReadRamin32(RaminBase + objectInstance) & RamhtObjectClassMask;
        return RamhtLookupResult{ objectInstance, objectClass };
    }

    return std::nullopt;
}

std::optional<DmaObjectDescriptor> DeviceState::DecodeDmaObject(
    std::uint32_t instance) const noexcept
{
    if(instance > RaminSize - DmaObjectByteSize)
    {
        return std::nullopt;
    }

    const std::uint32_t descriptorOffset = RaminBase + instance;
    return DmaObjectDescriptor{
        ReadRamin32(descriptorOffset),
        ReadRamin32(descriptorOffset + DmaObjectWordSize),
        ReadRamin32(descriptorOffset + DmaObjectWordSize * 2),
    };
}

const void* DeviceState::RaminData() const noexcept
{
    return ramin_.data();
}

} // namespace cxbx::nv2a
