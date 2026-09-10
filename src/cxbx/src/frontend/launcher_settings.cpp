#include "launcher_settings.h"
#include "xbox_video.h"
#include "xbox_controller.h"
#include "shared_video_config.h"
#include "shared_controller_config.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace XTL
{
#include <d3d8.h>
}

namespace cxbx::frontend
{
video_settings read_video_settings()
{
    XBVideo video;
    platform::GetSharedVideoConfig(video);
    return { static_cast<int>(video.GetDisplayAdapter()), static_cast<int>(video.GetDirect3DDevice()),
             video.GetVideoResolution(), video.GetFullscreen() != 0, video.GetVSync() != 0 };
}

void write_video_settings(const video_settings& settings)
{
    if(settings.adapter < 0 || settings.device < 0 || settings.device > 1 || settings.resolution.size() >= 100)
    {
        throw std::runtime_error("Invalid video settings.");
    }
    XBVideo video;
    platform::GetSharedVideoConfig(video);
    video.SetDisplayAdapter(settings.adapter);
    video.SetDirect3DDevice(settings.device);
    video.SetVideoResolution(settings.resolution.c_str());
    video.SetFullscreen(settings.fullscreen);
    video.SetVSync(settings.vsync);
    platform::SetSharedVideoConfig(video);
}

struct release_d3d
{
    void operator()(XTL::IDirect3D8* value) const
    {
        if(value)
        {
            value->Release();
        }
    }
};
using d3d_ptr = std::unique_ptr<XTL::IDirect3D8, release_d3d>;

static d3d_ptr system_d3d()
{
    // Settings enumeration must not initialize a guest-side DXVK override that
    // happens to be next to the launcher. Keep the module alive for its COM objects.
    static HMODULE module = []
    {
        wchar_t directory[MAX_PATH]{};
        UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if(length == 0 || length >= MAX_PATH)
        {
            return static_cast<HMODULE>(nullptr);
        }
        std::wstring path = directory;
        path += L"\\d3d8.dll";
        return LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    if(!module)
    {
        throw std::runtime_error("Could not load the Windows display enumerator.");
    }
    using create_d3d = XTL::IDirect3D8*(WINAPI*)(UINT);
    auto create = reinterpret_cast<create_d3d>(GetProcAddress(module, "Direct3DCreate8"));
    if(!create)
    {
        throw std::runtime_error("Windows Direct3DCreate8 is unavailable.");
    }
    return d3d_ptr(create(D3D_SDK_VERSION));
}

std::vector<std::string> video_adapters()
{
    auto d3d = system_d3d();
    if(!d3d)
    {
        throw std::runtime_error("Could not enumerate Direct3D display adapters.");
    }
    std::vector<std::string> result;
    for(UINT i = 0; i < d3d->GetAdapterCount(); ++i)
    {
        XTL::D3DADAPTER_IDENTIFIER8 adapter{};
        if(FAILED(d3d->GetAdapterIdentifier(i, 0, &adapter)))
        {
            throw std::runtime_error("Could not read display adapter.");
        }
        result.emplace_back(adapter.Description);
    }
    return result;
}

std::vector<std::string> video_resolutions(int adapter)
{
    std::vector<std::string> result{ "Automatic (Default)" };
    auto d3d = system_d3d();
    if(!d3d || adapter < 0 || static_cast<UINT>(adapter) >= d3d->GetAdapterCount())
    {
        return result;
    }
    for(UINT i = 0; i < d3d->GetAdapterModeCount(adapter); ++i)
    {
        XTL::D3DDISPLAYMODE mode{};
        if(FAILED(d3d->EnumAdapterModes(adapter, i, &mode)))
        {
            continue;
        }
        const char* format = nullptr;
        switch(mode.Format)
        {
            case XTL::D3DFMT_X1R5G5B5: format = "16bit x1r5g5b5"; break;
            case XTL::D3DFMT_R5G6B5: format = "16bit r5g6r5"; break;
            case XTL::D3DFMT_X8R8G8B8: format = "32bit x8r8g8b8"; break;
            case XTL::D3DFMT_A8R8G8B8: format = "32bit a8r8g8b8"; break;
            default: continue;
        }
        char text[100];
        if(mode.RefreshRate)
        {
            snprintf(text, sizeof(text), "%u x %u %s (%u hz)", mode.Width, mode.Height, format, mode.RefreshRate);
        }
        else
        {
            snprintf(text, sizeof(text), "%u x %u %s", mode.Width, mode.Height, format);
        }
        if(std::find(result.begin(), result.end(), text) == result.end())
        {
            result.emplace_back(text);
        }
    }
    return result;
}

struct controller_settings::impl
{
    XBController controller;
    bool capturing = false;
};
controller_settings::controller_settings() : state_(std::make_unique<impl>())
{
    platform::GetSharedControllerConfig(state_->controller);
}
controller_settings::~controller_settings()
{
    cancel();
}
void controller_settings::begin(void* window, int object)
{
    cancel();
    if(object < 0 || object >= XBCTRL_OBJECT_COUNT || !window)
    {
        throw std::runtime_error("Select a controller input first.");
    }
    state_->controller.ClearError();
    state_->capturing = true;
    state_->controller.ConfigBegin(static_cast<HWND>(window), static_cast<XBCtrlObject>(object));
    if(state_->controller.GetError())
    {
        std::string error = state_->controller.GetError();
        cancel();
        throw std::runtime_error(error);
    }
}
std::optional<std::string> controller_settings::poll()
{
    if(!state_->capturing)
    {
        return std::nullopt;
    }
    char status[260]{};
    if(state_->controller.ConfigPoll(status))
    {
        cancel();
        return std::string(status);
    }
    if(state_->controller.GetError())
    {
        std::string error = state_->controller.GetError();
        cancel();
        throw std::runtime_error(error);
    }
    return std::nullopt;
}
void controller_settings::cancel()
{
    if(state_->capturing)
    {
        state_->controller.ConfigEnd();
        state_->capturing = false;
    }
}
void controller_settings::accept()
{
    cancel();
    platform::SetSharedControllerConfig(state_->controller);
}
std::vector<std::string> controller_settings::objects()
{
    return { XBController::m_DeviceNameLookup, XBController::m_DeviceNameLookup + XBCTRL_OBJECT_COUNT };
}
} // namespace cxbx::frontend
