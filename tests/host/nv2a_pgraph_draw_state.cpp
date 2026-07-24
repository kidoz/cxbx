#include "hw/nv2a_pgraph_draw_state.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

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

bool ExpectSize(std::size_t actual, std::size_t expected,
                const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: expected %zu, got %zu\n", message,
                 expected, actual);
    return false;
}

template <typename T>
bool ExpectKind(T actual, T expected, const char* message) noexcept
{
    return ExpectEqual(static_cast<std::uint32_t>(actual),
                       static_cast<std::uint32_t>(expected), message);
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphDrawActionKind;
    using cxbx::nv2a::PgraphDrawIndexCapacity;
    using cxbx::nv2a::PgraphDrawMethod;
    using cxbx::nv2a::PgraphDrawState;
    using cxbx::nv2a::PgraphDrawStepKind;

    PgraphDrawState state{};
    if(!ExpectEqual(state.beginOp, 0, "default begin operation") ||
       !ExpectSize(state.elementIndexCount, 0, "default element count"))
    {
        return 1;
    }

    const auto beginStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::SetBeginEnd | 0xA0000003u, 5);
    const auto packedStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::ArrayElement16, 0x1234ABCDu);
    if(!ExpectKind(beginStep.kind, PgraphDrawStepKind::State,
                   "begin step kind") ||
       !ExpectKind(packedStep.kind, PgraphDrawStepKind::State,
                   "packed-index step kind") ||
       !ExpectEqual(state.beginOp, 5, "active begin operation") ||
       !ExpectSize(state.elementIndexCount, 2, "packed element count") ||
       !ExpectEqual(state.elementIndices[0], 0xABCDu,
                    "packed low element") ||
       !ExpectEqual(state.elementIndices[1], 0x1234u,
                    "packed high element"))
    {
        return 1;
    }

    const auto directStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::DrawArrays, 0x05001234u);
    if(!ExpectKind(directStep.kind, PgraphDrawStepKind::Draw,
                   "direct-draw step kind") ||
       !ExpectKind(directStep.action.kind, PgraphDrawActionKind::Direct,
                   "direct-draw action kind") ||
       !ExpectEqual(directStep.action.beginOp, 5,
                    "direct-draw operation snapshot") ||
       !ExpectEqual(directStep.action.startVertex, 0x1234u,
                    "direct-draw start") ||
       !ExpectSize(directStep.action.vertexCount, 6,
                   "direct-draw count") ||
       !ExpectSize(state.elementIndexCount, 2,
                   "direct draw changed element count"))
    {
        return 1;
    }

    static_cast<void>(cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::ArrayElement32, 0x89ABCDEFu));
    const auto endStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::SetBeginEnd, 0);
    if(!ExpectKind(endStep.kind, PgraphDrawStepKind::End,
                   "end step kind") ||
       !ExpectKind(endStep.action.kind, PgraphDrawActionKind::Indexed,
                   "indexed-draw action kind") ||
       !ExpectEqual(endStep.action.beginOp, 5,
                    "indexed-draw operation snapshot") ||
       !ExpectEqual(endStep.action.startVertex, 0,
                    "indexed-draw start") ||
       !ExpectSize(endStep.action.vertexCount, 3,
                   "indexed-draw count") ||
       !ExpectEqual(state.beginOp, 0, "end operation reset") ||
       !ExpectSize(state.elementIndexCount, 0, "end element reset") ||
       !ExpectEqual(state.elementIndices[2], 0x89ABCDEFu,
                    "indexed snapshot storage"))
    {
        return 1;
    }

    static_cast<void>(cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::SetBeginEnd, 6));
    static_cast<void>(cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::ArrayElement32, 7));
    static_cast<void>(cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::SetBeginEnd, 7));
    if(!ExpectEqual(state.beginOp, 7, "replacement begin operation") ||
       !ExpectSize(state.elementIndexCount, 0,
                   "begin did not reset element count"))
    {
        return 1;
    }

    const auto emptyEndStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::SetBeginEnd, 0);
    if(!ExpectKind(emptyEndStep.kind, PgraphDrawStepKind::End,
                   "empty end step kind") ||
       !ExpectKind(emptyEndStep.action.kind, PgraphDrawActionKind::None,
                   "empty end action kind") ||
       !ExpectEqual(emptyEndStep.action.beginOp, 7,
                    "empty end operation snapshot"))
    {
        return 1;
    }

    static_cast<void>(cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::SetBeginEnd, 5));
    for(std::size_t index = 0; index < PgraphDrawIndexCapacity / 2; ++index)
    {
        static_cast<void>(cxbx::nv2a::ApplyPgraphDrawMethod(
            state, PgraphDrawMethod::ArrayElement16,
            static_cast<std::uint32_t>(index) |
                (static_cast<std::uint32_t>(index + 1) << 16)));
    }
    static_cast<void>(cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::ArrayElement32, 0xFFFFFFFFu));
    if(!ExpectSize(state.elementIndexCount, PgraphDrawIndexCapacity,
                   "bounded element capacity"))
    {
        return 1;
    }

    const auto capacityEndStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::SetBeginEnd, 0);
    if(!ExpectSize(capacityEndStep.action.vertexCount,
                   PgraphDrawIndexCapacity,
                   "bounded indexed-draw count"))
    {
        return 1;
    }

    const PgraphDrawState beforeUnknown = state;
    const auto holeStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, PgraphDrawMethod::ArrayElement16 + 4u, 0xFFFFFFFFu);
    const auto unknownStep = cxbx::nv2a::ApplyPgraphDrawMethod(
        state, 0x00000400u, 0xFFFFFFFFu);
    if(!ExpectKind(holeStep.kind, PgraphDrawStepKind::Unhandled,
                   "draw method hole") ||
       !ExpectKind(unknownStep.kind, PgraphDrawStepKind::Unhandled,
                   "unknown draw method") ||
       !ExpectEqual(state.beginOp, beforeUnknown.beginOp,
                    "unknown method changed begin operation") ||
       !ExpectSize(state.elementIndexCount,
                   beforeUnknown.elementIndexCount,
                   "unknown method changed element count"))
    {
        return 1;
    }

    return 0;
}
