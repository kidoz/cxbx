// Document/converter regression tests. No guest boot or user configuration writes.
#include "../../src/cxbx/src/frontend/launcher_session.h"
#include "core/xbe.h"
#include "shared_runtime_state.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using cxbx::frontend::launcher_session;
static void require(bool condition, const char* message)
{
    if(!condition)
    {
        throw std::runtime_error(message);
    }
}
static std::vector<char> read(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), {} };
}
static void fixture(const std::string& path)
{
    std::array<char, 8192> image{};
    Xbe::Header header{};
    header.dwMagic = 0x48454258;
    header.dwBaseAddr = 0x10000;
    header.dwSizeofHeaders = 4096;
    header.dwSizeofImage = 8192;
    header.dwSizeofImageHeader = sizeof(header);
    header.dwCertificateAddr = 0x10200;
    header.dwSections = 1;
    header.dwSectionHeadersAddr = 0x10400;
    header.dwEntryAddr = 0x11000u ^ XOR_EP_RETAIL;
    header.dwKernelImageThunkAddr = 0x11010u ^ XOR_KT_RETAIL;
    header.dwPeStackCommit = 4096;
    header.dwPeHeapReserve = 0x100000;
    header.dwPeHeapCommit = 4096;
    header.dwLogoBitmapAddr = 0x10800;
    header.dwSizeofLogoBitmap = 512;
    Xbe::Certificate certificate{};
    certificate.dwSize = sizeof(certificate);
    certificate.dwTitleId = 0x12345678;
    const wchar_t title[] = L"Launcher regression";
    memcpy(certificate.wszTitleName, title, sizeof(title));
    Xbe::SectionHeader section{};
    section.dwFlags.bExecutable = 1;
    section.dwVirtualAddr = 0x11000;
    section.dwVirtualSize = 4096;
    section.dwRawAddr = 4096;
    section.dwSizeofRaw = 4096;
    section.dwSectionNameAddr = 0x10500;
    memcpy(image.data(), &header, sizeof(header));
    memcpy(image.data() + 0x200, &certificate, sizeof(certificate));
    memcpy(image.data() + 0x400, &section, sizeof(section));
    memcpy(image.data() + 0x500, ".text", 6);
    image[0x800] = 0x11; // short logo run
    image[0x1000] = static_cast<char>(0xc3);
    std::ofstream file(path, std::ios::binary);
    file.write(image.data(), image.size());
    require(file.good(), "fixture write failed");
}

int main(int argc, char** argv)
{
    try
    {
        cxbx::platform::DisableSharedRuntimePersist();
        require(argc == 2, "temporary test directory required");
        std::string root = std::string(argv[1]) + "\\";
        fixture(root + "source.xbe");
        launcher_session session;
        session.preferences.kernel_debug = 0;
        session.preferences.kernel_log.clear();
        session.open(root + "source.xbe");
        require(session.loaded() && !session.dirty(), "open state");
        require(session.description().find("12345678") != std::string::npos, "title metadata");
        const auto original = session.description();
        bool rejected = false;
        try
        {
            session.open(root + "missing.xbe");
        }
        catch(const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected && session.description() == original, "failed open replaced document");
        session.export_exe(root + "before.exe");
        session.patch_debug();
        session.patch_debug();
        session.patch_memory();
        session.patch_memory();
        require(session.dirty(), "patch dirty state");
        session.export_exe(root + "after.exe");
        require(read(root + "before.exe") == read(root + "after.exe"), "patch roundtrip changed generated PE");
        session.save(root + "saved.xbe");
        require(!session.dirty(), "save dirty state");
        session.close();
        session.open(root + "saved.xbe");
        session.export_exe(root + "reopened.exe");
        require(read(root + "before.exe") == read(root + "reopened.exe"), "save/reopen changed generated PE");
        session.logo_file(root + "logo.bmp", false);
        auto bitmap = read(root + "logo.bmp");
        require(bitmap.size() == 5154 && bitmap[0] == 'B' && bitmap[1] == 'M', "BMP export layout");
        session.logo_file(root + "logo.bmp", true);
        require(session.dirty(), "logo import dirty state");
        const auto saved_path = session.path();
        rejected = false;
        try
        {
            session.save(root + "missing-directory\\failed.xbe");
        }
        catch(const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected && session.dirty() && session.path() == saved_path, "failed save lost changes");
        session.dump(root + "info.txt");
        require(read(root + "info.txt").size() > 100, "information dump empty");
        require(session.preferences.recent_xbe.size() <= 10, "recent list cap");
        session.open(root + "before.exe", true);
        require(session.loaded() && session.dirty() && session.path().empty(), "EXE import state");
        session.save(root + "imported.xbe");
        session.open(root + "imported.xbe");
        require(session.loaded() && !session.dirty(), "imported XBE save/reopen");
        for(int i = 0; i < 12; ++i)
        {
            auto path = root + "recent" + std::to_string(i) + ".xbe";
            fixture(path);
            session.open(path);
        }
        require(session.preferences.recent_xbe.size() == 10, "recent history did not evict old entries");
        session.open(root + "recent11.xbe");
        require(session.preferences.recent_xbe.size() == 10 &&
                    session.preferences.recent_xbe.front().find("recent11.xbe") != std::string::npos,
                "recent duplicate handling");
        session.close();
        require(!session.loaded() && !session.dirty() && !session.poll_exit(), "closed state");
        printf("Launcher document, PE roundtrip, logo and error preservation checks passed.\n");
        return 0;
    }
    catch(const std::exception& error)
    {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
