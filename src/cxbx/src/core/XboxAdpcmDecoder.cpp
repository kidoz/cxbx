#include "core/XboxAdpcmDecoder.h"

#include <array>
#include <climits>
#include <cstring>

namespace cxbx::audio
{
namespace
{
constexpr std::size_t kHeaderBytesPerChannel = 4;
constexpr std::size_t kEncodedBytesPerChannelBlock = 36;

constexpr std::array<int, 16> kIndexAdjust = {
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
};

constexpr std::array<int, 89> kStepTable = {
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    16,
    17,
    19,
    21,
    23,
    25,
    28,
    31,
    34,
    37,
    41,
    45,
    50,
    55,
    60,
    66,
    73,
    80,
    88,
    97,
    107,
    118,
    130,
    143,
    157,
    173,
    190,
    209,
    230,
    253,
    279,
    307,
    337,
    371,
    408,
    449,
    494,
    544,
    598,
    658,
    724,
    796,
    876,
    963,
    1060,
    1166,
    1282,
    1411,
    1552,
    1707,
    1878,
    2066,
    2272,
    2499,
    2749,
    3024,
    3327,
    3660,
    4026,
    4428,
    4871,
    5358,
    5894,
    6484,
    7132,
    7845,
    8630,
    9493,
    10442,
    11487,
    12635,
    13899,
    15289,
    16818,
    18500,
    20350,
    22385,
    24623,
    27086,
    29794,
    32767,
};

struct DecoderState
{
    int Predictor;
    int StepIndex;
};

std::int16_t ReadInt16(const std::uint8_t* source) noexcept
{
    const std::uint16_t value = static_cast<std::uint16_t>(source[0]) |
                                (static_cast<std::uint16_t>(source[1]) << 8);
    return static_cast<std::int16_t>(value);
}

void WriteInt16(std::uint8_t*& destination, std::int16_t value) noexcept
{
    std::memcpy(destination, &value, sizeof(value));
    destination += sizeof(value);
}

bool ReadHeader(const std::uint8_t* source, DecoderState& state) noexcept
{
    state.Predictor = ReadInt16(source);
    state.StepIndex = source[2];
    return state.StepIndex >= 0 &&
           state.StepIndex < static_cast<int>(kStepTable.size());
}

std::int16_t DecodeNibble(DecoderState& state, std::uint8_t nibble) noexcept
{
    const int step = kStepTable[state.StepIndex];
    int difference = step >> 3;
    if((nibble & 4) != 0)
        difference += step;
    if((nibble & 2) != 0)
        difference += step >> 1;
    if((nibble & 1) != 0)
        difference += step >> 2;
    if((nibble & 8) != 0)
        difference = -difference;

    state.Predictor += difference;
    if(state.Predictor > INT16_MAX)
        state.Predictor = INT16_MAX;
    else if(state.Predictor < INT16_MIN)
        state.Predictor = INT16_MIN;

    state.StepIndex += kIndexAdjust[nibble & 0x0f];
    if(state.StepIndex < 0)
        state.StepIndex = 0;
    else if(state.StepIndex >= static_cast<int>(kStepTable.size()))
        state.StepIndex = static_cast<int>(kStepTable.size()) - 1;

    return static_cast<std::int16_t>(state.Predictor);
}

bool DecodeMonoBlock(const std::uint8_t* source,
                     std::uint8_t*& destination) noexcept
{
    DecoderState state{};
    if(!ReadHeader(source, state))
        return false;

    WriteInt16(destination, static_cast<std::int16_t>(state.Predictor));
    source += kHeaderBytesPerChannel;
    for(std::size_t sample = 1; sample < kXboxAdpcmSamplesPerBlock; ++sample)
    {
        const std::uint8_t packed = source[(sample - 1) / 2];
        const unsigned shift = ((sample - 1) & 1) != 0 ? 4 : 0;
        WriteInt16(destination, DecodeNibble(state, packed >> shift));
    }
    return true;
}

bool DecodeStereoBlock(const std::uint8_t* source,
                       std::uint8_t*& destination) noexcept
{
    DecoderState left{};
    DecoderState right{};
    if(!ReadHeader(source, left) ||
       !ReadHeader(source + kHeaderBytesPerChannel, right))
    {
        return false;
    }

    WriteInt16(destination, static_cast<std::int16_t>(left.Predictor));
    WriteInt16(destination, static_cast<std::int16_t>(right.Predictor));
    source += 2 * kHeaderBytesPerChannel;

    for(std::size_t sample = 1; sample < kXboxAdpcmSamplesPerBlock; ++sample)
    {
        const std::size_t group = (sample - 1) / 8;
        const std::size_t nibble = (sample - 1) % 8;
        const unsigned shift = static_cast<unsigned>((nibble & 1) * 4);
        const std::size_t byte = nibble / 2;
        const std::uint8_t* groupSource = source + group * 8;
        WriteInt16(destination, DecodeNibble(left, groupSource[byte] >> shift));
        WriteInt16(destination,
                   DecodeNibble(right, groupSource[4 + byte] >> shift));
    }
    return true;
}
} // namespace

std::size_t XboxAdpcmDecodedBytes(std::size_t sourceBytes,
                                  std::size_t channels) noexcept
{
    if(channels == 0 || channels > 2)
        return 0;

    const std::size_t encodedBlockBytes =
        kEncodedBytesPerChannelBlock * channels;
    if(sourceBytes == 0 || sourceBytes % encodedBlockBytes != 0)
        return 0;

    const std::size_t blockCount = sourceBytes / encodedBlockBytes;
    return blockCount * kXboxAdpcmSamplesPerBlock * channels * sizeof(std::int16_t);
}

bool DecodeXboxAdpcm(const std::uint8_t* source,
                     std::size_t sourceBytes,
                     std::size_t channels,
                     std::uint8_t* destination,
                     std::size_t destinationBytes) noexcept
{
    const std::size_t decodedBytes = XboxAdpcmDecodedBytes(sourceBytes, channels);
    if(source == nullptr || destination == nullptr || decodedBytes == 0 ||
       destinationBytes < decodedBytes)
    {
        return false;
    }

    const std::size_t encodedBlockBytes =
        kEncodedBytesPerChannelBlock * channels;
    std::uint8_t* output = destination;
    for(std::size_t offset = 0; offset < sourceBytes; offset += encodedBlockBytes)
    {
        const bool decoded = channels == 1
                                 ? DecodeMonoBlock(source + offset, output)
                                 : DecodeStereoBlock(source + offset, output);
        if(!decoded)
            return false;
    }
    return true;
}
} // namespace cxbx::audio
