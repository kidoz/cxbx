// ******************************************************************
// *
// *  Public API for the cxbx XISO tool (C++20). A XisoImage owns a
// *  std::ifstream and validates the XDFS volume descriptor on
// *  construction (RAII). All operations are free functions that take
// *  a const XisoImage&. xiso.cpp implements them; main() dispatches.
// *
// ******************************************************************
#ifndef XISO_H
#define XISO_H

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>

#include "xdfs.h"

// ******************************************************************
// * An open XISO image (RAII: the ifstream is owned and closed on
// * destruction).
// ******************************************************************
struct XisoImage
{
    mutable std::ifstream ifs;         // host file stream (mutable: position is cache)
    std::uint64_t base_offset = 0;     // XGD partition offset
    std::uint32_t root_dir_sector = 0; // root directory table sector
    std::uint32_t root_dir_size = 0;   // root directory table bytes
    std::uint8_t file_time[XISO_FILETIME_SIZE] = {};
    std::string path; // filesystem path (for display)

    // Seek to an absolute byte offset within the XDFS partition.
    void seek(std::uint64_t offset) const;
};

// ******************************************************************
// * Result type: { ok: bool, message: string }. A short ad-hoc result
// * rather than exceptions — the tool is a standalone CLI.
// ******************************************************************
struct XisoResult
{
    bool ok = true;
    std::string message;
    std::uint32_t files = 0;
    std::uint64_t bytes = 0;
};

// ******************************************************************
// * Open and validate an XISO image. On success, fills out_img and
// * returns a result with ok=true. Probes XGD offsets automatically.
// ******************************************************************
XisoResult xiso_open(const std::string& path, XisoImage& out_img);

// Print the directory tree (path + size + type) to stdout.
XisoResult xiso_list(const XisoImage& img);

// Extract all files to dest_dir (created if needed).
XisoResult xiso_extract(const XisoImage& img, const std::string& dest_dir);

// Create an XISO from src_dir. Applies the XBE media-enable patch
// unless no_media_enable is true.
XisoResult xiso_create(const std::string& src_dir, const std::string& out_name,
                       bool no_media_enable);

// Print the volume header details to stdout.
XisoResult xiso_info(const XisoImage& img);

// Validate the image; report total files/bytes.
XisoResult xiso_verify(const XisoImage& img);

// Print usage text to a stream.
void xiso_print_usage(std::ostream& os);

#endif // XISO_H
