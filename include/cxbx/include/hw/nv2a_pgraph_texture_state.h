#pragma once

#include <array>
#include <cstdint>

namespace cxbx::nv2a
{

inline constexpr std::uint32_t PgraphTextureStageCount = 4;

struct PgraphTextureMethod final
{
    static constexpr std::uint32_t SetContextDmaA = 0x1A60u;
    static constexpr std::uint32_t SetContextDmaB = 0x1A64u;
    static constexpr std::uint32_t SetTextureOffsetStage0 = 0x1B00u;
    static constexpr std::uint32_t SetTextureFormatStage0 = 0x1B04u;
    static constexpr std::uint32_t SetTextureAddressStage0 = 0x1B08u;
    static constexpr std::uint32_t SetTextureControl0Stage0 = 0x1B0Cu;
    static constexpr std::uint32_t SetTextureControl1Stage0 = 0x1B10u;
    static constexpr std::uint32_t SetTextureFilterStage0 = 0x1B14u;
    static constexpr std::uint32_t SetTextureImageRectStage0 = 0x1B1Cu;
    static constexpr std::uint32_t SetTexturePaletteStage0 = 0x1B20u;
    static constexpr std::uint32_t TextureStageStride = 0x40u;
};

struct PgraphTextureStageState
{
    std::uint32_t offset = 0;
    std::uint32_t format = 0;
    std::uint32_t address = 0;
    std::uint32_t control0 = 0;
    std::uint32_t control1 = 0;
    std::uint32_t imageRect = 0;
    std::uint32_t filter = 0;
    std::uint32_t palette = 0;
};

struct PgraphTextureState
{
    std::uint32_t contextDmaA = 0;
    std::uint32_t contextDmaB = 0;
    std::array<PgraphTextureStageState, PgraphTextureStageCount> stages{};
};

enum class PgraphTextureStepKind : std::uint8_t
{
    Unhandled,
    ContextDma,
    TextureStage,
};

struct PgraphTextureStep
{
    PgraphTextureStepKind kind = PgraphTextureStepKind::Unhandled;
    std::uint32_t stage = PgraphTextureStageCount;
    bool changed = false;
    std::uint32_t samplerInvalidationMask = 0;
};

[[nodiscard]] PgraphTextureStep ApplyPgraphTextureMethod(
    PgraphTextureState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

[[nodiscard]] std::uint32_t SelectPgraphTextureDmaHandle(
    const PgraphTextureState& state, std::uint32_t format) noexcept;

[[nodiscard]] std::uint32_t SelectPgraphPaletteDmaHandle(
    const PgraphTextureState& state, std::uint32_t palette) noexcept;

} // namespace cxbx::nv2a
