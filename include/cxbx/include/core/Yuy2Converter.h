#pragma once

#include <cstddef>
#include <cstdint>

namespace CxbxVideo
{
// Converts packed YUY2 rows to host D3D8's byte-ordered BGRA8 layout.
// Width may be odd; the final unpaired pixel is intentionally ignored.
bool ConvertYuy2ToBgra(const std::uint8_t* source,
                       std::size_t sourcePitch,
                       std::uint8_t* destination,
                       std::size_t destinationPitch,
                       std::uint32_t width,
                       std::uint32_t height);
} // namespace CxbxVideo
