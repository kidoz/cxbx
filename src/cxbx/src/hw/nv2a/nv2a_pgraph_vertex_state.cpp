#include "hw/nv2a_pgraph_vertex_state.h"

#include <bit>

namespace cxbx::nv2a
{

namespace
{

constexpr std::uint32_t VertexMethodStride = 4u;
constexpr std::uint32_t VertexMethodSpan =
    PgraphVertexAttributeCount * VertexMethodStride;

bool DecodeVertexAttribute(
    std::uint32_t method, std::uint32_t baseMethod,
    std::uint32_t& attribute) noexcept
{
    if(method < baseMethod || method >= baseMethod + VertexMethodSpan)
    {
        return false;
    }

    attribute = (method - baseMethod) / VertexMethodStride;
    return true;
}

std::uint32_t ReadVertexAttributeWord(
    const PgraphVertexAttributeBytes& bytes,
    std::uint32_t component) noexcept
{
    const std::uint32_t offset = component * sizeof(std::uint32_t);
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

std::uint32_t PackPgraphFloatColorComponent(float component) noexcept
{
    // Preserve the established truncate-after-bias and low-byte-mask policy.
    // NOLINTNEXTLINE(bugprone-incorrect-roundings)
    return static_cast<std::uint32_t>(component * 255.0f + 0.5f) & 0xFFu;
}

} // namespace

bool ApplyPgraphVertexStateMethod(
    PgraphVertexState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    method &= 0x1FFCu;
    if(method == PgraphVertexMethod::SetContextDmaVertexA)
    {
        state.contextDmaVertex = data;
        return true;
    }

    std::uint32_t attribute = 0;
    if(DecodeVertexAttribute(
           method, PgraphVertexMethod::SetVertexDataArrayOffset0,
           attribute))
    {
        state.arrays[attribute].offset = data;
        return true;
    }
    if(DecodeVertexAttribute(
           method, PgraphVertexMethod::SetVertexDataArrayFormat0,
           attribute))
    {
        state.arrays[attribute].format = data;
        return true;
    }

    return false;
}

std::uint32_t GetPgraphVertexComponentSize(std::uint32_t type) noexcept
{
    switch(type)
    {
        case 0: // UB_D3D
        case 4: // UB_OGL
            return 1;
        case 1: // S1
        case 5: // S32K
            return 2;
        case 2: // F
        case 6: // CMP
            return 4;
        default:
            return 0;
    }
}

bool BuildPgraphInlineVertexLayout(
    const PgraphVertexState& state, PgraphVertexLayout& layout) noexcept
{
    PgraphVertexLayout candidate{};
    candidate.vertexState = state;
    candidate.offsets.fill(PgraphVertexLayoutUnusedOffset);

    std::uint32_t offset = 0;
    for(std::uint32_t attribute = 0;
        attribute < PgraphVertexAttributeCount; ++attribute)
    {
        const PgraphVertexArrayFormat format =
            DecodePgraphVertexArrayFormat(state.arrays[attribute].format);
        if(format.componentCount == 0)
        {
            continue;
        }

        const std::uint32_t componentSize =
            GetPgraphVertexComponentSize(format.type);
        if(componentSize == 0 || format.componentCount > 4 ||
           (format.type == 6 && format.componentCount != 1))
        {
            return false;
        }

        offset =
            (offset + componentSize - 1u) & ~(componentSize - 1u);
        candidate.offsets[attribute] = offset;
        offset += componentSize * format.componentCount;
        offset =
            (offset + componentSize - 1u) & ~(componentSize - 1u);
    }

    if(offset == 0)
    {
        return false;
    }

    candidate.stride = offset;
    layout = candidate;
    return true;
}

PgraphVertexLayout BuildPgraphImmediateVertexLayout(
    const PgraphVertexState& state,
    const std::array<std::uint32_t, PgraphVertexAttributeCount>&
        immediateFormats) noexcept
{
    PgraphVertexLayout layout{};
    layout.vertexState = state;
    layout.offsets.fill(PgraphVertexLayoutUnusedOffset);
    layout.stride = PgraphImmediateVertexStride;

    for(std::uint32_t attribute = 0;
        attribute < PgraphVertexAttributeCount; ++attribute)
    {
        const std::uint32_t format = immediateFormats[attribute];
        layout.vertexState.arrays[attribute].format = format;
        if(format != 0)
        {
            layout.offsets[attribute] =
                attribute * PgraphImmediateVertexAttributeStride;
        }
    }

    return layout;
}

PgraphVertexFetchPlan BuildPgraphVertexFetchPlan(
    const PgraphVertexState& state) noexcept
{
    PgraphVertexFetchPlan plan{};
    plan.contextDmaVertex = state.contextDmaVertex;

    for(std::uint32_t attribute = 0;
        attribute < PgraphVertexAttributeCount; ++attribute)
    {
        const PgraphVertexArrayState& array = state.arrays[attribute];
        const PgraphVertexArrayFormat format =
            DecodePgraphVertexArrayFormat(array.format);
        plan.attributes[attribute] = {
            PgraphVertexFetchSource::VertexArray,
            array.offset,
            format.stride,
            array.format,
            format.type,
            format.componentCount,
        };
    }

    return plan;
}

PgraphVertexFetchPlan BuildPgraphInlineVertexFetchPlan(
    const PgraphVertexLayout& layout) noexcept
{
    PgraphVertexFetchPlan plan{};
    plan.contextDmaVertex = layout.vertexState.contextDmaVertex;

    for(std::uint32_t attribute = 0;
        attribute < PgraphVertexAttributeCount; ++attribute)
    {
        const PgraphVertexArrayState& array =
            layout.vertexState.arrays[attribute];
        const PgraphVertexArrayFormat format =
            DecodePgraphVertexArrayFormat(array.format);
        const bool enabled =
            layout.offsets[attribute] != PgraphVertexLayoutUnusedOffset;
        plan.attributes[attribute] = {
            enabled ? PgraphVertexFetchSource::Inline
                    : PgraphVertexFetchSource::Disabled,
            layout.offsets[attribute],
            enabled ? layout.stride : format.stride,
            array.format,
            format.type,
            format.componentCount,
        };
    }

    return plan;
}

PgraphVertexAttributeValue DecodePgraphFloatVertexAttribute(
    const PgraphVertexAttributeBytes& bytes,
    std::uint32_t componentCount) noexcept
{
    PgraphVertexAttributeValue value{};
    if(componentCount > PgraphVertexAttributeComponentCount)
    {
        componentCount = PgraphVertexAttributeComponentCount;
    }

    for(std::uint32_t component = 0; component < componentCount;
        ++component)
    {
        value.components[component] = std::bit_cast<float>(
            ReadVertexAttributeWord(bytes, component));
    }
    return value;
}

PgraphVertexAttributeValue DecodePgraphPackedColorVertexAttribute(
    const PgraphVertexAttributeBytes& bytes) noexcept
{
    PgraphVertexAttributeValue value{};
    value.packedColor = ReadVertexAttributeWord(bytes, 0);
    value.components[0] =
        static_cast<float>((value.packedColor >> 16u) & 0xFFu) / 255.0f;
    value.components[1] =
        static_cast<float>((value.packedColor >> 8u) & 0xFFu) / 255.0f;
    value.components[2] =
        static_cast<float>(value.packedColor & 0xFFu) / 255.0f;
    value.components[3] =
        static_cast<float>((value.packedColor >> 24u) & 0xFFu) / 255.0f;
    return value;
}

PgraphVertexAttributeValue DecodePgraphVertexAttribute(
    const PgraphVertexAttributeFetch& fetch,
    const PgraphVertexAttributeBytes& bytes) noexcept
{
    if(fetch.type == 2u)
    {
        return DecodePgraphFloatVertexAttribute(
            bytes, fetch.componentCount);
    }
    return DecodePgraphPackedColorVertexAttribute(bytes);
}

PgraphVertexProgramInput BuildPgraphVertexProgramInput(
    const PgraphVertexAttributeValues& attributeValues,
    std::uint32_t suppliedAttributeMask) noexcept
{
    PgraphVertexProgramInput input{};
    for(std::uint32_t attribute = 0;
        attribute < PgraphVertexAttributeCount; ++attribute)
    {
        const std::uint32_t inputOffset =
            attribute * PgraphVertexAttributeComponentCount;
        input[inputOffset + 3] = 1.0f;
        if((suppliedAttributeMask & (1u << attribute)) == 0)
        {
            continue;
        }

        for(std::uint32_t component = 0;
            component < PgraphVertexAttributeComponentCount; ++component)
        {
            input[inputOffset + component] =
                attributeValues[attribute].components[component];
        }
    }
    return input;
}

PgraphFixedFunctionVertexInput BuildPgraphFixedFunctionVertexInput(
    const PgraphVertexAttributeValues& attributeValues,
    const PgraphVertexFetchPlan& fetchPlan,
    std::uint32_t suppliedAttributeMask) noexcept
{
    PgraphFixedFunctionVertexInput input{};
    if((suppliedAttributeMask &
        (1u << PgraphVertexPositionAttribute)) != 0)
    {
        input.position =
            attributeValues[PgraphVertexPositionAttribute].components;
    }

    if((suppliedAttributeMask &
        (1u << PgraphVertexDiffuseAttribute)) != 0)
    {
        const PgraphVertexAttributeValue& diffuse =
            attributeValues[PgraphVertexDiffuseAttribute];
        if(fetchPlan.attributes[PgraphVertexDiffuseAttribute].type == 2u)
        {
            const std::uint32_t red =
                PackPgraphFloatColorComponent(diffuse.components[0]);
            const std::uint32_t green =
                PackPgraphFloatColorComponent(diffuse.components[1]);
            const std::uint32_t blue =
                PackPgraphFloatColorComponent(diffuse.components[2]);
            const std::uint32_t alpha =
                PackPgraphFloatColorComponent(diffuse.components[3]);
            input.diffuseColor =
                (alpha << 24u) | (red << 16u) |
                (green << 8u) | blue;
        }
        else
        {
            input.diffuseColor = diffuse.packedColor;
        }
    }

    for(std::uint32_t stage = 0;
        stage < PgraphFixedFunctionTextureCount; ++stage)
    {
        const std::uint32_t attribute =
            PgraphVertexTexcoord0Attribute + stage;
        if((suppliedAttributeMask & (1u << attribute)) != 0)
        {
            input.textureCoordinates[stage] =
                attributeValues[attribute].components;
        }
    }
    return input;
}

} // namespace cxbx::nv2a
