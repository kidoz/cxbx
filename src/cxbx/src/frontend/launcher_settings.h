#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cxbx::frontend
{
struct video_settings
{
    int adapter = 0;
    int device = 0;
    std::string resolution;
    bool fullscreen = false;
    bool vsync = false;
};
video_settings read_video_settings();
void write_video_settings(const video_settings& settings);
std::vector<std::string> video_adapters();
std::vector<std::string> video_resolutions(int adapter);

class controller_settings
{
  public:
    controller_settings();
    ~controller_settings();
    void begin(void* window, int object);
    std::optional<std::string> poll();
    void cancel();
    void accept();
    static std::vector<std::string> objects();

  private:
    struct impl;
    std::unique_ptr<impl> state_;
};
} // namespace cxbx::frontend
