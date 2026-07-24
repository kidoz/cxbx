#include "hw/nv2a_pgraph_vertex_state.h"

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

    return 0;
}
