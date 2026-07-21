// PCM channel conversion helpers.

#include "core/pcm_converter.h"

#include <climits>
#include <cstring>

namespace cxbx::audio
{
namespace
{
std::int16_t ReadPcm16Sample(const std::uint8_t* source) noexcept
{
    std::int16_t sample = 0;
    std::memcpy(&sample, source, sizeof(sample));
    return sample;
}

std::int16_t ClampPcm16(std::int32_t sample) noexcept
{
    if(sample > INT16_MAX)
    {
        return INT16_MAX;
    }
    if(sample < INT16_MIN)
    {
        return INT16_MIN;
    }
    return static_cast<std::int16_t>(sample);
}
} // namespace

bool CanDownmixPcm16ToStereo(std::size_t sourceChannels) noexcept
{
    return sourceChannels >= 3 && sourceChannels <= 6;
}

void DownmixPcm16ToStereo(const std::uint8_t* source,
                          std::size_t frameCount,
                          std::size_t sourceChannels,
                          std::uint8_t* destination) noexcept
{
    const std::size_t sourceFrameBytes = sourceChannels * sizeof(std::int16_t);
    for(std::size_t frame = 0; frame < frameCount; ++frame)
    {
        const std::uint8_t* input = source + frame * sourceFrameBytes;
        const std::int32_t frontLeft = ReadPcm16Sample(input);
        const std::int32_t frontRight = ReadPcm16Sample(input + sizeof(std::int16_t));
        std::int32_t left = frontLeft;
        std::int32_t right = frontRight;

        if(sourceChannels == 3 || sourceChannels >= 5)
        {
            const std::int32_t center =
                ReadPcm16Sample(input + 2 * sizeof(std::int16_t));
            left += center * 181 / 256;
            right += center * 181 / 256;
        }
        if(sourceChannels == 6)
        {
            const std::int32_t lowFrequency =
                ReadPcm16Sample(input + 3 * sizeof(std::int16_t));
            left += lowFrequency / 2;
            right += lowFrequency / 2;
        }

        const std::size_t surroundOffset = sourceChannels == 4 ? 2 : 3;
        if(sourceChannels >= 4)
        {
            const std::size_t leftSurround =
                sourceChannels == 6 ? surroundOffset + 1 : surroundOffset;
            left += ReadPcm16Sample(input + leftSurround * sizeof(std::int16_t)) *
                    181 / 256;
        }
        if(sourceChannels >= 4)
        {
            const std::size_t rightSurround =
                sourceChannels == 6 ? surroundOffset + 2 : surroundOffset + 1;
            right += ReadPcm16Sample(input + rightSurround * sizeof(std::int16_t)) *
                     181 / 256;
        }

        const std::int16_t stereo[2] = { ClampPcm16(left), ClampPcm16(right) };
        std::memcpy(destination + frame * sizeof(stereo), stereo, sizeof(stereo));
    }
}
} // namespace cxbx::audio
