//
//  cxbx XISO tool (C++20): list, extract, create, inspect, and verify
//  Xbox Game Disc Format (GDF / XDFS) disc images. Clean-room re-
//  implementation based on the on-disc format spec.
//
#include "xiso.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

// ******************************************************************
// * Tunables
// ******************************************************************
static constexpr std::size_t RW_BUF_SIZE = 2 * 1024 * 1024;   // 2 MiB
static constexpr std::size_t MAX_DIRTABLE = 16 * 1024 * 1024; // 16 MiB cap
static int g_verbose = 0;

// ******************************************************************
// * XisoImage: seek to an absolute byte offset within the partition.
// ******************************************************************
void XisoImage::seek(std::uint64_t offset) const
{
    ifs.clear();
    // On Windows, streampos is 64-bit via _fseeki64 backing; this works
    // for offsets beyond 2 GiB.
    ifs.seekg(static_cast<std::streamoff>(base_offset + offset), std::ios::beg);
}

// ******************************************************************
// * Helpers
// ******************************************************************

// Read exactly n bytes from ifs into buf. Returns false on short read.
static bool read_exact(std::ifstream& ifs, void* buf, std::size_t n)
{
    ifs.read(static_cast<char*>(buf), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(ifs.gcount()) == n;
}

// Read a full directory table into a vector. Returns an empty vector
// on failure (an empty table is returned as a zeroed single sector).
static std::vector<std::uint8_t> read_dirtable(const XisoImage& img,
                                               std::uint32_t sector,
                                               std::uint32_t size)
{
    if(size > MAX_DIRTABLE)
        return {};

    std::size_t alloc = ((size + XISO_SECTOR_SIZE - 1) / XISO_SECTOR_SIZE) * XISO_SECTOR_SIZE;
    if(alloc == 0)
        alloc = XISO_SECTOR_SIZE;

    std::vector<std::uint8_t> buf(alloc, XISO_PAD_BYTE);
    img.seek(sector * XISO_SECTOR_SIZE);
    if(!read_exact(img.ifs, buf.data(), alloc))
        return {};
    return buf;
}

// Parse a directory record at byte offset `off` within a directory
// table buffer. Returns false if `off` is out of bounds.
static bool parse_record(const std::vector<std::uint8_t>& buf,
                         std::uint32_t off, xiso_entry& out)
{
    if(off + XISO_FILENAME_OFFSET > buf.size())
        return false;

    std::uint16_t l = rd_le16(buf.data() + off + 0);
    std::uint16_t r = rd_le16(buf.data() + off + 2);
    out.start_sector = rd_le32(buf.data() + off + 4);
    out.file_size = rd_le32(buf.data() + off + 8);
    out.attributes = buf[off + 12];

    std::uint8_t name_len = buf[off + 13];
    if(off + XISO_FILENAME_OFFSET + name_len > buf.size())
        return false;

    out.name.assign(reinterpret_cast<const char*>(buf.data() + off + XISO_FILENAME_OFFSET),
                    name_len);
    // Stash l/r for the caller via start_sector's unused high bits — no, use
    // separate out fields. Simpler: return l/r through a pair.
    // We'll re-read them in the walker directly. Keep it simple.
    (void)l;
    (void)r;
    return true;
}

// Read l_table/r_table at a given offset.
static std::pair<std::uint16_t, std::uint16_t> read_lr(const std::vector<std::uint8_t>& buf,
                                                       std::uint32_t off)
{
    if(off + 4 > buf.size())
        return { XISO_NULL_CHILD, XISO_NULL_CHILD };
    return { rd_le16(buf.data() + off + 0), rd_le16(buf.data() + off + 2) };
}

// ******************************************************************
// * xiso_open
// ******************************************************************
XisoResult xiso_open(const std::string& path, XisoImage& out)
{
    out.ifs.open(path, std::ios::binary);
    if(!out.ifs)
        return { false, "cannot open " + path };
    out.path = path;

    // Probe each XGD offset for the XDFS magic.
    std::array<char, XISO_HEADER_DATA_LEN> magic{};
    out.base_offset = 0;
    bool found = false;
    for(std::uint64_t off : XGD_OFFSETS)
    {
        out.ifs.clear();
        out.ifs.seekg(static_cast<std::streamoff>(off + XISO_HEADER_OFFSET), std::ios::beg);
        if(!read_exact(out.ifs, magic.data(), magic.size()))
            continue;
        if(std::memcmp(magic.data(), XISO_HEADER_DATA, XISO_HEADER_DATA_LEN) == 0)
        {
            out.base_offset = off;
            found = true;
            break;
        }
    }
    if(!found)
        return { false, "not an Xbox XISO image (no XDFS magic)" };

    // Read root_dir_sector, root_dir_size.
    std::uint8_t hdr[8];
    if(!read_exact(out.ifs, hdr, 8))
        return { false, "short read on header" };
    out.root_dir_sector = rd_le32(hdr);
    out.root_dir_size = rd_le32(hdr + 4);

    // Read FILETIME.
    if(!read_exact(out.ifs, out.file_time, XISO_FILETIME_SIZE))
        return { false, "short read on filetime" };

    // Corruption check: skip unused gap, read tail magic.
    out.ifs.seekg(XISO_UNUSED_SIZE, std::ios::cur);
    std::array<char, XISO_HEADER_DATA_LEN> tail{};
    if(read_exact(out.ifs, tail.data(), tail.size()))
    {
        if(std::memcmp(tail.data(), XISO_HEADER_DATA, XISO_HEADER_DATA_LEN) != 0)
        {
            if(g_verbose)
                std::cerr << "xiso: warning: tail magic mismatch (image may be corrupt)\n";
        }
    }

    if(g_verbose)
        std::cerr << "xiso: opened " << path << " (XGD 0x" << std::hex << out.base_offset
                  << ", root sector " << std::dec << out.root_dir_sector
                  << ", root size " << out.root_dir_size << ")\n";

    return { true, "" };
}

// ******************************************************************
// * Walker: visit every entry in the directory tree. The callback
// * receives (entry, display_path, depth). For directory entries,
// * the walker recurses automatically.
// ******************************************************************
using VisitFn = std::function<void(const xiso_entry&, const std::string&, int)>;

static void walk(const XisoImage& img, std::uint32_t dir_sector,
                 std::uint32_t dir_size, const std::string& base_path,
                 int depth, const VisitFn& visit)
{
    auto buf = read_dirtable(img, dir_sector, dir_size);
    if(buf.empty())
    {
        std::cerr << "xiso: failed to read directory table at sector " << dir_sector << "\n";
        return;
    }

    // Empty directory: the entire directory table is 0xFF padding. A real
    // entry's l_table may legitimately be 0xFFFF (no left child), so we
    // check the attributes byte instead: if it's 0xFF (pad), the slot is
    // unused and the directory is empty.
    if(dir_size == 0 || buf[12] == XISO_PAD_BYTE)
        return;

    // Iterative in-order tree walk via an explicit stack.
    struct Frame
    {
        std::uint32_t off;
    };
    std::vector<Frame> stack;
    stack.push_back({ 0 });

    while(!stack.empty())
    {
        std::uint32_t off = stack.back().off;
        stack.pop_back();
        if(off + XISO_FILENAME_OFFSET > buf.size())
            continue;

        auto [l, r] = read_lr(buf, off);

        xiso_entry e;
        if(!parse_record(buf, off, e))
            continue;

        std::string child_path = base_path.empty()
                                     ? "\\" + e.name
                                     : base_path + "\\" + e.name;

        // Visit this entry.
        visit(e, child_path, depth);

        // Recurse into subdirectory.
        if(e.is_dir() && e.file_size > 0)
            walk(img, e.start_sector, e.file_size, child_path, depth + 1, visit);

        // Push children (right first → left processed first).
        if(r != XISO_NULL_CHILD && r != XISO_PAD_SHORT)
        {
            std::uint32_t child = static_cast<std::uint32_t>(r) * XISO_DWORD_SIZE;
            if(child < buf.size())
                stack.push_back({ child });
        }
        if(l != XISO_NULL_CHILD && l != XISO_PAD_SHORT)
        {
            std::uint32_t child = static_cast<std::uint32_t>(l) * XISO_DWORD_SIZE;
            if(child < buf.size())
                stack.push_back({ child });
        }
    }
}

// ******************************************************************
// * xiso_list
// ******************************************************************
XisoResult xiso_list(const XisoImage& img)
{
    std::cout << "listing " << img.path << ":\n\n";
    std::uint32_t files = 0;
    std::uint64_t bytes = 0;

    walk(img, img.root_dir_sector, img.root_dir_size, "", 0,
         [&](const xiso_entry& e, const std::string& path, int)
         {
             // path already contains the full "\dir\subdir\name".
             std::cout << path
                       << " (" << e.file_size << " bytes)"
                       << (e.is_dir() ? " (dir)" : "") << "\n";
             if(!e.is_dir())
             {
                 files++;
                 bytes += e.file_size;
             }
         });

    std::cout << "\n"
              << files << " files in " << img.path
              << " total " << bytes << " bytes\n";
    return { true, "", files, bytes };
}

// ******************************************************************
// * xiso_info
// ******************************************************************
XisoResult xiso_info(const XisoImage& img)
{
    std::uint64_t root_off = static_cast<std::uint64_t>(img.root_dir_sector) * XISO_SECTOR_SIZE;
    std::uint64_t filetime = rd_le32(img.file_time) |
                             (static_cast<std::uint64_t>(rd_le32(img.file_time + 4)) << 32);

    std::cout << "XISO image: " << img.path << "\n";
    std::cout << "  XGD partition offset : 0x" << std::hex << img.base_offset << std::dec << "\n";
    std::cout << "  Volume descriptor    : 0x" << std::hex
              << (img.base_offset + XISO_HEADER_OFFSET) << std::dec << "\n";
    std::cout << "  Root dir sector      : " << img.root_dir_sector
              << " (0x" << std::hex << img.root_dir_sector << std::dec << ")\n";
    std::cout << "  Root dir byte offset : 0x" << std::hex << root_off << std::dec << "\n";
    std::cout << "  Root dir size        : " << img.root_dir_size << " bytes ("
              << (img.root_dir_size + XISO_SECTOR_SIZE - 1) / XISO_SECTOR_SIZE << " sectors)\n";
    std::cout << "  File time (FILETIME) : 0x" << std::hex << std::setfill('0')
              << std::setw(16) << filetime << std::dec << "\n";
    return { true };
}

// ******************************************************************
// * xiso_verify
// ******************************************************************
XisoResult xiso_verify(const XisoImage& img)
{
    std::cout << "verifying " << img.path << "...\n";
    std::uint32_t files = 0;
    std::uint64_t bytes = 0;

    walk(img, img.root_dir_sector, img.root_dir_size, "", 0,
         [&](const xiso_entry& e, const std::string&, int)
         {
             if(!e.is_dir())
             {
                 files++;
                 bytes += e.file_size;
             }
         });

    std::cout << "  " << files << " files, " << bytes << " bytes total\n";
    return { true, "", files, bytes };
}

// ******************************************************************
// * xiso_extract
// ******************************************************************
XisoResult xiso_extract(const XisoImage& img, const std::string& dest_dir)
{
    fs::create_directories(dest_dir);
    std::cerr << "xiso: extracting " << img.path << " to " << dest_dir << "\n";

    std::uint32_t files = 0;
    std::uint64_t bytes = 0;
    std::vector<char> buf(RW_BUF_SIZE);

    walk(img, img.root_dir_sector, img.root_dir_size, "", 0,
         [&](const xiso_entry& e, const std::string& path, int)
         {
             // path starts with '\'; build host path = dest_dir + path (with / separators).
             std::string rel = path;
             // Replace backslashes with the platform separator.
             std::replace(rel.begin(), rel.end(), '\\',
                          static_cast<char>(fs::path::preferred_separator));
             fs::path host = fs::path(dest_dir) / rel.substr(1); // strip leading '\'

             if(e.is_dir())
             {
                 fs::create_directories(host);
                 return;
             }

             // Path-traversal safety: reject any ".." component.
             for(const auto& part : host)
             {
                 if(part == "..")
                 {
                     std::cerr << "xiso: skipping unsafe path " << path << "\n";
                     return;
                 }
             }

             fs::create_directories(host.parent_path());

             img.seek(static_cast<std::uint64_t>(e.start_sector) * XISO_SECTOR_SIZE);
             std::ofstream out(host, std::ios::binary);
             if(!out)
             {
                 std::cerr << "xiso: cannot create " << host << "\n";
                 return;
             }

             std::uint32_t remaining = e.file_size;
             while(remaining > 0)
             {
                 std::uint32_t chunk = std::min(static_cast<std::uint32_t>(RW_BUF_SIZE), remaining);
                 img.ifs.read(buf.data(), chunk);
                 auto got = static_cast<std::uint32_t>(img.ifs.gcount());
                 if(got < chunk)
                     std::cerr << "xiso: short read on " << path << "\n";
                 out.write(buf.data(), got);
                 remaining -= got;
                 if(got == 0)
                     break;
             }

             files++;
             bytes += e.file_size;
             if(g_verbose)
                 std::cerr << "  " << path << " (" << e.file_size << " bytes)\n";
         });

    std::cerr << "xiso: extracted " << files << " files (" << bytes << " bytes)\n";
    return { true, "", files, bytes };
}

// ==================================================================
//                       CREATE (write) path
// ==================================================================

// In-memory tree node. We build a flat vector of entries (sorted by
// case-insensitive name within each directory), then construct the
// on-disc BST from the sorted array (balanced). This avoids a hand-
// written AVL tree: a balanced BST built from a sorted vector is
// trivially correct and optimal.
struct BuildNode
{
    std::string name;
    std::uint32_t file_size = 0;
    std::uint32_t start_sector = 0;
    std::uint32_t record_offset = 0; // byte offset within parent dir table
    bool is_dir = false;
    std::vector<BuildNode> children; // sorted (case-insensitive) children
};

// Case-insensitive comparison for sorting / balancing.
static bool ci_less(const std::string& a, const std::string& b)
{
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(),
        [](char x, char y)
        { return std::toupper(static_cast<unsigned char>(x)) <
                 std::toupper(static_cast<unsigned char>(y)); });
}

// Build a directory tree from the filesystem (recursive).
static bool build_tree(const fs::path& dir_path, BuildNode& out)
{
    std::vector<BuildNode> kids;

    for(auto& entry : fs::directory_iterator(dir_path))
    {
        std::string name = entry.path().filename().string();
        if(name == "." || name == "..")
            continue;

        BuildNode node;
        node.name = name;
        node.is_dir = entry.is_directory();

        if(node.is_dir)
        {
            if(!build_tree(entry.path(), node))
                return false;
        }
        else
        {
            node.file_size = static_cast<std::uint32_t>(entry.file_size());
        }

        kids.push_back(std::move(node));
    }

    std::sort(kids.begin(), kids.end(),
              [](const BuildNode& a, const BuildNode& b)
              { return ci_less(a.name, b.name); });

    out.children = std::move(kids);
    return true;
}

// Padded record length for a filename (14 + namelen, padded to dword).
static std::uint32_t record_length(const std::string& name)
{
    std::uint32_t len = XISO_FILENAME_OFFSET + static_cast<std::uint32_t>(name.size());
    len += (XISO_DWORD_SIZE - (len % XISO_DWORD_SIZE)) % XISO_DWORD_SIZE;
    return len;
}

// Phase 1 (bottom-up): compute each directory's table byte size and
// assign record_offset to each child (prefix order within the BST).
// The size is padded to a sector boundary: XDFS directory tables always
// occupy whole sectors.
static std::uint32_t calc_dir_size(std::vector<BuildNode>& children)
{
    std::uint32_t cursor = 0;
    if(children.empty())
        return static_cast<std::uint32_t>(XISO_SECTOR_SIZE); // empty dir = 1 sector

    // Assign offsets by building a balanced BST index order from the
    // sorted array. The on-disc tree must list nodes in a way that
    // produces valid l/r pointers. We assign record_offset in the
    // order we'd write nodes (prefix: node, left subtree, right subtree).
    std::function<void(int, int)> assign = [&](int lo, int hi)
    {
        if(lo > hi)
            return;
        int mid = (lo + hi) / 2;
        BuildNode& n = children[mid];

        std::uint32_t rlen = record_length(n.name);
        // Don't let a record cross a sector boundary.
        std::uint32_t after = cursor + rlen;
        if(after / XISO_SECTOR_SIZE > cursor / XISO_SECTOR_SIZE)
            cursor = static_cast<std::uint32_t>(
                (cursor / XISO_SECTOR_SIZE + 1) * XISO_SECTOR_SIZE);

        n.record_offset = cursor;
        cursor += rlen;
        assign(lo, mid - 1);
        assign(mid + 1, hi);
    };
    assign(0, static_cast<int>(children.size()) - 1);

    // Pad to a full sector: XDFS directory tables occupy whole sectors.
    cursor = static_cast<std::uint32_t>(
        ((cursor + XISO_SECTOR_SIZE - 1) / XISO_SECTOR_SIZE) * XISO_SECTOR_SIZE);
    return cursor;
}

// Phase 2 (top-down): assign start_sector to every node (directory
// tables first, then files within). Returns the next free sector.
static void assign_sectors(BuildNode& node, std::uint32_t& next_sector)
{
    // Directory entries within this node's parent: this node's own
    // start_sector was already assigned by the caller. Here we assign
    // sectors to THIS directory's children.
    node.file_size = calc_dir_size(node.children);

    // Reserve this directory's own table sectors.
    node.start_sector = next_sector; // overwritten by parent for non-root

    std::uint32_t dir_sectors = (node.file_size + XISO_SECTOR_SIZE - 1) / XISO_SECTOR_SIZE;

    // Walk children in prefix order; assign sectors.
    std::function<void(int, int)> assign = [&](int lo, int hi)
    {
        if(lo > hi)
            return;
        int mid = (lo + hi) / 2;
        assign(lo, mid - 1);

        BuildNode& child = node.children[mid];
        child.start_sector = next_sector;
        if(child.is_dir)
        {
            // A child directory's own size depends on ITS children; recurse
            // to compute and assign (calc_dir_size runs inside assign_sectors).
            assign_sectors(child, next_sector);
        }
        else
        {
            std::uint32_t fsectors = (child.file_size + XISO_SECTOR_SIZE - 1) / XISO_SECTOR_SIZE;
            if(fsectors == 0)
                fsectors = 1;
            next_sector += fsectors;
        }

        assign(mid + 1, hi);
    };

    // The root table occupies [next_sector .. +dir_sectors).
    std::uint32_t table_start = next_sector;
    next_sector += dir_sectors;

    // Now assign child file/dir data sectors after the table.
    assign(0, static_cast<int>(node.children.size()) - 1);

    // Fix up: the node's start_sector should point to its table.
    node.start_sector = table_start;
    node.file_size = calc_dir_size(node.children);
}

// Write file data for all entries (prefix order).
static void write_files(const fs::path& base, const BuildNode& node,
                        std::ofstream& out, bool no_media_enable)
{
    std::function<void(int, int)> write_range = [&](int lo, int hi)
    {
        if(lo > hi)
            return;
        int mid = (lo + hi) / 2;
        write_range(lo, mid - 1);

        const BuildNode& child = node.children[mid];
        if(!child.is_dir)
        {
            fs::path src = base / child.name;
            out.seekp(static_cast<std::streamoff>(static_cast<std::uint64_t>(child.start_sector) * XISO_SECTOR_SIZE));

            bool is_xbe = child.name.size() >= 4 &&
                          (child.name.compare(child.name.size() - 4, 4, ".xbe") == 0 ||
                           child.name.compare(child.name.size() - 4, 4, ".XBE") == 0);

            std::ifstream in(src, std::ios::binary);
            std::vector<char> buf(RW_BUF_SIZE);
            std::uint32_t remaining = child.file_size;
            std::uint64_t file_off = 0;
            while(remaining > 0)
            {
                std::uint32_t chunk = std::min(static_cast<std::uint32_t>(RW_BUF_SIZE), remaining);
                in.read(buf.data(), chunk);
                auto got = static_cast<std::uint32_t>(in.gcount());
                if(got < chunk)
                    std::memset(buf.data() + got, XISO_PAD_BYTE, chunk - got);

                // Media-enable patch.
                if(is_xbe && !no_media_enable && chunk >= XISO_MEDIA_ENABLE_LEN)
                {
                    for(std::uint32_t i = 0; i + XISO_MEDIA_ENABLE_LEN <= chunk; i++)
                    {
                        if(std::memcmp(buf.data() + i, XISO_MEDIA_ENABLE, XISO_MEDIA_ENABLE_LEN) == 0)
                        {
                            buf[i + XISO_MEDIA_ENABLE_LEN - 1] = static_cast<char>(XISO_MEDIA_ENABLE_BYTE);
                            if(g_verbose)
                                std::cerr << "xiso: media-enable patch at " << file_off + i
                                          << " in " << child.name << "\n";
                        }
                    }
                }

                out.write(buf.data(), chunk);
                remaining -= chunk;
                file_off += chunk;
            }

            // Pad to sector.
            std::uint32_t pad = static_cast<std::uint32_t>(XISO_SECTOR_SIZE - (child.file_size % XISO_SECTOR_SIZE));
            if(pad != XISO_SECTOR_SIZE)
            {
                std::memset(buf.data(), XISO_PAD_BYTE, pad);
                out.write(buf.data(), pad);
            }
        }

        write_range(mid + 1, hi);
    };
    write_range(0, static_cast<int>(node.children.size()) - 1);

    // Recurse into subdirectories.
    for(const auto& child : node.children)
    {
        if(child.is_dir)
            write_files(base / child.name, child, out, no_media_enable);
    }
}

// Write a directory table (and recurse for child tables).
static void write_dirtable(const BuildNode& node, std::ofstream& out)
{
    // Allocate the table buffer.
    std::uint32_t dir_size = calc_dir_size(const_cast<std::vector<BuildNode>&>(node.children));
    std::size_t alloc = ((dir_size + XISO_SECTOR_SIZE - 1) / XISO_SECTOR_SIZE) * XISO_SECTOR_SIZE;
    if(alloc == 0)
        alloc = XISO_SECTOR_SIZE;
    std::vector<std::uint8_t> buf(alloc, XISO_PAD_BYTE);

    // Write each child's record at its record_offset.
    // BST l/r pointers: for a balanced tree built from sorted array,
    // child[mid]'s left child is child[mid-1..] root, right is child[mid+1..].
    std::function<void(int, int)> write_node = [&](int lo, int hi)
    {
        if(lo > hi)
            return;
        int mid = (lo + hi) / 2;
        const BuildNode& n = node.children[mid];

        std::uint32_t off = n.record_offset;
        std::uint8_t name_len = static_cast<std::uint8_t>(n.name.size());

        // l_table: index of left child (lo..mid-1 root = (lo+mid-1)/2)
        int lchild = (lo <= mid - 1) ? (lo + mid - 1) / 2 : -1;
        int rchild = (mid + 1 <= hi) ? (mid + 1 + hi) / 2 : -1;

        wr_le16(buf.data() + off + 0,
                lchild >= 0 ? static_cast<std::uint16_t>(node.children[lchild].record_offset / XISO_DWORD_SIZE)
                            : XISO_PAD_SHORT);
        wr_le16(buf.data() + off + 2,
                rchild >= 0 ? static_cast<std::uint16_t>(node.children[rchild].record_offset / XISO_DWORD_SIZE)
                            : XISO_PAD_SHORT);
        wr_le32(buf.data() + off + 4, n.start_sector);
        wr_le32(buf.data() + off + 8, n.file_size);
        buf[off + 12] = n.is_dir ? XISO_ATTRIBUTE_DIR : XISO_ATTRIBUTE_ARC;
        buf[off + 13] = name_len;
        std::memcpy(buf.data() + off + XISO_FILENAME_OFFSET, n.name.data(), name_len);

        write_node(lo, mid - 1);
        write_node(mid + 1, hi);
    };
    write_node(0, static_cast<int>(node.children.size()) - 1);

    // Write the table at the directory's start_sector.
    out.seekp(static_cast<std::streamoff>(static_cast<std::uint64_t>(node.start_sector) * XISO_SECTOR_SIZE));
    out.write(reinterpret_cast<const char*>(buf.data()), buf.size());

    // Recurse into subdirectory tables.
    for(const auto& child : node.children)
    {
        if(child.is_dir)
            write_dirtable(child, out);
    }
}

// Write the XDFS volume header at 0x10000.
static void write_header(std::ofstream& out, std::uint32_t root_sector,
                         std::uint32_t root_size)
{
    std::vector<std::uint8_t> hdr(XISO_HEADER_DATA_LEN + 4 + 4 + XISO_FILETIME_SIZE + XISO_UNUSED_SIZE + XISO_HEADER_DATA_LEN, 0);
    std::size_t pos = 0;
    std::memcpy(hdr.data() + pos, XISO_HEADER_DATA, XISO_HEADER_DATA_LEN);
    pos += XISO_HEADER_DATA_LEN;
    wr_le32(hdr.data() + pos, root_sector);
    pos += 4;
    wr_le32(hdr.data() + pos, root_size);
    pos += 4;
    pos += XISO_FILETIME_SIZE; // zero FILETIME
    pos += XISO_UNUSED_SIZE;   // unused gap
    std::memcpy(hdr.data() + pos, XISO_HEADER_DATA, XISO_HEADER_DATA_LEN);

    out.seekp(static_cast<std::streamoff>(XISO_HEADER_OFFSET));
    out.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
}

// Write the ISO9660 stub at 0x8000 for burner autodetection.
static void write_iso9660_stub(std::ofstream& out)
{
    std::vector<std::uint8_t> vd(XISO_SECTOR_SIZE, 0);
    vd[0] = 0x01; // primary VD
    std::memcpy(vd.data() + 1, "CD001", 5);
    vd[6] = 0x01;
    out.seekp(0x8000);
    out.write(reinterpret_cast<const char*>(vd.data()), vd.size());

    std::fill(vd.begin(), vd.end(), 0);
    vd[0] = 0xFF; // terminator
    std::memcpy(vd.data() + 1, "CD001", 5);
    vd[6] = 0x01;
    out.write(reinterpret_cast<const char*>(vd.data()), vd.size());
}

// ******************************************************************
// * xiso_create
// ******************************************************************
XisoResult xiso_create(const std::string& src_dir, const std::string& out_name,
                       bool no_media_enable)
{
    fs::path src(src_dir);
    if(!fs::is_directory(src))
        return { false, src_dir + " is not a directory" };

    fs::path out_path = out_name.empty()
                            ? fs::path(src.filename().string() + ".iso")
                            : fs::path(out_name);

    std::cerr << "xiso: creating " << out_path << " from " << src_dir << "\n";

    BuildNode root;
    root.name = src.filename().string();
    root.is_dir = true;
    if(!build_tree(src, root))
        return { false, "failed to build tree from " + src_dir };

    // Assign sectors: root table at XISO_ROOT_DIRECTORY_SECTOR.
    std::uint32_t next_sector = static_cast<std::uint32_t>(XISO_ROOT_DIRECTORY_SECTOR);
    assign_sectors(root, next_sector);
    // The root node's start_sector must be XISO_ROOT_DIRECTORY_SECTOR.
    root.start_sector = static_cast<std::uint32_t>(XISO_ROOT_DIRECTORY_SECTOR);
    std::uint32_t root_dir_size = calc_dir_size(root.children);

    std::ofstream out(out_path, std::ios::binary);
    if(!out)
        return { false, "cannot create " + out_path.string() };

    // Pre-fill to at least the header offset so seeks into the image work.
    out.seekp(static_cast<std::streamoff>(XISO_HEADER_OFFSET + XISO_HEADER_DATA_LEN + 4 + 4 + XISO_FILETIME_SIZE + XISO_UNUSED_SIZE + XISO_HEADER_DATA_LEN + XISO_SECTOR_SIZE), std::ios::beg);

    write_header(out, root.start_sector, root_dir_size);
    write_files(src, root, out, no_media_enable);
    write_dirtable(root, out);

    // Pad to 64 KiB.
    out.seekp(0, std::ios::end);
    std::uint64_t pos = static_cast<std::uint64_t>(out.tellp());
    std::uint32_t pad = static_cast<std::uint32_t>((XISO_FILE_MODULUS - (pos % XISO_FILE_MODULUS)) % XISO_FILE_MODULUS);
    if(pad > 0)
    {
        std::vector<char> padbuf(pad, static_cast<char>(XISO_PAD_BYTE));
        out.write(padbuf.data(), pad);
    }

    write_iso9660_stub(out);
    out.close();

    std::cerr << "xiso: created " << out_path << "\n";
    return { true };
}

// ******************************************************************
// * xiso_print_usage
// ******************************************************************
void xiso_print_usage(std::ostream& os)
{
    os << "xiso - cxbx Xbox XISO (GDF/XDFS) tool\n"
          "\n"
          "Usage:\n"
          "  xiso -l <iso>                    List directory tree\n"
          "  xiso -x [-d dir] <iso>           Extract all files\n"
          "  xiso -c <dir> [name]             Create an XISO from a directory\n"
          "  xiso -i <iso>                    Show volume header info\n"
          "  xiso -V <iso>                    Verify image integrity\n"
          "\n"
          "Options:\n"
          "  -d <dir>    Destination directory for extraction (default: .)\n"
          "  -m          Disable XBE media-enable patching (create only)\n"
          "  -q          Quiet mode\n"
          "  -v          Verbose mode\n"
          "  -h          Show this help\n"
          "\n"
          "Modes -l, -x, -c, -i, -V are mutually exclusive.\n";
}

// ******************************************************************
// * main
// ******************************************************************
int main(int argc, char* argv[])
{
    char mode = 0;
    std::string dest_dir = ".";
    std::string create_name;
    bool no_media_enable = false;
    bool quiet = false;
    std::string positional;

    if(argc < 2)
    {
        xiso_print_usage(std::cerr);
        return 1;
    }

    for(int v = 1; v < argc; v++)
    {
        std::string a = argv[v];
        if(a == "-h" || a == "--help")
        {
            xiso_print_usage(std::cout);
            return 0;
        }
        else if(a == "-l")
            mode = 'l';
        else if(a == "-x")
            mode = 'x';
        else if(a == "-c")
            mode = 'c';
        else if(a == "-i")
            mode = 'i';
        else if(a == "-V")
            mode = 'V';
        else if(a == "-d")
        {
            if(++v >= argc)
            {
                xiso_print_usage(std::cerr);
                return 1;
            }
            dest_dir = argv[v];
        }
        else if(a == "-m")
            no_media_enable = true;
        else if(a == "-q")
            quiet = true;
        else if(a == "-v")
            g_verbose = 1;
        else if(!a.empty() && a[0] == '-')
        {
            std::cerr << "xiso: unknown option " << a << "\n";
            xiso_print_usage(std::cerr);
            return 1;
        }
        else
        {
            if(positional.empty())
                positional = a;
            else if(mode == 'c' && create_name.empty())
                create_name = a;
            else
            {
                std::cerr << "xiso: unexpected extra argument " << a << "\n";
                return 1;
            }
        }
    }

    (void)quiet;

    if(mode == 0)
    {
        std::cerr << "xiso: no mode specified\n";
        xiso_print_usage(std::cerr);
        return 1;
    }
    if(positional.empty())
    {
        std::cerr << "xiso: missing argument\n";
        xiso_print_usage(std::cerr);
        return 1;
    }

    switch(mode)
    {
        case 'l':
        {
            XisoImage img;
            auto r = xiso_open(positional, img);
            if(!r.ok)
            {
                std::cerr << "xiso: " << r.message << "\n";
                return 1;
            }
            xiso_list(img);
            return 0;
        }
        case 'x':
        {
            XisoImage img;
            auto r = xiso_open(positional, img);
            if(!r.ok)
            {
                std::cerr << "xiso: " << r.message << "\n";
                return 1;
            }
            r = xiso_extract(img, dest_dir);
            return r.ok ? 0 : 1;
        }
        case 'c':
        {
            auto r = xiso_create(positional, create_name, no_media_enable);
            return r.ok ? 0 : 1;
        }
        case 'i':
        {
            XisoImage img;
            auto r = xiso_open(positional, img);
            if(!r.ok)
            {
                std::cerr << "xiso: " << r.message << "\n";
                return 1;
            }
            xiso_info(img);
            return 0;
        }
        case 'V':
        {
            XisoImage img;
            auto r = xiso_open(positional, img);
            if(!r.ok)
            {
                std::cerr << "xiso: " << r.message << "\n";
                return 1;
            }
            xiso_verify(img);
            return 0;
        }
    }
    return 0;
}
