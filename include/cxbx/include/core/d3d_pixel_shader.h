#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cxbx::d3d
{

inline constexpr std::size_t XboxPixelShaderDefinitionWords = 60;
using XboxPixelShaderDefinition =
    std::array<std::uint32_t, XboxPixelShaderDefinitionWords>;

enum class PixelShaderFallback
{
    Texture,
    TextureModulate,
    TextureModulate2X,
    DualTextureModulate2X,
    TextureModulate2XStage3,
};

inline constexpr const char* PixelShaderFallbackName(PixelShaderFallback fallback) noexcept
{
    switch(fallback)
    {
        case PixelShaderFallback::Texture:
            return "texture";
        case PixelShaderFallback::TextureModulate:
            return "texture_modulate";
        case PixelShaderFallback::TextureModulate2X:
            return "texture_modulate2x";
        case PixelShaderFallback::DualTextureModulate2X:
            return "dual_texture_modulate2x";
        case PixelShaderFallback::TextureModulate2XStage3:
            return "texture_modulate2x_stage3";
    }
    return "texture_modulate";
}

inline constexpr std::uint32_t PixelShaderTextureMode(
    const XboxPixelShaderDefinition& definition, unsigned int stage) noexcept
{
    constexpr std::array<unsigned int, 4> Shifts = { 0, 5, 10, 15 };
    return stage < Shifts.size() ? (definition[54] >> Shifts[stage]) & 0x1Fu : 0;
}

// Bump and dependent texture modes on stages 1-3 consume an earlier texture
// register. Stage 1 always consumes t0; the Xbox PS_INPUTTEXTURE field selects
// the source for stages 2 and 3.
inline constexpr unsigned int PixelShaderInputTexture(
    const XboxPixelShaderDefinition& definition, unsigned int stage) noexcept
{
    if(stage == 1)
    {
        return 0;
    }
    if(stage == 2)
    {
        return static_cast<unsigned int>((definition[55] >> 16) & 0x03u);
    }
    if(stage == 3)
    {
        return static_cast<unsigned int>((definition[55] >> 20) & 0x03u);
    }
    return 0;
}

inline constexpr std::uint32_t PixelShaderInputRegister(std::uint32_t input,
                                                        unsigned int operand) noexcept
{
    return operand < 4 ? (input >> ((3u - operand) * 8u)) & 0x0Fu : 0;
}

inline constexpr bool PixelShaderStageUsesRegister(
    const XboxPixelShaderDefinition& definition, unsigned int stage,
    std::uint32_t registerIndex) noexcept
{
    if(stage >= 8)
    {
        return false;
    }
    const std::uint32_t rgbInputs = definition[34 + stage];
    const std::uint32_t alphaInputs = definition[stage];
    for(unsigned int operand = 0; operand < 4; ++operand)
    {
        if(PixelShaderInputRegister(rgbInputs, operand) == registerIndex ||
           PixelShaderInputRegister(alphaInputs, operand) == registerIndex)
        {
            return true;
        }
    }
    return false;
}

inline constexpr PixelShaderFallback ClassifyPixelShaderFallback(
    const XboxPixelShaderDefinition& definition) noexcept
{
    constexpr std::uint32_t RegisterT0 = 8;
    constexpr std::uint32_t RegisterT1 = 9;
    constexpr std::uint32_t RegisterT3 = 11;
    constexpr std::uint32_t RegisterR0 = 12;
    constexpr std::uint32_t RegisterR1 = 13;

    const unsigned int combinerCount =
        static_cast<unsigned int>((definition[53] & 0x0Fu) > 8u
                                      ? 8u
                                      : (definition[53] & 0x0Fu));

    bool usesT1 = false;
    bool usesT3 = false;
    bool doublesTextureDiffuse = false;
    for(unsigned int stage = 0; stage < combinerCount; ++stage)
    {
        usesT1 = usesT1 || PixelShaderStageUsesRegister(definition, stage, RegisterT1);
        usesT3 = usesT3 || PixelShaderStageUsesRegister(definition, stage, RegisterT3);

        const std::uint32_t rgbInputs = definition[34 + stage];
        const std::uint32_t rgbOutput = definition[45 + stage];
        const bool multipliesTextureByLighting =
            PixelShaderInputRegister(rgbInputs, 0) == RegisterT0 &&
            PixelShaderInputRegister(rgbInputs, 1) == RegisterR1 &&
            ((rgbOutput >> 4) & 0x0Fu) == RegisterR0;
        const bool shiftsLeftOnce = ((rgbOutput >> 12) & 0x38u) == 0x10u;
        doublesTextureDiffuse = doublesTextureDiffuse ||
                                (multipliesTextureByLighting && shiftsLeftOnce);
    }

    if(PixelShaderTextureMode(definition, 1) != 0 && usesT1)
    {
        return PixelShaderFallback::DualTextureModulate2X;
    }
    if(PixelShaderTextureMode(definition, 3) != 0 && usesT3)
    {
        return PixelShaderFallback::TextureModulate2XStage3;
    }

    if(combinerCount != 0)
    {
        const std::uint32_t rgbInputs = definition[34];
        const std::uint32_t rgbOutput = definition[45];
        const bool selectsTexture =
            PixelShaderInputRegister(rgbInputs, 0) == RegisterT0 &&
            ((rgbInputs >> 16) & 0xFFu) == 0x20u &&
            ((rgbOutput >> 4) & 0x0Fu) == RegisterR0;
        if(selectsTexture)
        {
            return PixelShaderFallback::Texture;
        }
    }

    return doublesTextureDiffuse ? PixelShaderFallback::TextureModulate2X
                                 : PixelShaderFallback::TextureModulate;
}

} // namespace cxbx::d3d
