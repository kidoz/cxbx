#include "hw/nv2a_pgraph_vertex_state.h"

#include <array>
#include <bit>
#include <cmath>
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

bool Expect(bool condition, const char* message) noexcept
{
    if(condition)
    {
        return true;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

bool ExpectNear(float actual, float expected, const char* message) noexcept
{
    if(std::fabs(actual - expected) <= 0.000001f)
    {
        return true;
    }

    std::fprintf(stderr, "%s: expected %g, got %g\n", message,
                 static_cast<double>(expected),
                 static_cast<double>(actual));
    return false;
}

void WriteAttributeWord(
    cxbx::nv2a::PgraphVertexAttributeBytes& bytes,
    std::uint32_t component, std::uint32_t value) noexcept
{
    const std::uint32_t offset = component * sizeof(std::uint32_t);
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24u);
}

void WriteAttributeFloat(
    cxbx::nv2a::PgraphVertexAttributeBytes& bytes,
    std::uint32_t component, float value) noexcept
{
    WriteAttributeWord(
        bytes, component, std::bit_cast<std::uint32_t>(value));
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphVertexMethod;
    using cxbx::nv2a::PgraphVertexState;

    PgraphVertexState state{};
    if(!ExpectEqual(state.contextDmaVertex, 0,
                    "default vertex DMA handle"))
    {
        return 1;
    }
    for(const auto& array : state.arrays)
    {
        if(!ExpectEqual(array.offset, 0, "default vertex-array offset") ||
           !ExpectEqual(array.format, 0, "default vertex-array format"))
        {
            return 1;
        }
    }

    if(!Expect(cxbx::nv2a::ApplyPgraphVertexStateMethod(
                   state, PgraphVertexMethod::SetContextDmaVertexA | 0xA0000003u,
                   0x11223344u),
               "masked vertex DMA method was not handled") ||
       !ExpectEqual(state.contextDmaVertex, 0x11223344u,
                    "vertex DMA handle"))
    {
        return 1;
    }

    for(std::uint32_t attribute = 0;
        attribute < cxbx::nv2a::PgraphVertexAttributeCount; ++attribute)
    {
        const std::uint32_t offset = 0x00100000u + attribute * 0x100u;
        const std::uint32_t format =
            ((0x20u + attribute) << 8) |
            (((attribute % 4u) + 1u) << 4) |
            (attribute % 7u);
        if(!Expect(cxbx::nv2a::ApplyPgraphVertexStateMethod(
                       state,
                       PgraphVertexMethod::SetVertexDataArrayOffset0 +
                           attribute * 4u,
                       offset),
                   "vertex-array offset method was not handled") ||
           !Expect(cxbx::nv2a::ApplyPgraphVertexStateMethod(
                       state,
                       PgraphVertexMethod::SetVertexDataArrayFormat0 +
                           attribute * 4u,
                       format),
                   "vertex-array format method was not handled") ||
           !ExpectEqual(state.arrays[attribute].offset, offset,
                        "decoded vertex-array offset") ||
           !ExpectEqual(state.arrays[attribute].format, format,
                        "decoded vertex-array format"))
        {
            return 1;
        }
    }

    constexpr std::uint32_t MaskedAttribute = 7;
    if(!Expect(cxbx::nv2a::ApplyPgraphVertexStateMethod(
                   state,
                   (PgraphVertexMethod::SetVertexDataArrayFormat0 +
                    MaskedAttribute * 4u) |
                       0xC0000002u,
                   0x005A0032u),
               "masked vertex-array method was not handled") ||
       !ExpectEqual(state.arrays[MaskedAttribute].format, 0x005A0032u,
                    "masked vertex-array format"))
    {
        return 1;
    }

    const auto decoded =
        cxbx::nv2a::DecodePgraphVertexArrayFormat(0x005A0032u);
    if(!ExpectEqual(decoded.type, 2, "decoded vertex-array type") ||
       !ExpectEqual(decoded.componentCount, 3,
                    "decoded vertex-array component count") ||
       !ExpectEqual(decoded.stride, 0,
                    "decoded vertex-array stride"))
    {
        return 1;
    }

    const auto strideDecoded =
        cxbx::nv2a::DecodePgraphVertexArrayFormat(0x00005A32u);
    if(!ExpectEqual(strideDecoded.type, 2,
                    "stride-vector vertex-array type") ||
       !ExpectEqual(strideDecoded.componentCount, 3,
                    "stride-vector vertex-array component count") ||
       !ExpectEqual(strideDecoded.stride, 0x5Au,
                    "stride-vector vertex-array stride"))
    {
        return 1;
    }

    constexpr std::array<std::uint32_t, 8> ComponentSizes = {
        1,
        2,
        4,
        0,
        1,
        2,
        4,
        0,
    };
    for(std::uint32_t type = 0; type < ComponentSizes.size(); ++type)
    {
        if(!ExpectEqual(
               cxbx::nv2a::GetPgraphVertexComponentSize(type),
               ComponentSizes[type], "vertex component size"))
        {
            return 1;
        }
    }

    PgraphVertexState layoutState{};
    layoutState.contextDmaVertex = 0x76543210u;
    for(std::uint32_t attribute = 0;
        attribute < cxbx::nv2a::PgraphVertexAttributeCount; ++attribute)
    {
        layoutState.arrays[attribute].offset = 0x1000u + attribute * 0x20u;
    }
    layoutState.arrays[0].format = 0x00001030u;
    layoutState.arrays[1].format = 0x00002021u;
    layoutState.arrays[2].format = 0x00003032u;
    layoutState.arrays[3].format = 0x00004044u;
    layoutState.arrays[4].format = 0x00005015u;
    layoutState.arrays[5].format = 0x00006016u;
    layoutState.arrays[6].format = 0x0000700Fu;

    cxbx::nv2a::PgraphVertexLayout inlineLayout{};
    if(!Expect(cxbx::nv2a::BuildPgraphInlineVertexLayout(
                   layoutState, inlineLayout),
               "valid inline vertex layout was rejected") ||
       !ExpectEqual(inlineLayout.stride, 32,
                    "inline vertex layout stride") ||
       !ExpectEqual(inlineLayout.offsets[0], 0,
                    "inline byte attribute offset") ||
       !ExpectEqual(inlineLayout.offsets[1], 4,
                    "inline short attribute offset") ||
       !ExpectEqual(inlineLayout.offsets[2], 8,
                    "inline float attribute offset") ||
       !ExpectEqual(inlineLayout.offsets[3], 20,
                    "inline OGL byte attribute offset") ||
       !ExpectEqual(inlineLayout.offsets[4], 24,
                    "inline S32K attribute offset") ||
       !ExpectEqual(inlineLayout.offsets[5], 28,
                    "inline CMP attribute offset") ||
       !ExpectEqual(
           inlineLayout.offsets[6],
           cxbx::nv2a::PgraphVertexLayoutUnusedOffset,
           "disabled inline attribute offset") ||
       !ExpectEqual(
           inlineLayout.offsets[15],
           cxbx::nv2a::PgraphVertexLayoutUnusedOffset,
           "unused inline attribute offset") ||
       !ExpectEqual(inlineLayout.vertexState.contextDmaVertex,
                    layoutState.contextDmaVertex,
                    "inline layout vertex DMA snapshot") ||
       !ExpectEqual(inlineLayout.vertexState.arrays[2].format,
                    layoutState.arrays[2].format,
                    "inline layout format snapshot") ||
       !ExpectEqual(inlineLayout.vertexState.arrays[2].offset,
                    layoutState.arrays[2].offset,
                    "inline layout array-offset snapshot"))
    {
        return 1;
    }

    PgraphVertexState emptyLayoutState{};
    cxbx::nv2a::PgraphVertexLayout rejectedLayout{};
    rejectedLayout.stride = 0xFFFFFFFFu;
    if(!Expect(!cxbx::nv2a::BuildPgraphInlineVertexLayout(
                   emptyLayoutState, rejectedLayout),
               "empty inline vertex layout was accepted") ||
       !ExpectEqual(rejectedLayout.stride, 0xFFFFFFFFu,
                    "empty layout changed rejected output"))
    {
        return 1;
    }

    PgraphVertexState invalidLayoutState{};
    invalidLayoutState.arrays[0].format = 0x13u;
    if(!Expect(!cxbx::nv2a::BuildPgraphInlineVertexLayout(
                   invalidLayoutState, rejectedLayout),
               "unknown inline component type was accepted"))
    {
        return 1;
    }
    invalidLayoutState.arrays[0].format = 0x52u;
    if(!Expect(!cxbx::nv2a::BuildPgraphInlineVertexLayout(
                   invalidLayoutState, rejectedLayout),
               "five-component inline attribute was accepted"))
    {
        return 1;
    }
    invalidLayoutState.arrays[0].format = 0x26u;
    if(!Expect(!cxbx::nv2a::BuildPgraphInlineVertexLayout(
                   invalidLayoutState, rejectedLayout),
               "multi-component CMP inline attribute was accepted"))
    {
        return 1;
    }

    std::array<
        std::uint32_t, cxbx::nv2a::PgraphVertexAttributeCount>
        immediateFormats{};
    immediateFormats[0] = 0x42u;
    immediateFormats[3] = 0x40u;
    immediateFormats[9] = 0x22u;
    const auto immediateLayout =
        cxbx::nv2a::BuildPgraphImmediateVertexLayout(
            layoutState, immediateFormats);
    if(!ExpectEqual(
           immediateLayout.stride,
           cxbx::nv2a::PgraphImmediateVertexStride,
           "immediate vertex layout stride") ||
       !ExpectEqual(immediateLayout.offsets[0], 0,
                    "immediate position offset") ||
       !ExpectEqual(
           immediateLayout.offsets[3],
           3u * cxbx::nv2a::PgraphImmediateVertexAttributeStride,
           "immediate diffuse offset") ||
       !ExpectEqual(
           immediateLayout.offsets[9],
           9u * cxbx::nv2a::PgraphImmediateVertexAttributeStride,
           "immediate texture offset") ||
       !ExpectEqual(
           immediateLayout.offsets[1],
           cxbx::nv2a::PgraphVertexLayoutUnusedOffset,
           "disabled immediate attribute offset") ||
       !ExpectEqual(immediateLayout.vertexState.arrays[0].format, 0x42u,
                    "immediate position format") ||
       !ExpectEqual(immediateLayout.vertexState.arrays[3].format, 0x40u,
                    "immediate diffuse format") ||
       !ExpectEqual(immediateLayout.vertexState.arrays[9].format, 0x22u,
                    "immediate texture format") ||
       !ExpectEqual(immediateLayout.vertexState.arrays[1].format, 0,
                    "disabled immediate format") ||
       !ExpectEqual(immediateLayout.vertexState.contextDmaVertex,
                    layoutState.contextDmaVertex,
                    "immediate layout vertex DMA snapshot") ||
       !ExpectEqual(immediateLayout.vertexState.arrays[1].offset,
                    layoutState.arrays[1].offset,
                    "immediate layout array-offset snapshot"))
    {
        return 1;
    }

    const auto vertexArrayFetchPlan =
        cxbx::nv2a::BuildPgraphVertexFetchPlan(layoutState);
    if(!ExpectEqual(vertexArrayFetchPlan.contextDmaVertex,
                    layoutState.contextDmaVertex,
                    "array fetch-plan vertex DMA") ||
       !ExpectEqual(
           static_cast<std::uint32_t>(
               vertexArrayFetchPlan.attributes[0].source),
           static_cast<std::uint32_t>(
               cxbx::nv2a::PgraphVertexFetchSource::VertexArray),
           "array fetch-plan source") ||
       !ExpectEqual(vertexArrayFetchPlan.attributes[0].offset,
                    layoutState.arrays[0].offset,
                    "array fetch-plan offset") ||
       !ExpectEqual(vertexArrayFetchPlan.attributes[0].stride, 0x10u,
                    "array fetch-plan stride") ||
       !ExpectEqual(vertexArrayFetchPlan.attributes[0].rawFormat,
                    layoutState.arrays[0].format,
                    "array fetch-plan raw format") ||
       !ExpectEqual(vertexArrayFetchPlan.attributes[0].type, 0,
                    "array fetch-plan type") ||
       !ExpectEqual(vertexArrayFetchPlan.attributes[0].componentCount, 3,
                    "array fetch-plan component count") ||
       !ExpectEqual(
           static_cast<std::uint32_t>(
               vertexArrayFetchPlan.attributes[6].source),
           static_cast<std::uint32_t>(
               cxbx::nv2a::PgraphVertexFetchSource::VertexArray),
           "disabled-format array source compatibility") ||
       !ExpectEqual(vertexArrayFetchPlan.attributes[6].stride, 0x70u,
                    "disabled-format array stride compatibility") ||
       !ExpectEqual(vertexArrayFetchPlan.attributes[6].componentCount, 0,
                    "disabled-format array component count"))
    {
        return 1;
    }

    const auto inlineFetchPlan =
        cxbx::nv2a::BuildPgraphInlineVertexFetchPlan(inlineLayout);
    if(!ExpectEqual(inlineFetchPlan.contextDmaVertex,
                    layoutState.contextDmaVertex,
                    "inline fetch-plan vertex DMA") ||
       !ExpectEqual(
           static_cast<std::uint32_t>(
               inlineFetchPlan.attributes[0].source),
           static_cast<std::uint32_t>(
               cxbx::nv2a::PgraphVertexFetchSource::Inline),
           "inline fetch-plan source") ||
       !ExpectEqual(inlineFetchPlan.attributes[0].offset, 0,
                    "inline fetch-plan offset") ||
       !ExpectEqual(inlineFetchPlan.attributes[0].stride,
                    inlineLayout.stride,
                    "inline fetch-plan stride") ||
       !ExpectEqual(inlineFetchPlan.attributes[0].rawFormat,
                    layoutState.arrays[0].format,
                    "inline fetch-plan raw format") ||
       !ExpectEqual(inlineFetchPlan.attributes[0].componentCount, 3,
                    "inline fetch-plan component count") ||
       !ExpectEqual(
           static_cast<std::uint32_t>(
               inlineFetchPlan.attributes[6].source),
           static_cast<std::uint32_t>(
               cxbx::nv2a::PgraphVertexFetchSource::Disabled),
           "missing inline offset source") ||
       !ExpectEqual(
           inlineFetchPlan.attributes[6].offset,
           cxbx::nv2a::PgraphVertexLayoutUnusedOffset,
           "missing inline fetch-plan offset") ||
       !ExpectEqual(inlineFetchPlan.attributes[6].stride, 0x70u,
                    "missing inline stride compatibility"))
    {
        return 1;
    }

    const auto immediateFetchPlan =
        cxbx::nv2a::BuildPgraphInlineVertexFetchPlan(immediateLayout);
    if(!ExpectEqual(
           static_cast<std::uint32_t>(
               immediateFetchPlan.attributes[0].source),
           static_cast<std::uint32_t>(
               cxbx::nv2a::PgraphVertexFetchSource::Inline),
           "immediate fetch-plan source") ||
       !ExpectEqual(immediateFetchPlan.attributes[0].stride,
                    cxbx::nv2a::PgraphImmediateVertexStride,
                    "immediate fetch-plan stride") ||
       !ExpectEqual(immediateFetchPlan.attributes[0].type, 2,
                    "immediate fetch-plan type") ||
       !ExpectEqual(immediateFetchPlan.attributes[0].componentCount, 4,
                    "immediate fetch-plan component count") ||
       !ExpectEqual(
           static_cast<std::uint32_t>(
               immediateFetchPlan.attributes[1].source),
           static_cast<std::uint32_t>(
               cxbx::nv2a::PgraphVertexFetchSource::Disabled),
           "disabled immediate fetch-plan source") ||
       !ExpectEqual(immediateFetchPlan.attributes[1].stride, 0,
                    "disabled immediate fetch-plan stride") ||
       !ExpectEqual(immediateFetchPlan.attributes[1].rawFormat, 0,
                    "disabled immediate fetch-plan raw format"))
    {
        return 1;
    }

    cxbx::nv2a::PgraphVertexAttributeBytes attributeBytes{};
    WriteAttributeFloat(attributeBytes, 0, 1.5f);
    WriteAttributeFloat(attributeBytes, 1, -2.25f);
    WriteAttributeFloat(attributeBytes, 2, 0.5f);
    WriteAttributeFloat(attributeBytes, 3, 2.0f);

    const auto float3Value =
        cxbx::nv2a::DecodePgraphFloatVertexAttribute(
            attributeBytes, 3);
    if(!ExpectNear(float3Value.components[0], 1.5f,
                   "float3 attribute x") ||
       !ExpectNear(float3Value.components[1], -2.25f,
                   "float3 attribute y") ||
       !ExpectNear(float3Value.components[2], 0.5f,
                   "float3 attribute z") ||
       !ExpectNear(float3Value.components[3], 1.0f,
                   "float3 attribute default w") ||
       !ExpectEqual(float3Value.packedColor, 0,
                    "float attribute packed color"))
    {
        return 1;
    }

    const auto boundedFloatValue =
        cxbx::nv2a::DecodePgraphFloatVertexAttribute(
            attributeBytes, 5);
    if(!ExpectNear(boundedFloatValue.components[3], 2.0f,
                   "bounded float attribute w"))
    {
        return 1;
    }

    cxbx::nv2a::PgraphVertexAttributeFetch floatFetch{};
    floatFetch.type = 2;
    floatFetch.componentCount = 4;
    const auto typedFloatValue =
        cxbx::nv2a::DecodePgraphVertexAttribute(
            floatFetch, attributeBytes);
    if(!ExpectNear(typedFloatValue.components[0], 1.5f,
                   "typed float attribute x") ||
       !ExpectNear(typedFloatValue.components[3], 2.0f,
                   "typed float attribute w"))
    {
        return 1;
    }

    attributeBytes.fill(0);
    WriteAttributeWord(attributeBytes, 0, 0x80402010u);
    const auto packedColorValue =
        cxbx::nv2a::DecodePgraphPackedColorVertexAttribute(
            attributeBytes);
    if(!ExpectEqual(packedColorValue.packedColor, 0x80402010u,
                    "packed attribute color") ||
       !ExpectNear(packedColorValue.components[0], 64.0f / 255.0f,
                   "packed attribute red") ||
       !ExpectNear(packedColorValue.components[1], 32.0f / 255.0f,
                   "packed attribute green") ||
       !ExpectNear(packedColorValue.components[2], 16.0f / 255.0f,
                   "packed attribute blue") ||
       !ExpectNear(packedColorValue.components[3], 128.0f / 255.0f,
                   "packed attribute alpha"))
    {
        return 1;
    }

    cxbx::nv2a::PgraphVertexAttributeFetch packedFetch{};
    packedFetch.type = 6;
    packedFetch.componentCount = 1;
    const auto typedPackedValue =
        cxbx::nv2a::DecodePgraphVertexAttribute(
            packedFetch, attributeBytes);
    if(!ExpectEqual(typedPackedValue.packedColor, 0x80402010u,
                    "typed packed attribute color") ||
       !ExpectNear(typedPackedValue.components[3],
                   128.0f / 255.0f,
                   "typed packed attribute alpha"))
    {
        return 1;
    }

    cxbx::nv2a::PgraphVertexAttributeValues inputAttributeValues{};
    inputAttributeValues[0].components = {
        1.0f,
        2.0f,
        3.0f,
        4.0f,
    };
    inputAttributeValues[7].components = {
        -1.0f,
        -2.0f,
        -3.0f,
        -4.0f,
    };
    inputAttributeValues[15].components = {
        0.25f,
        0.5f,
        0.75f,
        1.0f,
    };

    const auto defaultProgramInput =
        cxbx::nv2a::BuildPgraphVertexProgramInput(
            inputAttributeValues, 0);
    for(std::uint32_t attribute = 0;
        attribute < cxbx::nv2a::PgraphVertexAttributeCount;
        ++attribute)
    {
        const std::uint32_t inputOffset =
            attribute *
            cxbx::nv2a::PgraphVertexAttributeComponentCount;
        if(!ExpectNear(defaultProgramInput[inputOffset], 0.0f,
                       "default program input x") ||
           !ExpectNear(defaultProgramInput[inputOffset + 1], 0.0f,
                       "default program input y") ||
           !ExpectNear(defaultProgramInput[inputOffset + 2], 0.0f,
                       "default program input z") ||
           !ExpectNear(defaultProgramInput[inputOffset + 3], 1.0f,
                       "default program input w"))
        {
            return 1;
        }
    }

    const std::uint32_t suppliedAttributeMask =
        (1u << 0u) | (1u << 15u);
    const auto assembledProgramInput =
        cxbx::nv2a::BuildPgraphVertexProgramInput(
            inputAttributeValues, suppliedAttributeMask);
    if(!ExpectNear(assembledProgramInput[0], 1.0f,
                   "assembled first input x") ||
       !ExpectNear(assembledProgramInput[3], 4.0f,
                   "assembled first input w") ||
       !ExpectNear(
           assembledProgramInput[7u * cxbx::nv2a::PgraphVertexAttributeComponentCount],
           0.0f, "unsupplied middle input x") ||
       !ExpectNear(
           assembledProgramInput[7u * cxbx::nv2a::PgraphVertexAttributeComponentCount + 3u],
           1.0f, "unsupplied middle input w") ||
       !ExpectNear(
           assembledProgramInput[15u * cxbx::nv2a::PgraphVertexAttributeComponentCount],
           0.25f, "assembled last input x") ||
       !ExpectNear(
           assembledProgramInput[15u * cxbx::nv2a::PgraphVertexAttributeComponentCount + 2u],
           0.75f, "assembled last input z"))
    {
        return 1;
    }

    cxbx::nv2a::PgraphVertexAttributeValues fixedAttributeValues{};
    fixedAttributeValues[cxbx::nv2a::PgraphVertexPositionAttribute]
        .components = {
        10.0f,
        20.0f,
        30.0f,
        2.0f,
    };
    fixedAttributeValues[cxbx::nv2a::PgraphVertexDiffuseAttribute]
        .components = {
        1.0f,
        0.5f,
        0.25f,
        0.0f,
    };
    fixedAttributeValues[cxbx::nv2a::PgraphVertexDiffuseAttribute]
        .packedColor = 0x80402010u;
    fixedAttributeValues[cxbx::nv2a::PgraphVertexTexcoord0Attribute]
        .components = {
        0.125f,
        0.25f,
        0.0f,
        0.5f,
    };
    fixedAttributeValues[cxbx::nv2a::PgraphVertexTexcoord0Attribute + 1u]
        .components = {
        -1.0f,
        -2.0f,
        -3.0f,
        -4.0f,
    };
    fixedAttributeValues[cxbx::nv2a::PgraphVertexTexcoord0Attribute + 3u]
        .components = {
        0.75f,
        0.875f,
        0.0f,
        1.0f,
    };

    cxbx::nv2a::PgraphVertexFetchPlan fixedFetchPlan{};
    const auto defaultFixedInput =
        cxbx::nv2a::BuildPgraphFixedFunctionVertexInput(
            fixedAttributeValues, fixedFetchPlan, 0);
    if(!ExpectNear(defaultFixedInput.position[0], 0.0f,
                   "default fixed position x") ||
       !ExpectNear(defaultFixedInput.position[3], 1.0f,
                   "default fixed position w") ||
       !ExpectEqual(defaultFixedInput.diffuseColor, 0xFFFFFFFFu,
                    "default fixed diffuse") ||
       !ExpectNear(defaultFixedInput.textureCoordinates[0][0], 0.0f,
                   "default fixed texture u") ||
       !ExpectNear(defaultFixedInput.textureCoordinates[0][3], 1.0f,
                   "default fixed texture q"))
    {
        return 1;
    }

    const std::uint32_t fixedSuppliedMask =
        (1u << cxbx::nv2a::PgraphVertexPositionAttribute) |
        (1u << cxbx::nv2a::PgraphVertexDiffuseAttribute) |
        (1u << cxbx::nv2a::PgraphVertexTexcoord0Attribute) |
        (1u << (cxbx::nv2a::PgraphVertexTexcoord0Attribute + 3u));
    const auto packedFixedInput =
        cxbx::nv2a::BuildPgraphFixedFunctionVertexInput(
            fixedAttributeValues, fixedFetchPlan,
            fixedSuppliedMask);
    if(!ExpectNear(packedFixedInput.position[0], 10.0f,
                   "packed fixed position x") ||
       !ExpectNear(packedFixedInput.position[3], 2.0f,
                   "packed fixed position w") ||
       !ExpectEqual(packedFixedInput.diffuseColor, 0x80402010u,
                    "packed fixed diffuse") ||
       !ExpectNear(packedFixedInput.textureCoordinates[0][0], 0.125f,
                   "packed fixed texture-0 u") ||
       !ExpectNear(packedFixedInput.textureCoordinates[0][3], 0.5f,
                   "packed fixed texture-0 q") ||
       !ExpectNear(packedFixedInput.textureCoordinates[1][0], 0.0f,
                   "unsupplied fixed texture-1 u") ||
       !ExpectNear(packedFixedInput.textureCoordinates[1][3], 1.0f,
                   "unsupplied fixed texture-1 q") ||
       !ExpectNear(packedFixedInput.textureCoordinates[3][0], 0.75f,
                   "packed fixed texture-3 u"))
    {
        return 1;
    }

    fixedFetchPlan.attributes[cxbx::nv2a::PgraphVertexDiffuseAttribute]
        .type = 2;
    const auto floatFixedInput =
        cxbx::nv2a::BuildPgraphFixedFunctionVertexInput(
            fixedAttributeValues, fixedFetchPlan,
            fixedSuppliedMask);
    if(!ExpectEqual(floatFixedInput.diffuseColor, 0x00FF8040u,
                    "float fixed diffuse"))
    {
        return 1;
    }

    const PgraphVertexState beforeUnknown = state;
    if(!Expect(!cxbx::nv2a::ApplyPgraphVertexStateMethod(
                   state,
                   PgraphVertexMethod::SetVertexDataArrayOffset0 - 4u,
                   0xFFFFFFFFu),
               "method below vertex-offset range was handled") ||
       !Expect(!cxbx::nv2a::ApplyPgraphVertexStateMethod(
                   state,
                   PgraphVertexMethod::SetVertexDataArrayFormat0 +
                       cxbx::nv2a::PgraphVertexAttributeCount * 4u,
                   0xFFFFFFFFu),
               "method above vertex-format range was handled") ||
       !Expect(!cxbx::nv2a::ApplyPgraphVertexStateMethod(
                   state, 0x00000400u, 0xFFFFFFFFu),
               "unknown vertex method was handled") ||
       !ExpectEqual(state.contextDmaVertex,
                    beforeUnknown.contextDmaVertex,
                    "unknown method vertex DMA handle") ||
       !ExpectEqual(state.arrays[MaskedAttribute].format,
                    beforeUnknown.arrays[MaskedAttribute].format,
                    "unknown method vertex-array format"))
    {
        return 1;
    }

    static_assert(cxbx::nv2a::PgraphImmediateVertexStride == 256);
    return 0;
}
