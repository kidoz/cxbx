#include "hw/nv2a_pgraph_vertex_submission_state.h"

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

bool ExpectBool(bool actual, bool expected, const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: expected %u, got %u\n", message,
                 expected ? 1u : 0u, actual ? 1u : 0u);
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
    using cxbx::nv2a::PgraphImmediateFormatFloat2;
    using cxbx::nv2a::PgraphImmediateFormatFloat4;
    using cxbx::nv2a::PgraphImmediateFormatUnsignedByte4;
    using cxbx::nv2a::PgraphImmediateWordsPerVertex;
    using cxbx::nv2a::PgraphVertexAttributeCount;
    using cxbx::nv2a::PgraphVertexBatchKind;
    using cxbx::nv2a::PgraphVertexSubmissionMethod;
    using cxbx::nv2a::PgraphVertexSubmissionState;
    using cxbx::nv2a::PgraphVertexSubmissionStepKind;
    using cxbx::nv2a::PgraphVertexSubmissionWordCapacity;

    static PgraphVertexSubmissionState state{};
    if(!ExpectSize(state.inlineWordCount, 0, "default inline count") ||
       !ExpectBool(state.inlineOverflow, false, "default inline overflow") ||
       !ExpectSize(state.immediateWordCount, 0, "default immediate count") ||
       !ExpectBool(state.immediateOverflow, false,
                   "default immediate overflow"))
    {
        return 1;
    }

    const auto float2Step =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::SetVertexData2FM + 15u * 8u + 4u,
            0x11223344u);
    const auto byte4Step =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::SetVertexData4Ub + 3u * 4u,
            0xAABBCCDDu);
    const auto float4Step =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::SetVertexData4FM + 9u * 16u + 2u * 4u,
            0x55667788u);
    if(!ExpectKind(float2Step.kind, PgraphVertexSubmissionStepKind::State,
                   "float2 state step") ||
       !ExpectKind(byte4Step.kind, PgraphVertexSubmissionStepKind::State,
                   "byte4 state step") ||
       !ExpectKind(float4Step.kind, PgraphVertexSubmissionStepKind::State,
                   "float4 state step") ||
       !ExpectEqual(state.immediateAttributes[15][1], 0x11223344u,
                    "float2 attribute component") ||
       !ExpectEqual(state.immediateFormats[15], PgraphImmediateFormatFloat2,
                    "float2 attribute format") ||
       !ExpectEqual(state.immediateAttributes[3][0], 0xAABBCCDDu,
                    "byte4 attribute value") ||
       !ExpectEqual(state.immediateFormats[3],
                    PgraphImmediateFormatUnsignedByte4,
                    "byte4 attribute format") ||
       !ExpectEqual(state.immediateAttributes[9][2], 0x55667788u,
                    "float4 attribute component") ||
       !ExpectEqual(state.immediateFormats[9], PgraphImmediateFormatFloat4,
                    "float4 attribute format"))
    {
        return 1;
    }

    static PgraphVertexSubmissionState float2PositionState{};
    static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
        float2PositionState,
        PgraphVertexSubmissionMethod::SetVertexData2FM + 9u * 8u,
        0x3F800000u));
    const auto float2PositionXStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            float2PositionState,
            PgraphVertexSubmissionMethod::SetVertexData2FM,
            0x44200000u);
    const auto float2PositionYStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            float2PositionState,
            PgraphVertexSubmissionMethod::SetVertexData2FM + 4u,
            0x43F00000u);
    const auto float2PositionBatch =
        cxbx::nv2a::SnapshotPgraphVertexBatch(float2PositionState);
    if(!ExpectKind(float2PositionXStep.kind,
                   PgraphVertexSubmissionStepKind::State,
                   "float2 position x step") ||
       !ExpectKind(float2PositionYStep.kind,
                   PgraphVertexSubmissionStepKind::ImmediateVertex,
                   "float2 position completion") ||
       !ExpectKind(float2PositionBatch.kind,
                   PgraphVertexBatchKind::Immediate,
                   "float2 position batch") ||
       !ExpectSize(float2PositionBatch.wordCount,
                   PgraphImmediateWordsPerVertex,
                   "float2 position batch word count") ||
       !ExpectEqual(float2PositionState.immediateWords[0], 0x44200000u,
                    "float2 position x snapshot") ||
       !ExpectEqual(float2PositionState.immediateWords[1], 0x43F00000u,
                    "float2 position y snapshot") ||
       !ExpectEqual(float2PositionState.immediateWords[9u * 4u],
                    0x3F800000u, "float2 attribute snapshot"))
    {
        return 1;
    }

    static PgraphVertexSubmissionState float4PositionState{};
    for(std::uint32_t component = 0; component < 3; ++component)
    {
        const auto componentStep =
            cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
                float4PositionState,
                PgraphVertexSubmissionMethod::SetVertexData4FM +
                    component * 4u,
                component + 1u);
        if(!ExpectKind(componentStep.kind,
                       PgraphVertexSubmissionStepKind::State,
                       "float4 position component step"))
        {
            return 1;
        }
    }
    const auto float4PositionStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            float4PositionState,
            PgraphVertexSubmissionMethod::SetVertexData4FM + 12u,
            0x00000004u);
    if(!ExpectKind(float4PositionStep.kind,
                   PgraphVertexSubmissionStepKind::ImmediateVertex,
                   "float4 position completion") ||
       !ExpectSize(float4PositionState.immediateWordCount,
                   PgraphImmediateWordsPerVertex,
                   "float4 position vertex count"))
    {
        return 1;
    }

    static PgraphVertexSubmissionState byte4PositionState{};
    const auto byte4PositionStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            byte4PositionState,
            PgraphVertexSubmissionMethod::SetVertexData4Ub,
            0xAABBCCDDu);
    if(!ExpectKind(byte4PositionStep.kind,
                   PgraphVertexSubmissionStepKind::ImmediateVertex,
                   "byte4 position completion") ||
       !ExpectSize(byte4PositionState.immediateWordCount,
                   PgraphImmediateWordsPerVertex,
                   "byte4 position vertex count"))
    {
        return 1;
    }

    static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
        state, PgraphVertexSubmissionMethod::SetVertex4F | 0xA0000003u,
        0x00000001u));
    static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
        state, PgraphVertexSubmissionMethod::SetVertex4F + 4u,
        0x00000002u));
    static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
        state, PgraphVertexSubmissionMethod::SetVertex4F + 8u,
        0x00000003u));
    const auto firstVertexStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::SetVertex4F + 12u,
            0x00000004u);
    if(!ExpectKind(firstVertexStep.kind,
                   PgraphVertexSubmissionStepKind::ImmediateVertex,
                   "first completed vertex step") ||
       !ExpectSize(firstVertexStep.wordOffset, 0,
                   "first completed vertex offset") ||
       !ExpectSize(firstVertexStep.wordCount, PgraphImmediateWordsPerVertex,
                   "first completed vertex size") ||
       !ExpectSize(state.immediateWordCount, PgraphImmediateWordsPerVertex,
                   "first completed vertex count") ||
       !ExpectEqual(state.immediateWords[0], 0x00000001u,
                    "first position x snapshot") ||
       !ExpectEqual(state.immediateWords[3], 0x00000004u,
                    "first position w snapshot") ||
       !ExpectEqual(state.immediateWords[3u * 4u], 0xAABBCCDDu,
                    "first byte4 snapshot") ||
       !ExpectEqual(state.immediateWords[9u * 4u + 2u], 0x55667788u,
                    "first float4 snapshot") ||
       !ExpectEqual(state.immediateWords[15u * 4u + 1u], 0x11223344u,
                    "first float2 snapshot"))
    {
        return 1;
    }

    static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
        state, PgraphVertexSubmissionMethod::SetVertex4F, 0x00000011u));
    const auto secondVertexStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::SetVertex4F + 12u,
            0x00000014u);
    if(!ExpectKind(secondVertexStep.kind,
                   PgraphVertexSubmissionStepKind::ImmediateVertex,
                   "second completed vertex step") ||
       !ExpectSize(secondVertexStep.wordOffset,
                   PgraphImmediateWordsPerVertex,
                   "second completed vertex offset") ||
       !ExpectEqual(state.immediateWords[0], 0x00000001u,
                    "first snapshot stayed immutable") ||
       !ExpectEqual(
           state.immediateWords[PgraphImmediateWordsPerVertex],
           0x00000011u, "second position x snapshot") ||
       !ExpectEqual(
           state.immediateWords[PgraphImmediateWordsPerVertex + 1u],
           0x00000002u, "second position y persistence"))
    {
        return 1;
    }

    const auto inlineStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::InlineArray, 0xCAFEBABEu);
    const auto immediateBatch =
        cxbx::nv2a::SnapshotPgraphVertexBatch(state);
    if(!ExpectKind(inlineStep.kind,
                   PgraphVertexSubmissionStepKind::InlineWord,
                   "inline word step") ||
       !ExpectSize(inlineStep.wordOffset, 0, "inline word offset") ||
       !ExpectSize(inlineStep.wordCount, 1, "inline word size") ||
       !ExpectKind(immediateBatch.kind, PgraphVertexBatchKind::Immediate,
                   "immediate batch priority") ||
       !ExpectSize(immediateBatch.wordCount,
                   2u * PgraphImmediateWordsPerVertex,
                   "immediate batch word count") ||
       !ExpectBool(immediateBatch.overflow, false,
                   "immediate batch overflow"))
    {
        return 1;
    }

    cxbx::nv2a::ResetPgraphVertexSubmissionBatch(state);
    if(!ExpectSize(state.inlineWordCount, 0, "reset inline count") ||
       !ExpectBool(state.inlineOverflow, false, "reset inline overflow") ||
       !ExpectSize(state.immediateWordCount, 0, "reset immediate count") ||
       !ExpectBool(state.immediateOverflow, false,
                   "reset immediate overflow") ||
       !ExpectEqual(state.immediateAttributes[15][1], 0x11223344u,
                    "reset preserved immediate attribute") ||
       !ExpectEqual(state.immediateFormats[15], PgraphImmediateFormatFloat2,
                    "reset preserved immediate format") ||
       !ExpectEqual(state.immediateWords[0], 0x00000001u,
                    "reset preserved completed words"))
    {
        return 1;
    }

    static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
        state, PgraphVertexSubmissionMethod::InlineArray, 0x12345678u));
    const auto inlineBatch = cxbx::nv2a::SnapshotPgraphVertexBatch(state);
    if(!ExpectKind(inlineBatch.kind, PgraphVertexBatchKind::Inline,
                   "inline batch kind") ||
       !ExpectSize(inlineBatch.wordCount, 1, "inline batch word count") ||
       !ExpectBool(inlineBatch.overflow, false, "inline batch overflow"))
    {
        return 1;
    }

    cxbx::nv2a::ResetPgraphVertexSubmissionBatch(state);
    const auto emptyBatch = cxbx::nv2a::SnapshotPgraphVertexBatch(state);
    if(!ExpectKind(emptyBatch.kind, PgraphVertexBatchKind::None,
                   "empty batch kind"))
    {
        return 1;
    }

    for(std::size_t index = 0;
        index < PgraphVertexSubmissionWordCapacity; ++index)
    {
        static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::InlineArray,
            static_cast<std::uint32_t>(index)));
    }
    const auto inlineOverflowStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::InlineArray, 0xFFFFFFFFu);
    const auto fullInlineBatch =
        cxbx::nv2a::SnapshotPgraphVertexBatch(state);
    if(!ExpectKind(inlineOverflowStep.kind,
                   PgraphVertexSubmissionStepKind::Overflow,
                   "inline overflow step") ||
       !ExpectSize(state.inlineWordCount,
                   PgraphVertexSubmissionWordCapacity,
                   "bounded inline capacity") ||
       !ExpectBool(state.inlineOverflow, true, "sticky inline overflow") ||
       !ExpectKind(fullInlineBatch.kind, PgraphVertexBatchKind::Inline,
                   "full inline batch kind") ||
       !ExpectBool(fullInlineBatch.overflow, true,
                   "full inline batch overflow"))
    {
        return 1;
    }

    cxbx::nv2a::ResetPgraphVertexSubmissionBatch(state);
    for(std::size_t vertex = 0;
        vertex < PgraphVertexSubmissionWordCapacity /
                     PgraphImmediateWordsPerVertex;
        ++vertex)
    {
        static_cast<void>(cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::SetVertex4F + 12u,
            static_cast<std::uint32_t>(vertex)));
    }
    const auto immediateOverflowStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::SetVertex4F + 12u,
            0xFFFFFFFFu);
    const auto fullImmediateBatch =
        cxbx::nv2a::SnapshotPgraphVertexBatch(state);
    if(!ExpectKind(immediateOverflowStep.kind,
                   PgraphVertexSubmissionStepKind::Overflow,
                   "immediate overflow step") ||
       !ExpectSize(state.immediateWordCount,
                   PgraphVertexSubmissionWordCapacity,
                   "bounded immediate capacity") ||
       !ExpectBool(state.immediateOverflow, true,
                   "sticky immediate overflow") ||
       !ExpectKind(fullImmediateBatch.kind, PgraphVertexBatchKind::Immediate,
                   "full immediate batch kind") ||
       !ExpectBool(fullImmediateBatch.overflow, true,
                   "full immediate batch overflow"))
    {
        return 1;
    }

    cxbx::nv2a::ResetPgraphVertexSubmissionBatch(state);
    const std::uint32_t preservedFormat = state.immediateFormats[9];
    const auto holeStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, PgraphVertexSubmissionMethod::InlineArray + 4u,
            0xFFFFFFFFu);
    const auto unknownStep =
        cxbx::nv2a::ApplyPgraphVertexSubmissionMethod(
            state, 0x00000400u, 0xFFFFFFFFu);
    if(!ExpectKind(holeStep.kind, PgraphVertexSubmissionStepKind::Unhandled,
                   "submission method hole") ||
       !ExpectKind(unknownStep.kind,
                   PgraphVertexSubmissionStepKind::Unhandled,
                   "unknown submission method") ||
       !ExpectSize(state.inlineWordCount, 0,
                   "unknown method changed inline count") ||
       !ExpectSize(state.immediateWordCount, 0,
                   "unknown method changed immediate count") ||
       !ExpectEqual(state.immediateFormats[9], preservedFormat,
                    "unknown method changed immediate format"))
    {
        return 1;
    }

    static_assert(
        PgraphImmediateWordsPerVertex ==
        PgraphVertexAttributeCount * 4);
    return 0;
}
