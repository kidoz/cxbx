#include "hw/nv2a_pgraph_combiner_state.h"

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
    using cxbx::nv2a::PgraphCombinerMethod;
    using cxbx::nv2a::PgraphCombinerState;

    PgraphCombinerState state{};
    if(!ExpectEqual(state.control, 0, "default combiner control") ||
       !ExpectEqual(state.shaderStageProgram, 0,
                    "default shader-stage program") ||
       !ExpectEqual(state.finalCombinerCw0, 0,
                    "default final-combiner CW0") ||
       !ExpectEqual(state.finalCombinerCw1, 0,
                    "default final-combiner CW1") ||
       !ExpectEqual(state.finalCombinerMask, 0,
                    "default final-combiner mask") ||
       !Expect(!cxbx::nv2a::IsPgraphFinalCombinerComplete(state),
               "default final combiner is complete"))
    {
        return 1;
    }

    for(const auto& stage : state.stages)
    {
        if(!ExpectEqual(stage.alphaIcw, 0, "default alpha ICW") ||
           !ExpectEqual(stage.colorIcw, 0, "default color ICW") ||
           !ExpectEqual(stage.alphaOcw, 0, "default alpha OCW") ||
           !ExpectEqual(stage.colorOcw, 0, "default color OCW") ||
           !ExpectEqual(stage.factor0, 0, "default factor 0") ||
           !ExpectEqual(stage.factor1, 0, "default factor 1"))
        {
            return 1;
        }
    }

    for(std::uint32_t stage = 0;
        stage < cxbx::nv2a::PgraphCombinerStageCount; ++stage)
    {
        const std::uint32_t data = 0x10000000u + stage;
        if(!Expect(cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                       state,
                       PgraphCombinerMethod::SetAlphaIcwStage0 + stage * 4u,
                       data),
                   "alpha ICW method was not handled") ||
           !ExpectEqual(state.stages[stage].alphaIcw, data,
                        "decoded alpha ICW stage"))
        {
            return 1;
        }
    }

    constexpr std::uint32_t Stage = 5;
    constexpr std::uint32_t StageOffset = Stage * 4u;
    struct Transition
    {
        std::uint32_t method;
        std::uint32_t data;
    };
    constexpr Transition Transitions[] = {
        { PgraphCombinerMethod::SetColorIcwStage0, 0x20000001u },
        { PgraphCombinerMethod::SetAlphaOcwStage0, 0x20000002u },
        { PgraphCombinerMethod::SetColorOcwStage0, 0x20000003u },
        { PgraphCombinerMethod::SetFactor0Stage0, 0x20000004u },
        { PgraphCombinerMethod::SetFactor1Stage0, 0x20000005u },
    };
    for(const Transition& transition : Transitions)
    {
        if(!Expect(cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                       state, transition.method + StageOffset,
                       transition.data),
                   "combiner-stage method was not handled"))
        {
            return 1;
        }
    }

    const auto& combiner = state.stages[Stage];
    if(!ExpectEqual(combiner.alphaIcw, 0x10000005u, "stage alpha ICW") ||
       !ExpectEqual(combiner.colorIcw, 0x20000001u, "stage color ICW") ||
       !ExpectEqual(combiner.alphaOcw, 0x20000002u, "stage alpha OCW") ||
       !ExpectEqual(combiner.colorOcw, 0x20000003u, "stage color OCW") ||
       !ExpectEqual(combiner.factor0, 0x20000004u, "stage factor 0") ||
       !ExpectEqual(combiner.factor1, 0x20000005u, "stage factor 1"))
    {
        return 1;
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state, PgraphCombinerMethod::SetCombinerControl,
                   0x00000008u),
               "combiner-control method was not handled") ||
       !Expect(cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state, PgraphCombinerMethod::SetShaderStageProgram,
                   0x00064210u),
               "shader-stage-program method was not handled") ||
       !ExpectEqual(state.control, 0x00000008u, "combiner control") ||
       !ExpectEqual(state.shaderStageProgram, 0x00064210u,
                    "shader-stage program"))
    {
        return 1;
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state, PgraphCombinerMethod::SetFinalCombinerCw1,
                   0xABCDEF01u),
               "final-combiner CW1 method was not handled") ||
       !ExpectEqual(state.finalCombinerMask, 2,
                    "final-combiner CW1 mask") ||
       !Expect(!cxbx::nv2a::IsPgraphFinalCombinerComplete(state),
               "partial final combiner is complete") ||
       !Expect(cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state, PgraphCombinerMethod::SetFinalCombinerCw0 | 0xE0000003u,
                   0x12345678u),
               "masked final-combiner CW0 method was not handled") ||
       !ExpectEqual(state.finalCombinerCw0, 0x12345678u,
                    "final-combiner CW0") ||
       !ExpectEqual(state.finalCombinerCw1, 0xABCDEF01u,
                    "final-combiner CW1") ||
       !ExpectEqual(state.finalCombinerMask, 3,
                    "complete final-combiner mask") ||
       !Expect(cxbx::nv2a::IsPgraphFinalCombinerComplete(state),
               "final combiner is incomplete"))
    {
        return 1;
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state,
                   (PgraphCombinerMethod::SetFactor0Stage0 + 3u * 4u) |
                       0xC0000002u,
                   0x76543210u),
               "masked stage method was not handled") ||
       !ExpectEqual(state.stages[3].factor0, 0x76543210u,
                    "masked stage method value"))
    {
        return 1;
    }

    const PgraphCombinerState beforeUnknown = state;
    if(!Expect(!cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state, PgraphCombinerMethod::SetAlphaIcwStage0 - 4u,
                   0xFFFFFFFFu),
               "method below alpha ICW range was handled") ||
       !Expect(!cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state,
                   PgraphCombinerMethod::SetAlphaIcwStage0 +
                       cxbx::nv2a::PgraphCombinerStageCount * 4u,
                   0xFFFFFFFFu),
               "method above alpha ICW range was handled") ||
       !Expect(!cxbx::nv2a::ApplyPgraphCombinerStateMethod(
                   state, 0x00000400u, 0xFFFFFFFFu),
               "unknown combiner method was handled") ||
       !ExpectEqual(state.control, beforeUnknown.control,
                    "unknown method combiner control") ||
       !ExpectEqual(state.stages[Stage].colorIcw,
                    beforeUnknown.stages[Stage].colorIcw,
                    "unknown method stage state"))
    {
        return 1;
    }

    return 0;
}
