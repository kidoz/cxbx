#pragma once

namespace cxbx::nv2a
{

struct ProjectedTextureCoordinates
{
    float u;
    float v;
    float interpolationWeight;
};

inline constexpr ProjectedTextureCoordinates ProjectTexture2D(
    float s, float t, float q, float inverseW) noexcept
{
    if(q > -1.0e-6f && q < 1.0e-6f)
    {
        return { s, t, inverseW };
    }

    return { s / q, t / q, q * inverseW };
}

} // namespace cxbx::nv2a
