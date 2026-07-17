#pragma once

#include <array>

namespace cxbx::d3d
{

enum class PresentSceneStep
{
    EndScene,
    CaptureMirror,
    Present,
    BlitMirror,
    BeginScene,
};

enum class CpuFallbackMaterial
{
    PreservePixelShader,
    Diffuse,
    TextureModulate,
};

inline constexpr bool CpuFallbackTextureUsable(bool stage0TextureBound,
                                               unsigned int texCoordIndex,
                                               bool texCoordsFinite,
                                               bool texCoordsVary,
                                               bool textureHasData) noexcept
{
    return stage0TextureBound && texCoordIndex < 4 && texCoordsFinite && texCoordsVary &&
           textureHasData;
}

inline constexpr CpuFallbackMaterial SelectCpuFallbackMaterial(bool pixelShaderFallback,
                                                                bool pixelShaderBound,
                                                                bool stage0TextureUsable) noexcept
{
    if(pixelShaderBound && !pixelShaderFallback)
    {
        return CpuFallbackMaterial::PreservePixelShader;
    }

    return stage0TextureUsable ? CpuFallbackMaterial::TextureModulate
                               : CpuFallbackMaterial::Diffuse;
}

inline constexpr bool CpuFallbackTextureNeedsProjection(unsigned int textureMode) noexcept
{
    // Xbox PROJECT2D and PROJECT3D divide by the fourth texture component.
    return textureMode == 1u || textureMode == 2u;
}

inline constexpr bool ProjectCpuFallbackTextureCoordinates(float (&coordinates)[4],
                                                            unsigned int textureMode) noexcept
{
    if(!CpuFallbackTextureNeedsProjection(textureMode) ||
       (coordinates[3] > -1.0e-6f && coordinates[3] < 1.0e-6f))
    {
        return false;
    }

    const float q = coordinates[3];
    coordinates[0] /= q;
    coordinates[1] /= q;
    if(textureMode == 2u)
    {
        coordinates[2] /= q;
    }
    coordinates[3] = 1.0f;
    return true;
}

inline constexpr std::array<PresentSceneStep, 5> PresentSceneSteps() noexcept
{
    return {
        PresentSceneStep::EndScene,
        PresentSceneStep::CaptureMirror,
        PresentSceneStep::Present,
        PresentSceneStep::BlitMirror,
        PresentSceneStep::BeginScene,
    };
}

inline constexpr long long NextPresentDeadline(long long now, long long deadline,
                                               long long interval) noexcept
{
    if(interval <= 0)
    {
        return deadline;
    }
    if(deadline == 0)
    {
        return now + interval;
    }
    if(now >= deadline)
    {
        return deadline + ((now - deadline) / interval + 1) * interval;
    }
    return deadline;
}

inline bool MirrorWindowEnabled(const char* environmentValue) noexcept
{
    return environmentValue == nullptr || environmentValue[0] != '0' || environmentValue[1] != '\0';
}

inline bool ControlProbeEnabled(const char* environmentValue) noexcept
{
    return environmentValue != nullptr && environmentValue[0] == '1' &&
           environmentValue[1] == '\0';
}

} // namespace cxbx::d3d
