#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace cxbx::nv2a
{

inline constexpr std::uint32_t CaptureFormatVersion = 2;
inline constexpr std::uint64_t DefaultCaptureLimit = 256ull * 1024ull * 1024ull;

enum class CaptureRecordType : std::uint32_t
{
    PushRun = 1,
    PushWord = 2,
    Method = 3,
    Memory = 4,
    Scanout = 5,
    Finish = 6,
    Ramin = 7,
    PgraphState = 8,
};

std::uint32_t CaptureCrc32(const void* data, std::size_t size) noexcept;

class PushbufferCaptureWriter final
{
  public:
    PushbufferCaptureWriter() = default;
    ~PushbufferCaptureWriter();

    PushbufferCaptureWriter(const PushbufferCaptureWriter&) = delete;
    PushbufferCaptureWriter& operator=(const PushbufferCaptureWriter&) = delete;

    [[nodiscard]] bool Open(const char* path, std::uint32_t targetFrame,
                            std::uint64_t byteLimit = DefaultCaptureLimit) noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool CapturesFrame(std::uint32_t frame) const noexcept;
    [[nodiscard]] std::uint32_t TargetFrame() const noexcept;

    void RecordPushRun(std::uint32_t frame, bool hostMode, std::uint32_t base,
                       std::uint32_t get, std::uint32_t put, std::uint32_t state,
                       std::uint32_t dcount, std::uint32_t subroutine,
                       std::uint32_t limit) noexcept;
    void RecordPushWord(std::uint32_t frame, std::uint32_t address,
                        std::uint32_t word) noexcept;
    void RecordMethod(std::uint32_t frame, std::uint32_t subchannel,
                      std::uint32_t method, std::uint32_t data) noexcept;
    void RecordMemory(std::uint32_t address, const void* data,
                      std::uint32_t size) noexcept;
    void RecordRamin(const void* data, std::uint32_t size) noexcept;
    void RecordTransformConstant(std::uint32_t frame, std::uint32_t index,
                                 const float value[4]) noexcept;
    [[nodiscard]] std::uint32_t RecordScanout(
        std::uint32_t frame, std::uint32_t address, std::uint32_t width,
        std::uint32_t height, const void* pixels, std::uint32_t size) noexcept;
    void Finish(std::uint32_t completedFrame, std::uint32_t outputCrc) noexcept;

  private:
    bool WriteRecord(CaptureRecordType type, const std::uint32_t* fields,
                     std::size_t fieldCount, const void* blob,
                     std::uint32_t blobSize) noexcept;
    void Close() noexcept;

    std::FILE* file_ = nullptr;
    std::uint64_t byteLimit_ = 0;
    std::uint64_t bytesWritten_ = 0;
    std::uint32_t targetFrame_ = 0;
    std::uint32_t recordCount_ = 0;
    bool truncated_ = false;
    bool finished_ = false;
};

} // namespace cxbx::nv2a
