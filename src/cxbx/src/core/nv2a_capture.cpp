#include "core/nv2a_capture.h"

#include <array>
#include <cstring>
#include <limits>

namespace cxbx::nv2a
{
namespace
{

constexpr std::array<unsigned char, 8> CaptureMagic = {
    'C', 'X', 'N', 'V', 'C', 'A', 'P', '\0'
};
constexpr std::uint32_t CaptureEndianMarker = 0x01020304u;
constexpr std::uint32_t CaptureHeaderSize = 32;
constexpr std::uint64_t CaptureOutputReserve = 40ull * 1024ull * 1024ull;

bool WriteBytes(std::FILE* file, const void* data, std::size_t size) noexcept
{
    return size == 0 || std::fwrite(data, 1, size, file) == size;
}

bool WriteU32(std::FILE* file, std::uint32_t value) noexcept
{
    const std::array<unsigned char, 4> bytes = {
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24),
    };
    return WriteBytes(file, bytes.data(), bytes.size());
}

bool WriteU64(std::FILE* file, std::uint64_t value) noexcept
{
    return WriteU32(file, static_cast<std::uint32_t>(value)) &&
           WriteU32(file, static_cast<std::uint32_t>(value >> 32));
}

} // namespace

std::uint32_t CaptureCrc32(const void* data, std::size_t size) noexcept
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint32_t crc = 0xFFFFFFFFu;
    for(std::size_t index = 0; index < size; ++index)
    {
        crc ^= bytes[index];
        for(unsigned bit = 0; bit < 8; ++bit)
        {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

PushbufferCaptureWriter::~PushbufferCaptureWriter()
{
    Close();
}

bool PushbufferCaptureWriter::Open(const char* path, std::uint32_t targetFrame,
                                   std::uint64_t byteLimit) noexcept
{
    Close();
    if(path == nullptr || path[0] == '\0' || byteLimit < CaptureHeaderSize + 64)
    {
        return false;
    }

#ifdef _WIN32
    if(fopen_s(&file_, path, "wb") != 0)
    {
        file_ = nullptr;
    }
#else
    file_ = std::fopen(path, "wb");
#endif
    if(file_ == nullptr)
    {
        return false;
    }

    byteLimit_ = byteLimit;
    targetFrame_ = targetFrame;
    bytesWritten_ = 0;
    recordCount_ = 0;
    truncated_ = false;
    finished_ = false;
    const bool headerOk =
        WriteBytes(file_, CaptureMagic.data(), CaptureMagic.size()) &&
        WriteU32(file_, CaptureFormatVersion) &&
        WriteU32(file_, CaptureEndianMarker) &&
        WriteU32(file_, CaptureHeaderSize) && WriteU32(file_, targetFrame_) &&
        WriteU64(file_, byteLimit_);
    if(!headerOk)
    {
        Close();
        return false;
    }

    bytesWritten_ = CaptureHeaderSize;
    return true;
}

bool PushbufferCaptureWriter::IsActive() const noexcept
{
    return file_ != nullptr && !finished_;
}

bool PushbufferCaptureWriter::CapturesFrame(std::uint32_t frame) const noexcept
{
    return IsActive() && frame <= targetFrame_;
}

std::uint32_t PushbufferCaptureWriter::TargetFrame() const noexcept
{
    return targetFrame_;
}

bool PushbufferCaptureWriter::WriteRecord(
    CaptureRecordType type, const std::uint32_t* fields, std::size_t fieldCount,
    const void* blob, std::uint32_t blobSize) noexcept
{
    if(!IsActive() || fieldCount >
                          (std::numeric_limits<std::uint32_t>::max)() / sizeof(std::uint32_t))
    {
        return false;
    }

    const std::uint64_t payloadSize = fieldCount * sizeof(std::uint32_t) + blobSize;
    const std::uint64_t recordSize = 8 + payloadSize;
    const std::uint64_t footerReserve =
        type == CaptureRecordType::Finish ? 0 : 28;
    const bool isFinalScanout =
        type == CaptureRecordType::Scanout && fieldCount != 0 &&
        fields[0] == targetFrame_;
    const std::uint64_t outputReserve =
        type != CaptureRecordType::Finish && !isFinalScanout
            ? CaptureOutputReserve
            : 0;
    const std::uint64_t reserve = footerReserve + outputReserve;
    if(payloadSize > (std::numeric_limits<std::uint32_t>::max)() ||
       recordSize > byteLimit_ || bytesWritten_ > byteLimit_ - recordSize ||
       (reserve != 0 && bytesWritten_ + recordSize + reserve > byteLimit_))
    {
        truncated_ = true;
        return false;
    }

    if(!WriteU32(file_, static_cast<std::uint32_t>(type)) ||
       !WriteU32(file_, static_cast<std::uint32_t>(payloadSize)))
    {
        Close();
        return false;
    }
    for(std::size_t index = 0; index < fieldCount; ++index)
    {
        if(!WriteU32(file_, fields[index]))
        {
            Close();
            return false;
        }
    }
    if(!WriteBytes(file_, blob, blobSize))
    {
        Close();
        return false;
    }

    bytesWritten_ += recordSize;
    ++recordCount_;
    return true;
}

void PushbufferCaptureWriter::RecordPushRun(
    std::uint32_t frame, bool hostMode, std::uint32_t base, std::uint32_t get,
    std::uint32_t put, std::uint32_t state, std::uint32_t dcount,
    std::uint32_t subroutine, std::uint32_t limit) noexcept
{
    const std::array fields = { frame, hostMode ? 1u : 0u, base, get, put,
                                state, dcount, subroutine, limit };
    WriteRecord(CaptureRecordType::PushRun, fields.data(), fields.size(),
                nullptr, 0);
}

void PushbufferCaptureWriter::RecordPushWord(
    std::uint32_t frame, std::uint32_t address, std::uint32_t word) noexcept
{
    const std::array fields = { frame, address, word };
    WriteRecord(CaptureRecordType::PushWord, fields.data(), fields.size(),
                nullptr, 0);
}

void PushbufferCaptureWriter::RecordMethod(
    std::uint32_t frame, std::uint32_t subchannel, std::uint32_t method,
    std::uint32_t data) noexcept
{
    const std::array fields = { frame, subchannel, method, data };
    WriteRecord(CaptureRecordType::Method, fields.data(), fields.size(),
                nullptr, 0);
}

void PushbufferCaptureWriter::RecordMemory(
    std::uint32_t address, const void* data, std::uint32_t size) noexcept
{
    if(data == nullptr || size == 0)
    {
        return;
    }
    const std::array fields = { address, size, CaptureCrc32(data, size) };
    WriteRecord(CaptureRecordType::Memory, fields.data(), fields.size(), data,
                size);
}

void PushbufferCaptureWriter::RecordRamin(const void* data,
                                          std::uint32_t size) noexcept
{
    if(data == nullptr || size == 0)
    {
        return;
    }
    const std::array fields = { size, CaptureCrc32(data, size) };
    WriteRecord(CaptureRecordType::Ramin, fields.data(), fields.size(), data,
                size);
}

void PushbufferCaptureWriter::RecordTransformConstant(
    std::uint32_t frame, std::uint32_t index, const float value[4]) noexcept
{
    if(value == nullptr || index >= 192)
    {
        return;
    }
    std::array<std::uint32_t, 6> fields = { frame, index, 0, 0, 0, 0 };
    std::memcpy(&fields[2], value, 4 * sizeof(float));
    WriteRecord(CaptureRecordType::PgraphState, fields.data(), fields.size(),
                nullptr, 0);
}

std::uint32_t PushbufferCaptureWriter::RecordScanout(
    std::uint32_t frame, std::uint32_t address, std::uint32_t width,
    std::uint32_t height, const void* pixels, std::uint32_t size) noexcept
{
    if(pixels == nullptr || size == 0)
    {
        return 0;
    }
    const std::uint32_t crc = CaptureCrc32(pixels, size);
    const std::array fields = { frame, address, width, height, crc, size };
    WriteRecord(CaptureRecordType::Scanout, fields.data(), fields.size(),
                pixels, size);
    return crc;
}

void PushbufferCaptureWriter::Finish(std::uint32_t completedFrame,
                                     std::uint32_t outputCrc) noexcept
{
    if(!IsActive())
    {
        return;
    }
    const std::array fields = { targetFrame_, completedFrame, recordCount_,
                                truncated_ ? 1u : 0u, outputCrc };
    WriteRecord(CaptureRecordType::Finish, fields.data(), fields.size(),
                nullptr, 0);
    finished_ = true;
    if(file_ != nullptr)
    {
        std::fflush(file_);
    }
    Close();
}

void PushbufferCaptureWriter::Close() noexcept
{
    if(file_ != nullptr)
    {
        std::fclose(file_);
        file_ = nullptr;
    }
}

} // namespace cxbx::nv2a
