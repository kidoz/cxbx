#include "hw/nv2a_device_state.h"

#include <cstdio>
#include <cstdint>

namespace
{

bool ExpectEqual(std::uint32_t actual, std::uint32_t expected,
                 const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: expected 0x%08X, got 0x%08X\n", message,
                 expected, actual);
    return false;
}

void WriteRamhtEntry(cxbx::nv2a::DeviceState& state,
                     std::uint32_t tableOffset, std::uint32_t entryIndex,
                     std::uint32_t handle, std::uint32_t context) noexcept
{
    const std::uint32_t entryOffset =
        cxbx::nv2a::DeviceState::RaminBase + tableOffset + entryIndex * 8;
    state.WriteRamin32(entryOffset, handle);
    state.WriteRamin32(entryOffset + 4, context);
}

} // namespace

int main() noexcept
{
    static cxbx::nv2a::DeviceState state;

    if(state.RaminByteSize() != cxbx::nv2a::DeviceState::RaminSize ||
       state.RaminData() == nullptr)
    {
        std::fputs("RAMIN storage view is invalid\n", stderr);
        return 1;
    }

    const std::uint32_t first = cxbx::nv2a::DeviceState::RaminBase;
    const std::uint32_t lastDword =
        first + cxbx::nv2a::DeviceState::RaminSize - sizeof(std::uint32_t);
    const std::uint32_t end =
        first + cxbx::nv2a::DeviceState::RaminSize;

    if(state.IsRaminOffset(first - 1) || !state.IsRaminOffset(first) ||
       !state.IsRaminOffset(end - 1) || state.IsRaminOffset(end))
    {
        std::fputs("RAMIN address bounds are invalid\n", stderr);
        return 1;
    }

    if(!ExpectEqual(state.ReadRamin32(first), 0, "RAMIN must start cleared") ||
       !ExpectEqual(state.ReadRamin32(first - 1), 0,
                    "read below RAMIN must return zero") ||
       !ExpectEqual(state.ReadRamin32(end), 0,
                    "read above RAMIN must return zero"))
    {
        return 1;
    }

    state.WriteRamin32(first, 0x11223344u);
    state.WriteRamin32(lastDword, 0xAABBCCDDu);
    if(!ExpectEqual(state.ReadRamin32(first), 0x11223344u,
                    "first RAMIN dword") ||
       !ExpectEqual(state.ReadRamin32(lastDword), 0xAABBCCDDu,
                    "last RAMIN dword"))
    {
        return 1;
    }

    state.WriteRamin32(first + 3, 0x55667788u);
    if(!ExpectEqual(state.ReadRamin32(first + 1), 0x55667788u,
                    "misaligned RAMIN access must select its containing dword"))
    {
        return 1;
    }

    state.WriteRamin32(end - 1, 0xFFFFFFFFu);
    state.WriteRamin32(end, 0xFFFFFFFFu);
    if(!ExpectEqual(state.ReadRamin32(lastDword), 0xAABBCCDDu,
                    "partial or out-of-range writes must be ignored"))
    {
        return 1;
    }

    constexpr std::uint32_t handle = 0x00000123u;
    constexpr std::uint32_t objectInstance = 0x00000400u;
    WriteRamhtEntry(state, 0, 0x123u, handle,
                    0x80000000u | (objectInstance >> 4));
    state.WriteRamin32(first + objectInstance, 0x00000097u);

    const auto directLookup = state.LookupRamht(handle, 0, 0);
    if(!directLookup ||
       !ExpectEqual(directLookup->instance, objectInstance,
                    "direct RAMHT object instance") ||
       !ExpectEqual(directLookup->objectClass, 0x97u,
                    "direct RAMHT object class"))
    {
        return 1;
    }

    constexpr std::uint32_t channelThreeHash = 0x0A3u;
    WriteRamhtEntry(state, 0, channelThreeHash, handle, objectInstance >> 4);
    WriteRamhtEntry(state, 0, channelThreeHash + 1, handle + 1,
                    0x80000000u | (objectInstance >> 4));
    constexpr std::uint32_t zeroClassInstance = 0x00000500u;
    WriteRamhtEntry(state, 0, channelThreeHash + 2, handle,
                    0x80000000u | (zeroClassInstance >> 4));

    const auto collisionLookup = state.LookupRamht(handle, 0, 3);
    if(!collisionLookup ||
       !ExpectEqual(collisionLookup->instance, zeroClassInstance,
                    "probed RAMHT object instance") ||
       !ExpectEqual(collisionLookup->objectClass, 0,
                    "RAMHT must preserve a raw zero object class"))
    {
        return 1;
    }

    constexpr std::uint32_t cappedHandle = 0x00000321u;
    constexpr std::uint32_t cappedHash = 0x121u;
    WriteRamhtEntry(state, 0, cappedHash + 16, cappedHandle,
                    0x80000000u | (objectInstance >> 4));
    if(state.LookupRamht(cappedHandle, 0, 0))
    {
        std::fputs("RAMHT lookup exceeded its 16-entry probe bound\n", stderr);
        return 1;
    }

    constexpr std::uint32_t configuredRamht = 0x00010010u;
    constexpr std::uint32_t configuredTableOffset = 0x00001000u;
    constexpr std::uint32_t foldedHandle = 0xABCDEF01u;
    constexpr std::uint32_t foldedHash = 0x174u;
    constexpr std::uint32_t configuredInstance = 0x00000600u;
    WriteRamhtEntry(state, configuredTableOffset, foldedHash, foldedHandle,
                    0x80000000u | (configuredInstance >> 4));
    state.WriteRamin32(first + configuredInstance, 0x0000003Du);

    const auto configuredLookup =
        state.LookupRamht(foldedHandle, configuredRamht, 2);
    if(!configuredLookup ||
       !ExpectEqual(configuredLookup->instance, configuredInstance,
                    "configured RAMHT object instance") ||
       !ExpectEqual(configuredLookup->objectClass, 0x3Du,
                    "configured RAMHT object class"))
    {
        return 1;
    }

    constexpr std::uint32_t dmaInstance = 0x00000700u;
    constexpr std::uint32_t dmaFlags = 0xABC3003Du;
    constexpr std::uint32_t dmaLimit = 0x001FFFFFu;
    constexpr std::uint32_t dmaFrame = 0x81234000u;
    state.WriteRamin32(first + dmaInstance, dmaFlags);
    state.WriteRamin32(first + dmaInstance + 4, dmaLimit);
    state.WriteRamin32(first + dmaInstance + 8, dmaFrame);

    const auto dmaObject = state.DecodeDmaObject(dmaInstance);
    if(!dmaObject ||
       !ExpectEqual(dmaObject->rawFlags, dmaFlags,
                    "DMA object raw flags") ||
       !ExpectEqual(dmaObject->limit, dmaLimit, "DMA object limit") ||
       !ExpectEqual(dmaObject->rawFrame, dmaFrame,
                    "DMA object raw frame") ||
       !ExpectEqual(dmaObject->ObjectClass(), 0x3Du,
                    "DMA object class") ||
       !ExpectEqual(dmaObject->AdjustedAddress(), 0x81234ABCu,
                    "DMA object adjusted address") ||
       !ExpectEqual(dmaObject->LegacyPusherAddress(), 0x0123403Du,
                    "DMA object legacy pusher address"))
    {
        return 1;
    }

    const auto misalignedDmaObject = state.DecodeDmaObject(dmaInstance + 1);
    if(!misalignedDmaObject ||
       !ExpectEqual(misalignedDmaObject->AdjustedAddress(), 0x81234ABCu,
                    "legacy containing-dword DMA object access"))
    {
        return 1;
    }

    constexpr std::uint32_t lastDmaInstance =
        cxbx::nv2a::DeviceState::RaminSize - 12;
    state.WriteRamin32(first + lastDmaInstance, 0x00200003u);
    state.WriteRamin32(first + lastDmaInstance + 4, 0x00000FFFu);
    state.WriteRamin32(first + lastDmaInstance + 8, 0x00123000u);
    const auto lastDmaObject = state.DecodeDmaObject(lastDmaInstance);
    if(!lastDmaObject ||
       !ExpectEqual(lastDmaObject->AdjustedAddress(), 0x00123002u,
                    "last bounded DMA object"))
    {
        return 1;
    }

    if(state.DecodeDmaObject(lastDmaInstance + 1) ||
       state.DecodeDmaObject(cxbx::nv2a::DeviceState::RaminSize))
    {
        std::fputs("out-of-range DMA object decode must fail\n", stderr);
        return 1;
    }

    return 0;
}
