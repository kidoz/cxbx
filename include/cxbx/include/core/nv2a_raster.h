#pragma once

#include <cstddef>
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

// A vertex program emits oPos already in screen space, so w only drives
// perspective-correct attribute interpolation -- it is not a divisor. A 2D
// pass-through program may therefore never write oPos.w, leaving it at zero,
// which CanRasterizeHomogeneousTriangle would reject and so drop the whole
// primitive. Map that degenerate case onto a neutral w. A negative w still
// means behind the viewer and must keep failing the guard, so it is preserved.
inline constexpr float NeutralizeDegenerateScreenSpaceW(float w) noexcept
{
    return (w > -1.0e-5f && w < 1.0e-5f) ? 1.0f : w;
}

struct ProjectedTextureCoordinates
{
    float u;
    float v;
    float interpolationWeight;
};

struct LinearDisplayFilterPass
{
    bool immediate = false;
    std::uint32_t primitive = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t sourceOffset = 0;
    std::uint32_t sourceFormat = 0;
    std::uint32_t sourceControl0 = 0;
    std::uint32_t sourceControl1 = 0;
    std::uint32_t sourceImageRect = 0;
    std::uint32_t destinationOffset = 0;
    std::uint32_t destinationPitch = 0;
    std::uint32_t destinationWidth = 0;
    std::uint32_t destinationHeight = 0;
    std::uint32_t shaderStageProgram = 0;
    std::uint32_t combinerControl = 0;
    bool depthTest = false;
    bool blend = false;
    bool alphaTest = false;
    bool stencilTest = false;
    float x[3] = {};
    float y[3] = {};
    float u[3] = {};
    float v[3] = {};
};

struct LinearDisplayFilterPlan
{
    bool valid = false;
    float sourceWidth = 0.0f;
    float sourceHeight = 0.0f;
};

struct PalettedTextPass
{
    bool inlineVertices = false;
    std::uint32_t primitive = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t textureOffset = 0;
    std::uint32_t textureFormat = 0;
    std::uint32_t textureControl0 = 0;
    std::uint32_t texturePalette = 0;
    std::uint32_t shaderStageProgram = 0;
    std::uint32_t combinerControl = 0;
    bool blend = false;
    std::uint32_t blendSourceFactor = 0;
    std::uint32_t blendDestinationFactor = 0;
    std::uint32_t blendEquation = 0;
    bool alphaTest = false;
    bool depthTest = false;
    bool stencilTest = false;
    bool normalizedCoordinates = false;
    float minU = 0.0f;
    float maxU = 0.0f;
    float minV = 0.0f;
    float maxV = 0.0f;
};

// Some XDK text batches rely on pixel state restored from a GPU context rather
// than resubmitting it in the method stream. Restrict reconstruction to
// normalized P8 font-atlas quads with the complete observed render-state shape.
inline constexpr bool IsLegacyPalettedTextState(
    const PalettedTextPass& pass) noexcept
{
    constexpr std::uint32_t Quads = 8u;
    constexpr std::uint32_t TextureEnable = 0x40000000u;
    constexpr std::uint32_t Texture2D = 2u;
    constexpr std::uint32_t Paletted8 = 0x0Bu;
    constexpr std::uint32_t SourceAlpha = 0x0302u;
    constexpr std::uint32_t OneMinusSourceAlpha = 0x0303u;
    constexpr std::uint32_t Add = 0x8006u;

    const std::uint32_t dimension = (pass.textureFormat >> 4) & 0xFu;
    const std::uint32_t colorFormat = (pass.textureFormat >> 8) & 0xFFu;
    const std::uint32_t sizeU = (pass.textureFormat >> 20) & 0xFu;
    const std::uint32_t sizeV = (pass.textureFormat >> 24) & 0xFu;
    if(!pass.inlineVertices || pass.primitive != Quads ||
       pass.vertexCount < 4u || (pass.vertexCount % 4u) != 0u ||
       pass.textureOffset == 0u || pass.texturePalette == 0u ||
       dimension != Texture2D || colorFormat != Paletted8 ||
       sizeU == 0u || sizeU > 12u || sizeV == 0u || sizeV > 12u ||
       (pass.textureControl0 & TextureEnable) != 0u ||
       pass.shaderStageProgram != 0u ||
       (pass.combinerControl & 0xFFu) != 0u ||
       !pass.blend || pass.blendSourceFactor != SourceAlpha ||
       pass.blendDestinationFactor != OneMinusSourceAlpha ||
       pass.blendEquation != Add || !pass.alphaTest || !pass.depthTest ||
       pass.stencilTest)
    {
        return false;
    }

    return true;
}

inline constexpr bool IsLegacyPalettedTextPass(
    const PalettedTextPass& pass) noexcept
{
    if(!IsLegacyPalettedTextState(pass) || !pass.normalizedCoordinates)
    {
        return false;
    }

    constexpr float CoordinateEpsilon = 1.0e-4f;
    constexpr float MinimumSpan = 1.0e-6f;
    return pass.minU >= -CoordinateEpsilon &&
           pass.maxU <= 1.0f + CoordinateEpsilon &&
           pass.minV >= -CoordinateEpsilon &&
           pass.maxV <= 1.0f + CoordinateEpsilon &&
           pass.maxU - pass.minU > MinimumSpan &&
           pass.maxV - pass.minV > MinimumSpan;
}

// XDK software display filtering is submitted as a disabled linear-texture
// descriptor plus an oversized immediate triangle. The texture/combiner state
// normally restored from the GPU context is absent from a raw method replay, so
// recognize the complete geometry and state signature before reconstructing it.
inline constexpr LinearDisplayFilterPlan BuildLinearDisplayFilterPlan(
    const LinearDisplayFilterPass& pass) noexcept
{
    constexpr std::uint32_t TextureEnable = 0x40000000u;
    constexpr std::uint32_t LinearA8R8G8B8 = 0x12u;
    const std::uint32_t sourceColor = (pass.sourceFormat >> 8) & 0xFFu;
    const std::uint32_t sourcePitch = pass.sourceControl1 >> 16;
    const std::uint32_t sourceWidth = pass.sourceImageRect >> 16;
    const std::uint32_t sourceHeight = pass.sourceImageRect & 0xFFFFu;
    if(!pass.immediate || pass.primitive != 5u || pass.vertexCount != 3u ||
       pass.sourceOffset == 0u ||
       pass.sourceOffset == pass.destinationOffset ||
       sourceColor != LinearA8R8G8B8 ||
       (pass.sourceControl0 & TextureEnable) != 0u ||
       sourcePitch != pass.destinationPitch ||
       sourceWidth != pass.destinationWidth ||
       sourceHeight <= pass.destinationHeight ||
       pass.destinationWidth == 0u || pass.destinationHeight == 0u ||
       pass.shaderStageProgram != 0u ||
       (pass.combinerControl & 0xFFu) != 0u ||
       pass.depthTest || pass.blend || pass.alphaTest || pass.stencilTest)
    {
        return {};
    }

    constexpr float Epsilon = 1.0e-3f;
    const auto nearZero = [](float value)
    {
        return value > -Epsilon && value < Epsilon;
    };
    if(!nearZero(pass.x[0]) || !nearZero(pass.y[0]) ||
       !nearZero(pass.y[1]) || !nearZero(pass.x[2]) ||
       !nearZero(pass.u[0]) || !nearZero(pass.v[0]) ||
       !nearZero(pass.v[1]) || !nearZero(pass.u[2]) ||
       pass.x[1] < static_cast<float>(pass.destinationWidth) * 2.0f ||
       pass.y[2] < static_cast<float>(pass.destinationHeight) * 2.0f ||
       pass.u[1] <= 0.0f || pass.v[2] <= 0.0f)
    {
        return {};
    }

    const float usedSourceWidth =
        pass.u[1] * static_cast<float>(pass.destinationWidth) / pass.x[1];
    const float usedSourceHeight =
        pass.v[2] * static_cast<float>(pass.destinationHeight) / pass.y[2];
    if(usedSourceWidth < static_cast<float>(pass.destinationWidth) - 1.0f ||
       usedSourceWidth > static_cast<float>(sourceWidth) + 1.0f ||
       usedSourceHeight < static_cast<float>(pass.destinationHeight) ||
       usedSourceHeight > static_cast<float>(sourceHeight) + 1.0f)
    {
        return {};
    }

    return { true, usedSourceWidth, usedSourceHeight };
}

inline constexpr float NormalizeLinearTextureCoordinate(
    float coordinate, std::uint32_t dimension) noexcept
{
    return dimension != 0u ? coordinate / static_cast<float>(dimension) : 0.0f;
}

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

inline void StretchSurfaceRowsNearest(
    const std::uint32_t* source, std::size_t sourcePitchPixels,
    std::size_t sourceWidth, std::size_t sourceHeight,
    std::uint32_t* destination, std::size_t destinationPitchPixels,
    std::size_t destinationWidth) noexcept
{
    if(source == nullptr || destination == nullptr || sourceWidth == 0 ||
       sourceHeight == 0 || sourcePitchPixels < sourceWidth ||
       destinationWidth == 0 || destinationPitchPixels < destinationWidth)
    {
        return;
    }

    for(std::size_t y = 0; y < sourceHeight; ++y)
    {
        const std::uint32_t* sourceRow = source + y * sourcePitchPixels;
        std::uint32_t* destinationRow =
            destination + y * destinationPitchPixels;
        for(std::size_t x = 0; x < destinationWidth; ++x)
        {
            destinationRow[x] =
                sourceRow[(x * sourceWidth) / destinationWidth];
        }
    }
}

struct RasterBounds
{
    int minX;
    int minY;
    int maxX;
    int maxY;

    [[nodiscard]] constexpr bool Empty() const noexcept
    {
        return minX >= maxX || minY >= maxY;
    }
};

inline constexpr RasterBounds IntersectRasterBounds(
    RasterBounds bounds, RasterBounds clip) noexcept
{
    if(bounds.minX < clip.minX)
    {
        bounds.minX = clip.minX;
    }
    if(bounds.minY < clip.minY)
    {
        bounds.minY = clip.minY;
    }
    if(bounds.maxX > clip.maxX)
    {
        bounds.maxX = clip.maxX;
    }
    if(bounds.maxY > clip.maxY)
    {
        bounds.maxY = clip.maxY;
    }
    return bounds;
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
