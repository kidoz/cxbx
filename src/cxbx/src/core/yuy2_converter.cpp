#include "core/yuy2_converter.h"

#include <limits>

namespace
{
constexpr int kColorScale = 1024;
constexpr int kRedFromV = 1436;
constexpr int kGreenFromU = 352;
constexpr int kGreenFromV = 731;
constexpr int kBlueFromU = 1815;

std::uint8_t ClampFixedColor(int color)
{
    if(color <= 0)
    {
        return 0;
    }
    if(color >= 255 * kColorScale)
    {
        return 255;
    }
    return static_cast<std::uint8_t>(color / kColorScale);
}

void StoreBgraPixel(std::uint8_t*& destination,
                    int scaledLuma,
                    int blueDelta,
                    int greenDelta,
                    int redDelta)
{
    destination[0] = ClampFixedColor(scaledLuma + blueDelta);
    destination[1] = ClampFixedColor(scaledLuma + greenDelta);
    destination[2] = ClampFixedColor(scaledLuma + redDelta);
    destination[3] = 0xFF;
    destination += 4;
}

bool RowsFitAddressSpace(std::size_t pitch,
                         std::size_t rowBytes,
                         std::uint32_t height)
{
    if(height == 0 || pitch < rowBytes)
    {
        return false;
    }

    const std::size_t remainingRows = static_cast<std::size_t>(height - 1);
    return remainingRows <=
           (std::numeric_limits<std::size_t>::max() - rowBytes) / pitch;
}
} // namespace

namespace CxbxVideo
{
bool ConvertYuy2ToBgra(const std::uint8_t* source,
                       std::size_t sourcePitch,
                       std::uint8_t* destination,
                       std::size_t destinationPitch,
                       std::uint32_t width,
                       std::uint32_t height)
{
    const std::uint32_t evenWidth = width & ~1u;
    if(source == nullptr || destination == nullptr || evenWidth == 0)
    {
        return false;
    }
    if constexpr(sizeof(std::size_t) <= sizeof(std::uint32_t))
    {
        if(evenWidth > std::numeric_limits<std::size_t>::max() / 4)
        {
            return false;
        }
    }

    const std::size_t sourceRowBytes = static_cast<std::size_t>(evenWidth) * 2;
    const std::size_t destinationRowBytes = static_cast<std::size_t>(evenWidth) * 4;
    if(!RowsFitAddressSpace(sourcePitch, sourceRowBytes, height) ||
       !RowsFitAddressSpace(destinationPitch, destinationRowBytes, height))
    {
        return false;
    }

    for(std::uint32_t y = 0; y < height; ++y)
    {
        const std::uint8_t* sourcePixel = source + static_cast<std::size_t>(y) * sourcePitch;
        std::uint8_t* destinationPixel = destination + static_cast<std::size_t>(y) * destinationPitch;

        for(std::uint32_t x = 0; x < evenWidth; x += 2)
        {
            const int u = static_cast<int>(sourcePixel[1]) - 128;
            const int v = static_cast<int>(sourcePixel[3]) - 128;
            const int redDelta = kRedFromV * v;
            const int greenDelta = -kGreenFromU * u - kGreenFromV * v;
            const int blueDelta = kBlueFromU * u;

            StoreBgraPixel(destinationPixel,
                           static_cast<int>(sourcePixel[0]) * kColorScale,
                           blueDelta, greenDelta, redDelta);
            StoreBgraPixel(destinationPixel,
                           static_cast<int>(sourcePixel[2]) * kColorScale,
                           blueDelta, greenDelta, redDelta);
            sourcePixel += 4;
        }
    }

    return true;
}
} // namespace CxbxVideo
