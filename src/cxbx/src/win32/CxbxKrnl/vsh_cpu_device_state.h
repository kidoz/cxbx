#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace XTL
{
struct X_D3DIndexBuffer;
struct X_D3DVertexBuffer;

namespace VshCpuDeviceState
{
struct StreamBinding
{
    X_D3DVertexBuffer* resource = nullptr;
    std::uint32_t stride = 0;
};

struct IndexBinding
{
    X_D3DIndexBuffer* resource = nullptr;
    std::uint32_t baseVertex = 0;
};

[[nodiscard]] std::span<const float> Constants();
void SetConstant(std::size_t hardwareIndex, std::span<const float, 4> value);

[[nodiscard]] StreamBinding Stream(std::size_t stream);
void SetStream(std::size_t stream, StreamBinding binding);

[[nodiscard]] IndexBinding IndexBuffer();
void SetIndexBuffer(IndexBinding binding);
} // namespace VshCpuDeviceState
} // namespace XTL
