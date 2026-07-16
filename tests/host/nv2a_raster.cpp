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

    return 0;
}
