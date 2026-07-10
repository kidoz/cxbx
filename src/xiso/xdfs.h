// ******************************************************************
// *
// *  On-disc layout of the Xbox Game Disc Format (GDF / XDFS / XISO).
// *  A binary search tree of directory records anchored by a volume
// *  descriptor at offset 0x10000. All multi-byte fields are little-
// *  endian (the Xbox CPU is x86 LE).
// *
// ******************************************************************
#ifndef XDFS_H
#define XDFS_H

#include <array>
#include <cstdint>
#include <cstring>

// ******************************************************************
// * Volume descriptor / header constants
// ******************************************************************
inline constexpr char XISO_HEADER_DATA[] = "MICROSOFT*XBOX*MEDIA";
inline constexpr unsigned XISO_HEADER_DATA_LEN = 20;
inline constexpr std::uint64_t XISO_HEADER_OFFSET = 0x10000;
inline constexpr std::uint64_t XISO_SECTOR_SIZE = 2048;
inline constexpr std::uint64_t XISO_FILE_MODULUS = 0x10000;
inline constexpr std::uint64_t XISO_ROOT_DIRECTORY_SECTOR = 0x108;
inline constexpr unsigned XISO_DWORD_SIZE = 4;
inline constexpr std::uint8_t XISO_PAD_BYTE = 0xFF;
inline constexpr std::uint16_t XISO_PAD_SHORT = 0xFFFF;
inline constexpr unsigned XISO_FILENAME_MAX_CHARS = 255;
inline constexpr unsigned XISO_FILETIME_SIZE = 8;
inline constexpr unsigned XISO_UNUSED_SIZE = 0x7C8;

// Both 0x0000 and 0xFFFF mean "no child" in l_table/r_table: offset 0
// is always the root of the directory's tree, so a child pointer of 0
// is a self-referential null sentinel.
inline constexpr std::uint16_t XISO_NULL_CHILD = 0;

// ******************************************************************
// * XGD (Xbox Game Disc) partition offsets. Disc dumps may embed the
// * game partition at one of these absolute offsets; probe in order.
// ******************************************************************
inline constexpr std::array<std::uint64_t, 4> XGD_OFFSETS = {
    0x0ull,        // plain file
    0x0FD90000ull, // GLOBAL
    0x02080000ull, // XGD3
    0x18300000ull, // XGD1
};

// ******************************************************************
// * Directory record field offsets and sizes
// ******************************************************************
inline constexpr unsigned XISO_FILENAME_LENGTH_OFFSET = 13;
inline constexpr unsigned XISO_FILENAME_OFFSET = 14;

// Attribute byte (directory record offset 0x0C).
inline constexpr std::uint8_t XISO_ATTRIBUTE_DIR = 0x10;
inline constexpr std::uint8_t XISO_ATTRIBUTE_ARC = 0x20;

// XBE media-enable patch pattern.
inline constexpr std::uint8_t XISO_MEDIA_ENABLE[XISO_HEADER_DATA_LEN] = {
    0xE8, 0xCA, 0xFD, 0xFF, 0xFF, 0x85, 0xC0, 0x7D
};
inline constexpr unsigned XISO_MEDIA_ENABLE_LEN = 8;
inline constexpr std::uint8_t XISO_MEDIA_ENABLE_BYTE = 0xEB;

// ******************************************************************
// * Portable little-endian readers / writers (byte assembly).
// ******************************************************************
inline std::uint16_t rd_le16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::uint32_t rd_le32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void wr_le16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

inline void wr_le32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// ******************************************************************
// * On-disc directory record (variable length). Fixed 14-byte prefix
// * + filename_length bytes + dword padding.
// ******************************************************************
struct xdfs_dirent
{
    std::uint16_t l_table;        // +0x00 left subtree (dword units)
    std::uint16_t r_table;        // +0x02 right subtree (dword units)
    std::uint32_t start_sector;   // +0x04 data sector / child table sector
    std::uint32_t file_size;      // +0x08 byte size / child table byte size
    std::uint8_t attributes;      // +0x0C bit flags (0x10 = directory)
    std::uint8_t filename_length; // +0x0D
    // char       filename[];      // +0x0E
};

// Parsed entry from a directory record — value type used by the walker.
struct xiso_entry
{
    std::string name;
    std::uint32_t start_sector;
    std::uint32_t file_size;
    std::uint8_t attributes;
    bool is_dir() const { return (attributes & XISO_ATTRIBUTE_DIR) != 0; }
};

#endif // XDFS_H
