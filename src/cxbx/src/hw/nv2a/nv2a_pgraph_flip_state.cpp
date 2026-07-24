#include "hw/nv2a_pgraph_flip_state.h"

namespace cxbx::nv2a
{

PgraphFlipStep ApplyPgraphFlipMethod(
    PgraphFlipState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    method &= 0x1FFCu;
    switch(method)
    {
        case PgraphFlipMethod::SetFlipRead:
            state.readIndex = data;
            return { PgraphFlipStepKind::State };
        case PgraphFlipMethod::SetFlipWrite:
            state.writeIndex = data;
            return { PgraphFlipStepKind::State };
        case PgraphFlipMethod::SetFlipModulo:
            state.modulo = data != 0 ? data : 1;
            return { PgraphFlipStepKind::State };
        case PgraphFlipMethod::FlipIncrementWrite:
            state.writeIndex = (state.writeIndex + 1) % state.modulo;
            return { PgraphFlipStepKind::State };
        case PgraphFlipMethod::FlipStall:
            return { PgraphFlipStepKind::Present };
        default:
            return {};
    }
}

} // namespace cxbx::nv2a
