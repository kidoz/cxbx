#include "hw/nv2a_pgraph_vertex_state.h"

#include <array>
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
