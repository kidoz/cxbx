// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbx->win_main.cpp
// *
// *  This file is part of the cxbx project.
// *
// *  cxbx and cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file LICENSE.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#include "launcher_session.h"
#include "win32/xbe_to_pe_converter.h"
#include "core/xbe.h"
#include <windows.h>
#include "core/exe.h"
#include "shared_runtime_state.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace cxbx::frontend
{
static void AppendLogLine(const char* szLogFile, const char* szLine)
{
    if(szLogFile == NULL || szLogFile[0] == '\0')
    {
        return;
    }

    HANDLE hFile = CreateFile(szLogFile, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if(hFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD dwWritten = 0;
    WriteFile(hFile, szLine, (DWORD)strlen(szLine), &dwWritten, NULL);
    WriteFile(hFile, "\r\n", 2, &dwWritten, NULL);
    CloseHandle(hFile);
}

void configure_log_file(const char* szLogFile)
{
    if(szLogFile == NULL || szLogFile[0] == '\0')
    {
        return;
    }

    AppendLogLine(szLogFile, "--- cxbx launcher pre-crt log ---");

    SetEnvironmentVariable("CXBX_LOG_FILE", szLogFile);

    FILE* out = freopen(szLogFile, "a", stdout);
    if(out != NULL)
    {
        setvbuf(out, NULL, _IONBF, 0);
    }

    FILE* err = freopen(szLogFile, "a", stderr);
    if(err != NULL)
    {
        setvbuf(err, NULL, _IONBF, 0);
    }

    printf("\n--- cxbx launcher start ---\n");
}

static void GetModuleDirectory(char* szDirectory)
{
    GetModuleFileName(NULL, szDirectory, 260);

    sint32 spot = -1;
    for(int v = 0; v < 260; v++)
    {
        if(szDirectory[v] == '\\')
        {
            spot = v;
        }
        else if(szDirectory[v] == '\0')
        {
            break;
        }
    }

    if(spot != -1)
    {
        szDirectory[spot] = '\0';
    }
}

static void BuildTempExePath(const char* xbe_path, char* exe_path)
{
    char root[MAX_PATH]{};
    DWORD length = GetTempPathA(MAX_PATH, root);
    if(length == 0 || length >= MAX_PATH)
    {
        throw std::runtime_error("Could not resolve the Windows temporary directory.");
    }
    std::string name = xbe_path;
    const auto separator = name.find_last_of("\\/");
    if(separator != std::string::npos)
    {
        name.erase(0, separator + 1);
    }
    const auto extension = name.find_last_of('.');
    if(extension != std::string::npos)
    {
        name.resize(extension);
    }
    const std::string path = std::string(root) + name + ".exe";
    if(path.size() >= MAX_PATH)
    {
        throw std::runtime_error("The generated executable path is too long.");
    }
    memcpy(exe_path, path.c_str(), path.size() + 1);
}

static void BuildXbeDirectory(const char* szXbePath, char* szDirectory)
{
    strncpy(szDirectory, szXbePath, 259);
    szDirectory[259] = '\0';

    char* szSlash = strrchr(szDirectory, '\\');
    if(szSlash == NULL)
    {
        szSlash = strrchr(szDirectory, '/');
    }

    if(szSlash != NULL)
    {
        *szSlash = '\0';
    }
    else
    {
        GetModuleDirectory(szDirectory);
    }
}

static void BuildAbsolutePath(const char* szPath, char* szAbsolutePath)
{
    DWORD dwAbsolutePath = GetFullPathName(szPath, 260, szAbsolutePath, NULL);
    if(dwAbsolutePath == 0 || dwAbsolutePath >= 260)
    {
        strncpy(szAbsolutePath, szPath, 259);
        szAbsolutePath[259] = '\0';
    }
}

static void CopyRuntimeDllNextToExe(const char* szExePath)
{
    char szModuleDirectory[260];
    char szSourceDll[260];
    char szTargetDll[260];

    GetModuleDirectory(szModuleDirectory);
    snprintf(szSourceDll, sizeof(szSourceDll), "%s\\Cxbx.dll", szModuleDirectory);

    strncpy(szTargetDll, szExePath, sizeof(szTargetDll) - 1);
    szTargetDll[sizeof(szTargetDll) - 1] = '\0';

    char* szSlash = strrchr(szTargetDll, '\\');
    if(szSlash == NULL)
    {
        szSlash = strrchr(szTargetDll, '/');
    }

    if(szSlash != NULL)
    {
        strcpy(szSlash + 1, "Cxbx.dll");
    }
    else
    {
        strcpy(szTargetDll, "Cxbx.dll");
    }

    if(CopyFile(szSourceDll, szTargetDll, FALSE))
    {
        printf("cxbx: copied runtime DLL to %s.\n", szTargetDll);
    }
    else
    {
        printf("cxbx: failed to copy runtime DLL from %s to %s (error=%lu).\n", szSourceDll, szTargetDll, GetLastError());
    }
}

// The guest process resolves d3d8.dll from its own directory first, so the
// dll set staged here (DXVK, delivered next to cxbx.exe by the build; see
// third_party/dxvk/3.1/README.md) replaces the system d3d8 for the launch.
// The system d3d8 shipped by the 2026-08-12 Windows update accepts every
// call the HLE makes but rasterizes nothing, so the override is what makes
// titles render. Best-effort: without the directory the launch proceeds
// against the system d3d8.
static void CopyHostD3D8DllsNextToExe(const char* szExePath)
{
    char szTargetDirectory[260];
    strncpy(szTargetDirectory, szExePath, sizeof(szTargetDirectory) - 1);
    szTargetDirectory[sizeof(szTargetDirectory) - 1] = '\0';

    char* szSlash = strrchr(szTargetDirectory, '\\');
    if(szSlash == NULL)
    {
        szSlash = strrchr(szTargetDirectory, '/');
    }
    if(szSlash == NULL)
    {
        return;
    }
    *szSlash = '\0';

    if(GetEnvironmentVariableA("CXBX_NO_HOST_D3D8", NULL, 0) != 0)
    {
        // Remove a previously staged pair so the switch really falls back to
        // the system d3d8 even when %TEMP% still holds an earlier copy.
        const char* szStagedNames[] = { "d3d8.dll", "d3d9.dll" };
        for(unsigned n = 0; n < sizeof(szStagedNames) / sizeof(szStagedNames[0]); n++)
        {
            char szStaged[260];
            snprintf(szStaged, sizeof(szStaged), "%s\\%s", szTargetDirectory, szStagedNames[n]);
            DeleteFileA(szStaged);
        }
        printf("cxbx: host d3d8 staging disabled (CXBX_NO_HOST_D3D8).\n");
        return;
    }

    char szModuleDirectory[260];
    char szSourceDirectory[260];
    GetModuleDirectory(szModuleDirectory);

    char szEnvDirectory[260];
    const DWORD dwEnvLength = GetEnvironmentVariableA("CXBX_HOST_D3D8_DIR", szEnvDirectory, sizeof(szEnvDirectory));
    if(dwEnvLength != 0 && dwEnvLength < sizeof(szEnvDirectory) && strchr(szEnvDirectory, ':') != NULL)
    {
        snprintf(szSourceDirectory, sizeof(szSourceDirectory), "%s", szEnvDirectory);
    }
    else if(dwEnvLength != 0 && dwEnvLength < sizeof(szEnvDirectory))
    {
        snprintf(szSourceDirectory, sizeof(szSourceDirectory), "%s\\%s", szModuleDirectory, szEnvDirectory);
    }
    else
    {
        snprintf(szSourceDirectory, sizeof(szSourceDirectory), "%s\\host-d3d8", szModuleDirectory);
    }

    char szPattern[260];
    snprintf(szPattern, sizeof(szPattern), "%s\\*.dll", szSourceDirectory);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(szPattern, &findData);
    if(hFind == INVALID_HANDLE_VALUE)
    {
        printf("cxbx: no host d3d8 override in %s (using the system d3d8).\n", szSourceDirectory);
        return;
    }

    do
    {
        char szSource[260];
        char szTarget[260];
        snprintf(szSource, sizeof(szSource), "%s\\%s", szSourceDirectory, findData.cFileName);
        snprintf(szTarget, sizeof(szTarget), "%s\\%s", szTargetDirectory, findData.cFileName);

        if(CopyFile(szSource, szTarget, FALSE))
        {
            printf("cxbx: staged host d3d8 override %s.\n", findData.cFileName);
        }
        else
        {
            printf("cxbx: failed to stage %s from %s (error=%lu).\n", findData.cFileName, szSourceDirectory, GetLastError());
        }
    } while(FindNextFileA(hFind, &findData));

    FindClose(hFind);
}

static void* launch_generated(const char* szExePath, const char* xbe_path, const char (&shared_path)[260])
{
    CopyRuntimeDllNextToExe(szExePath);
    CopyHostD3D8DllsNextToExe(szExePath);

    cxbx::platform::SetSharedXbePath(shared_path);

    char szWorkingDirectory[260];
    BuildXbeDirectory(xbe_path, szWorkingDirectory);

    // Spawn suspended so the low Xbox-RAM window (< 0x04000000) can be reserved
    // in the child before its loader/CRT fragments it. Contiguous ("physical")
    // guest memory is committed from that window, which keeps host==physical in
    // the low 28 address bits -- required by the NV2A DMA (nxdk pbkit and titles
    // that program the pushbuffer with masked-physical addresses).
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if(!CreateProcessA(szExePath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED,
                       NULL, szWorkingDirectory, &si, &pi))
    {
        printf("cxbx: failed to launch %s (error=%lu).\n", szExePath, GetLastError());
        throw std::runtime_error("Could not create guest process (Win32 error " + std::to_string(GetLastError()) + ").");
    }

    // Xbox RAM window for contiguous memory: 0x01000000..0x04000000 (48 MiB),
    // above the XBE image (loads at 0x00010000) and below the 64 MiB boundary.
    // Reserve as much of the free region at 0x01000000 as is contiguously free
    // (it typically runs to ~0x02000000). Guest contiguous memory is committed
    // from it, keeping those "physical" pointers below the 64 MiB Xbox RAM line.
    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    void* pReserved = NULL;
    ULONG reserveSize = 0;
    if(VirtualQueryEx(pi.hProcess, (void*)(uintptr_t)0x01000000, &mbi, sizeof(mbi)) != 0 &&
       mbi.State == MEM_FREE && (uintptr_t)mbi.BaseAddress == 0x01000000)
    {
        reserveSize = (ULONG)mbi.RegionSize & ~0xFFFFul; // 64 KiB granularity
        if(reserveSize > 0x03000000)
        {
            reserveSize = 0x03000000; // cap at the 64 MiB line
        }
        if(reserveSize >= 0x00100000)
        {
            pReserved = VirtualAllocEx(pi.hProcess, (void*)(uintptr_t)0x01000000,
                                       reserveSize, MEM_RESERVE, PAGE_READWRITE);
        }
    }
    printf("cxbx: child Xbox-RAM window 0x01000000 size 0x%lX (%s).\n",
           reserveSize, pReserved ? "ok" : "failed");

    // The generated exe is large-address-aware so the emulator can back parts
    // of the Xbox physical window with real memory (EmuInit's page-0 window).
    // LAA also makes the high half allocatable by the child's own heap, which
    // must never land inside a trap-emulated aperture -- the emu range checks
    // would misclassify such host pointers. Fence the trapped windows off with
    // PAGE_NOACCESS reservations before the child runs: reserved-untouchable
    // pages still fault on guest access, so trap-and-emulate is unchanged.
    // Best-effort per 16 MiB chunk; on a non-LAA image these fail closed.
    {
        static const struct
        {
            ULONG Base;
            ULONG Size;
        } kFences[] = {
            { 0x80000000, 0x10000000 }, // Xbox physical identity view
            { 0xF0000000, 0x0D000000 }, // physical/AGP shadow aperture
            { 0xFD000000, 0x01000000 }, // NV2A MMIO
            { 0xFE000000, 0x01000000 }, // APU/ACI/USB/NVNET stub MMIO
            { 0xFF000000, 0x00F00000 }, // flash/BIOS aperture (see EmuFlashInit)
        };
        for(unsigned f = 0; f < sizeof(kFences) / sizeof(kFences[0]); f++)
        {
            ULONG Done = 0;
            for(ULONG Off = 0; Off < kFences[f].Size; Off += 0x01000000)
            {
                ULONG Chunk = kFences[f].Size - Off;
                if(Chunk > 0x01000000)
                {
                    Chunk = 0x01000000;
                }
                if(VirtualAllocEx(pi.hProcess, (void*)(uintptr_t)(kFences[f].Base + Off),
                                  Chunk, MEM_RESERVE, PAGE_NOACCESS) != NULL)
                {
                    Done += Chunk;
                }
            }
            printf("cxbx: child trap fence 0x%08lX size 0x%lX reserved 0x%lX.\n",
                   kFences[f].Base, kFences[f].Size, Done);
        }
    }

    if(ResumeThread(pi.hThread) == static_cast<DWORD>(-1))
    {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        throw std::runtime_error("Could not resume guest process.");
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

struct launcher_session::impl
{
    std::unique_ptr<Xbe> xbe;
    std::string filename;
    bool dirty = false;
    HANDLE process = nullptr;
    ~impl()
    {
        if(process)
        {
            CloseHandle(process);
        }
    }
};

static void check(Error& object)
{
    if(object.GetError())
    {
        std::string error = object.GetError();
        object.ClearError();
        throw std::runtime_error(error);
    }
}

static std::string absolute_path(const std::string& path)
{
    char result[MAX_PATH];
    DWORD length = GetFullPathNameA(path.c_str(), MAX_PATH, result, nullptr);
    if(path.empty() || length == 0 || length >= MAX_PATH)
    {
        throw std::runtime_error("A nonempty path shorter than 260 bytes is required.");
    }
    return result;
}

static void remember(std::vector<std::string>& recent, const std::string& path)
{
    std::erase_if(recent, [&](const auto& entry)
                  { return _stricmp(entry.c_str(), path.c_str()) == 0; });
    recent.insert(recent.begin(), path);
    if(recent.size() > 10)
    {
        recent.resize(10);
    }
}

launcher_session::launcher_session() : state_(std::make_unique<impl>())
{
    HKEY key;
    if(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\cxbx", 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return;
    }
    auto number = [&](const char* name, int fallback, int maximum)
    {
        DWORD value = fallback, size = sizeof(value);
        if(RegGetValueA(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS || value > static_cast<DWORD>(maximum))
        {
            return fallback;
        }
        return static_cast<int>(value);
    };
    auto string = [&](const char* name)
    {
        char value[260] = {};
        DWORD size = sizeof(value);
        if(RegGetValueA(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS)
        {
            return std::string{};
        }
        return std::string(value);
    };
    preferences.generation = number("AutoConvertToExe", 2, 2);
    preferences.launcher_debug = number("CxbxDebug", 0, 2);
    preferences.kernel_debug = number("KrnlDebug", 0, 2);
    preferences.launcher_log = string("CxbxDebugFilename");
    preferences.kernel_log = string("KrnlDebugFilename");
    for(auto [name, entries] : { std::pair{ "RecentXbe", &preferences.recent_xbe }, { "RecentExe", &preferences.recent_exe } })
    {
        int count = number(name, 0, 10);
        for(int i = 0; i < count; ++i)
        {
            auto value = string((std::string(name) + std::to_string(i)).c_str());
            if(!value.empty())
            {
                entries->push_back(value);
            }
        }
    }
    RegCloseKey(key);
}

launcher_session::~launcher_session() = default;

void launcher_session::persist_preferences()
{
    HKEY key;
    if(RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\cxbx", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    {
        throw std::runtime_error("Could not save launcher preferences.");
    }
    bool success = true;
    auto number = [&](const char* name, DWORD value)
    { success &= RegSetValueExA(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS; };
    auto string = [&](const char* name, const std::string& value)
    { success &= RegSetValueExA(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>(value.size()) + 1u) == ERROR_SUCCESS; };
    number("AutoConvertToExe", preferences.generation);
    number("CxbxDebug", preferences.launcher_debug);
    number("KrnlDebug", preferences.kernel_debug);
    string("CxbxDebugFilename", preferences.launcher_log);
    string("KrnlDebugFilename", preferences.kernel_log);
    for(auto [name, entries] : { std::pair{ "RecentXbe", &preferences.recent_xbe }, { "RecentExe", &preferences.recent_exe } })
    {
        number(name, static_cast<DWORD>(entries->size()));
        for(size_t i = 0; i < entries->size(); ++i)
        {
            string((std::string(name) + std::to_string(i)).c_str(), (*entries)[i]);
        }
    }
    RegCloseKey(key);
    if(!success)
    {
        throw std::runtime_error("Could not save all launcher preferences.");
    }
}

void launcher_session::open(const std::string& path, bool import_exe)
{
    if(running())
    {
        throw std::runtime_error("Close the guest before opening another title.");
    }
    auto full = absolute_path(path);
    std::unique_ptr<Xbe> candidate;
    if(import_exe)
    {
        Exe exe(full.c_str());
        check(exe);
        candidate = std::make_unique<Xbe>(&exe, "Untitled", true);
    }
    else
    {
        candidate = std::make_unique<Xbe>(full.c_str());
    }
    check(*candidate);
    state_->xbe = std::move(candidate);
    state_->filename = import_exe ? std::string{} : full;
    state_->dirty = import_exe;
    remember(import_exe ? preferences.recent_exe : preferences.recent_xbe, full);
}

void launcher_session::close()
{
    if(running())
    {
        throw std::runtime_error("Close the guest before closing its document.");
    }
    state_->xbe.reset();
    state_->filename.clear();
    state_->dirty = false;
}

void launcher_session::save(const std::string& path)
{
    if(!loaded())
    {
        throw std::runtime_error("Open an XBE first.");
    }
    auto full = absolute_path(path);
    state_->xbe->Export(full.c_str());
    check(*state_->xbe);
    state_->filename = full;
    // Save As changes the directory used by the guest's file APIs too.
    const auto slash = full.find_last_of('\\');
    const auto directory = full.substr(0, slash + 1);
    memset(state_->xbe->m_szPath, 0, sizeof(state_->xbe->m_szPath));
    memcpy(state_->xbe->m_szPath, directory.c_str(), directory.size());
    state_->dirty = false;
    remember(preferences.recent_xbe, full);
}

void launcher_session::export_exe(const std::string& path)
{
    if(!loaded())
    {
        throw std::runtime_error("Open an XBE first.");
    }
    auto full = absolute_path(path);
    std::string log = preferences.kernel_log.empty() ? "" : absolute_path(preferences.kernel_log);
    if(preferences.kernel_debug == DM_FILE && log.empty())
    {
        throw std::runtime_error("Choose a kernel log file in preferences.");
    }
    // The converter copies the entire legacy 260-byte field into its prolog.
    char log_field[260]{};
    memcpy(log_field, log.c_str(), log.size() + 1);
    EmuExe exe(state_->xbe.get(), static_cast<DebugMode>(preferences.kernel_debug), log_field);
    check(exe);
    exe.Export(full.c_str());
    check(exe);
}

bool launcher_session::loaded() const
{
    return state_->xbe != nullptr;
}
bool launcher_session::dirty() const
{
    return state_->dirty;
}
bool launcher_session::running() const
{
    return state_->process != nullptr;
}
std::string launcher_session::path() const
{
    return state_->filename;
}

std::string launcher_session::description() const
{
    if(!loaded())
    {
        return "Open an Xbox executable to begin.";
    }
    char info[512];
    snprintf(info, sizeof(info), "%s\nTitle ID: %08lX   Sections: %lu\n%s | %s\n%s%s", state_->xbe->m_szAsciiTitle,
             state_->xbe->m_Certificate.dwTitleId, state_->xbe->m_Header.dwSections,
             state_->xbe->m_Header.dwInitFlags.bLimit64MB ? "64 MB limit" : "More than 64 MB allowed",
             (state_->xbe->m_Header.dwEntryAddr ^ XOR_EP_RETAIL) > 0x01000000 ? "Debug" : "Retail",
             state_->filename.empty() ? "Unsaved XBE" : state_->filename.c_str(), dirty() ? " (modified)" : "");
    return info;
}

std::array<std::uint32_t, 1700> launcher_session::logo() const
{
    std::array<std::uint32_t, 1700> result{};
    if(loaded())
    {
        uint08 gray[1700] = {};
        state_->xbe->ExportLogoBitmap(gray);
        for(size_t i = 0; i < result.size(); ++i)
        {
            result[i] = 0xff000000u | (gray[i] * 0x010101u);
        }
    }
    return result;
}

void launcher_session::patch_memory()
{
    if(loaded())
    {
        state_->xbe->m_Header.dwInitFlags.bLimit64MB = !state_->xbe->m_Header.dwInitFlags.bLimit64MB;
        state_->dirty = true;
    }
}

void launcher_session::patch_debug()
{
    if(loaded())
    {
        state_->xbe->m_Header.dwEntryAddr ^= XOR_EP_RETAIL ^ XOR_EP_DEBUG;
        state_->xbe->m_Header.dwKernelImageThunkAddr ^= XOR_KT_RETAIL ^ XOR_KT_DEBUG;
        state_->dirty = true;
    }
}

void launcher_session::dump(const std::string& path)
{
    if(!loaded())
    {
        throw std::runtime_error("Open an XBE first.");
    }
    FILE* file = path.empty() ? stdout : fopen(absolute_path(path).c_str(), "w");
    if(!file)
    {
        throw std::runtime_error("Could not open information file.");
    }
    state_->xbe->DumpInformation(file);
    bool failed = ferror(file) != 0;
    if(file != stdout)
    {
        failed |= fclose(file) != 0;
    }
    if(failed)
    {
        throw std::runtime_error("Could not write information file.");
    }
}

void launcher_session::logo_file(const std::string& path, bool importing)
{
    if(!loaded())
    {
        throw std::runtime_error("Open an XBE first.");
    }
    auto full = absolute_path(path);
    using file_ptr = std::unique_ptr<FILE, decltype(&fclose)>;
    file_ptr file(fopen(full.c_str(), importing ? "rb" : "wb"), fclose);
    if(!file)
    {
        throw std::runtime_error("Could not open bitmap file.");
    }
    BITMAPFILEHEADER header{};
    BITMAPINFOHEADER info{};
    std::array<unsigned char, 5100> pixels{};
    uint08 gray[1700]{};
    if(importing)
    {
        if(fread(&header, sizeof(header), 1, file.get()) != 1 || fread(&info, sizeof(info), 1, file.get()) != 1 ||
           header.bfType != 0x4d42 || info.biSize != sizeof(info) || info.biWidth != 100 ||
           (info.biHeight != 17 && info.biHeight != -17) || info.biBitCount != 24 || info.biPlanes != 1 || info.biCompression != BI_RGB ||
           header.bfOffBits < sizeof(header) + sizeof(info) || header.bfOffBits > 1024 * 1024 ||
           fseek(file.get(), static_cast<long>(header.bfOffBits), SEEK_SET) != 0 || fread(pixels.data(), pixels.size(), 1, file.get()) != 1)
        {
            throw std::runtime_error("Logo must be an uncompressed 100 x 17, 24-bit BMP.");
        }
        for(int y = 0; y < 17; ++y)
        {
            for(int x = 0; x < 100; ++x)
            {
                int offset = ((info.biHeight < 0 ? y : 16 - y) * 100 + x) * 3;
                gray[y * 100 + x] = static_cast<uint08>((pixels[offset] + pixels[offset + 1] + pixels[offset + 2]) / 3);
            }
        }
        state_->xbe->ImportLogoBitmap(gray);
        check(*state_->xbe);
        state_->dirty = true;
    }
    else
    {
        state_->xbe->ExportLogoBitmap(gray);
        check(*state_->xbe);
        for(size_t i = 0; i < pixels.size(); ++i)
        {
            pixels[i] = gray[i / 3];
        }
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(info);
        header.bfSize = header.bfOffBits + static_cast<DWORD>(pixels.size());
        info.biSize = sizeof(info);
        info.biWidth = 100;
        info.biHeight = -17;
        info.biPlanes = 1;
        info.biBitCount = 24;
        info.biSizeImage = static_cast<DWORD>(pixels.size());
        if(fwrite(&header, sizeof(header), 1, file.get()) != 1 || fwrite(&info, sizeof(info), 1, file.get()) != 1 ||
           fwrite(pixels.data(), pixels.size(), 1, file.get()) != 1 || fflush(file.get()) != 0)
        {
            throw std::runtime_error("Could not write logo bitmap.");
        }
    }
}

void launcher_session::start(const std::string& manual_path)
{
    if(running())
    {
        throw std::runtime_error("A guest is already running.");
    }
    if(!loaded() || state_->filename.empty())
    {
        throw std::runtime_error("Save the XBE before starting emulation.");
    }
    char generated[MAX_PATH];
    BuildTempExePath(state_->filename.c_str(), generated);
    std::string exe = generated;
    if(preferences.generation == 0)
    {
        exe = absolute_path(manual_path);
    }
    if(preferences.generation == 1)
    {
        exe = state_->filename.substr(0, state_->filename.find_last_of('.')) + ".exe";
    }
    export_exe(exe);
    state_->process = launch_generated(exe.c_str(), state_->filename.c_str(), state_->xbe->m_szPath);
}

std::optional<std::uint32_t> launcher_session::poll_exit()
{
    if(!running() || WaitForSingleObject(state_->process, 0) == WAIT_TIMEOUT)
    {
        return std::nullopt;
    }
    DWORD code = 1;
    GetExitCodeProcess(state_->process, &code);
    CloseHandle(state_->process);
    state_->process = nullptr;
    return code;
}

int launcher_session::wait()
{
    if(!running())
    {
        return 1;
    }
    WaitForSingleObject(state_->process, INFINITE);
    return static_cast<int>(poll_exit().value_or(1));
}

int run_xbe_batch(const char* path, const char* log)
{
    try
    {
        launcher_session session;
        session.preferences.generation = 2;
        session.preferences.kernel_debug = log && *log ? DM_FILE : DM_NONE;
        session.preferences.kernel_log = log ? log : "";
        session.open(path);
        session.start();
        int code = session.wait();
        printf("cxbx: batch process exited with code %lu.\n", static_cast<unsigned long>(code));
        return code;
    }
    catch(const std::exception& error)
    {
        printf("cxbx: %s\n", error.what());
        return 1;
    }
}
} // namespace cxbx::frontend
