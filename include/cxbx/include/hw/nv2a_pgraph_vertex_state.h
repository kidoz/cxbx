#pragma once

#include <array>
#include <cstdint>

namespace cxbx::nv2a
{

inline constexpr std::uint32_t PgraphVertexAttributeCount = 16;

struct PgraphVertexMethod final
{
    static constexpr std::uint32_t SetContextDmaVertexA = 0x019Cu;
    static constexpr std::uint32_t SetVertexDataArrayOffset0 = 0x1720u;
    static constexpr std::uint32_t SetVertexDataArrayFormat0 = 0x1760u;
};

struct PgraphVertexArrayState
{
    std::uint32_t offset = 0;
    std::uint32_t format = 0;
};

struct PgraphVertexState
{
    std::uint32_t contextDmaVertex = 0;
    std::array<PgraphVertexArrayState, PgraphVertexAttributeCount> arrays{};
};

struct PgraphVertexArrayFormat
{
    std::uint32_t type = 0;
    std::uint32_t componentCount = 0;
    std::uint32_t stride = 0;
};

[[nodiscard]] bool ApplyPgraphVertexStateMethod(
    PgraphVertexState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

[[nodiscard]] constexpr PgraphVertexArrayFormat DecodePgraphVertexArrayFormat(
    std::uint32_t format) noexcept
{
    return {
        format & 0x0Fu,
        (format >> 4) & 0x0Fu,
        (format >> 8) & 0xFFu,
    };
}

} // namespace cxbx::nv2a
