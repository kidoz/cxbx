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

    return 0;
}
