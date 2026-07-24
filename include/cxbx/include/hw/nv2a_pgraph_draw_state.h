#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cxbx::nv2a
{

inline constexpr std::size_t PgraphDrawIndexCapacity = 4096;

struct PgraphDrawMethod final
{
    static constexpr std::uint32_t SetBeginEnd = 0x17FCu;
    static constexpr std::uint32_t ArrayElement16 = 0x1800u;
    static constexpr std::uint32_t ArrayElement32 = 0x1808u;
    static constexpr std::uint32_t DrawArrays = 0x1810u;
};

struct PgraphDrawState
{
    std::uint32_t beginOp = 0;
    std::array<std::uint32_t, PgraphDrawIndexCapacity> elementIndices{};
    std::size_t elementIndexCount = 0;
};

enum class PgraphDrawActionKind : std::uint8_t
{
    None,
    Direct,
    Indexed,
};

struct PgraphDrawAction
{
    PgraphDrawActionKind kind = PgraphDrawActionKind::None;
    std::uint32_t beginOp = 0;
    std::uint32_t startVertex = 0;
    std::size_t vertexCount = 0;
};

enum class PgraphDrawStepKind : std::uint8_t
{
    Unhandled,
    State,
    End,
    Draw,
};

struct PgraphDrawStep
{
    PgraphDrawStepKind kind = PgraphDrawStepKind::Unhandled;
    PgraphDrawAction action{};
};

[[nodiscard]] PgraphDrawStep ApplyPgraphDrawMethod(
    PgraphDrawState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

} // namespace cxbx::nv2a
