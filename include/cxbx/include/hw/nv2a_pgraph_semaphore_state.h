#pragma once

#include <cstdint>

namespace cxbx::nv2a
{

struct PgraphSemaphoreMethod final
{
    static constexpr std::uint32_t SetContextDmaSemaphore = 0x01A4u;
    static constexpr std::uint32_t SetSemaphoreOffset = 0x1D6Cu;
    static constexpr std::uint32_t BackendWriteSemaphoreRelease = 0x1D70u;
};

struct PgraphSemaphoreState
{
    std::uint32_t contextDmaSemaphore = 0;
    std::uint32_t offset = 0;
};

struct PgraphSemaphoreRelease
{
    std::uint32_t contextDmaSemaphore = 0;
    std::uint32_t offset = 0;
    std::uint32_t value = 0;
};

enum class PgraphSemaphoreStepKind : std::uint8_t
{
    Unhandled,
    State,
    Release,
};

struct PgraphSemaphoreStep
{
    PgraphSemaphoreStepKind kind = PgraphSemaphoreStepKind::Unhandled;
    PgraphSemaphoreRelease release{};
};

[[nodiscard]] PgraphSemaphoreStep ApplyPgraphSemaphoreMethod(
    PgraphSemaphoreState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

} // namespace cxbx::nv2a
