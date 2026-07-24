#include "hw/nv2a_pgraph_vertex_state.h"

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

} // namespace cxbx::nv2a
