#include "core/xbox_adpcm_decoder.h"

#include <array>
#include <cstdint>

namespace
{
void WriteHeader(std::uint8_t* destination, std::int16_t predictor, std::uint8_t index)
{
    destination[0] = static_cast<std::uint8_t>(predictor);
    destination[1] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(predictor) >> 8);
    destination[2] = index;
    destination[3] = 0;
}
} // namespace

int main()
{
    std::array<std::uint8_t, 36> mono{};
    WriteHeader(mono.data(), 1234, 0);
    std::array<std::int16_t, 64> monoPcm{};
    if(cxbx::audio::XboxAdpcmDecodedBytes(mono.size(), 1) !=
           monoPcm.size() * sizeof(std::int16_t) ||
       !cxbx::audio::DecodeXboxAdpcm(
           mono.data(), mono.size(), 1,
           reinterpret_cast<std::uint8_t*>(monoPcm.data()), sizeof(monoPcm)))
    {
        return 1;
    }
    for(const std::int16_t sample : monoPcm)
    {
        if(sample != 1234)
            return 2;
    }

    std::array<std::uint8_t, 72> stereo{};
    WriteHeader(stereo.data(), 1000, 0);
    WriteHeader(stereo.data() + 4, -1000, 0);
    std::array<std::int16_t, 128> stereoPcm{};
    if(!cxbx::audio::DecodeXboxAdpcm(
           stereo.data(), stereo.size(), 2,
           reinterpret_cast<std::uint8_t*>(stereoPcm.data()), sizeof(stereoPcm)))
    {
        return 3;
    }
    for(std::size_t frame = 0; frame < 64; ++frame)
    {
        if(stereoPcm[frame * 2] != 1000 || stereoPcm[frame * 2 + 1] != -1000)
            return 4;
    }

    std::array<std::uint8_t, 72> twoBlocks{};
    WriteHeader(twoBlocks.data(), 10, 0);
    WriteHeader(twoBlocks.data() + mono.size(), 20, 0);
    std::array<std::int16_t, 128> twoBlockPcm{};
    if(!cxbx::audio::DecodeXboxAdpcm(
           twoBlocks.data(), twoBlocks.size(), 1,
           reinterpret_cast<std::uint8_t*>(twoBlockPcm.data()), sizeof(twoBlockPcm)) ||
       twoBlockPcm[0] != 10 || twoBlockPcm[63] != 10 ||
       twoBlockPcm[64] != 20 || twoBlockPcm[127] != 20)
    {
        return 5;
    }

    mono[2] = 89;
    if(cxbx::audio::DecodeXboxAdpcm(
           mono.data(), mono.size(), 1,
           reinterpret_cast<std::uint8_t*>(monoPcm.data()), sizeof(monoPcm)) ||
       cxbx::audio::XboxAdpcmDecodedBytes(35, 1) != 0 ||
       cxbx::audio::XboxAdpcmDecodedBytes(36, 3) != 0)
    {
        return 6;
    }

    return 0;
}
