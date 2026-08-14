#include "hw/nv2a_pgraph_render_state.h"

namespace cxbx::nv2a
{

namespace
{

constexpr std::uint32_t MethodMask = 0x1FFCu;
constexpr std::uint32_t StencilValueMask = 0xFFu;

} // namespace

bool ApplyPgraphRenderStateMethod(
    PgraphRenderState& state, std::uint32_t method,
    std::uint32_t data) noexcept
{
    switch(method & MethodMask)
    {
        case PgraphRenderStateMethod::SetAlphaTestEnable:
            state.alphaTest = data != 0;
            break;
        case PgraphRenderStateMethod::SetAlphaFunc:
            state.alphaFunc = data;
            break;
        case PgraphRenderStateMethod::SetAlphaRef:
            state.alphaRef = data & StencilValueMask;
            break;
        case PgraphRenderStateMethod::SetDepthTestEnable:
            state.depthTest = data != 0;
            break;
        case PgraphRenderStateMethod::SetDepthFunc:
            state.depthFunc = data;
            break;
        case PgraphRenderStateMethod::SetDepthMask:
            state.depthWrite = data != 0;
            break;
        case PgraphRenderStateMethod::SetBlendEnable:
            state.blendEnable = data != 0;
            break;
        case PgraphRenderStateMethod::SetBlendSourceFactor:
            state.blendSourceFactor = data;
            break;
        case PgraphRenderStateMethod::SetBlendDestinationFactor:
            state.blendDestinationFactor = data;
            break;
        case PgraphRenderStateMethod::SetBlendEquation:
            state.blendEquation = data;
            break;
        case PgraphRenderStateMethod::SetStencilTestEnable:
            state.stencilTest = data != 0;
            break;
        case PgraphRenderStateMethod::SetStencilMask:
            state.stencilMask = data & StencilValueMask;
            break;
        case PgraphRenderStateMethod::SetStencilFunc:
            state.stencilFunc = data;
            break;
        case PgraphRenderStateMethod::SetStencilFuncRef:
            state.stencilRef = data & StencilValueMask;
            break;
        case PgraphRenderStateMethod::SetStencilFuncMask:
            state.stencilFuncMask = data & StencilValueMask;
            break;
        case PgraphRenderStateMethod::SetStencilOpFail:
            state.stencilOpFail = data;
            break;
        case PgraphRenderStateMethod::SetStencilOpZFail:
            state.stencilOpZFail = data;
            break;
        case PgraphRenderStateMethod::SetStencilOpZPass:
            state.stencilOpZPass = data;
            break;
        case PgraphRenderStateMethod::SetPolyOffsetPointEnable:
            state.polyOffsetPoint = data != 0;
            break;
        case PgraphRenderStateMethod::SetPolyOffsetLineEnable:
            state.polyOffsetLine = data != 0;
            break;
        case PgraphRenderStateMethod::SetPolyOffsetFillEnable:
            state.polyOffsetFill = data != 0;
            break;
        case PgraphRenderStateMethod::SetBlendColor:
            state.blendColor = data;
            break;
        case PgraphRenderStateMethod::SetPolygonOffsetScaleFactor:
            state.polygonOffsetScaleFactor = data;
            break;
        case PgraphRenderStateMethod::SetPolygonOffsetBias:
            state.polygonOffsetBias = data;
            break;
        case PgraphRenderStateMethod::SetZMinMaxControl:
            state.zMinMaxControl = data;
            break;
        case PgraphRenderStateMethod::SetColorClearValue:
            state.colorClearValue = data;
            break;
        default:
            return false;
    }

    return true;
}

} // namespace cxbx::nv2a
