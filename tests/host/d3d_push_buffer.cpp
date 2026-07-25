#include "core/d3d_push_buffer.h"

#include <cstdio>
#include <exception>
#include <utility>
#include <vector>

namespace
{

bool Check(cxbx::d3d::PushBufferReplay expected, std::uint32_t common, std::uint32_t data,
           std::uint32_t size, std::uint32_t allocation, const char* name)
{
    const cxbx::d3d::PushBufferReplay actual =
        cxbx::d3d::ClassifyPushBuffer(common, data, size, allocation);
    if(actual == expected)
    {
        return true;
    }
    std::fprintf(stderr, "%s classification mismatch\n", name);
    return false;
}

bool CheckWalk(const std::vector<std::uint32_t>& words,
               const std::vector<std::pair<std::uint32_t, std::uint32_t>>& expected,
               const char* name)
{
    std::vector<std::pair<std::uint32_t, std::uint32_t>> methods;
    const bool walked = cxbx::d3d::WalkPushBuffer(
        static_cast<std::uint32_t>(words.size() * sizeof(std::uint32_t)),
        [&words](std::uint32_t offset, std::uint32_t& word)
        {
            word = words[offset / sizeof(std::uint32_t)];
            return true;
        },
        [&methods](std::uint32_t, std::uint32_t method, std::uint32_t data)
        {
            methods.emplace_back(method, data);
            return true;
        });
    if(walked && methods == expected)
    {
        return true;
    }
    std::fprintf(stderr, "%s walk mismatch\n", name);
    return false;
}

bool CheckRejectedWalk(const std::vector<std::uint32_t>& words, const char* name)
{
    const bool walked = cxbx::d3d::WalkPushBuffer(
        static_cast<std::uint32_t>(words.size() * sizeof(std::uint32_t)),
        [&words](std::uint32_t offset, std::uint32_t& word)
        {
            word = words[offset / sizeof(std::uint32_t)];
            return true;
        },
        [](std::uint32_t, std::uint32_t, std::uint32_t)
        { return true; });
    if(!walked)
    {
        return true;
    }
    std::fprintf(stderr, "%s unexpectedly accepted\n", name);
    return false;
}

int RunPushBufferTest()
{
    bool passed = true;
    constexpr std::uint32_t textureType = 0x00040000u;
    constexpr std::uint32_t dxt1_256x256 =
        0x00000C00u | (8u << 20) | (8u << 24);
    constexpr std::uint32_t dxt1_128x128 =
        0x00000C00u | (7u << 20) | (7u << 24);
    constexpr std::uint32_t dxt3_2x2 =
        0x00000E00u | (1u << 20) | (1u << 24);
    constexpr auto recordedTexture = cxbx::d3d::CaptureRecordedTextureDescriptor(
        textureType | 1u, 0x01000000u, dxt1_256x256, 0);
    constexpr auto sameTexture = cxbx::d3d::CaptureRecordedTextureDescriptor(
        textureType | 7u, 0x01000000u, dxt1_256x256, 0);
    constexpr auto movedBacking = cxbx::d3d::CaptureRecordedTextureDescriptor(
        textureType | 7u, 0x02000000u, dxt1_256x256, 0);
    constexpr auto reconfiguredLayout =
        cxbx::d3d::CaptureRecordedTextureDescriptor(
            textureType | 7u, 0x02000000u, dxt1_128x128, 0);
    constexpr auto reusedTinyLayout =
        cxbx::d3d::CaptureRecordedTextureDescriptor(
            textureType | 7u, 0x02000000u, dxt3_2x2, 0);
    constexpr auto recordedLinearTexture =
        cxbx::d3d::CaptureRecordedTextureDescriptor(
            textureType | 1u, 0x01000000u, 0x00001200u,
            (255u << 12) | 255u);
    constexpr auto reusedTinyLinearLayout =
        cxbx::d3d::CaptureRecordedTextureDescriptor(
            textureType | 7u, 0x02000000u, 0x00001200u,
            (1u << 12) | 1u);
    constexpr auto reusedType = cxbx::d3d::CaptureRecordedTextureDescriptor(
        0x00050000u | 7u, 0x01000000u, dxt1_256x256, 0);
    if(!cxbx::d3d::CanRefreshRecordedTexture(recordedTexture, sameTexture) ||
       !cxbx::d3d::CanRefreshRecordedTexture(recordedTexture, movedBacking) ||
       !cxbx::d3d::CanRefreshRecordedTexture(
           recordedTexture, reconfiguredLayout) ||
       cxbx::d3d::CanRefreshRecordedTexture(
           recordedTexture, reusedTinyLayout) ||
       cxbx::d3d::CanRefreshRecordedTexture(
           recordedLinearTexture, reusedTinyLinearLayout) ||
       cxbx::d3d::CanRefreshRecordedTexture(recordedTexture, reusedType) ||
       cxbx::d3d::CanRefreshRecordedTexture(
           recordedTexture, cxbx::d3d::RecordedTextureDescriptor{}))
    {
        std::fputs(
            "recorded textures must refresh across backing moves, not layout reuse\n",
            stderr);
        passed = false;
    }
    constexpr auto triangleStrip = cxbx::d3d::ClassifyIndexedBatch(6, 7);
    constexpr auto quadList = cxbx::d3d::ClassifyIndexedBatch(8, 8);
    constexpr auto incompleteTriangle = cxbx::d3d::ClassifyIndexedBatch(6, 2);
    if(triangleStrip.topology != cxbx::d3d::IndexedBatchTopology::TriangleStrip ||
       triangleStrip.primitiveCount != 5 ||
       quadList.topology != cxbx::d3d::IndexedBatchTopology::QuadList ||
       quadList.primitiveCount != 2 ||
       incompleteTriangle.topology != cxbx::d3d::IndexedBatchTopology::Invalid ||
       incompleteTriangle.primitiveCount != 0)
    {
        std::fputs("indexed NV2A batches must map to HLE primitive counts\n", stderr);
        passed = false;
    }
    constexpr auto packedIndices = cxbx::d3d::DecodeArrayElement16(0xABCD1234u);
    if(packedIndices[0] != 0x1234u || packedIndices[1] != 0xABCDu)
    {
        std::fputs("16-bit array elements must decode low index first\n", stderr);
        passed = false;
    }
    constexpr std::uint32_t cpuCopy = cxbx::d3d::PushBufferCpuCopy;
    passed &= Check(cxbx::d3d::PushBufferReplay::Empty, 0, 0, 0, 0, "empty");
    passed &= Check(cxbx::d3d::PushBufferReplay::RecordedOnly, 0, 0, 4, 4,
                    "recorded_only");
    passed &= Check(cxbx::d3d::PushBufferReplay::GuestDecode, 0, 0x80001000, 4096,
                    4096, "gpu_address");
    passed &= Check(cxbx::d3d::PushBufferReplay::Decode, cpuCopy, 0x1000, 4, 4,
                    "one_word");
    passed &= Check(cxbx::d3d::PushBufferReplay::Decode, cpuCopy, 0x1000, 16, 64,
                    "bounded");
    passed &= Check(cxbx::d3d::PushBufferReplay::Invalid, cpuCopy, 0, 4, 4, "null_data");
    passed &= Check(cxbx::d3d::PushBufferReplay::Invalid, cpuCopy, 0x1000, 8, 4,
                    "oversize");
    passed &= Check(cxbx::d3d::PushBufferReplay::Invalid, cpuCopy, 0x1000, 6, 8,
                    "unaligned");
    passed &= Check(cxbx::d3d::PushBufferReplay::Invalid, cpuCopy, 0x1000,
                    cxbx::d3d::PushBufferMaxBytes + 4,
                    cxbx::d3d::PushBufferMaxBytes + 4, "hard_limit");
    if(cxbx::d3d::SelectPushBufferReplay(true, true, cpuCopy, 0x1000, 4, 4) !=
           cxbx::d3d::PushBufferReplay::Decode ||
       cxbx::d3d::SelectPushBufferReplay(true, false, 0, 0, 4, 4) !=
           cxbx::d3d::PushBufferReplay::Decode ||
       cxbx::d3d::SelectPushBufferReplay(false, false, 0, 0, 4, 4) !=
           cxbx::d3d::PushBufferReplay::RecordedOnly)
    {
        std::fputs("registered commands must supersede recorded fallback draws\n", stderr);
        passed = false;
    }
    passed &= CheckWalk({ 0x00080100, 0x11, 0x22 }, { { 0x100, 0x11 }, { 0x104, 0x22 } },
                        "incrementing_methods");
    passed &= CheckWalk({ 0x40080100, 0x11, 0x22 }, { { 0x100, 0x11 }, { 0x100, 0x22 } },
                        "nonincrementing_methods");
    passed &= CheckWalk({ 0x20000008, 0, 0x00040100, 0x33 }, { { 0x100, 0x33 } },
                        "jump");
    passed &= CheckWalk({ 0x00000012, 0x00040100, 0x44, 0x2000001C, 0x00040104,
                          0x55, 0x00020000, 0 },
                        { { 0x104, 0x55 }, { 0x100, 0x44 } }, "call_return");
    passed &= CheckRejectedWalk({ 0x20000010 }, "jump_out_of_range");
    passed &= CheckRejectedWalk({ 0x00000006, 0x00000006 }, "nested_call");
    passed &= CheckRejectedWalk({ 0x00020000 }, "return_without_call");
    const bool unreadableRejected = !cxbx::d3d::WalkPushBuffer(
        2 * sizeof(std::uint32_t),
        [](std::uint32_t offset, std::uint32_t& word)
        {
            word = 0x00040100;
            return offset == 0;
        },
        [](std::uint32_t, std::uint32_t, std::uint32_t)
        { return true; });
    if(!unreadableRejected)
    {
        std::fprintf(stderr, "unreadable_data unexpectedly accepted\n");
        passed = false;
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> rejectedMethods;
    const std::vector<std::uint32_t> partiallyValid = { 0x00040100, 0x66, 0xDEADBEEF };
    const auto readPartial = [&partiallyValid](std::uint32_t offset, std::uint32_t& word)
    {
        word = partiallyValid[offset / sizeof(std::uint32_t)];
        return true;
    };
    if(cxbx::d3d::ValidatePushBuffer(
           static_cast<std::uint32_t>(partiallyValid.size() * sizeof(std::uint32_t)),
           readPartial))
    {
        cxbx::d3d::WalkPushBuffer(
            static_cast<std::uint32_t>(partiallyValid.size() * sizeof(std::uint32_t)),
            readPartial,
            [&rejectedMethods](std::uint32_t, std::uint32_t method, std::uint32_t data)
            {
                rejectedMethods.emplace_back(method, data);
                return true;
            });
    }
    if(!rejectedMethods.empty())
    {
        std::fprintf(stderr, "invalid stream dispatched methods before rejection\n");
        passed = false;
    }
    return passed ? 0 : 1;
}

} // namespace

int main() noexcept
{
    try
    {
        return RunPushBufferTest();
    }
    catch(const std::exception& error)
    {
        std::fprintf(stderr, "unexpected exception: %s\n", error.what());
    }
    catch(...)
    {
        std::fputs("unexpected non-standard exception\n", stderr);
    }
    return 1;
}
