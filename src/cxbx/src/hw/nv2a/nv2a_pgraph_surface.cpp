#include "hw/nv2a_pgraph_surface.h"

namespace cxbx::nv2a
{

PgraphSurfaceStep ApplyPgraphSurfaceMethod(
    PgraphSurfaceState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    switch(method)
    {
        case PgraphSurfaceMethod::SetContextDmaColor:
            state.contextDmaColor = data;
            break;
        case PgraphSurfaceMethod::SetContextDmaZeta:
            state.contextDmaZeta = data;
            break;
        case PgraphSurfaceMethod::SetSurfaceClipHorizontal:
            state.clipX = data & 0xFFFFu;
            state.clipWidth = (data >> 16) & 0xFFFFu;
            break;
        case PgraphSurfaceMethod::SetSurfaceClipVertical:
            state.clipY = data & 0xFFFFu;
            state.clipHeight = (data >> 16) & 0xFFFFu;
            break;
        case PgraphSurfaceMethod::SetSurfaceFormat:
            state.format = data;
            state.zetaFormat = (data >> 4) & 0x0Fu;
            break;
        case PgraphSurfaceMethod::SetSurfacePitch:
            state.colorPitch = data & 0xFFFFu;
            state.zetaPitch = (data >> 16) & 0xFFFFu;
            break;
        case PgraphSurfaceMethod::SetSurfaceColorOffset:
            state.colorOffset = data;
            break;
        case PgraphSurfaceMethod::SetSurfaceZetaOffset:
            state.zetaOffset = data;
            break;
        case PgraphSurfaceMethod::SetZStencilClearValue:
            state.zStencilClearValue = data;
            break;
        case PgraphSurfaceMethod::SetColorClearValue:
            state.colorClearValue = data;
            break;
        case PgraphSurfaceMethod::ClearSurface:
            return { PgraphSurfaceStepKind::ClearSurface, data };
        case PgraphSurfaceMethod::SetClearRectHorizontal:
            state.clearRectHorizontal = data;
            break;
        case PgraphSurfaceMethod::SetClearRectVertical:
            state.clearRectVertical = data;
            break;
        default:
            return { PgraphSurfaceStepKind::Unhandled };
    }

    return { PgraphSurfaceStepKind::StateUpdate };
}

} // namespace cxbx::nv2a
