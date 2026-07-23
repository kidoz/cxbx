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
    std::array<std::int16_t, 65> monoPcm{};
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
        {
            return 2;
        }
    }

    std::array<std::uint8_t, 72> stereo{};
    WriteHeader(stereo.data(), 1000, 0);
    WriteHeader(stereo.data() + 4, -1000, 0);
    std::array<std::int16_t, 130> stereoPcm{};
    if(!cxbx::audio::DecodeXboxAdpcm(
           stereo.data(), stereo.size(), 2,
           reinterpret_cast<std::uint8_t*>(stereoPcm.data()), sizeof(stereoPcm)))
    {
        return 3;
    }
    for(std::size_t frame = 0; frame < 65; ++frame)
    {
        if(stereoPcm[frame * 2] != 1000 ||
           stereoPcm[frame * 2 + 1] != -1000)
        {
            return 4;
        }
    }

    std::array<std::uint8_t, 72> twoBlocks{};
    WriteHeader(twoBlocks.data(), 10, 0);
    WriteHeader(twoBlocks.data() + mono.size(), 20, 0);
    std::array<std::int16_t, 130> twoBlockPcm{};
    if(!cxbx::audio::DecodeXboxAdpcm(
           twoBlocks.data(), twoBlocks.size(), 1,
           reinterpret_cast<std::uint8_t*>(twoBlockPcm.data()), sizeof(twoBlockPcm)) ||
       twoBlockPcm[0] != 10 || twoBlockPcm[64] != 10 ||
       twoBlockPcm[65] != 20 || twoBlockPcm[129] != 20)
    {
        return 5;
    }

    mono[2] = 0;
    mono[35] = 0x70;
    if(!cxbx::audio::DecodeXboxAdpcm(
           mono.data(), mono.size(), 1,
           reinterpret_cast<std::uint8_t*>(monoPcm.data()), sizeof(monoPcm)) ||
       monoPcm[63] != 1234 || monoPcm[64] == 1234)
    {
        return 6;
    }

    if(cxbx::audio::XboxAdpcmEncodedBlockBytes(2) != 72 ||
       cxbx::audio::XboxAdpcmDecodedBlockBytes(2) != 260 ||
       cxbx::audio::XboxAdpcmDecodedBytes(0x1ffe0, 2) != 0x73870 ||
       cxbx::audio::XboxAdpcmGuestToPcmBytes(72 * 7 + 71, 2) != 260 * 7 ||
       cxbx::audio::XboxAdpcmPcmToGuestBytes(260 * 7 + 259, 2) != 72 * 7)
    {
        return 7;
    }

    mono[2] = 89;
    if(cxbx::audio::DecodeXboxAdpcm(
           mono.data(), mono.size(), 1,
           reinterpret_cast<std::uint8_t*>(monoPcm.data()), sizeof(monoPcm)) ||
       cxbx::audio::XboxAdpcmDecodedBytes(35, 1) != 0 ||
       cxbx::audio::XboxAdpcmDecodedBytes(36, 3) != 0)
    {
        return 8;
    }

    return 0;
}
