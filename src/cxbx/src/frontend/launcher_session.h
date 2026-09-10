#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cxbx::frontend
{

struct launcher_preferences
{
    int generation = 2; // manual, XBE directory, temporary directory
    int launcher_debug = 0;
    int kernel_debug = 0;
    std::string launcher_log;
    std::string kernel_log;
    std::vector<std::string> recent_xbe;
    std::vector<std::string> recent_exe;
};

// This header is the C++20/C++23 boundary: no Win32, DirectX, or toolkit types.
// The UI serializes session operations; a worker owns it exclusively during I/O.
class launcher_session
{
  public:
    launcher_session();
    ~launcher_session();
    launcher_session(const launcher_session&) = delete;
    launcher_session& operator=(const launcher_session&) = delete;

    launcher_preferences preferences;
    void persist_preferences();
    void open(const std::string& path, bool import_exe = false);
    void close();
    void save(const std::string& path);
    void export_exe(const std::string& path);
    void logo_file(const std::string& path, bool importing);
    void dump(const std::string& path);
    void patch_memory();
    void patch_debug();
    void start(const std::string& manual_path = {});
    std::optional<std::uint32_t> poll_exit();
    int wait();

    bool loaded() const;
    bool dirty() const;
    bool running() const;
    std::string path() const;
    std::string description() const;
    std::array<std::uint32_t, 1700> logo() const;

  private:
    struct impl;
    std::unique_ptr<impl> state_;
};

void configure_log_file(const char* path);
int run_xbe_batch(const char* path, const char* log);

} // namespace cxbx::frontend
