#include "hw/nv2a_pgraph_transform_state.h"

#include <array>
#include <bit>
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

bool ExpectFloatBits(float actual, std::uint32_t expected,
                     const char* message) noexcept
{
    return ExpectEqual(std::bit_cast<std::uint32_t>(actual), expected, message);
}

bool Expect(bool condition, const char* message) noexcept
{
    if(condition)
    {
        return true;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphTransformMethod;
    using cxbx::nv2a::PgraphTransformState;

    PgraphTransformState state{};
    for(std::uint32_t component = 0;
        component < cxbx::nv2a::PgraphTransformVectorComponentCount;
        ++component)
    {
        if(!ExpectFloatBits(state.viewportOffset[component], 0,
                            "default viewport offset") ||
           !ExpectFloatBits(state.viewportScale[component], 0x3F800000u,
                            "default viewport scale"))
        {
            return 1;
        }
    }
    for(std::uint32_t element = 0;
        element < cxbx::nv2a::PgraphTransformMatrixElementCount; ++element)
    {
        const std::uint32_t expected =
            element % 5u == 0 ? 0x3F800000u : 0u;
        if(!ExpectFloatBits(state.compositeMatrix[element], expected,
                            "default composite matrix"))
        {
            return 1;
        }
    }
    for(const std::uint32_t word : state.program)
    {
        if(!ExpectEqual(word, 0, "default program word"))
        {
            return 1;
        }
    }
    for(const float component : state.constants)
    {
        if(!ExpectFloatBits(component, 0, "default transform constant"))
        {
            return 1;
        }
    }
    if(!ExpectEqual(state.programWriteWord, 0,
                    "default program upload cursor") ||
       !ExpectEqual(state.programInstructionCount, 0,
                    "default program instruction count") ||
       !ExpectEqual(state.programStart, 0, "default program start") ||
       !ExpectEqual(state.executionMode, 0, "default execution mode") ||
       !ExpectEqual(state.constantWriteComponent, 0,
                    "default constant upload cursor"))
    {
        return 1;
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                   state,
                   PgraphTransformMethod::SetTransformExecutionMode |
                       0xA0000003u,
                   0xFFFFFFFFu),
               "masked execution-mode method was not handled") ||
       !ExpectEqual(state.executionMode, 3, "masked execution mode") ||
       !Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                   state, PgraphTransformMethod::SetTransformProgramStart,
                   17u),
               "program-start method was not handled") ||
       !ExpectEqual(state.programStart, 17u, "program start"))
    {
        return 1;
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                   state, PgraphTransformMethod::SetTransformProgramLoad,
                   0u),
               "program-load method was not handled"))
    {
        return 1;
    }
    for(std::uint32_t word = 0; word < 8; ++word)
    {
        const std::uint32_t data = 0x10203040u + word;
        if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                       state,
                       PgraphTransformMethod::SetTransformProgram0 +
                           (word % 32u) * 4u,
                       data),
                   "program-upload method was not handled") ||
           !ExpectEqual(state.program[word], data, "program-upload word"))
        {
            return 1;
        }
    }
    if(!ExpectEqual(state.programWriteWord, 8u,
                    "program upload cursor") ||
       !ExpectEqual(state.programInstructionCount, 2u,
                    "program instruction count"))
    {
        return 1;
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                   state, PgraphTransformMethod::SetTransformProgramLoad,
                   cxbx::nv2a::PgraphTransformProgramInstructionCount - 1u),
               "last program-load method was not handled"))
    {
        return 1;
    }
    for(std::uint32_t word = 0;
        word < cxbx::nv2a::PgraphTransformVectorComponentCount; ++word)
    {
        if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                       state,
                       PgraphTransformMethod::SetTransformProgram0,
                       0xA0B0C000u + word),
                   "last program word was not handled"))
        {
            return 1;
        }
    }
    const std::uint32_t finalProgramWord =
        cxbx::nv2a::PgraphTransformProgramWordCount - 1u;
    if(!ExpectEqual(state.program[finalProgramWord], 0xA0B0C003u,
                    "last stored program word") ||
       !ExpectEqual(
           state.programWriteWord,
           cxbx::nv2a::PgraphTransformProgramWordCount,
           "saturated program upload cursor") ||
       !ExpectEqual(
           state.programInstructionCount,
           cxbx::nv2a::PgraphTransformProgramInstructionCount,
           "maximum program instruction count") ||
       !Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                   state, PgraphTransformMethod::SetTransformProgram0,
                   0xFFFFFFFFu),
               "overflow program word was not classified") ||
       !ExpectEqual(
           state.programWriteWord,
           cxbx::nv2a::PgraphTransformProgramWordCount,
           "overflow changed program upload cursor") ||
       !ExpectEqual(state.program[finalProgramWord], 0xA0B0C003u,
                    "overflow changed last program word"))
    {
        return 1;
    }

    for(std::uint32_t element = 0;
        element < cxbx::nv2a::PgraphTransformMatrixElementCount; ++element)
    {
        const std::uint32_t bits = 0x3F000000u + element * 0x00010000u;
        if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                       state,
                       PgraphTransformMethod::SetCompositeMatrix0 +
                           element * 4u,
                       bits),
                   "composite-matrix method was not handled") ||
           !ExpectFloatBits(state.compositeMatrix[element], bits,
                            "composite-matrix element"))
        {
            return 1;
        }
    }
    for(std::uint32_t component = 0;
        component < cxbx::nv2a::PgraphTransformVectorComponentCount;
        ++component)
    {
        const std::uint32_t offsetBits =
            0x40000000u + component * 0x00100000u;
        const std::uint32_t scaleBits =
            0x3F800000u + component * 0x00080000u;
        if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                       state,
                       PgraphTransformMethod::SetViewportOffset0 +
                           component * 4u,
                       offsetBits),
                   "viewport-offset method was not handled") ||
           !Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                       state,
                       PgraphTransformMethod::SetViewportScale0 +
                           component * 4u,
                       scaleBits),
                   "viewport-scale method was not handled") ||
           !ExpectFloatBits(state.viewportOffset[component], offsetBits,
                            "viewport-offset component") ||
           !ExpectFloatBits(state.viewportScale[component], scaleBits,
                            "viewport-scale component"))
        {
            return 1;
        }
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                   state, PgraphTransformMethod::SetTransformConstantLoad,
                   cxbx::nv2a::PgraphTransformConstantCount - 1u),
               "last constant-load method was not handled"))
    {
        return 1;
    }
    for(std::uint32_t component = 0;
        component < cxbx::nv2a::PgraphTransformVectorComponentCount;
        ++component)
    {
        const std::uint32_t bits =
            0xBF800000u + component * 0x00010000u;
        if(!Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                       state,
                       PgraphTransformMethod::SetTransformConstant0,
                       bits),
                   "constant-upload method was not handled") ||
           !ExpectFloatBits(
               state.constants[cxbx::nv2a::PgraphTransformConstantComponentCount -
                               cxbx::nv2a::PgraphTransformVectorComponentCount +
                               component],
               bits, "constant-upload component"))
        {
            return 1;
        }
    }
    const std::uint32_t finalConstantComponent =
        cxbx::nv2a::PgraphTransformConstantComponentCount - 1u;
    if(!ExpectEqual(
           state.constantWriteComponent,
           cxbx::nv2a::PgraphTransformConstantComponentCount,
           "saturated constant upload cursor") ||
       !Expect(cxbx::nv2a::ApplyPgraphTransformMethod(
                   state, PgraphTransformMethod::SetTransformConstant0,
                   0xFFFFFFFFu),
               "overflow constant was not classified") ||
       !ExpectEqual(
           state.constantWriteComponent,
           cxbx::nv2a::PgraphTransformConstantComponentCount,
           "overflow changed constant upload cursor") ||
       !ExpectFloatBits(state.constants[finalConstantComponent],
                        0xBF830000u,
                        "overflow changed last constant component"))
    {
        return 1;
    }

    const std::array<float,
                     cxbx::nv2a::PgraphTransformVectorComponentCount>
        directConstant{ 1.25f, -2.5f, 3.75f, -4.0f };
    if(!Expect(cxbx::nv2a::SetPgraphTransformConstant(
                   state, 37u, directConstant),
               "direct transform constant was rejected"))
    {
        return 1;
    }
    for(std::uint32_t component = 0;
        component < cxbx::nv2a::PgraphTransformVectorComponentCount;
        ++component)
    {
        if(!ExpectFloatBits(
               state.constants[37u * cxbx::nv2a::PgraphTransformVectorComponentCount +
                               component],
               std::bit_cast<std::uint32_t>(directConstant[component]),
               "direct transform constant component"))
        {
            return 1;
        }
    }
    if(!Expect(!cxbx::nv2a::SetPgraphTransformConstant(
                   state,
                   cxbx::nv2a::PgraphTransformConstantCount,
                   directConstant),
               "out-of-range direct transform constant was accepted"))
    {
        return 1;
    }

    const PgraphTransformState beforeUnknown = state;
    if(!Expect(!cxbx::nv2a::ApplyPgraphTransformMethod(
                   state, 0x00000440u, 0xFFFFFFFFu),
               "unknown projection method was handled") ||
       !Expect(!cxbx::nv2a::ApplyPgraphTransformMethod(
                   state,
                   PgraphTransformMethod::SetTransformExecutionMode + 4u,
                   0xFFFFFFFFu),
               "execution-mode method hole was handled") ||
       !ExpectEqual(state.executionMode, beforeUnknown.executionMode,
                    "unknown method changed execution mode") ||
       !ExpectEqual(state.programWriteWord,
                    beforeUnknown.programWriteWord,
                    "unknown method changed program cursor") ||
       !ExpectFloatBits(state.viewportOffset[0],
                        std::bit_cast<std::uint32_t>(
                            beforeUnknown.viewportOffset[0]),
                        "unknown method changed viewport offset"))
    {
        return 1;
    }

    return 0;
}
