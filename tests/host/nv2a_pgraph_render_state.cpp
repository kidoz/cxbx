#include "hw/nv2a_pgraph_render_state.h"

#include <cstdio>
#include <cstdint>

namespace
{

bool ExpectEqual(std::uint32_t actual, std::uint32_t expected,
                 const char* message) noexcept
{
    if(actual == expected)
    {
        return true;
    }

    std::fprintf(stderr, "%s: expected 0x%08X, got 0x%08X\n", message,
                 expected, actual);
    return false;
}

bool Expect(bool condition, const char* message) noexcept
{
    if(condition)
    {
        return true;
    }

    std::fprintf(stderr, "%s\n", message);
    return false;
}

} // namespace

int main() noexcept
{
    using cxbx::nv2a::PgraphRenderState;
    using cxbx::nv2a::PgraphRenderStateMethod;

    PgraphRenderState state{};
    if(!Expect(!state.depthTest && state.depthWrite &&
                   !state.alphaTest && !state.blendEnable &&
                   !state.stencilTest,
               "render-state enable defaults are invalid") ||
       !ExpectEqual(state.depthFunc, 0x0201u, "default depth function") ||
       !ExpectEqual(state.alphaFunc, 0x0207u, "default alpha function") ||
       !ExpectEqual(state.blendSourceFactor, 0x0001u,
                    "default source blend factor") ||
       !ExpectEqual(state.blendDestinationFactor, 0,
                    "default destination blend factor") ||
       !ExpectEqual(state.blendEquation, 0x8006u,
                    "default blend equation") ||
       !ExpectEqual(state.stencilFunc, 0x0207u,
                    "default stencil function") ||
       !ExpectEqual(state.stencilFuncMask, 0xFFu,
                    "default stencil function mask") ||
       !ExpectEqual(state.stencilMask, 0xFFu,
                    "default stencil write mask") ||
       !ExpectEqual(state.stencilOpFail, 0x1E00u,
                    "default stencil fail operation") ||
       !ExpectEqual(state.stencilOpZFail, 0x1E00u,
                    "default stencil depth-fail operation") ||
       !ExpectEqual(state.stencilOpZPass, 0x1E00u,
                    "default stencil depth-pass operation"))
    {
        return 1;
    }

    if(!cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetDepthTestEnable, 1) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetDepthMask, 0) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetDepthFunc, 0x0206u) ||
       !Expect(state.depthTest && !state.depthWrite,
               "depth enable/write transitions are invalid") ||
       !ExpectEqual(state.depthFunc, 0x0206u, "depth function"))
    {
        return 1;
    }

    if(!cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetAlphaTestEnable, 7) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetAlphaFunc, 0x0204u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetAlphaRef, 0x1234u) ||
       !Expect(state.alphaTest, "alpha-test enable transition is invalid") ||
       !ExpectEqual(state.alphaFunc, 0x0204u, "alpha function") ||
       !ExpectEqual(state.alphaRef, 0x34u, "masked alpha reference"))
    {
        return 1;
    }

    if(!cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetBlendEnable, 1) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetBlendSourceFactor,
           0x0302u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetBlendDestinationFactor,
           0x0303u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetBlendEquation, 0x800Au) ||
       !Expect(state.blendEnable, "blend-enable transition is invalid") ||
       !ExpectEqual(state.blendSourceFactor, 0x0302u,
                    "source blend factor") ||
       !ExpectEqual(state.blendDestinationFactor, 0x0303u,
                    "destination blend factor") ||
       !ExpectEqual(state.blendEquation, 0x800Au, "blend equation"))
    {
        return 1;
    }

    if(!cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilTestEnable, 1) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilFunc, 0x0205u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilFuncRef,
           0x12345678u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilFuncMask, 0x1357u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilMask, 0x2468u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilOpFail, 0x1E01u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilOpZFail, 0x1E02u) ||
       !cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetStencilOpZPass, 0x1E03u) ||
       !Expect(state.stencilTest,
               "stencil-test enable transition is invalid") ||
       !ExpectEqual(state.stencilFunc, 0x0205u, "stencil function") ||
       !ExpectEqual(state.stencilRef, 0x78u,
                    "masked stencil reference") ||
       !ExpectEqual(state.stencilFuncMask, 0x57u,
                    "masked stencil function mask") ||
       !ExpectEqual(state.stencilMask, 0x68u,
                    "masked stencil write mask") ||
       !ExpectEqual(state.stencilOpFail, 0x1E01u,
                    "stencil fail operation") ||
       !ExpectEqual(state.stencilOpZFail, 0x1E02u,
                    "stencil depth-fail operation") ||
       !ExpectEqual(state.stencilOpZPass, 0x1E03u,
                    "stencil depth-pass operation"))
    {
        return 1;
    }

    if(!cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, PgraphRenderStateMethod::SetAlphaRef | 0xE0000003u,
           0xABCDu) ||
       !ExpectEqual(state.alphaRef, 0xCDu,
                    "masked render-state method ID"))
    {
        return 1;
    }

    const std::uint32_t depthFuncBeforeUnknown = state.depthFunc;
    const std::uint32_t stencilMaskBeforeUnknown = state.stencilMask;
    if(cxbx::nv2a::ApplyPgraphRenderStateMethod(
           state, 0x00000400u, 0xFFFFFFFFu) ||
       !ExpectEqual(state.depthFunc, depthFuncBeforeUnknown,
                    "unknown method depth function") ||
       !ExpectEqual(state.stencilMask, stencilMaskBeforeUnknown,
                    "unknown method stencil mask"))
    {
        return 1;
    }

    return 0;
}
