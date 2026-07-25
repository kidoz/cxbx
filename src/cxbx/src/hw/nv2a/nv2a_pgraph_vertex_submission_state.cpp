#include "hw/nv2a_pgraph_vertex_submission_state.h"

namespace cxbx::nv2a
{

namespace
{

constexpr std::size_t PgraphPositionAttribute = 0;

PgraphVertexSubmissionStep AppendInlineWord(
    PgraphVertexSubmissionState& state, std::uint32_t data) noexcept
{
    if(state.inlineWordCount >= state.inlineWords.size())
    {
        state.inlineOverflow = true;
        return {
            PgraphVertexSubmissionStepKind::Overflow,
            state.inlineWordCount,
            0,
        };
    }

    const std::size_t wordOffset = state.inlineWordCount;
    state.inlineWords[state.inlineWordCount++] = data;
    return {
        PgraphVertexSubmissionStepKind::InlineWord,
        wordOffset,
        1,
    };
}

PgraphVertexSubmissionStep CompleteImmediateVertex(
    PgraphVertexSubmissionState& state) noexcept
{
    if(state.immediateWordCount >
       state.immediateWords.size() - PgraphImmediateWordsPerVertex)
    {
        state.immediateOverflow = true;
        return {
            PgraphVertexSubmissionStepKind::Overflow,
            state.immediateWordCount,
            0,
        };
    }

    const std::size_t wordOffset = state.immediateWordCount;
    for(std::size_t attribute = 0;
        attribute < PgraphVertexAttributeCount; ++attribute)
    {
        for(std::size_t component = 0;
            component < PgraphImmediateComponentCount; ++component)
        {
            state.immediateWords[state.immediateWordCount++] =
                state.immediateAttributes[attribute][component];
        }
    }

    return {
        PgraphVertexSubmissionStepKind::ImmediateVertex,
        wordOffset,
        PgraphImmediateWordsPerVertex,
    };
}

} // namespace

PgraphVertexSubmissionStep ApplyPgraphVertexSubmissionMethod(
    PgraphVertexSubmissionState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    method &= 0x1FFCu;

    if(method == PgraphVertexSubmissionMethod::InlineArray)
    {
        return AppendInlineWord(state, data);
    }

    if(method >= PgraphVertexSubmissionMethod::SetVertexData2FM &&
       method < PgraphVertexSubmissionMethod::SetVertexData2FM +
                    PgraphVertexAttributeCount * 8)
    {
        const std::uint32_t relative =
            method - PgraphVertexSubmissionMethod::SetVertexData2FM;
        const std::size_t attribute = relative / 8;
        const std::size_t component = (relative & 7u) / 4;
        state.immediateAttributes[attribute][component] = data;
        state.immediateFormats[attribute] = PgraphImmediateFormatFloat2;
        if(attribute == PgraphPositionAttribute && component == 1)
        {
            return CompleteImmediateVertex(state);
        }
        return { PgraphVertexSubmissionStepKind::State, 0, 0 };
    }

    if(method >= PgraphVertexSubmissionMethod::SetVertexData4Ub &&
       method < PgraphVertexSubmissionMethod::SetVertexData4Ub +
                    PgraphVertexAttributeCount * 4)
    {
        const std::size_t attribute =
            (method - PgraphVertexSubmissionMethod::SetVertexData4Ub) / 4;
        state.immediateAttributes[attribute][0] = data;
        state.immediateFormats[attribute] =
            PgraphImmediateFormatUnsignedByte4;
        if(attribute == PgraphPositionAttribute)
        {
            return CompleteImmediateVertex(state);
        }
        return { PgraphVertexSubmissionStepKind::State, 0, 0 };
    }

    if(method >= PgraphVertexSubmissionMethod::SetVertexData4FM &&
       method < PgraphVertexSubmissionMethod::SetVertexData4FM +
                    PgraphVertexAttributeCount * 16)
    {
        const std::uint32_t relative =
            method - PgraphVertexSubmissionMethod::SetVertexData4FM;
        const std::size_t attribute = relative / 16;
        const std::size_t component = (relative & 15u) / 4;
        state.immediateAttributes[attribute][component] = data;
        state.immediateFormats[attribute] = PgraphImmediateFormatFloat4;
        if(attribute == PgraphPositionAttribute &&
           component == PgraphImmediateComponentCount - 1)
        {
            return CompleteImmediateVertex(state);
        }
        return { PgraphVertexSubmissionStepKind::State, 0, 0 };
    }

    if(method >= PgraphVertexSubmissionMethod::SetVertex4F &&
       method < PgraphVertexSubmissionMethod::SetVertex4F + 16)
    {
        const std::size_t component =
            (method - PgraphVertexSubmissionMethod::SetVertex4F) / 4;
        state.immediateAttributes[PgraphPositionAttribute][component] = data;
        state.immediateFormats[PgraphPositionAttribute] =
            PgraphImmediateFormatFloat4;
        if(component == PgraphImmediateComponentCount - 1)
        {
            return CompleteImmediateVertex(state);
        }
        return { PgraphVertexSubmissionStepKind::State, 0, 0 };
    }

    return {};
}

PgraphVertexBatchAction SnapshotPgraphVertexBatch(
    const PgraphVertexSubmissionState& state) noexcept
{
    if(state.immediateWordCount != 0)
    {
        return {
            PgraphVertexBatchKind::Immediate,
            state.immediateWordCount,
            state.immediateOverflow,
        };
    }

    if(state.inlineWordCount != 0)
    {
        return {
            PgraphVertexBatchKind::Inline,
            state.inlineWordCount,
            state.inlineOverflow,
        };
    }

    return {};
}

void ResetPgraphVertexSubmissionBatch(
    PgraphVertexSubmissionState& state) noexcept
{
    state.inlineWordCount = 0;
    state.inlineOverflow = false;
    state.immediateWordCount = 0;
    state.immediateOverflow = false;
}

} // namespace cxbx::nv2a
