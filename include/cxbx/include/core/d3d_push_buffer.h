#pragma once

#include <array>
#include <cstdint>

namespace cxbx::d3d
{

constexpr std::uint32_t PushBufferCpuCopy = 0x80000000u;
constexpr std::uint32_t PushBufferMaxBytes = 16u * 1024u * 1024u;

inline constexpr std::array<std::uint32_t, 2> DecodeArrayElement16(
    std::uint32_t packedIndices) noexcept
{
    return { packedIndices & 0xFFFFu, packedIndices >> 16 };
}

enum class PushBufferReplay
{
    Empty,
    RecordedOnly,
    GuestDecode,
    Decode,
    Invalid,
};

enum class IndexedBatchTopology
{
    Invalid,
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    TriangleFan,
    QuadList,
};

constexpr std::uint32_t XboxD3dResourceTypeMask = 0x00070000u;

struct RecordedTextureDescriptor
{
    std::uint32_t resourceType = 0;
    std::uint32_t dataAddress = 0;
    std::uint32_t format = 0;
    std::uint32_t size = 0;
    bool valid = false;
};

inline constexpr RecordedTextureDescriptor CaptureRecordedTextureDescriptor(
    std::uint32_t common, std::uint32_t data, std::uint32_t format,
    std::uint32_t size) noexcept
{
    return {
        common & XboxD3dResourceTypeMask,
        data,
        format,
        size,
        true,
    };
}

struct RecordedTextureDimensions
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

inline constexpr bool IsRecordedLinearTextureFormat(
    std::uint32_t format) noexcept
{
    switch(format)
    {
        case 0x11u: // LIN_R5G6B5
        case 0x12u: // LIN_A8R8G8B8
        case 0x13u: // LIN_L8
        case 0x17u: // LIN_G8B8
        case 0x1Eu: // LIN_X8R8G8B8
        case 0x20u: // LIN_A8L8
        case 0x2Eu: // LIN_D24S8
        case 0x30u: // LIN_D16
            return true;
        default:
            return false;
    }
}

inline constexpr RecordedTextureDimensions DecodeRecordedTextureDimensions(
    const RecordedTextureDescriptor& descriptor) noexcept
{
    constexpr std::uint32_t formatMask = 0x0000FF00u;
    constexpr std::uint32_t formatShift = 8;
    const std::uint32_t format =
        (descriptor.format & formatMask) >> formatShift;
    if(IsRecordedLinearTextureFormat(format))
    {
        return {
            (descriptor.size & 0x00000FFFu) + 1,
            ((descriptor.size & 0x00FFF000u) >> 12) + 1,
        };
    }
    return {
        1u << ((descriptor.format & 0x00F00000u) >> 20),
        1u << ((descriptor.format & 0x0F000000u) >> 24),
    };
}

inline constexpr bool CanRefreshRecordedTexture(
    const RecordedTextureDescriptor& recorded,
    const RecordedTextureDescriptor& current) noexcept
{
    // Data is the mutable guest backing address. A late upload may move it
    // without changing the texture object recorded by the push buffer.
    if(!recorded.valid || !current.valid ||
       recorded.resourceType != current.resourceType)
    {
        return false;
    }
    if(recorded.format == current.format && recorded.size == current.size)
    {
        return true;
    }

    // Titles can recycle the guest header while an earlier push buffer still
    // owns the original texture. A collapse to a 1x1/2x2 resource is a new
    // generation, not a late backing update for the recorded texture.
    const RecordedTextureDimensions recordedDimensions =
        DecodeRecordedTextureDimensions(recorded);
    const RecordedTextureDimensions currentDimensions =
        DecodeRecordedTextureDimensions(current);
    return currentDimensions.width > 2 || currentDimensions.height > 2 ||
           (recordedDimensions.width <= 2 && recordedDimensions.height <= 2);
}

struct IndexedBatch
{
    IndexedBatchTopology topology = IndexedBatchTopology::Invalid;
    std::uint32_t primitiveCount = 0;
};

inline constexpr IndexedBatch ClassifyIndexedBatch(std::uint32_t beginEndOperation,
                                                    std::uint32_t indexCount) noexcept
{
    switch(beginEndOperation)
    {
        case 1:
            return { indexCount == 0 ? IndexedBatchTopology::Invalid
                                     : IndexedBatchTopology::PointList,
                     indexCount };
        case 2:
            return { indexCount < 2 ? IndexedBatchTopology::Invalid
                                    : IndexedBatchTopology::LineList,
                     indexCount / 2 };
        case 4:
            return { indexCount < 2 ? IndexedBatchTopology::Invalid
                                    : IndexedBatchTopology::LineStrip,
                     indexCount < 2 ? 0 : indexCount - 1 };
        case 5:
            return { indexCount < 3 ? IndexedBatchTopology::Invalid
                                    : IndexedBatchTopology::TriangleList,
                     indexCount / 3 };
        case 6:
            return { indexCount < 3 ? IndexedBatchTopology::Invalid
                                    : IndexedBatchTopology::TriangleStrip,
                     indexCount < 3 ? 0 : indexCount - 2 };
        case 7:
            return { indexCount < 3 ? IndexedBatchTopology::Invalid
                                    : IndexedBatchTopology::TriangleFan,
                     indexCount < 3 ? 0 : indexCount - 2 };
        case 8:
            return { indexCount < 4 ? IndexedBatchTopology::Invalid
                                    : IndexedBatchTopology::QuadList,
                     indexCount / 4 };
        default:
            return {};
    }
}

inline PushBufferReplay ClassifyPushBuffer(std::uint32_t common, std::uint32_t dataAddress,
                                           std::uint32_t size,
                                           std::uint32_t allocationSize) noexcept
{
    if(size == 0)
    {
        return PushBufferReplay::Empty;
    }
    if(size > PushBufferMaxBytes || size > allocationSize ||
       (size % sizeof(std::uint32_t)) != 0)
    {
        return PushBufferReplay::Invalid;
    }
    // A non-CPU-copy push buffer stores an Xbox GPU address in Data, not a host
    // pointer. It must be resolved through guest physical memory before decode.
    if((common & PushBufferCpuCopy) == 0)
    {
        return dataAddress == 0 ? PushBufferReplay::RecordedOnly
                                : PushBufferReplay::GuestDecode;
    }
    if(dataAddress == 0)
    {
        return PushBufferReplay::Invalid;
    }
    return PushBufferReplay::Decode;
}

inline PushBufferReplay SelectPushBufferReplay(bool hasRegisteredData,
                                               bool hasRecordedDraws,
                                               std::uint32_t common,
                                               std::uint32_t dataAddress,
                                               std::uint32_t size,
                                               std::uint32_t allocationSize) noexcept
{
    if(hasRegisteredData)
    {
        return PushBufferReplay::Decode;
    }
    if(hasRecordedDraws)
    {
        return PushBufferReplay::RecordedOnly;
    }
    return ClassifyPushBuffer(common, dataAddress, size, allocationSize);
}

template <typename ReadWord, typename HandleMethod>
bool WalkPushBuffer(std::uint32_t size, ReadWord&& readWord,
                    HandleMethod&& handleMethod)
{
    if(size < sizeof(std::uint32_t) || size > PushBufferMaxBytes ||
       (size % sizeof(std::uint32_t)) != 0)
    {
        return false;
    }

    std::uint32_t offset = 0;
    std::uint32_t guard = 0;
    std::uint32_t returnOffset = 0;
    const std::uint32_t guardLimit = (size / sizeof(std::uint32_t)) * 2;
    bool inSubroutine = false;

    while(offset <= size - sizeof(std::uint32_t) && guard++ < guardLimit)
    {
        std::uint32_t word = 0;
        if(!readWord(offset, word))
        {
            return false;
        }
        offset += sizeof(std::uint32_t);

        if((word & 0xE0000003u) == 0x20000000u || (word & 3u) == 1u)
        {
            const std::uint32_t target =
                word & ((word & 0xE0000003u) == 0x20000000u ? 0x1FFFFFFCu
                                                            : 0xFFFFFFFCu);
            if(target >= size)
            {
                return false;
            }
            offset = target;
            continue;
        }

        if((word & 3u) == 2u)
        {
            const std::uint32_t target = word & 0xFFFFFFFCu;
            if(inSubroutine || target >= size)
            {
                return false;
            }
            returnOffset = offset;
            inSubroutine = true;
            offset = target;
            continue;
        }

        if(word == 0x00020000u)
        {
            if(!inSubroutine)
            {
                return false;
            }
            offset = returnOffset;
            inSubroutine = false;
            continue;
        }

        if((word & 0xE0030003u) != 0 && (word & 0xE0030003u) != 0x40000000u)
        {
            return false;
        }

        const bool incrementing = (word & 0xE0030003u) == 0;
        std::uint32_t method = word & 0x1FFCu;
        const std::uint32_t subchannel = (word >> 13) & 0x07u;
        const std::uint32_t count = (word >> 18) & 0x07FFu;
        if(count > (size - offset) / sizeof(std::uint32_t))
        {
            return false;
        }

        for(std::uint32_t index = 0; index < count; ++index)
        {
            std::uint32_t data = 0;
            if(!readWord(offset, data) || !handleMethod(subchannel, method, data))
            {
                return false;
            }
            offset += sizeof(std::uint32_t);
            if(incrementing)
            {
                method += sizeof(std::uint32_t);
            }
        }
    }

    return offset == size && !inSubroutine;
}

template <typename ReadWord>
bool ValidatePushBuffer(std::uint32_t size, ReadWord&& readWord)
{
    return WalkPushBuffer(
        size, readWord,
        [](std::uint32_t, std::uint32_t, std::uint32_t)
        { return true; });
}

} // namespace cxbx::d3d
