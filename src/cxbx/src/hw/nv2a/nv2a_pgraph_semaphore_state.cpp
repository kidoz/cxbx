#include "hw/nv2a_pgraph_semaphore_state.h"

namespace cxbx::nv2a
{

PgraphSemaphoreStep ApplyPgraphSemaphoreMethod(
    PgraphSemaphoreState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    method &= 0x1FFCu;
    switch(method)
    {
        case PgraphSemaphoreMethod::SetContextDmaSemaphore:
            state.contextDmaSemaphore = data;
            return {
                PgraphSemaphoreStepKind::State,
                {},
            };
        case PgraphSemaphoreMethod::SetSemaphoreOffset:
            state.offset = data;
            return {
                PgraphSemaphoreStepKind::State,
                {},
            };
        case PgraphSemaphoreMethod::BackendWriteSemaphoreRelease:
            return {
                PgraphSemaphoreStepKind::Release,
                {
                    state.contextDmaSemaphore,
                    state.offset,
                    data,
                },
            };
        default:
            return {};
    }
}

} // namespace cxbx::nv2a
