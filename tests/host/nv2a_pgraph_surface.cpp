#include "hw/nv2a_pgraph_surface.h"

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

bool ExpectKind(cxbx::nv2a::PgraphSurfaceStepKind actual,
                cxbx::nv2a::PgraphSurfaceStepKind expected,
                const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: unexpected step kind\n", message);
    return false;
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphSurfaceMethod;
    using cxbx::nv2a::PgraphSurfaceState;
    using cxbx::nv2a::PgraphSurfaceStepKind;

    PgraphSurfaceState state{};
    auto step = cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetContextDmaColor, 0x11223344u);
    if(!ExpectKind(step.kind, PgraphSurfaceStepKind::StateUpdate,
                   "color DMA update") ||
       !ExpectEqual(state.contextDmaColor, 0x11223344u,
                    "color DMA handle"))
    {
        return 1;
    }

    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetContextDmaZeta, 0x55667788u);
    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetSurfaceColorOffset, 0x00100000u);
    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetSurfaceZetaOffset, 0x00200000u);
    if(!ExpectEqual(state.contextDmaZeta, 0x55667788u,
                    "zeta DMA handle") ||
       !ExpectEqual(state.colorOffset, 0x00100000u,
                    "color surface offset") ||
       !ExpectEqual(state.zetaOffset, 0x00200000u,
                    "zeta surface offset"))
    {
        return 1;
    }

    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetSurfaceClipHorizontal,
        0x02800020u);
    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetSurfaceClipVertical,
        0x01E00010u);
    if(!ExpectEqual(state.clipX, 0x20u, "surface clip X") ||
       !ExpectEqual(state.clipWidth, 0x280u, "surface clip width") ||
       !ExpectEqual(state.clipY, 0x10u, "surface clip Y") ||
       !ExpectEqual(state.clipHeight, 0x1E0u, "surface clip height"))
    {
        return 1;
    }

    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetSurfaceFormat, 0x000012A5u);
    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetSurfacePitch, 0x08000A00u);
    if(!ExpectEqual(state.format, 0x000012A5u, "surface format") ||
       !ExpectEqual(state.zetaFormat, 0x0Au, "zeta format") ||
       !ExpectEqual(state.colorPitch, 0x0A00u, "color pitch") ||
       !ExpectEqual(state.zetaPitch, 0x0800u, "zeta pitch"))
    {
        return 1;
    }

    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetColorClearValue, 0xAABBCCDDu);
    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetZStencilClearValue, 0x12345678u);
    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetClearRectHorizontal, 0x027F0020u);
    (void)cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::SetClearRectVertical, 0x01DF0010u);
    if(!ExpectEqual(state.colorClearValue, 0xAABBCCDDu,
                    "color clear value") ||
       !ExpectEqual(state.zStencilClearValue, 0x12345678u,
                    "zeta/stencil clear value") ||
       !ExpectEqual(state.clearRectHorizontal, 0x027F0020u,
                    "horizontal clear rectangle") ||
       !ExpectEqual(state.clearRectVertical, 0x01DF0010u,
                    "vertical clear rectangle"))
    {
        return 1;
    }

    step = cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, PgraphSurfaceMethod::ClearSurface, 0x000000F3u);
    if(!ExpectKind(step.kind, PgraphSurfaceStepKind::ClearSurface,
                   "clear action") ||
       !ExpectEqual(step.data, 0x000000F3u, "clear flags"))
    {
        return 1;
    }

    const PgraphSurfaceState beforeUnknown = state;
    step = cxbx::nv2a::ApplyPgraphSurfaceMethod(
        state, 0xDEADu, 0xFFFFFFFFu);
    if(!ExpectKind(step.kind, PgraphSurfaceStepKind::Unhandled,
                   "unknown surface method") ||
       !ExpectEqual(state.colorOffset, beforeUnknown.colorOffset,
                    "unknown method color offset") ||
       !ExpectEqual(state.format, beforeUnknown.format,
                    "unknown method format") ||
       !ExpectEqual(state.colorClearValue,
                    beforeUnknown.colorClearValue,
                    "unknown method clear value"))
    {
        return 1;
    }

    return 0;
}
