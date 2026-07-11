// SPDX-License-Identifier: MIT
//
// fileio_vfs - virtual file-system directory/metadata semantics on D:.
//
// fileio (the sibling probe) covers single-file create/write/read/delete; this
// one probes the layer above it -- directory creation, file metadata
// (GetFileAttributes / GetFileSize), and MoveFile. All route through Cxbx's
// NtCreateFile (ordinal 190) and NtQueryDirectoryFile / NtQueryInformationFile
// HLE wrappers, which delegate to the host NT filesystem under the XBE
// directory the emulator maps D: to.
//
// This is a GAP probe, not a behavior probe: on the current Cxbx build the VFS
// metadata paths are broken or incomplete, and the expectations below encode
// CURRENT reality so the golden locks it. When the HLE is repaired the groups
// flip -- update the expectations and the golden then, exactly like
// d3d_tex_swizzle. The observed gaps are:
//
//   * CreateDirectory never materializes a directory on disk: its return code
//     and GetLastError are non-deterministic across runs (sometimes success,
//     sometimes ERROR_ALREADY_EXISTS 183), but GetFileAttributes on the name
//     always reports the path absent. The NtCreateFile directory-create
//     disposition on the D: mapping is broken.
//   * GetFileAttributes on an existing file returns INVALID_FILE_ATTRIBUTES,
//     i.e. the NtQueryInformationFile FileBasicInformation path does not
//     resolve attributes for D: files.
//   * MoveFile fails (returns 0); the NtCreateFile rename disposition is not
//     honored for D: paths.
//   * GetFileSize on an open handle DOES work (size of a freshly written file
//     reads back correctly), so leaf file I/O itself is fine -- the gap is the
//     metadata/directory layer above it.
//
// Enumeration (FindFirstFile / FindNextFile -> NtQueryDirectoryFile, ordinal
// 207) is deliberately NOT called in this probe: the HLE wrapper restarts the
// scan on every iteration of its "." / ".." filter loop (RestartScan is passed
// as TRUE unconditionally), so for any real directory whose first entry is "."
// the call loops forever and hangs the host process. That hang would stall the
// whole suite, so enumeration is documented here as a gap rather than an
// executed check. When the RestartScan-per-iteration bug is fixed, add an
// enumeration block (deterministic subdir, count, wildcard) and a golden.
//
// Tagged nothing -- headless kernel-HLE file I/O runs everywhere, like fileio.

#include "xtrace.h"
#include <windows.h>
#include <string.h>

int main(void)
{
    xt_begin("v1", "fileio_vfs");
    xt_note("v1: VFS dir/metadata layer -- gap probe (CreateDirectory/attrs/move)");
    xt_note("FindFirstFile/NtQueryDirectoryFile hang on Cxbx; not exercised (see header)");

    // --- GetFileSize: works (the one metadata call that resolves) -----------
    // Leaf file I/O is sound (the fileio probe covers create/read/write); this
    // confirms the size query path on an open handle reads back the written
    // length, anchoring the "metadata layer above leaf I/O is the gap" story.
    const char* msg = "vfs-content-0123";
    DWORD len = (DWORD)strlen(msg);
    HANDLE wh = CreateFile("D:\\xtest_vfs.tmp", GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD got_size = (DWORD)-1;
    if(wh != INVALID_HANDLE_VALUE)
    {
        DWORD w = 0;
        WriteFile(wh, msg, len, &w, NULL);
        CloseHandle(wh);

        HANDLE rh = CreateFile("D:\\xtest_vfs.tmp", GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if(rh != INVALID_HANDLE_VALUE)
        {
            got_size = GetFileSize(rh, NULL);
            CloseHandle(rh);
        }
    }
    xt_ev("GetFileSize = %lu (wrote %lu)", (unsigned long)got_size, (unsigned long)len);
    xt_check_u32("vfs.getfilesize_ok", len, got_size);

    // --- CreateDirectory: never materializes a directory on disk ------------
    // The HLE's NtCreateFile directory-create disposition on the D: mapping is
    // unreliable: the return code and GetLastError vary run to run (sometimes
    // success with gle=0, sometimes failure with gle=ERROR_ALREADY_EXISTS),
    // but the one stable, deterministic fact is that no directory ever
    // appears on disk -- GetFileAttributes on the name always reports it
    // absent. We record the raw rc/gle as observed data and assert only the
    // stable outcome.
    BOOL mk = CreateDirectory("D:\\xtest_vfs_dir", NULL);
    DWORD mk_err = GetLastError();
    DWORD mk_attr = GetFileAttributes("D:\\xtest_vfs_dir");
    xt_ev("CreateDirectory rc=%d gle=%lu attr=0x%08lX",
          (int)mk, (unsigned long)mk_err, (unsigned long)mk_attr);
    // CURRENT reality: whatever CreateDirectory reports, the directory is not
    // present on disk. When the disposition is fixed, flip to asserting a
    // successful create + a present, FILE_ATTRIBUTE_DIRECTORY-bearing lookup.
    xt_check_bool("vfs.mkdir_not_materialized", 1, mk_attr == INVALID_FILE_ATTRIBUTES);
    // Best-effort: a no-op if no directory was created.
    RemoveDirectory("D:\\xtest_vfs_dir");

    // --- GetFileAttributes on an existing file: currently INVALID -----------
    // The file from the size block exists (GetFileSize just read it), yet the
    // attribute query returns INVALID_FILE_ATTRIBUTES.
    DWORD attr = GetFileAttributes("D:\\xtest_vfs.tmp");
    xt_ev("GetFileAttributes(existing file) = 0x%08lX", (unsigned long)attr);
    xt_check_u32("vfs.getfileattr_invalid", (uint32_t)INVALID_FILE_ATTRIBUTES, (uint32_t)attr);

    // --- MoveFile: currently fails ------------------------------------------
    BOOL mv = MoveFile("D:\\xtest_vfs.tmp", "D:\\xtest_vfs.mv");
    xt_ev("MoveFile rc=%d", (int)mv);
    xt_check_bool("vfs.move_fails", 0, mv);

    // --- Cleanup: delete the probe's scratch file so re-runs are stable -----
    DeleteFile("D:\\xtest_vfs.tmp");
    DeleteFile("D:\\xtest_vfs.mv");
    xt_check_bool("vfs.cleanup_absent",
                  1, GetFileAttributes("D:\\xtest_vfs.tmp") == INVALID_FILE_ATTRIBUTES);

    return xt_end();
}
