#include "hw/nv2a_pgraph_transform_state.h"

#include <algorithm>
#include <bit>

namespace cxbx::nv2a
{

namespace
{

constexpr std::uint32_t TransformMethodStride = 4u;
constexpr std::uint32_t TransformUploadMethodCount = 32u;

bool DecodeTransformElement(
    std::uint32_t method, std::uint32_t baseMethod,
    std::uint32_t elementCount, std::uint32_t& element) noexcept
{
    const std::uint32_t methodSpan = elementCount * TransformMethodStride;
    if(method < baseMethod || method >= baseMethod + methodSpan)
    {
        return false;
    }

    element = (method - baseMethod) / TransformMethodStride;
    return true;
}

void StoreFloat(std::span<float> destination, std::uint32_t element,
                std::uint32_t data) noexcept
{
    destination[element] = std::bit_cast<float>(data);
}

} // namespace

bool ApplyPgraphTransformMethod(
    PgraphTransformState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    method &= 0x1FFCu;
    switch(method)
    {
        case PgraphTransformMethod::SetTransformExecutionMode:
            state.executionMode = data & 0x3u;
            return true;
        case PgraphTransformMethod::SetTransformProgramLoad:
            state.programWriteWord =
                data * PgraphTransformVectorComponentCount;
            return true;
        case PgraphTransformMethod::SetTransformProgramStart:
            state.programStart = data;
            return true;
        case PgraphTransformMethod::SetTransformConstantLoad:
            state.constantWriteComponent =
                data * PgraphTransformVectorComponentCount;
            return true;
        default:
            break;
    }

    std::uint32_t element = 0;
    if(DecodeTransformElement(
           method, PgraphTransformMethod::SetTransformProgram0,
           TransformUploadMethodCount, element))
    {
        // The upload-method aliases append at the selected load cursor; their
        // method offsets do not select program storage directly.
        if(state.programWriteWord < state.program.size())
        {
            state.program[state.programWriteWord++] = data;
            const std::uint32_t instructionCount =
                (state.programWriteWord +
                 PgraphTransformVectorComponentCount - 1u) /
                PgraphTransformVectorComponentCount;
            state.programInstructionCount =
                std::max(state.programInstructionCount, instructionCount);
        }
        return true;
    }
    if(DecodeTransformElement(
           method, PgraphTransformMethod::SetTransformConstant0,
           TransformUploadMethodCount, element))
    {
        // Constant upload uses the same cursor-driven alias behavior.
        if(state.constantWriteComponent < state.constants.size())
        {
            StoreFloat(
                state.constants, state.constantWriteComponent++, data);
        }
        return true;
    }
    if(DecodeTransformElement(
           method, PgraphTransformMethod::SetCompositeMatrix0,
           PgraphTransformMatrixElementCount, element))
    {
        StoreFloat(state.compositeMatrix, element, data);
        return true;
    }
    if(DecodeTransformElement(
           method, PgraphTransformMethod::SetViewportOffset0,
           PgraphTransformVectorComponentCount, element))
    {
        StoreFloat(state.viewportOffset, element, data);
        return true;
    }
    if(DecodeTransformElement(
           method, PgraphTransformMethod::SetViewportScale0,
           PgraphTransformVectorComponentCount, element))
    {
        StoreFloat(state.viewportScale, element, data);
        return true;
    }

    return false;
}

bool SetPgraphTransformConstant(
    PgraphTransformState& state, std::uint32_t constantIndex,
    std::span<const float, PgraphTransformVectorComponentCount> value) noexcept
{
    if(constantIndex >= PgraphTransformConstantCount)
    {
        return false;
    }

    const std::size_t firstComponent =
        constantIndex * PgraphTransformVectorComponentCount;
    std::copy(value.begin(), value.end(),
              state.constants.data() + firstComponent);
    return true;
}

} // namespace cxbx::nv2a
