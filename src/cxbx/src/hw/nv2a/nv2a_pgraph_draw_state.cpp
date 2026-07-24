#include "hw/nv2a_pgraph_draw_state.h"

namespace cxbx::nv2a
{

namespace
{

void AppendElementIndex(
    PgraphDrawState& state, std::uint32_t index) noexcept
{
    if(state.elementIndexCount < state.elementIndices.size())
    {
        state.elementIndices[state.elementIndexCount++] = index;
    }
}

} // namespace

PgraphDrawStep ApplyPgraphDrawMethod(
    PgraphDrawState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    method &= 0x1FFCu;
    switch(method)
    {
        case PgraphDrawMethod::SetBeginEnd:
        {
            if(data != 0)
            {
                state.beginOp = data;
                state.elementIndexCount = 0;
                return { PgraphDrawStepKind::State, {} };
            }

            const PgraphDrawAction action = {
                state.elementIndexCount >= 3
                    ? PgraphDrawActionKind::Indexed
                    : PgraphDrawActionKind::None,
                state.beginOp,
                0,
                state.elementIndexCount,
            };
            state.beginOp = 0;
            state.elementIndexCount = 0;
            return { PgraphDrawStepKind::End, action };
        }
        case PgraphDrawMethod::ArrayElement16:
            AppendElementIndex(state, data & 0xFFFFu);
            AppendElementIndex(state, data >> 16);
            return { PgraphDrawStepKind::State, {} };
        case PgraphDrawMethod::ArrayElement32:
            AppendElementIndex(state, data);
            return { PgraphDrawStepKind::State, {} };
        case PgraphDrawMethod::DrawArrays:
            return {
                PgraphDrawStepKind::Draw,
                {
                    PgraphDrawActionKind::Direct,
                    state.beginOp,
                    data & 0x00FFFFFFu,
                    ((data >> 24) & 0xFFu) + 1u,
                },
            };
        default:
            return {};
    }
}

} // namespace cxbx::nv2a
