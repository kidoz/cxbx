#include "hw/nv2a_pgraph_combiner_state.h"

namespace cxbx::nv2a
{

namespace
{

using CombinerStageField = std::uint32_t PgraphCombinerStageState::*;

bool ApplyStageMethod(
    PgraphCombinerState& state, std::uint32_t method, std::uint32_t data,
    std::uint32_t stage0Method, CombinerStageField field) noexcept
{
    constexpr std::uint32_t StageMethodStride = 4u;
    constexpr std::uint32_t StageMethodSpan =
        PgraphCombinerStageCount * StageMethodStride;
    if(method < stage0Method || method >= stage0Method + StageMethodSpan)
    {
        return false;
    }

    const std::uint32_t stage =
        (method - stage0Method) / StageMethodStride;
    state.stages[stage].*field = data;
    return true;
}

} // namespace

bool ApplyPgraphCombinerStateMethod(
    PgraphCombinerState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    method &= 0x1FFCu;
    switch(method)
    {
        case PgraphCombinerMethod::SetFinalCombinerCw0:
            state.finalCombinerCw0 = data;
            state.finalCombinerMask |= 1u;
            return true;
        case PgraphCombinerMethod::SetFinalCombinerCw1:
            state.finalCombinerCw1 = data;
            state.finalCombinerMask |= 2u;
            return true;
        case PgraphCombinerMethod::SetCombinerControl:
            state.control = data;
            return true;
        case PgraphCombinerMethod::SetShaderStageProgram:
            state.shaderStageProgram = data;
            return true;
        default:
            break;
    }

    return ApplyStageMethod(
               state, method, data,
               PgraphCombinerMethod::SetAlphaIcwStage0,
               &PgraphCombinerStageState::alphaIcw) ||
           ApplyStageMethod(
               state, method, data,
               PgraphCombinerMethod::SetFactor0Stage0,
               &PgraphCombinerStageState::factor0) ||
           ApplyStageMethod(
               state, method, data,
               PgraphCombinerMethod::SetFactor1Stage0,
               &PgraphCombinerStageState::factor1) ||
           ApplyStageMethod(
               state, method, data,
               PgraphCombinerMethod::SetAlphaOcwStage0,
               &PgraphCombinerStageState::alphaOcw) ||
           ApplyStageMethod(
               state, method, data,
               PgraphCombinerMethod::SetColorIcwStage0,
               &PgraphCombinerStageState::colorIcw) ||
           ApplyStageMethod(
               state, method, data,
               PgraphCombinerMethod::SetColorOcwStage0,
               &PgraphCombinerStageState::colorOcw);
}

bool IsPgraphFinalCombinerComplete(
    const PgraphCombinerState& state) noexcept
{
    return state.finalCombinerMask == 3u;
}

} // namespace cxbx::nv2a
