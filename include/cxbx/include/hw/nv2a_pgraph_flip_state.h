#pragma once

#include <cstdint>

namespace cxbx::nv2a
{

struct PgraphFlipMethod final
{
    static constexpr std::uint32_t SetFlipRead = 0x0120u;
    static constexpr std::uint32_t SetFlipWrite = 0x0124u;
    static constexpr std::uint32_t SetFlipModulo = 0x0128u;
    static constexpr std::uint32_t FlipIncrementWrite = 0x012Cu;
    static constexpr std::uint32_t FlipStall = 0x0130u;
};

struct PgraphFlipState
{
    std::uint32_t readIndex = 0;
    std::uint32_t writeIndex = 0;
    std::uint32_t modulo = 1;
};

enum class PgraphFlipStepKind : std::uint8_t
{
    Unhandled,
    State,
    Present,
};

struct PgraphFlipStep
{
    PgraphFlipStepKind kind = PgraphFlipStepKind::Unhandled;
};

[[nodiscard]] PgraphFlipStep ApplyPgraphFlipMethod(
    PgraphFlipState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

} // namespace cxbx::nv2a
