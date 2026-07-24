#include "hw/nv2a_pgraph_texture_state.h"

#include <cstdio>
#include <cstdint>

namespace
{

bool ExpectEqual(std::uint32_t actual, std::uint32_t expected,
                 const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: expected 0x%08X, got 0x%08X\n", message,
                 expected, actual);
    return false;
}

bool Expect(bool condition, const char* message) noexcept
{
    if(condition)
    {
        return true;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphTextureMethod;
    using cxbx::nv2a::PgraphTextureState;
    using cxbx::nv2a::PgraphTextureStepKind;

    PgraphTextureState state{};
    if(!ExpectEqual(state.contextDmaA, 0, "default texture DMA A") ||
       !ExpectEqual(state.contextDmaB, 0, "default texture DMA B"))
    {
        return 1;
    }
    for(const auto& stage : state.stages)
    {
        if(!ExpectEqual(stage.offset, 0, "default texture offset") ||
           !ExpectEqual(stage.format, 0, "default texture format") ||
           !ExpectEqual(stage.address, 0, "default texture address") ||
           !ExpectEqual(stage.control0, 0, "default texture control 0") ||
           !ExpectEqual(stage.control1, 0, "default texture control 1") ||
           !ExpectEqual(stage.imageRect, 0, "default texture image rectangle") ||
           !ExpectEqual(stage.filter, 0, "default texture filter") ||
           !ExpectEqual(stage.palette, 0, "default texture palette"))
        {
            return 1;
        }
    }

    auto step = cxbx::nv2a::ApplyPgraphTextureMethod(
        state, PgraphTextureMethod::SetContextDmaA, 0x11110000u);
    if(!Expect(step.kind == PgraphTextureStepKind::ContextDma,
               "texture DMA A method was not handled") ||
       !Expect(step.changed, "texture DMA A change was not reported") ||
       !ExpectEqual(step.samplerInvalidationMask, 0xFu,
                    "texture DMA A sampler invalidation") ||
       !ExpectEqual(state.contextDmaA, 0x11110000u, "texture DMA A"))
    {
        return 1;
    }

    step = cxbx::nv2a::ApplyPgraphTextureMethod(
        state, PgraphTextureMethod::SetContextDmaA, 0x11110000u);
    if(!Expect(!step.changed, "unchanged texture DMA A was reported changed") ||
       !ExpectEqual(step.samplerInvalidationMask, 0,
                    "unchanged texture DMA A invalidation"))
    {
        return 1;
    }

    step = cxbx::nv2a::ApplyPgraphTextureMethod(
        state, PgraphTextureMethod::SetContextDmaB, 0x22220000u);
    if(!Expect(step.kind == PgraphTextureStepKind::ContextDma &&
                   step.changed,
               "texture DMA B transition is invalid") ||
       !ExpectEqual(step.samplerInvalidationMask, 0xFu,
                    "texture DMA B sampler invalidation") ||
       !ExpectEqual(state.contextDmaB, 0x22220000u, "texture DMA B"))
    {
        return 1;
    }

    if(!ExpectEqual(cxbx::nv2a::SelectPgraphTextureDmaHandle(state, 0),
                    state.contextDmaA, "texture DMA selector A") ||
       !ExpectEqual(cxbx::nv2a::SelectPgraphTextureDmaHandle(state, 1),
                    state.contextDmaB, "texture DMA selector B bit 0") ||
       !ExpectEqual(cxbx::nv2a::SelectPgraphTextureDmaHandle(state, 2),
                    state.contextDmaB, "texture DMA selector B bit 1") ||
       !ExpectEqual(cxbx::nv2a::SelectPgraphTextureDmaHandle(state, 3),
                    state.contextDmaB, "texture DMA selector B bits 0 and 1") ||
       !ExpectEqual(cxbx::nv2a::SelectPgraphPaletteDmaHandle(state, 0),
                    state.contextDmaA, "palette DMA selector A") ||
       !ExpectEqual(cxbx::nv2a::SelectPgraphPaletteDmaHandle(state, 1),
                    state.contextDmaB, "palette DMA selector B") ||
       !ExpectEqual(cxbx::nv2a::SelectPgraphPaletteDmaHandle(state, 2),
                    state.contextDmaA, "palette DMA selector ignores bit 1"))
    {
        return 1;
    }

    constexpr std::uint32_t Stage = 2;
    constexpr std::uint32_t StageOffset =
        Stage * PgraphTextureMethod::TextureStageStride;
    for(std::uint32_t stage = 0;
        stage < cxbx::nv2a::PgraphTextureStageCount; ++stage)
    {
        const std::uint32_t offset = 0x100u + stage;
        step = cxbx::nv2a::ApplyPgraphTextureMethod(
            state,
            PgraphTextureMethod::SetTextureOffsetStage0 +
                stage * PgraphTextureMethod::TextureStageStride,
            offset);
        if(!Expect(step.kind == PgraphTextureStepKind::TextureStage,
                   "texture offset method was not handled") ||
           !ExpectEqual(step.stage, stage, "texture offset stage") ||
           !ExpectEqual(step.samplerInvalidationMask, 1u << stage,
                        "texture offset sampler invalidation") ||
           !ExpectEqual(state.stages[stage].offset, offset,
                        "texture offset stage value"))
        {
            return 1;
        }
    }

    struct Transition
    {
        std::uint32_t method;
        std::uint32_t data;
        std::uint32_t expectedInvalidation;
    };
    constexpr Transition Transitions[] = {
        { PgraphTextureMethod::SetTextureOffsetStage0, 0x00001000u,
          1u << Stage },
        { PgraphTextureMethod::SetTextureFormatStage0, 0x00022001u,
          1u << Stage },
        { PgraphTextureMethod::SetTextureAddressStage0, 0x00000003u, 0 },
        { PgraphTextureMethod::SetTextureControl0Stage0, 0x40000000u, 0 },
        { PgraphTextureMethod::SetTextureControl1Stage0, 0x00800000u,
          1u << Stage },
        { PgraphTextureMethod::SetTextureImageRectStage0, 0x00400080u,
          1u << Stage },
        { PgraphTextureMethod::SetTextureFilterStage0, 0x02000000u, 0 },
        { PgraphTextureMethod::SetTexturePaletteStage0, 0x00000001u, 0 },
    };

    for(const Transition& transition : Transitions)
    {
        step = cxbx::nv2a::ApplyPgraphTextureMethod(
            state, transition.method + StageOffset, transition.data);
        if(!Expect(step.kind == PgraphTextureStepKind::TextureStage,
                   "texture-stage method was not handled") ||
           !ExpectEqual(step.stage, Stage, "decoded texture stage") ||
           !Expect(step.changed, "texture-stage change was not reported") ||
           !ExpectEqual(step.samplerInvalidationMask,
                        transition.expectedInvalidation,
                        "texture-stage sampler invalidation"))
        {
            return 1;
        }
    }

    const auto& texture = state.stages[Stage];
    if(!ExpectEqual(texture.offset, 0x00001000u, "texture offset") ||
       !ExpectEqual(texture.format, 0x00022001u, "texture format") ||
       !ExpectEqual(texture.address, 0x00000003u, "texture address") ||
       !ExpectEqual(texture.control0, 0x40000000u, "texture control 0") ||
       !ExpectEqual(texture.control1, 0x00800000u, "texture control 1") ||
       !ExpectEqual(texture.imageRect, 0x00400080u,
                    "texture image rectangle") ||
       !ExpectEqual(texture.filter, 0x02000000u, "texture filter") ||
       !ExpectEqual(texture.palette, 0x00000001u, "texture palette"))
    {
        return 1;
    }

    step = cxbx::nv2a::ApplyPgraphTextureMethod(
        state, PgraphTextureMethod::SetTextureOffsetStage0 + StageOffset,
        texture.offset);
    if(!Expect(!step.changed, "unchanged texture field was reported changed") ||
       !ExpectEqual(step.samplerInvalidationMask, 0,
                    "unchanged texture field invalidation"))
    {
        return 1;
    }

    step = cxbx::nv2a::ApplyPgraphTextureMethod(
        state,
        PgraphTextureMethod::SetTextureOffsetStage0 + StageOffset + 0x18u,
        0xFFFFFFFFu);
    if(!Expect(step.kind == PgraphTextureStepKind::TextureStage,
               "texture-stage method hole was not classified") ||
       !ExpectEqual(step.stage, Stage, "texture-stage method-hole stage") ||
       !Expect(!step.changed, "texture-stage method hole changed state") ||
       !ExpectEqual(step.samplerInvalidationMask, 0,
                    "texture-stage method-hole invalidation"))
    {
        return 1;
    }

    const PgraphTextureState beforeUnknown = state;
    step = cxbx::nv2a::ApplyPgraphTextureMethod(
        state, 0x00000400u, 0xFFFFFFFFu);
    if(!Expect(step.kind == PgraphTextureStepKind::Unhandled,
               "unknown texture method was handled") ||
       !ExpectEqual(state.contextDmaA, beforeUnknown.contextDmaA,
                    "unknown method texture DMA A") ||
       !ExpectEqual(state.stages[Stage].format,
                    beforeUnknown.stages[Stage].format,
                    "unknown method texture format"))
    {
        return 1;
    }

    return 0;
}
