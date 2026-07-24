#pragma once

#include "hw/nv2a_pgraph_vertex_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cxbx::nv2a
{

inline constexpr std::size_t PgraphVertexSubmissionWordCapacity = 65536;
inline constexpr std::size_t PgraphImmediateComponentCount = 4;
inline constexpr std::size_t PgraphImmediateWordsPerVertex =
    PgraphVertexAttributeCount * PgraphImmediateComponentCount;

inline constexpr std::uint32_t PgraphImmediateFormatFloat2 = 0x22u;
inline constexpr std::uint32_t PgraphImmediateFormatUnsignedByte4 = 0x40u;
inline constexpr std::uint32_t PgraphImmediateFormatFloat4 = 0x42u;

struct PgraphVertexSubmissionMethod final
{
    static constexpr std::uint32_t SetVertex4F = 0x1518u;
    static constexpr std::uint32_t InlineArray = 0x1818u;
    static constexpr std::uint32_t SetVertexData2FM = 0x1880u;
    static constexpr std::uint32_t SetVertexData4Ub = 0x1940u;
    static constexpr std::uint32_t SetVertexData4FM = 0x1A00u;
};

struct PgraphVertexSubmissionState
{
    std::array<std::uint32_t, PgraphVertexSubmissionWordCapacity> inlineWords{};
    std::size_t inlineWordCount = 0;
    bool inlineOverflow = false;
    std::array<std::uint32_t, PgraphVertexSubmissionWordCapacity> immediateWords{};
    std::size_t immediateWordCount = 0;
    bool immediateOverflow = false;
    std::array<
        std::array<std::uint32_t, PgraphImmediateComponentCount>,
        PgraphVertexAttributeCount>
        immediateAttributes{};
    std::array<std::uint32_t, PgraphVertexAttributeCount> immediateFormats{};
};

enum class PgraphVertexSubmissionStepKind : std::uint8_t
{
    Unhandled,
    State,
    InlineWord,
    ImmediateVertex,
    Overflow,
};

struct PgraphVertexSubmissionStep
{
    PgraphVertexSubmissionStepKind kind =
        PgraphVertexSubmissionStepKind::Unhandled;
    std::size_t wordOffset = 0;
    std::size_t wordCount = 0;
};

enum class PgraphVertexBatchKind : std::uint8_t
{
    None,
    Inline,
    Immediate,
};

struct PgraphVertexBatchAction
{
    PgraphVertexBatchKind kind = PgraphVertexBatchKind::None;
    std::size_t wordCount = 0;
    bool overflow = false;
};

[[nodiscard]] PgraphVertexSubmissionStep ApplyPgraphVertexSubmissionMethod(
    PgraphVertexSubmissionState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

[[nodiscard]] PgraphVertexBatchAction SnapshotPgraphVertexBatch(
    const PgraphVertexSubmissionState& state) noexcept;

void ResetPgraphVertexSubmissionBatch(
    PgraphVertexSubmissionState& state) noexcept;

} // namespace cxbx::nv2a
