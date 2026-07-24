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

} // namespace cxbx::nv2a
