#include "core/yuy2_converter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <vector>

namespace
{
int gFailures = 0;

void Check(const char* name, bool passed)
{
    if(!passed)
    {
        std::fprintf(stderr, "FAIL %s\n", name);
        ++gFailures;
    }
}

std::uint8_t ReferenceColor(float color)
{
    return static_cast<std::uint8_t>(std::clamp(color, 0.0f, 255.0f));
}

void CheckPixelNear(const char* name,
                    const std::uint8_t* actual,
                    std::uint8_t y,
                    std::uint8_t u,
                    std::uint8_t v)
{
    const float chromaU = static_cast<float>(u) - 128.0f;
    const float chromaV = static_cast<float>(v) - 128.0f;
    const std::array<std::uint8_t, 4> expected = {
        ReferenceColor(static_cast<float>(y) + 1.772f * chromaU),
        ReferenceColor(static_cast<float>(y) - 0.344f * chromaU - 0.714f * chromaV),
        ReferenceColor(static_cast<float>(y) + 1.402f * chromaV),
        0xFF,
    };

    for(std::size_t channel = 0; channel < expected.size(); ++channel)
    {
        const int difference = std::abs(static_cast<int>(actual[channel]) -
                                        static_cast<int>(expected[channel]));
        Check(name, difference <= 1);
    }
}
} // namespace

int main()
try
{
    std::array<std::uint8_t, 4> source = { 16, 128, 235, 128 };
    std::array<std::uint8_t, 8> destination{};
    Check("neutral conversion",
          CxbxVideo::ConvertYuy2ToBgra(source.data(), source.size(),
                                       destination.data(), destination.size(), 2, 1));
    Check("neutral black", destination[0] == 16 && destination[1] == 16 &&
                               destination[2] == 16 && destination[3] == 0xFF);
    Check("neutral white", destination[4] == 235 && destination[5] == 235 &&
                               destination[6] == 235 && destination[7] == 0xFF);

    Check("null source rejected",
          !CxbxVideo::ConvertYuy2ToBgra(nullptr, 4, destination.data(), 8, 2, 1));
    Check("short source pitch rejected",
          !CxbxVideo::ConvertYuy2ToBgra(source.data(), 3, destination.data(), 8, 2, 1));
    Check("short destination pitch rejected",
          !CxbxVideo::ConvertYuy2ToBgra(source.data(), 4, destination.data(), 7, 2, 1));

    for(int y = 0; y <= 255; y += 17)
    {
        for(int u = 0; u <= 255; u += 31)
        {
            for(int v = 0; v <= 255; v += 29)
            {
                source = { static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(u),
                           static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(v) };
                Check("sample conversion",
                      CxbxVideo::ConvertYuy2ToBgra(source.data(), 4,
                                                   destination.data(), 8, 2, 1));
                CheckPixelNear("sample pixel 0", destination.data(), source[0], source[1], source[3]);
                CheckPixelNear("sample pixel 1", destination.data() + 4, source[2], source[1], source[3]);
            }
        }
    }

    constexpr std::uint32_t width = 640;
    constexpr std::uint32_t height = 480;
    constexpr int frameCount = 60;
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(width) * height * 2, 128);
    std::vector<std::uint8_t> converted(static_cast<std::size_t>(width) * height * 4);
    const auto start = std::chrono::steady_clock::now();
    for(int frameIndex = 0; frameIndex < frameCount; ++frameIndex)
    {
        Check("benchmark conversion",
              CxbxVideo::ConvertYuy2ToBgra(frame.data(), width * 2,
                                           converted.data(), width * 4, width, height));
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double megapixelsPerSecond =
        static_cast<double>(width) * height * frameCount / seconds / 1'000'000.0;
    std::printf("INFO YUY2 fixed-point conversion %.1f MP/s\n", megapixelsPerSecond);

    if(gFailures == 0)
    {
        std::puts("PASS host YUY2 conversion");
    }
    return gFailures;
}
catch(const std::exception& exception)
{
    std::fprintf(stderr, "FAIL host YUY2 conversion exception: %s\n", exception.what());
    return 1;
}
