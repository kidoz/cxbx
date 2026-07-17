#include "core/nv2a_raster.h"

#include <cstdio>

namespace
{

constexpr bool Near(float lhs, float rhs)
{
    const float difference = lhs - rhs;
    return difference > -1.0e-6f && difference < 1.0e-6f;
}

} // namespace

int main()
{
    constexpr auto projected =
        cxbx::nv2a::ProjectTexture2D(0.5f, 0.25f, 2.0f, 0.5f);
    if(!Near(projected.u, 0.25f) || !Near(projected.v, 0.125f) ||
       !Near(projected.interpolationWeight, 1.0f))
    {
        std::fputs("PROJECT2D must divide s and t by q and interpolate with q/w\n",
                   stderr);
        return 1;
    }

    constexpr auto unprojected =
        cxbx::nv2a::ProjectTexture2D(0.5f, 0.25f, 0.0f, 0.5f);
    if(!Near(unprojected.u, 0.5f) || !Near(unprojected.v, 0.25f) ||
       !Near(unprojected.interpolationWeight, 0.5f))
    {
        std::fputs("zero q must preserve legacy texture coordinates\n", stderr);
        return 1;
    }

    if(!cxbx::nv2a::IsFinalCombinerPassthroughR0(
           0x0000000Cu, 0x00001C80u) ||
       cxbx::nv2a::IsFinalCombinerPassthroughR0(
           0x0000000Cu, 0x00001480u))
    {
        std::fputs("only an r0 RGB/alpha final combiner may be bypassed\n",
                   stderr);
        return 1;
    }

    if(cxbx::nv2a::BlendSourceAlpha(0x80FF0000u, 0xFF0000FFu) !=
       0xBF80007Fu ||
       cxbx::nv2a::BlendSourceAlpha(0x00FFFFFFu, 0x12345678u) !=
       0x12345678u ||
       cxbx::nv2a::BlendSourceAlpha(0xFFFFFFFFu, 0x12345678u) !=
       0xFFFFFFFFu)
    {
        std::fputs("source-alpha blend fast path must match ADD blending\n",
                   stderr);
        return 1;
    }

    constexpr auto affineSpan = cxbx::nv2a::BuildAffineQuadSpan(
        0.0f, 10.0f, 30.0f, 20.0f, 0.25f, 0.2f, 0.2f);
    if(!Near(affineSpan.value, 7.0f) || !Near(affineSpan.step, 2.0f) ||
       !cxbx::nv2a::CanUseAffineQuadInterpolation(
           0.5f, 0.5f, 0.5f, 0.5f) ||
       cxbx::nv2a::CanUseAffineQuadInterpolation(
           0.5f, 0.5f, 0.4f, 0.5f) ||
       cxbx::nv2a::CanUseAffineQuadInterpolation(
           0.0f, 0.0f, 0.0f, 0.0f))
    {
        std::fputs("affine quad interpolation must require constant nonzero weights\n",
                   stderr);
        return 1;
    }

    cxbx::nv2a::FinalCombinerRegisters registers = {};
    registers.r0 = 0x7A123456u;
    if(cxbx::nv2a::RunFinalCombiner(0x0000000Cu, 0x00001C80u,
                                    registers) != registers.r0)
    {
        std::fputs("Samurai final combiner must preserve r0\n", stderr);
        return 1;
    }

    registers = {};
    registers.primary = 0x80FFFFFFu;
    registers.constant0 = 0x00010101u;
    registers.constant1 = 0x000000FFu;
    registers.texture0 = 0x00FF0000u;
    registers.r0 = 0x44000000u;
    if(cxbx::nv2a::RunFinalCombiner(0x14080201u, 0x00001C00u,
                                    registers) != 0x44810180u)
    {
        std::fputs("final combiner must evaluate A*B+(1-A)*C+D\n", stderr);
        return 1;
    }

    registers = {};
    registers.primary = 0xFF804020u;
    registers.texture0 = 0xFF4080FFu;
    registers.r0 = 0x55000000u;
    if(cxbx::nv2a::RunFinalCombiner(0x200F0000u, 0x04081C00u,
                                    registers) != 0x55202020u)
    {
        std::fputs("final combiner must expose the E*F product\n", stderr);
        return 1;
    }

    return 0;
}
