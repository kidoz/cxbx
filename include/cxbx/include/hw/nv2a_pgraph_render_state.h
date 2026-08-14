#pragma once

#include <cstdint>

namespace cxbx::nv2a
{

struct PgraphRenderStateMethod final
{
    static constexpr std::uint32_t SetAlphaTestEnable = 0x0300u;
    static constexpr std::uint32_t SetBlendEnable = 0x0304u;
    static constexpr std::uint32_t SetDepthTestEnable = 0x030Cu;
    static constexpr std::uint32_t SetStencilTestEnable = 0x032Cu;
    static constexpr std::uint32_t SetAlphaFunc = 0x033Cu;
    static constexpr std::uint32_t SetAlphaRef = 0x0340u;
    static constexpr std::uint32_t SetBlendSourceFactor = 0x0344u;
    static constexpr std::uint32_t SetBlendDestinationFactor = 0x0348u;
    static constexpr std::uint32_t SetBlendEquation = 0x0350u;
    static constexpr std::uint32_t SetDepthFunc = 0x0354u;
    static constexpr std::uint32_t SetDepthMask = 0x035Cu;
    static constexpr std::uint32_t SetStencilMask = 0x0360u;
    static constexpr std::uint32_t SetStencilFunc = 0x0364u;
    static constexpr std::uint32_t SetStencilFuncRef = 0x0368u;
    static constexpr std::uint32_t SetStencilFuncMask = 0x036Cu;
    static constexpr std::uint32_t SetStencilOpFail = 0x0370u;
    static constexpr std::uint32_t SetStencilOpZFail = 0x0374u;
    static constexpr std::uint32_t SetStencilOpZPass = 0x0378u;
    static constexpr std::uint32_t SetPolyOffsetPointEnable = 0x0330u;
    static constexpr std::uint32_t SetPolyOffsetLineEnable = 0x0334u;
    static constexpr std::uint32_t SetPolyOffsetFillEnable = 0x0338u;
    static constexpr std::uint32_t SetBlendColor = 0x034Cu;
    static constexpr std::uint32_t SetPolygonOffsetScaleFactor = 0x0384u;
    static constexpr std::uint32_t SetPolygonOffsetBias = 0x0388u;
    static constexpr std::uint32_t SetZMinMaxControl = 0x1D78u;
    static constexpr std::uint32_t SetColorClearValue = 0x1D90u;
};

struct PgraphRenderState
{
    bool depthTest = false;
    bool depthWrite = true;
    std::uint32_t depthFunc = 0x0201u;
    bool alphaTest = false;
    std::uint32_t alphaFunc = 0x0207u;
    std::uint32_t alphaRef = 0;
    bool blendEnable = false;
    std::uint32_t blendSourceFactor = 0x0001u;
    std::uint32_t blendDestinationFactor = 0;
    std::uint32_t blendEquation = 0x8006u;
    bool stencilTest = false;
    std::uint32_t stencilFunc = 0x0207u;
    std::uint32_t stencilRef = 0;
    std::uint32_t stencilFuncMask = 0xFFu;
    std::uint32_t stencilMask = 0xFFu;
    std::uint32_t stencilOpFail = 0x1E00u;
    std::uint32_t stencilOpZFail = 0x1E00u;
    std::uint32_t stencilOpZPass = 0x1E00u;
    bool polyOffsetPoint = false;
    bool polyOffsetLine = false;
    bool polyOffsetFill = false;
    std::uint32_t blendColor = 0;
    std::uint32_t polygonOffsetScaleFactor = 0;
    std::uint32_t polygonOffsetBias = 0;
    std::uint32_t zMinMaxControl = 0;
    std::uint32_t colorClearValue = 0;
};

[[nodiscard]] bool ApplyPgraphRenderStateMethod(
    PgraphRenderState& state, std::uint32_t method,
    std::uint32_t data) noexcept;

} // namespace cxbx::nv2a
