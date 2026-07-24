#include "hw/nv2a_pgraph_semaphore_state.h"

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

bool ExpectKind(cxbx::nv2a::PgraphSemaphoreStepKind actual,
                cxbx::nv2a::PgraphSemaphoreStepKind expected,
                const char* message) noexcept
{
    return ExpectEqual(static_cast<std::uint32_t>(actual),
                       static_cast<std::uint32_t>(expected), message);
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphSemaphoreMethod;
    using cxbx::nv2a::PgraphSemaphoreState;
    using cxbx::nv2a::PgraphSemaphoreStepKind;

    PgraphSemaphoreState state{};
    if(!ExpectEqual(state.contextDmaSemaphore, 0,
                    "default semaphore DMA handle") ||
       !ExpectEqual(state.offset, 0, "default semaphore offset"))
    {
        return 1;
    }

    const auto contextStep = cxbx::nv2a::ApplyPgraphSemaphoreMethod(
        state,
        PgraphSemaphoreMethod::SetContextDmaSemaphore | 0xA0000003u,
        0x11223344u);
    if(!ExpectKind(contextStep.kind, PgraphSemaphoreStepKind::State,
                   "context-DMA step kind") ||
       !ExpectEqual(state.contextDmaSemaphore, 0x11223344u,
                    "semaphore DMA handle") ||
       !ExpectEqual(contextStep.release.contextDmaSemaphore, 0,
                    "state step release DMA handle") ||
       !ExpectEqual(contextStep.release.offset, 0,
                    "state step release offset") ||
       !ExpectEqual(contextStep.release.value, 0,
                    "state step release value"))
    {
        return 1;
    }

    const auto offsetStep = cxbx::nv2a::ApplyPgraphSemaphoreMethod(
        state, PgraphSemaphoreMethod::SetSemaphoreOffset, 0x00000120u);
    if(!ExpectKind(offsetStep.kind, PgraphSemaphoreStepKind::State,
                   "offset step kind") ||
       !ExpectEqual(state.offset, 0x00000120u, "semaphore offset"))
    {
        return 1;
    }

    const auto releaseStep = cxbx::nv2a::ApplyPgraphSemaphoreMethod(
        state,
        PgraphSemaphoreMethod::BackendWriteSemaphoreRelease | 0xC0000003u,
        0xA5A5F00Du);
    if(!ExpectKind(releaseStep.kind, PgraphSemaphoreStepKind::Release,
                   "release step kind") ||
       !ExpectEqual(releaseStep.release.contextDmaSemaphore, 0x11223344u,
                    "release DMA handle snapshot") ||
       !ExpectEqual(releaseStep.release.offset, 0x00000120u,
                    "release offset snapshot") ||
       !ExpectEqual(releaseStep.release.value, 0xA5A5F00Du,
                    "release value") ||
       !ExpectEqual(state.contextDmaSemaphore, 0x11223344u,
                    "release changed DMA handle") ||
       !ExpectEqual(state.offset, 0x00000120u,
                    "release changed semaphore offset"))
    {
        return 1;
    }

    const PgraphSemaphoreState beforeUnknown = state;
    const auto holeStep = cxbx::nv2a::ApplyPgraphSemaphoreMethod(
        state, PgraphSemaphoreMethod::SetContextDmaSemaphore + 4u,
        0xFFFFFFFFu);
    const auto unknownStep = cxbx::nv2a::ApplyPgraphSemaphoreMethod(
        state, 0x00000400u, 0xFFFFFFFFu);
    if(!ExpectKind(holeStep.kind, PgraphSemaphoreStepKind::Unhandled,
                   "context-DMA method hole") ||
       !ExpectKind(unknownStep.kind, PgraphSemaphoreStepKind::Unhandled,
                   "unknown semaphore method") ||
       !ExpectEqual(state.contextDmaSemaphore,
                    beforeUnknown.contextDmaSemaphore,
                    "unknown method changed DMA handle") ||
       !ExpectEqual(state.offset, beforeUnknown.offset,
                    "unknown method changed semaphore offset"))
    {
        return 1;
    }

    return 0;
}
