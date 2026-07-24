#include "hw/nv2a_pgraph_transform_state.h"

#include <algorithm>
#include <bit>

namespace cxbx::nv2a
{

namespace
{

constexpr std::uint32_t TransformMethodStride = 4u;
constexpr std::uint32_t TransformUploadMethodCount = 32u;
constexpr float FixedFunctionMinimumDivisorMagnitude = 1.0e-6f;

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

PgraphFixedFunctionTransformResult TransformPgraphFixedFunctionPosition(
    const PgraphTransformState& state,
    const PgraphFixedFunctionTransformInput& input) noexcept
{
    PgraphFixedFunctionTransformResult result{};
    const auto& position = input.position;
    const auto& matrix = state.compositeMatrix;
    // The four uploaded NV2A constant vectors are matrix columns, matching
    // mat4(c0,c1,c2,c3) and the hardware's row-vector multiplication.
    result.homogeneousPosition = {
        position[0] * matrix[0] +
            position[1] * matrix[1] +
            position[2] * matrix[2] +
            position[3] * matrix[3],
        position[0] * matrix[4] +
            position[1] * matrix[5] +
            position[2] * matrix[6] +
            position[3] * matrix[7],
        position[0] * matrix[8] +
            position[1] * matrix[9] +
            position[2] * matrix[10] +
            position[3] * matrix[11],
        position[0] * matrix[12] +
            position[1] * matrix[13] +
            position[2] * matrix[14] +
            position[3] * matrix[15],
    };

    const float homogeneousW = result.homogeneousPosition[3];
    if(homogeneousW > FixedFunctionMinimumDivisorMagnitude ||
       homogeneousW < -FixedFunctionMinimumDivisorMagnitude)
    {
        result.inverseW = 1.0f / homogeneousW;
    }
    result.screenPosition = {
        result.homogeneousPosition[0] * result.inverseW +
            state.viewportOffset[0],
        result.homogeneousPosition[1] * result.inverseW +
            state.viewportOffset[1],
        result.homogeneousPosition[2] * result.inverseW,
    };
    return result;
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
