#pragma once

#include <array>
#include <cstdint>

namespace cxbx::nv2a
{

inline constexpr std::uint32_t PgraphCombinerStageCount = 8;

struct PgraphCombinerMethod final
{
    static constexpr std::uint32_t SetAlphaIcwStage0 = 0x0260u;
    static constexpr std::uint32_t SetFinalCombinerCw0 = 0x0288u;
    static constexpr std::uint32_t SetFinalCombinerCw1 = 0x028Cu;
    static constexpr std::uint32_t SetFactor0Stage0 = 0x0A60u;
    static constexpr std::uint32_t SetFactor1Stage0 = 0x0A80u;
    static constexpr std::uint32_t SetAlphaOcwStage0 = 0x0AA0u;
    static constexpr std::uint32_t SetColorIcwStage0 = 0x0AC0u;
    static constexpr std::uint32_t SetColorOcwStage0 = 0x1E40u;
    static constexpr std::uint32_t SetCombinerControl = 0x1E60u;
    static constexpr std::uint32_t SetShaderStageProgram = 0x1E70u;
};

struct PgraphCombinerStageState
{
    std::uint32_t alphaIcw = 0;
    std::uint32_t colorIcw = 0;
    std::uint32_t alphaOcw = 0;
    std::uint32_t colorOcw = 0;
    std::uint32_t factor0 = 0;
    std::uint32_t factor1 = 0;
};

struct PgraphCombinerState
{
    std::array<PgraphCombinerStageState, PgraphCombinerStageCount> stages{};
    std::uint32_t control = 0;
    std::uint32_t shaderStageProgram = 0;
    std::uint32_t finalCombinerCw0 = 0;
    std::uint32_t finalCombinerCw1 = 0;
    std::uint32_t finalCombinerMask = 0;
};

[[nodiscard]] bool ApplyPgraphCombinerStateMethod(
    PgraphCombinerState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

[[nodiscard]] bool IsPgraphFinalCombinerComplete(
    const PgraphCombinerState& state) noexcept;

} // namespace cxbx::nv2a
