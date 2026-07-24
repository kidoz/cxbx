#include "hw/nv2a_pgraph_flip_state.h"

#include <cstdint>
#include <cstdio>
#include <limits>

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

bool ExpectKind(cxbx::nv2a::PgraphFlipStepKind actual,
                cxbx::nv2a::PgraphFlipStepKind expected,
                const char* message) noexcept
{
    return ExpectEqual(static_cast<std::uint32_t>(actual),
                       static_cast<std::uint32_t>(expected), message);
}

bool ExpectState(const cxbx::nv2a::PgraphFlipState& actual,
                 const cxbx::nv2a::PgraphFlipState& expected,
                 const char* message) noexcept
{
    if(actual.readIndex == expected.readIndex &&
       actual.writeIndex == expected.writeIndex &&
       actual.modulo == expected.modulo)
    {
        return true;
    }

    std::fprintf(
        stderr,
        "%s: expected {%08X,%08X,%08X}, got {%08X,%08X,%08X}\n",
        message, expected.readIndex, expected.writeIndex, expected.modulo,
        actual.readIndex, actual.writeIndex, actual.modulo);
    return false;
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphFlipMethod;
    using cxbx::nv2a::PgraphFlipState;
    using cxbx::nv2a::PgraphFlipStepKind;

    PgraphFlipState state{};
    if(!ExpectState(state, {}, "default flip state"))
    {
        return 1;
    }

    const auto readStep = cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::SetFlipRead | 0xA0000003u, 2);
    const auto writeStep = cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::SetFlipWrite, 2);
    const auto moduloStep = cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::SetFlipModulo, 3);
    if(!ExpectKind(readStep.kind, PgraphFlipStepKind::State,
                   "read step kind") ||
       !ExpectKind(writeStep.kind, PgraphFlipStepKind::State,
                   "write step kind") ||
       !ExpectKind(moduloStep.kind, PgraphFlipStepKind::State,
                   "modulo step kind") ||
       !ExpectState(state, { 2, 2, 3 }, "configured flip state"))
    {
        return 1;
    }

    const auto incrementStep = cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::FlipIncrementWrite, 0xFFFFFFFFu);
    if(!ExpectKind(incrementStep.kind, PgraphFlipStepKind::State,
                   "increment step kind") ||
       !ExpectEqual(state.writeIndex, 0, "wrapped write index"))
    {
        return 1;
    }

    state.writeIndex = std::numeric_limits<std::uint32_t>::max();
    static_cast<void>(cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::FlipIncrementWrite, 0));
    if(!ExpectEqual(state.writeIndex, 0, "overflowed write index"))
    {
        return 1;
    }

    static_cast<void>(cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::SetFlipModulo, 0));
    state.writeIndex = 7;
    static_cast<void>(cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::FlipIncrementWrite, 0));
    if(!ExpectEqual(state.modulo, 1, "zero modulo normalization") ||
       !ExpectEqual(state.writeIndex, 0, "single-buffer write index"))
    {
        return 1;
    }

    const PgraphFlipState beforePresent = state;
    const auto presentStep = cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::FlipStall | 0xC0000003u, 0xFFFFFFFFu);
    if(!ExpectKind(presentStep.kind, PgraphFlipStepKind::Present,
                   "present step kind") ||
       !ExpectState(state, beforePresent, "present changed flip state"))
    {
        return 1;
    }

    const auto holeStep = cxbx::nv2a::ApplyPgraphFlipMethod(
        state, PgraphFlipMethod::FlipStall + 4u, 0xFFFFFFFFu);
    const auto unknownStep = cxbx::nv2a::ApplyPgraphFlipMethod(
        state, 0x00000400u, 0xFFFFFFFFu);
    if(!ExpectKind(holeStep.kind, PgraphFlipStepKind::Unhandled,
                   "flip method hole") ||
       !ExpectKind(unknownStep.kind, PgraphFlipStepKind::Unhandled,
                   "unknown flip method") ||
       !ExpectState(state, beforePresent, "unknown method changed flip state"))
    {
        return 1;
    }

    return 0;
}
