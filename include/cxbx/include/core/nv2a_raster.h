#pragma once

#include <cstdint>
#include <limits>

namespace cxbx::nv2a
{

inline constexpr bool IsUsableHomogeneousW(float w) noexcept
{
    return w > 1.0e-5f && w < (std::numeric_limits<float>::max)();
}

inline constexpr bool CanRasterizeHomogeneousTriangle(
    float w0, float w1, float w2) noexcept
{
    return IsUsableHomogeneousW(w0) && IsUsableHomogeneousW(w1) &&
           IsUsableHomogeneousW(w2);
}

struct ProjectedTextureCoordinates
{
    float u;
    float v;
    float interpolationWeight;
};

inline constexpr ProjectedTextureCoordinates ProjectTexture2D(
    float s, float t, float q, float inverseW) noexcept
{
    if(q > -1.0e-6f && q < 1.0e-6f)
    {
        return { s, t, inverseW };
    }

    return { s / q, t / q, q * inverseW };
}

inline constexpr bool IsFinalCombinerPassthroughR0(
    std::uint32_t inputsAbcd, std::uint32_t inputsEfg) noexcept
{
    return inputsAbcd == 0x0000000Cu &&
           ((inputsEfg >> 8) & 0xFFu) == 0x1Cu;
}

inline constexpr std::uint32_t BlendSourceAlpha(
    std::uint32_t source, std::uint32_t destination) noexcept
{
    const std::uint32_t alpha = source >> 24;
    if(alpha == 0)
    {
        return destination;
    }
    if(alpha == 255)
    {
        return source;
    }
    const std::uint32_t inverseAlpha = 255u - alpha;
    std::uint32_t output = 0;
    for(int shift = 0; shift < 32; shift += 8)
    {
        const std::uint32_t sourceChannel = (source >> shift) & 0xFFu;
        const std::uint32_t destinationChannel =
            (destination >> shift) & 0xFFu;
        const std::uint32_t channel =
            (sourceChannel * alpha + destinationChannel * inverseAlpha + 127u) /
            255u;
        output |= channel << shift;
    }
    return output;
}

struct AffineQuadSpan
{
    float value;
    float step;
};

inline constexpr bool CanUseAffineQuadInterpolation(
    float topLeft, float topRight, float bottomRight, float bottomLeft) noexcept
{
    const auto sameWeight = [](float a, float b)
    {
        const float difference = a - b;
        return difference > -1.0e-6f && difference < 1.0e-6f;
    };
    return (topLeft > 1.0e-9f || topLeft < -1.0e-9f) &&
           sameWeight(topLeft, topRight) &&
           sameWeight(topLeft, bottomRight) &&
           sameWeight(topLeft, bottomLeft);
}

inline constexpr AffineQuadSpan BuildAffineQuadSpan(
    float topLeft, float topRight, float bottomRight, float bottomLeft,
    float y, float firstX, float xStep) noexcept
{
    const float left = topLeft + (bottomLeft - topLeft) * y;
    const float right = topRight + (bottomRight - topRight) * y;
    return { left + (right - left) * firstX, (right - left) * xStep };
}

inline constexpr int MapRegisterCombinerOutput(
    int value, std::uint32_t flags) noexcept
{
    switch(flags & 0x38u)
    {
        case 0x08u: value -= 128; break;
        case 0x10u: value *= 2; break;
        case 0x18u: value = (value - 128) * 2; break;
        case 0x20u: value *= 4; break;
        case 0x30u: value /= 2; break;
        default: break;
    }
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}

inline constexpr int SelectRegisterCombinerOutput(
    int ab, int cd, std::uint32_t output, int previous) noexcept
{
    const std::uint32_t flags = output >> 12;
    int result = previous;
    if(((output >> 4) & 0x0Fu) == 0x0Cu)
    {
        result = MapRegisterCombinerOutput(ab, flags);
    }
    if((output & 0x0Fu) == 0x0Cu)
    {
        result = MapRegisterCombinerOutput(cd, flags);
    }
    if(((output >> 8) & 0x0Fu) == 0x0Cu)
    {
        result = MapRegisterCombinerOutput(ab + cd, flags);
    }
    return result;
}

struct FinalCombinerRegisters
{
    std::uint32_t constant0 = 0;
    std::uint32_t constant1 = 0;
    std::uint32_t fog = 0;
    std::uint32_t primary = 0;
    std::uint32_t secondary = 0;
    std::uint32_t texture0 = 0;
    std::uint32_t texture1 = 0;
    std::uint32_t texture2 = 0;
    std::uint32_t texture3 = 0;
    std::uint32_t r0 = 0;
    std::uint32_t r1 = 0;
};

namespace detail
{

inline constexpr int ClampColor(int value) noexcept
{
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}

inline constexpr int PackedChannel(std::uint32_t color, int component) noexcept
{
    const int shift = component == 0 ? 16 : (component == 1 ? 8 : (component == 2 ? 0 : 24));
    return static_cast<int>((color >> shift) & 0xFFu);
}

inline constexpr std::uint32_t FinalCombinerRegister(
    const FinalCombinerRegisters& registers, unsigned index) noexcept
{
    switch(index)
    {
        case 0x01: return registers.constant0;
        case 0x02: return registers.constant1;
        case 0x03: return registers.fog;
        case 0x04: return registers.primary;
        case 0x05: return registers.secondary;
        case 0x08: return registers.texture0;
        case 0x09: return registers.texture1;
        case 0x0A: return registers.texture2;
        case 0x0B: return registers.texture3;
        case 0x0C: return registers.r0;
        case 0x0D: return registers.r1;
        default: return 0;
    }
}

inline constexpr int MapFinalCombinerInput(int value, unsigned mapping) noexcept
{
    const int unsignedValue = value < 0 ? 0 : value;
    switch(mapping)
    {
        case 0x01: return 255 - unsignedValue;
        case 0x02: return unsignedValue * 2 - 255;
        case 0x03: return 255 - unsignedValue * 2;
        case 0x04: return unsignedValue - 128;
        case 0x05: return 128 - unsignedValue;
        case 0x06: return value;
        case 0x07: return -value;
        default: return unsignedValue;
    }
}

inline constexpr int FinalCombinerInput(
    unsigned input, int component, const FinalCombinerRegisters& registers,
    const int* colorSum, const int* efProduct) noexcept
{
    const unsigned index = input & 0x0Fu;
    int value = 0;
    if(index == 0x0E && component < 3)
    {
        value = colorSum[component];
    }
    else if(index == 0x0F && component < 3)
    {
        value = efProduct[component];
    }
    else
    {
        const bool alpha = (input & 0x10u) != 0;
        const int sourceComponent = alpha ? 3 : (component < 3 ? component : 2);
        value = PackedChannel(FinalCombinerRegister(registers, index),
                              sourceComponent);
    }
    return MapFinalCombinerInput(value, (input >> 5) & 0x07u);
}

} // namespace detail

// NV2A final combiner: rgb = A*B + (1-A)*C + D, alpha = G. E/F form
// EF_PROD, while the low byte of inputsEfg configures the V1+R0 color sum.
inline constexpr std::uint32_t RunFinalCombiner(
    std::uint32_t inputsAbcd, std::uint32_t inputsEfg,
    const FinalCombinerRegisters& registers) noexcept
{
    const unsigned eInput = (inputsEfg >> 24) & 0xFFu;
    const unsigned fInput = (inputsEfg >> 16) & 0xFFu;
    const unsigned gInput = (inputsEfg >> 8) & 0xFFu;
    const unsigned settings = inputsEfg & 0xFFu;
    int colorSum[3] = {};
    int efProduct[3] = {};
    const int unused[3] = {};

    for(int component = 0; component < 3; ++component)
    {
        int secondary = detail::PackedChannel(registers.secondary, component);
        int r0 = detail::PackedChannel(registers.r0, component);
        if((settings & 0x40u) != 0)
        {
            secondary = 255 - secondary;
        }
        if((settings & 0x20u) != 0)
        {
            r0 = 255 - r0;
        }
        colorSum[component] = secondary + r0;
        if((settings & 0x80u) != 0)
        {
            colorSum[component] = detail::ClampColor(colorSum[component]);
        }

        const int e = detail::FinalCombinerInput(
            eInput, component, registers, unused, unused);
        const int f = detail::FinalCombinerInput(
            fInput, component, registers, unused, unused);
        efProduct[component] = (e * f) / 255;
    }

    std::uint32_t output = 0;
    for(int component = 0; component < 3; ++component)
    {
        const int a = detail::FinalCombinerInput(
            (inputsAbcd >> 24) & 0xFFu, component, registers, colorSum,
            efProduct);
        const int b = detail::FinalCombinerInput(
            (inputsAbcd >> 16) & 0xFFu, component, registers, colorSum,
            efProduct);
        const int c = detail::FinalCombinerInput(
            (inputsAbcd >> 8) & 0xFFu, component, registers, colorSum,
            efProduct);
        const int d = detail::FinalCombinerInput(
            inputsAbcd & 0xFFu, component, registers, colorSum, efProduct);
        const int value = detail::ClampColor(
            (a * b + (255 - a) * c) / 255 + d);
        const int shift = component == 0 ? 16 : (component == 1 ? 8 : 0);
        output |= static_cast<std::uint32_t>(value) << shift;
    }

    const int alpha = detail::ClampColor(detail::FinalCombinerInput(
        gInput, 3, registers, colorSum, efProduct));
    return output | (static_cast<std::uint32_t>(alpha) << 24);
}

} // namespace cxbx::nv2a
