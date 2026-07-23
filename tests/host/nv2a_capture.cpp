#include "core/nv2a_capture.h"

#include <array>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{

class RemoveFileOnExit final
{
  public:
    explicit RemoveFileOnExit(const char* path) noexcept : path_(path)
    {
    }

    ~RemoveFileOnExit()
    {
        std::remove(path_);
    }

    RemoveFileOnExit(const RemoveFileOnExit&) = delete;
    RemoveFileOnExit& operator=(const RemoveFileOnExit&) = delete;

  private:
    const char* path_;
};

std::uint32_t ReadU32(const std::vector<unsigned char>& bytes,
                      std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

int RunCaptureTest(const char* path)
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
    constexpr float transformConstant[4] = { 320.0f, -240.0f, 1.0f, 0.0f };
    writer.RecordTransformConstant(0, 58, transformConstant);
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

    if(bytes.size() < 84 ||
       std::string(bytes.begin(), bytes.begin() + 7) != "CXNVCAP" ||
       ReadU32(bytes, 8) != cxbx::nv2a::CaptureFormatVersion ||
       ReadU32(bytes, 12) != 0x01020304u || ReadU32(bytes, 16) != 32u ||
       ReadU32(bytes, 32) !=
           static_cast<std::uint32_t>(cxbx::nv2a::CaptureRecordType::Ramin) ||
       ReadU32(bytes, 64) !=
           static_cast<std::uint32_t>(cxbx::nv2a::CaptureRecordType::PgraphState) ||
       ReadU32(bytes, 68) != 24u || ReadU32(bytes, 72) != 0u ||
       ReadU32(bytes, 76) != 58u || ReadU32(bytes, 80) != 0x43A00000u)
    {
        std::fputs("capture bundle header or first record is invalid\n", stderr);
        return 1;
    }

    return 0;
}
} // namespace

int main() noexcept
{
    std::array<char, MAX_PATH> temporaryDirectory{};
    const DWORD directoryLength = GetTempPathA(
        static_cast<DWORD>(temporaryDirectory.size()), temporaryDirectory.data());
    if(directoryLength == 0 || directoryLength >= temporaryDirectory.size())
    {
        std::fputs("could not resolve the temporary directory\n", stderr);
        return 1;
    }

    std::array<char, MAX_PATH> path{};
    if(GetTempFileNameA(temporaryDirectory.data(), "cxn", 0, path.data()) == 0)
    {
        std::fputs("could not create a unique capture path\n", stderr);
        return 1;
    }

    int result = 1;
    try
    {
        const RemoveFileOnExit cleanup(path.data());
        result = RunCaptureTest(path.data());
    }
    catch(const std::exception& exception)
    {
        std::fprintf(stderr, "capture test raised an exception: %s\n",
                     exception.what());
    }
    catch(...)
    {
        std::fputs("capture test raised an unknown exception\n", stderr);
    }

    if(GetFileAttributesA(path.data()) != INVALID_FILE_ATTRIBUTES)
    {
        std::fputs("capture test did not remove its temporary file\n", stderr);
        return 1;
    }
    return result;
}
