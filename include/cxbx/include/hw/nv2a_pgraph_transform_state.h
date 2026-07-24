#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace cxbx::nv2a
{

inline constexpr std::uint32_t PgraphTransformVectorComponentCount = 4;
inline constexpr std::uint32_t PgraphTransformMatrixElementCount = 16;
inline constexpr std::uint32_t PgraphTransformProgramInstructionCount = 136;
inline constexpr std::uint32_t PgraphTransformProgramWordCount =
    PgraphTransformProgramInstructionCount *
    PgraphTransformVectorComponentCount;
inline constexpr std::uint32_t PgraphTransformConstantCount = 192;
inline constexpr std::uint32_t PgraphTransformConstantComponentCount =
    PgraphTransformConstantCount * PgraphTransformVectorComponentCount;

struct PgraphTransformMethod final
{
    static constexpr std::uint32_t SetCompositeMatrix0 = 0x0680u;
    static constexpr std::uint32_t SetViewportOffset0 = 0x0A20u;
    static constexpr std::uint32_t SetViewportScale0 = 0x0AF0u;
    static constexpr std::uint32_t SetTransformProgram0 = 0x0B00u;
    static constexpr std::uint32_t SetTransformConstant0 = 0x0B80u;
    static constexpr std::uint32_t SetTransformExecutionMode = 0x1E94u;
    static constexpr std::uint32_t SetTransformProgramLoad = 0x1E9Cu;
    static constexpr std::uint32_t SetTransformProgramStart = 0x1EA0u;
    static constexpr std::uint32_t SetTransformConstantLoad = 0x1EA4u;
};

struct PgraphTransformState
{
    std::array<float, PgraphTransformVectorComponentCount> viewportOffset{};
    std::array<float, PgraphTransformVectorComponentCount> viewportScale{
        1.0f, 1.0f, 1.0f, 1.0f
    };
    std::array<float, PgraphTransformMatrixElementCount> compositeMatrix{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    std::array<std::uint32_t, PgraphTransformProgramWordCount> program{};
    std::uint32_t programWriteWord = 0;
    std::uint32_t programInstructionCount = 0;
    std::uint32_t programStart = 0;
    std::uint32_t executionMode = 0;
    std::array<float, PgraphTransformConstantComponentCount> constants{};
    std::uint32_t constantWriteComponent = 0;
};

[[nodiscard]] bool ApplyPgraphTransformMethod(
    PgraphTransformState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

[[nodiscard]] bool SetPgraphTransformConstant(
    PgraphTransformState& state, std::uint32_t constantIndex,
    std::span<const float, PgraphTransformVectorComponentCount> value) noexcept;

} // namespace cxbx::nv2a
