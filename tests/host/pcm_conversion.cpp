#include "core/PcmConverter.h"

#include <array>
#include <cstdint>

namespace
{
std::array<std::int16_t, 2> Downmix(const std::array<std::int16_t, 6>& input)
{
    std::array<std::int16_t, 2> output{};
    cxbx::audio::DownmixPcm16ToStereo(
        reinterpret_cast<const std::uint8_t*>(input.data()),
        1,
        input.size(),
        reinterpret_cast<std::uint8_t*>(output.data()));
    return output;
}
} // namespace

int main()
{
    if(cxbx::audio::CanDownmixPcm16ToStereo(2) ||
       !cxbx::audio::CanDownmixPcm16ToStereo(6) ||
       cxbx::audio::CanDownmixPcm16ToStereo(7))
    {
        return 1;
    }

    if(Downmix({ 1000, -1000, 0, 0, 0, 0 }) !=
       std::array<std::int16_t, 2>{ 1000, -1000 })
    {
        return 2;
    }

    if(Downmix({ 0, 0, 256, 0, 256, -256 }) !=
       std::array<std::int16_t, 2>{ 362, 0 })
    {
        return 3;
    }

    if(Downmix({ 32767, 32767, 32767, 32767, 32767, 32767 }) !=
       std::array<std::int16_t, 2>{ 32767, 32767 })
    {
        return 4;
    }

    std::array<std::int16_t, 2> quadOutput{};
    const std::array<std::int16_t, 4> quadInput{ 0, 0, 256, -256 };
    cxbx::audio::DownmixPcm16ToStereo(
        reinterpret_cast<const std::uint8_t*>(quadInput.data()),
        1,
        quadInput.size(),
        reinterpret_cast<std::uint8_t*>(quadOutput.data()));
    if(quadOutput != std::array<std::int16_t, 2>{ 181, -181 })
    {
        return 5;
    }

    return 0;
}
