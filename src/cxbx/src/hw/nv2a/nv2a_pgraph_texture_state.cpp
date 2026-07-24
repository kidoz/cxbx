#include "hw/nv2a_pgraph_texture_state.h"

namespace cxbx::nv2a
{

namespace
{

constexpr std::uint32_t AllSamplerMask =
    (1u << PgraphTextureStageCount) - 1u;

PgraphTextureStep ApplyContextDma(
    std::uint32_t& handle, std::uint32_t data) noexcept
{
    const bool changed = handle != data;
    handle = data;
    return {
        PgraphTextureStepKind::ContextDma,
        PgraphTextureStageCount,
        changed,
        changed ? AllSamplerMask : 0,
    };
}

} // namespace

PgraphTextureStep ApplyPgraphTextureMethod(
    PgraphTextureState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    if(method == PgraphTextureMethod::SetContextDmaA)
    {
        return ApplyContextDma(state.contextDmaA, data);
    }
    if(method == PgraphTextureMethod::SetContextDmaB)
    {
        return ApplyContextDma(state.contextDmaB, data);
    }

    constexpr std::uint32_t TextureMethodEnd =
        PgraphTextureMethod::SetTextureOffsetStage0 +
        PgraphTextureMethod::TextureStageStride * PgraphTextureStageCount;
    if(method < PgraphTextureMethod::SetTextureOffsetStage0 ||
       method >= TextureMethodEnd)
    {
        return {};
    }

    const std::uint32_t stage =
        (method - PgraphTextureMethod::SetTextureOffsetStage0) /
        PgraphTextureMethod::TextureStageStride;
    const std::uint32_t stageMethod =
        PgraphTextureMethod::SetTextureOffsetStage0 +
        stage * PgraphTextureMethod::TextureStageStride;
    PgraphTextureStageState& texture = state.stages[stage];
    std::uint32_t* field = nullptr;
    bool invalidatesSampler = true;

    switch(method - stageMethod)
    {
        case PgraphTextureMethod::SetTextureOffsetStage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.offset;
            break;
        case PgraphTextureMethod::SetTextureFormatStage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.format;
            break;
        case PgraphTextureMethod::SetTextureAddressStage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.address;
            invalidatesSampler = false;
            break;
        case PgraphTextureMethod::SetTextureControl0Stage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.control0;
            invalidatesSampler = false;
            break;
        case PgraphTextureMethod::SetTextureControl1Stage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.control1;
            break;
        case PgraphTextureMethod::SetTextureImageRectStage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.imageRect;
            break;
        case PgraphTextureMethod::SetTextureFilterStage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.filter;
            invalidatesSampler = false;
            break;
        case PgraphTextureMethod::SetTexturePaletteStage0 -
            PgraphTextureMethod::SetTextureOffsetStage0:
            field = &texture.palette;
            invalidatesSampler = false;
            break;
        default:
            return {
                PgraphTextureStepKind::TextureStage,
                stage,
                false,
                0,
            };
    }

    const bool changed = *field != data;
    *field = data;
    return {
        PgraphTextureStepKind::TextureStage,
        stage,
        changed,
        changed && invalidatesSampler ? 1u << stage : 0,
    };
}

std::uint32_t SelectPgraphTextureDmaHandle(
    const PgraphTextureState& state, std::uint32_t format) noexcept
{
    return (format & 0x3u) != 0 ? state.contextDmaB : state.contextDmaA;
}

std::uint32_t SelectPgraphPaletteDmaHandle(
    const PgraphTextureState& state, std::uint32_t palette) noexcept
{
    return (palette & 0x1u) != 0 ? state.contextDmaB : state.contextDmaA;
}

} // namespace cxbx::nv2a
