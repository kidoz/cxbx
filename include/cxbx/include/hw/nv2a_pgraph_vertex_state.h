#pragma once

#include <array>
#include <cstdint>

namespace cxbx::nv2a
{

inline constexpr std::uint32_t PgraphVertexAttributeCount = 16;
inline constexpr std::uint32_t PgraphVertexAttributeComponentCount = 4;
inline constexpr std::uint32_t PgraphVertexPositionAttribute = 0;
inline constexpr std::uint32_t PgraphVertexDiffuseAttribute = 3;
inline constexpr std::uint32_t PgraphVertexTexcoord0Attribute = 9;
inline constexpr std::uint32_t PgraphFixedFunctionTextureCount = 4;
inline constexpr std::uint32_t PgraphVertexProgramInputComponentCount =
    PgraphVertexAttributeCount * PgraphVertexAttributeComponentCount;
static_assert(PgraphVertexTexcoord0Attribute +
                  PgraphFixedFunctionTextureCount <=
              PgraphVertexAttributeCount);
inline constexpr std::uint32_t PgraphVertexLayoutUnusedOffset = 0xFFFFFFFFu;
inline constexpr std::uint32_t PgraphImmediateVertexAttributeStride =
    PgraphVertexAttributeComponentCount * sizeof(std::uint32_t);
inline constexpr std::uint32_t PgraphImmediateVertexStride =
    PgraphVertexAttributeCount * PgraphImmediateVertexAttributeStride;

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

struct PgraphVertexLayout
{
    PgraphVertexState vertexState{};
    std::array<std::uint32_t, PgraphVertexAttributeCount> offsets{};
    std::uint32_t stride = 0;
};

enum class PgraphVertexFetchSource : std::uint8_t
{
    Disabled,
    VertexArray,
    Inline,
};

struct PgraphVertexAttributeFetch
{
    PgraphVertexFetchSource source = PgraphVertexFetchSource::Disabled;
    std::uint32_t offset = 0;
    std::uint32_t stride = 0;
    std::uint32_t rawFormat = 0;
    std::uint32_t type = 0;
    std::uint32_t componentCount = 0;
};

struct PgraphVertexFetchPlan
{
    std::uint32_t contextDmaVertex = 0;
    std::array<PgraphVertexAttributeFetch, PgraphVertexAttributeCount>
        attributes{};
};

enum class PgraphVertexAttributeReadPurpose : std::uint8_t
{
    VertexProgram,
    FixedPosition,
    FixedDiffuse,
    FixedTexture,
};

struct PgraphVertexAttributeReadPlan
{
    std::uint32_t componentMask = 0;
    std::uint32_t decodeComponentCount = 0;
    std::uint8_t initialByte = 0;
};

using PgraphVertexAttributeBytes =
    std::array<std::uint8_t, PgraphImmediateVertexAttributeStride>;
using PgraphVertexComponents =
    std::array<float, PgraphVertexAttributeComponentCount>;

inline constexpr PgraphVertexComponents PgraphDefaultVertexComponents = {
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};

struct PgraphVertexAttributeValue
{
    PgraphVertexComponents components = PgraphDefaultVertexComponents;
    std::uint32_t packedColor = 0;
};

using PgraphVertexAttributeValues =
    std::array<PgraphVertexAttributeValue, PgraphVertexAttributeCount>;
using PgraphVertexProgramInput =
    std::array<float, PgraphVertexProgramInputComponentCount>;

struct PgraphFixedFunctionVertexInput
{
    PgraphVertexComponents position = PgraphDefaultVertexComponents;
    std::uint32_t diffuseColor = 0xFFFFFFFFu;
    std::array<PgraphVertexComponents, PgraphFixedFunctionTextureCount>
        textureCoordinates = {
            PgraphDefaultVertexComponents,
            PgraphDefaultVertexComponents,
            PgraphDefaultVertexComponents,
            PgraphDefaultVertexComponents,
        };
};

[[nodiscard]] bool ApplyPgraphVertexStateMethod(
    PgraphVertexState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

[[nodiscard]] std::uint32_t GetPgraphVertexComponentSize(
    std::uint32_t type) noexcept;

[[nodiscard]] bool BuildPgraphInlineVertexLayout(
    const PgraphVertexState& state, PgraphVertexLayout& layout) noexcept;

[[nodiscard]] PgraphVertexLayout BuildPgraphImmediateVertexLayout(
    const PgraphVertexState& state,
    const std::array<std::uint32_t, PgraphVertexAttributeCount>&
        immediateFormats) noexcept;

[[nodiscard]] PgraphVertexFetchPlan BuildPgraphVertexFetchPlan(
    const PgraphVertexState& state) noexcept;

[[nodiscard]] PgraphVertexFetchPlan BuildPgraphInlineVertexFetchPlan(
    const PgraphVertexLayout& layout) noexcept;

[[nodiscard]] PgraphVertexAttributeReadPlan
BuildPgraphVertexAttributeReadPlan(
    const PgraphVertexAttributeFetch& fetch,
    PgraphVertexAttributeReadPurpose purpose) noexcept;

[[nodiscard]] PgraphVertexAttributeBytes
InitializePgraphVertexAttributeBytes(
    const PgraphVertexAttributeReadPlan& plan) noexcept;

[[nodiscard]] PgraphVertexAttributeValue DecodePgraphFloatVertexAttribute(
    const PgraphVertexAttributeBytes& bytes,
    std::uint32_t componentCount) noexcept;

[[nodiscard]] PgraphVertexAttributeValue DecodePgraphPackedColorVertexAttribute(
    const PgraphVertexAttributeBytes& bytes) noexcept;

[[nodiscard]] PgraphVertexAttributeValue DecodePgraphVertexAttribute(
    const PgraphVertexAttributeFetch& fetch,
    const PgraphVertexAttributeBytes& bytes) noexcept;

[[nodiscard]] PgraphVertexProgramInput BuildPgraphVertexProgramInput(
    const PgraphVertexAttributeValues& attributeValues,
    std::uint32_t suppliedAttributeMask) noexcept;

[[nodiscard]] PgraphFixedFunctionVertexInput
BuildPgraphFixedFunctionVertexInput(
    const PgraphVertexAttributeValues& attributeValues,
    const PgraphVertexFetchPlan& fetchPlan,
    std::uint32_t suppliedAttributeMask) noexcept;

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
