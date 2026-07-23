#pragma once

#include <cstdint>

namespace cxbx::nv2a
{

struct PgraphSurfaceMethod final
{
    static constexpr std::uint32_t SetContextDmaColor = 0x0194u;
    static constexpr std::uint32_t SetContextDmaZeta = 0x0198u;
    static constexpr std::uint32_t SetSurfaceClipHorizontal = 0x0200u;
    static constexpr std::uint32_t SetSurfaceClipVertical = 0x0204u;
    static constexpr std::uint32_t SetSurfaceFormat = 0x0208u;
    static constexpr std::uint32_t SetSurfacePitch = 0x020Cu;
    static constexpr std::uint32_t SetSurfaceColorOffset = 0x0210u;
    static constexpr std::uint32_t SetSurfaceZetaOffset = 0x0214u;
    static constexpr std::uint32_t SetZStencilClearValue = 0x1D8Cu;
    static constexpr std::uint32_t SetColorClearValue = 0x1D90u;
    static constexpr std::uint32_t ClearSurface = 0x1D94u;
    static constexpr std::uint32_t SetClearRectHorizontal = 0x1D98u;
    static constexpr std::uint32_t SetClearRectVertical = 0x1D9Cu;
};

enum class PgraphSurfaceStepKind
{
    Unhandled,
    StateUpdate,
    ClearSurface,
};

struct PgraphSurfaceState
{
    std::uint32_t contextDmaColor = 0;
    std::uint32_t contextDmaZeta = 0;
    std::uint32_t colorOffset = 0;
    std::uint32_t zetaOffset = 0;
    std::uint32_t colorPitch = 0;
    std::uint32_t zetaPitch = 0;
    std::uint32_t format = 0;
    std::uint32_t zetaFormat = 0;
    std::uint32_t clipX = 0;
    std::uint32_t clipY = 0;
    std::uint32_t clipWidth = 0;
    std::uint32_t clipHeight = 0;
    std::uint32_t zStencilClearValue = 0;
    std::uint32_t colorClearValue = 0;
    std::uint32_t clearRectHorizontal = 0;
    std::uint32_t clearRectVertical = 0;
};

struct PgraphSurfaceStep
{
    PgraphSurfaceStepKind kind = PgraphSurfaceStepKind::Unhandled;
    std::uint32_t data = 0;
};

[[nodiscard]] PgraphSurfaceStep ApplyPgraphSurfaceMethod(
    PgraphSurfaceState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

} // namespace cxbx::nv2a
