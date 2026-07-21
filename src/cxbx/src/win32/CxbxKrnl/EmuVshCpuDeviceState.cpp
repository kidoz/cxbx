#include "EmuVshCpuDeviceState.h"

#include <algorithm>
#include <array>

namespace XTL::VshCpuDeviceState
{
namespace
{
constexpr std::size_t constantCount = 192;
constexpr std::size_t constantWidth = 4;
constexpr std::size_t streamCount = 16;

std::array<float, constantCount * constantWidth> g_constants{};
std::array<StreamBinding, streamCount> g_streams{};
IndexBinding g_indexBuffer{};
} // namespace

std::span<const float> Constants()
{
    return g_constants;
}

void SetConstant(std::size_t hardwareIndex, std::span<const float, 4> value)
{
    if(hardwareIndex >= constantCount)
    {
        return;
    }
    const std::ptrdiff_t offset =
        static_cast<std::ptrdiff_t>(hardwareIndex * constantWidth);
    std::copy(value.begin(), value.end(), g_constants.begin() + offset);
}

StreamBinding Stream(std::size_t stream)
{
    return stream < g_streams.size() ? g_streams[stream] : StreamBinding{};
}

void SetStream(std::size_t stream, StreamBinding binding)
{
    if(stream < g_streams.size())
    {
        g_streams[stream] = binding;
    }
}

IndexBinding IndexBuffer()
{
    return g_indexBuffer;
}

void SetIndexBuffer(IndexBinding binding)
{
    g_indexBuffer = binding;
}
} // namespace XTL::VshCpuDeviceState
