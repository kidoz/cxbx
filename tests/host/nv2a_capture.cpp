#include "core/nv2a_capture.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

std::uint32_t ReadU32(const std::vector<unsigned char>& bytes,
                      std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

} // namespace

int main()
{
    constexpr std::array<unsigned char, 9> crcInput = {
        '1', '2', '3', '4', '5', '6', '7', '8', '9'
    };
    if(cxbx::nv2a::CaptureCrc32(crcInput.data(), crcInput.size()) !=
       0xCBF43926u)
    {
        std::fputs("capture CRC must match zlib CRC32\n", stderr);
        return 1;
    }

    constexpr const char* path = "cxbx_nv2a_capture_test.bin";
    std::remove(path);

    cxbx::nv2a::PushbufferCaptureWriter writer;
    if(!writer.Open(path, 0, 64ull * 1024ull * 1024ull))
    {
        std::fputs("capture writer did not open\n", stderr);
        return 1;
    }

    constexpr std::array<std::uint32_t, 4> ramin = {
        0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u
    };
    constexpr std::array<std::uint32_t, 4> pixels = {
        0xFF000000u, 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu
    };
    writer.RecordRamin(ramin.data(), sizeof(ramin));
    writer.RecordPushRun(0, true, 0x00100000u, 0, 12, 0, 0, 0,
                         0xFFFFFFFFu);
    writer.RecordPushWord(0, 0, 0x00040100u);
    writer.RecordPushWord(0, 4, 0xAABBCCDDu);
    writer.RecordMethod(0, 0, 0x0100u, 0xAABBCCDDu);
    writer.RecordMemory(0x00200000u, pixels.data(), sizeof(pixels));
    const std::uint32_t outputCrc = writer.RecordScanout(
        0, 0x00200000u, 2, 2, pixels.data(), sizeof(pixels));
    writer.Finish(0, outputCrc);

    std::ifstream input(path, std::ios::binary);
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    input.close();
    std::remove(path);

    if(bytes.size() < 40 ||
       std::string(bytes.begin(), bytes.begin() + 7) != "CXNVCAP" ||
       ReadU32(bytes, 8) != cxbx::nv2a::CaptureFormatVersion ||
       ReadU32(bytes, 12) != 0x01020304u || ReadU32(bytes, 16) != 32u ||
       ReadU32(bytes, 32) !=
           static_cast<std::uint32_t>(cxbx::nv2a::CaptureRecordType::Ramin))
    {
        std::fputs("capture bundle header or first record is invalid\n", stderr);
        return 1;
    }

    return 0;
}
