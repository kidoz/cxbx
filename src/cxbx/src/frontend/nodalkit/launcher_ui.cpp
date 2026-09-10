#include "launcher_ui.h"
#include "../launcher_session.h"
#include "../launcher_settings.h"

#include <nk/layout/box_layout.h>
#include <nk/controllers/event_controller.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/platform/window_inspector.h>
#include <nk/platform/windows_interop.h>
#include <nk/style/theme.h>
#include <nk/widgets/button.h>
#include <nk/widgets/check_box.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/log_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/text_field.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>

namespace cxbx::frontend
{
namespace
{
using namespace std::chrono_literals;
using response = nk::DialogResponse;
using item = nk::NativeMenuItem;

// NodalKit uses UTF-8. The existing XBE/converter contract uses Win32 ANSI paths.
// Reject lossy paths instead of silently opening or overwriting a different file.
std::string transcode(const std::string& text, UINT from, UINT to)
{
    if(text.empty())
    {
        return {};
    }
    int count = MultiByteToWideChar(from, 0, text.c_str(), -1, nullptr, 0);
    if(!count)
    {
        throw std::runtime_error("Invalid text encoding.");
    }
    std::wstring wide(count, L'\0');
    MultiByteToWideChar(from, 0, text.c_str(), -1, wide.data(), count);
    BOOL substituted = FALSE;
    BOOL* check = to == CP_UTF8 ? nullptr : &substituted;
    count = WideCharToMultiByte(to, 0, wide.c_str(), -1, nullptr, 0, nullptr, check);
    if(!count || substituted)
    {
        throw std::runtime_error("This path cannot be represented by the emulator's Windows code page.");
    }
    std::string result(count, '\0');
    WideCharToMultiByte(to, 0, wide.c_str(), -1, result.data(), count, nullptr, check);
    result.pop_back();
    return result;
}
std::string utf8(const std::string& text)
{
    return transcode(text, CP_ACP, CP_UTF8);
}
std::string ansi(const std::string& text)
{
    return transcode(text, CP_UTF8, CP_ACP);
}

class container : public nk::Widget
{
  public:
    using nk::Widget::append_child;
};

class logo_view : public nk::ImageView
{
  public:
    logo_view() = default;
    nk::SizeRequest measure(const nk::Constraints&) const override { return { 100, 34, 200, 50 }; }
};

std::shared_ptr<container> box(nk::Orientation orientation = nk::Orientation::Vertical)
{
    auto widget = std::make_shared<container>();
    auto layout = std::make_unique<nk::BoxLayout>(orientation);
    layout->set_spacing(8);
    widget->set_layout_manager(std::move(layout));
    widget->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    return widget;
}

std::shared_ptr<nk::ComboBox> combo(const std::shared_ptr<container>& parent, const std::string& label,
                                    std::vector<std::string> entries, int selected)
{
    parent->append_child(nk::Label::create(label));
    auto result = nk::ComboBox::create();
    result->set_items(std::move(entries));
    result->set_selected_index(selected);
    result->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    parent->append_child(result);
    return result;
}

class launcher_ui
{
  public:
    explicit launcher_ui(const char* log_file, bool smoke_test) : app_(nk::ApplicationConfig{ .app_id = "org.cxbx.launcher", .app_name = "CXBX" }),
                                                                  window_({ .title = "CXBX", .width = 880, .height = 660 }), smoke_test_(smoke_test)
    {
        auto root = box();
        root->set_vertical_size_policy(nk::SizePolicy::Expanding);
        menu_ = nk::MenuBar::create();
        root->append_child(menu_);
        (void)menu_->on_action().connect([this](std::string_view action)
                                         { dispatch(std::string(action)); });
        auto buttons = box(nk::Orientation::Horizontal);
        buttons->set_margin(nk::Insets::symmetric(0, 12));
        auto open = nk::Button::create("Open XBE...");
        start_ = nk::Button::create("Start  (F5)");
        buttons->append_child(open);
        buttons->append_child(start_);
        (void)open->on_clicked().connect([this]
                                         { dispatch("open"); });
        (void)start_->on_clicked().connect([this]
                                           { dispatch("start"); });
        root->append_child(buttons);
        metadata_ = nk::Label::create();
        metadata_->set_margin(nk::Insets::symmetric(0, 12));
        metadata_->set_wrapping(true);
        root->append_child(metadata_);
        logo_ = std::make_shared<logo_view>();
        logo_->set_margin(nk::Insets::symmetric(0, 12));
        root->append_child(logo_);
        status_ = nk::Label::create("Ready");
        status_->set_margin(nk::Insets::symmetric(0, 12));
        status_->set_wrapping(true);
        root->append_child(status_);
        log_ = nk::LogView::create();
        log_->add_style_class("cxbx-log");
        log_->set_margin(nk::Insets::uniform(12));
        install_log_style();
        (void)app_.on_system_preferences_changed().connect([this](const auto&)
                                                           { install_log_style(); });
        (void)app_.on_theme_selection_changed().connect([this](const auto&)
                                                        { install_log_style(); });
        log_->set_max_lines(4000);
        log_->set_vertical_size_policy(nk::SizePolicy::Expanding);
        log_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        root->append_child(log_);
        window_.set_child(root);
        auto keyboard = std::make_shared<nk::KeyboardController>();
        (void)keyboard->on_key_pressed().connect([this](int key, int)
                                                 {
            if(key == static_cast<int>(nk::KeyCode::F5)) { dispatch("start"); } });
        root->add_controller(keyboard);
        window_.set_close_policy([this]
                                 {
            try { request_exit(); } catch(const std::exception& error) { report_error(error.what()); }
            return false; });
        (void)window_.on_close_requested().connect([this]
                                                   { app_.quit(); });
        window_.present();
        open->grab_focus();
        const auto hwnd = nk::window_hwnd(window_);
        auto icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        MONITORINFO monitor{ sizeof(monitor) };
        if(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
        {
            const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
            window_.resize(std::min(880, static_cast<int>(static_cast<float>(monitor.rcWork.right - monitor.rcWork.left) / scale) - 40),
                           std::min(660, static_cast<int>(static_cast<float>(monitor.rcWork.bottom - monitor.rcWork.top) / scale) - 60));
        }
        timer_ = app_.event_loop().set_interval(50ms, [this]
                                                { tick(); }, "launcher-poll");
        refresh();
        if(log_file && *log_file)
        {
            session_.preferences.kernel_debug = 2;
            session_.preferences.kernel_log = log_file;
        }
        else if(!smoke_test_)
        {
            apply_logging();
        }
    }

    ~launcher_ui()
    {
        app_.event_loop().cancel(timer_);
        // The async future is destroyed before session_; its destructor joins.
    }

    int run(const char* initial_path)
    {
        if(initial_path)
        {
            std::string path = initial_path;
            work("Opening XBE", [this, path]
                 { session_.open(path); }, [this]
                 {
                if(!smoke_test_) { start_emulation(true); }
                else if(std::getenv("CXBX_UI_SMOKE_LAUNCH")) {
                    session_.preferences.generation = 2;
                    window_.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::F5});
                    window_.dispatch_key_event({.type = nk::KeyEvent::Type::Release, .key = nk::KeyCode::F5});
                    if(!busy_) { smoke_failed_ = true; }
                } });
        }
        if(smoke_test_)
        {
            (void)app_.event_loop().set_timeout(1500ms, [this]
                                                {
                // Exercise the real widget tree and asynchronous modal lifetime.
                if(busy_) { smoke_pending_ = true; return; }
                finish_smoke(); }, "launcher-smoke");
        }
        return app_.run();
    }

  private:
    nk::Application app_;
    nk::Window window_;
    launcher_session session_;
    std::shared_ptr<nk::MenuBar> menu_;
    std::shared_ptr<nk::Button> start_;
    std::shared_ptr<nk::Label> metadata_, status_;
    std::shared_ptr<nk::ImageView> logo_;
    std::shared_ptr<nk::LogView> log_;
    std::weak_ptr<nk::Theme> styled_theme_;
    std::shared_ptr<nk::Dialog> dialog_;
    std::shared_ptr<controller_settings> controller_;
    std::shared_ptr<nk::Label> capture_status_;
    std::chrono::steady_clock::time_point capture_deadline_{};
    nk::CallbackHandle timer_;
    std::future<std::string> job_;
    std::function<void()> completion_;
    bool busy_ = false, file_pending_ = false;
    bool smoke_test_ = false, smoke_pending_ = false, smoke_failed_ = false;
    size_t smoke_step_ = 0;
    std::string tail_path_, pending_line_;
    std::uint64_t tail_offset_ = 0;

    void install_log_style()
    {
        // NodalKit 0.2.0's log view falls back to a dark background even under
        // the light Windows theme. Use the theme's matching view/text pair.
        auto theme = nk::Theme::active();
        if(styled_theme_.lock() == theme)
        {
            return;
        }
        styled_theme_ = theme;
        theme->add_rule({ { .classes = { "cxbx-log" } },
                          { { "view-background", std::string("view-bg") }, { "text-color", std::string("text-primary") } } });
        log_->queue_redraw();
    }

    void refresh()
    {
        bool available = !busy_ && !session_.running();
        bool document = available && session_.loaded();
        metadata_->set_text(utf8(session_.description()));
        auto pixels = session_.logo();
        logo_->update_pixel_buffer(pixels.data(), 100, 17);
        logo_->set_visible(session_.loaded());
        start_->set_sensitive(document);
        window_.set_title(session_.loaded() ? "CXBX - " + utf8(session_.path()) + (session_.dirty() ? " *" : "") : "CXBX");
        auto action = [](const char* label, const char* id, bool enabled = true)
        {
            auto result = item::action(label, id);
            result.enabled = enabled;
            return result;
        };
        std::vector<item> xbe, exe;
        for(size_t i = 0; i < session_.preferences.recent_xbe.size(); ++i)
        {
            auto entry = item::action(utf8(session_.preferences.recent_xbe[i]), "recent-xbe-" + std::to_string(i));
            entry.enabled = available;
            xbe.push_back(std::move(entry));
        }
        for(size_t i = 0; i < session_.preferences.recent_exe.size(); ++i)
        {
            auto entry = item::action(utf8(session_.preferences.recent_exe[i]), "recent-exe-" + std::to_string(i));
            entry.enabled = available;
            exe.push_back(std::move(entry));
        }
        menu_->clear();
        menu_->add_menu({ "File", { action("Open XBE...", "open", available), action("Close XBE", "close", document), action("Save XBE", "save", document), action("Save XBE as...", "save-as", document), item::make_separator(), action("Import EXE...", "import", available), action("Export EXE...", "export", document), item::submenu("Recent XBE files", std::move(xbe)), item::submenu("Recent EXE files", std::move(exe)), item::make_separator(), action("Exit", "exit") } });
        menu_->add_menu({ "Edit", { action("Import logo BMP...", "logo-import", document), action("Export logo BMP...", "logo-export", document), action("Toggle 64 MB limit", "patch-memory", document), action("Toggle debug / retail", "patch-debug", document), action("Dump XBE information...", "dump", document), action("Dump information to debug output", "dump-console", document) } });
        menu_->add_menu({ "Settings", { action("Video...", "video", available), action("Controller...", "controller", available), action("Generation and debug output...", "preferences", available) } });
        menu_->add_menu({ "Emulation", { action("Start (F5)", "start", document) } });
        menu_->add_menu({ "View", { action("Clear log", "clear-log"), action("Export visible log...", "export-log") } });
        menu_->add_menu({ "Help", { action("Project home", "home"), action("About CXBX", "about") } });
    }

    void message(const std::string& text)
    {
        log_->append_line(text);
        status_->set_text(text);
    }

    void show_dialog(std::shared_ptr<nk::Dialog> dialog, std::function<void(response)> callback = {})
    {
        if(dialog_)
        {
            return;
        }
        dialog_ = std::move(dialog);
        (void)dialog_->on_response().connect([this, callback](response result)
                                             {
            // Keep the sender alive until its synchronous signal dispatch finishes.
            (void)app_.event_loop().post([this, callback, result] {
                dialog_.reset();
                if(callback) {
                    try { callback(result); } catch(const std::exception& error) { report_error(error.what()); }
                }
            }, "launcher-dialog-response"); });
        dialog_->present(window_);
    }

    void report_error(const std::string& text)
    {
        smoke_failed_ = true;
        message(utf8(text));
        auto dialog = nk::Dialog::create("CXBX", utf8(text));
        dialog->add_button("Close", response::Close);
        show_dialog(dialog);
    }

    void work(std::string label, std::function<void()> operation, std::function<void()> completion = {})
    {
        if(busy_)
        {
            return;
        }
        message(label + "...");
        busy_ = true;
        completion_ = std::move(completion);
        refresh();
        try
        {
            job_ = std::async(std::launch::async, [operation = std::move(operation)]
                              {
            try { operation(); return std::string{}; }
            catch(const std::exception& error) { return std::string(error.what()); }
            catch(...) { return std::string("Unexpected launcher operation failure."); } });
        }
        catch(...)
        {
            busy_ = false;
            completion_ = {};
            refresh();
            throw;
        }
    }

    void choose(bool saving, const std::string& title, const std::string& extension,
                std::function<void(std::string)> callback, std::string suggestion = {})
    {
        file_pending_ = true;
        // NodalKit owns this callback until the async dialog completes; the
        // analyzer cannot follow the std::function capture into that API.
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        auto done = [this, callback](nk::FileDialogResult result)
        {
            file_pending_ = false;
            if(result)
            {
                try
                {
                    callback(ansi(*result));
                }
                catch(const std::exception& error)
                {
                    report_error(error.what());
                }
            }
            else if(result.error() != nk::FileDialogError::Cancelled)
            {
                report_error("The file dialog could not be opened.");
            }
        };
        if(saving)
        {
            app_.save_file_dialog_async({ .title = title, .suggested_filename = utf8(suggestion), .filters = { extension } }, std::move(done));
        }
        else
        {
            app_.open_file_dialog_async(title, { extension }, std::move(done));
        }
    }

    void save(std::function<void()> after = {}, bool save_as = false)
    {
        auto save_path = [this, after](std::string path)
        { work("Saving XBE", [this, path]
               { session_.save(path); }, after); };
        if(save_as || session_.path().empty())
        {
            choose(true, "Save XBE", "*.xbe", save_path, session_.path().empty() ? "default.xbe" : session_.path());
        }
        else
        {
            save_path(session_.path());
        }
    }

    void replace_document(std::function<void()> after)
    {
        if(!session_.dirty())
        {
            after();
            return;
        }
        auto dialog = nk::Dialog::create("Unsaved XBE", "Save your changes before continuing?");
        dialog->add_button("Cancel", response::Cancel);
        dialog->add_button("Discard", response::Custom);
        dialog->add_button("Save", response::Accept);
        show_dialog(dialog, [this, after](response result)
                    {
            if(result == response::Accept) { save(after); }
            else if(result == response::Custom) { after(); } });
    }

    void start_emulation(bool temporary = false)
    {
        if(!session_.loaded() || session_.running() || busy_)
        {
            return;
        }
        if(session_.path().empty())
        {
            save([this]
                 { start_emulation(); });
            return;
        }
        auto launch = [this, temporary](std::string path)
        {
            tail_path_ = session_.preferences.kernel_debug == 2 ? session_.preferences.kernel_log : session_.preferences.launcher_log;
            tail_offset_ = 0;
            pending_line_.clear();
            work("Starting emulation", [this, path, temporary]
                 {
                const int generation = session_.preferences.generation;
                if(temporary) { session_.preferences.generation = 2; }
                try { session_.start(path); }
                catch(...) { session_.preferences.generation = generation; throw; }
                session_.preferences.generation = generation; }, [this]
                 { message("Guest running. Close its render window to stop emulation."); });
        };
        if(!temporary && session_.preferences.generation == 0)
        {
            choose(true, "Generate executable", "*.exe", launch, "default.exe");
        }
        else
        {
            launch({});
        }
    }

    void request_exit()
    {
        if(busy_ || file_pending_ || dialog_)
        {
            return;
        }
        auto finish = [this]
        {
            if(!smoke_test_)
            {
                session_.persist_preferences();
            }
            window_.close();
        };
        if(session_.running())
        {
            auto dialog = nk::Dialog::create("Guest still running", "Close the launcher and leave the guest running?");
            dialog->add_button("Cancel", response::Cancel);
            dialog->add_button("Close launcher", response::Accept);
            show_dialog(dialog, [this, finish](response result)
                        { if(result == response::Accept) { replace_document(finish); } });
        }
        else
        {
            replace_document(finish);
        }
    }

    void dispatch(const std::string& action)
    {
        if(busy_ || file_pending_ || dialog_)
        {
            return;
        }
        try
        {
            if(action == "exit")
            {
                request_exit();
            }
            else if(action == "clear-log")
            {
                log_->clear();
            }
            else if(action == "export-log")
            {
                auto text = log_->export_text();
                choose(true, "Export visible log", "*.txt", [this, text](std::string path)
                       { work("Exporting log", [text, path]
                              {
                        FILE* file = fopen(path.c_str(), "wb");
                        if(!file) { throw std::runtime_error("Could not open log file."); }
                        bool failed = fwrite(text.data(), 1, text.size(), file) != text.size();
                        failed |= fclose(file) != 0;
                        if(failed) { throw std::runtime_error("Could not write log file."); } }); }, "cxbx-ui.txt");
            }
            else if(action == "home")
            {
                ShellExecuteW(nullptr, L"open", L"https://github.com/kidoz/cxbx", nullptr, nullptr, SW_SHOWNORMAL);
            }
            else if(action == "about")
            {
                auto dialog = nk::Dialog::create("About CXBX", "Xbox emulator\nLauncher powered by NodalKit 0.2.0\nCXBX: GPL-2.0-or-later | NodalKit: MIT");
                dialog->add_button("Close", response::Close);
                show_dialog(dialog);
            }
            else if(session_.running())
            {
                message("Close the guest before changing its configuration or document.");
            }
            else if(action == "open" || action == "import")
            {
                bool importing = action == "import";
                replace_document([this, importing]
                                 { choose(false, importing ? "Import EXE" : "Open XBE", importing ? "*.exe" : "*.xbe", [this, importing](std::string path)
                                          { work("Opening document", [this, path, importing]
                                                 { session_.open(path, importing); }); }); });
            }
            else if(action.starts_with("recent-"))
            {
                bool importing = action.starts_with("recent-exe-");
                auto& recent = importing ? session_.preferences.recent_exe : session_.preferences.recent_xbe;
                auto index = static_cast<size_t>(std::stoul(action.substr(11)));
                if(index < recent.size())
                {
                    auto path = recent[index];
                    replace_document([this, path, importing]
                                     { work("Opening document", [this, path, importing]
                                            { session_.open(path, importing); }); });
                }
            }
            else if(action == "close")
            {
                replace_document([this]
                                 { session_.close(); refresh(); });
            }
            else if(action == "video")
            {
                video_dialog();
            }
            else if(action == "controller")
            {
                controller_dialog();
            }
            else if(action == "preferences")
            {
                preferences_dialog();
            }
            else if(session_.loaded())
            {
                if(action == "save" || action == "save-as")
                {
                    save({}, action == "save-as");
                }
                else if(action == "start")
                {
                    start_emulation();
                }
                else if(action == "patch-memory")
                {
                    session_.patch_memory();
                    refresh();
                }
                else if(action == "patch-debug")
                {
                    session_.patch_debug();
                    refresh();
                }
                else if(action == "dump-console")
                {
                    work("Dumping information", [this]
                         { session_.dump({}); });
                }
                else
                {
                    bool importing = action == "logo-import";
                    bool logo = importing || action == "logo-export";
                    choose(!importing, logo ? "XBE logo bitmap" : action == "dump" ? "Dump XBE information"
                                                                                   : "Export EXE",
                           logo ? "*.bmp" : action == "dump" ? "*.txt"
                                                             : "*.exe",
                           [this, action = std::string(action), importing, logo](std::string path)
                           { work("Writing document", [this, path, action = std::string(action), importing, logo]
                                  {
                                if(logo) { session_.logo_file(path, importing); }
                                else if(action == "dump") { session_.dump(path); }
                                else { session_.export_exe(path); } }); }, logo ? "logo.bmp" : action == "dump" ? "xbe-info.txt"
                                                                                                                                                              : "default.exe");
                }
            }
        }
        catch(const std::exception& error)
        {
            report_error(error.what());
        }
    }

    void video_dialog()
    {
        auto settings = read_video_settings();
        auto content = box();
        auto adapters = video_adapters();
        if(adapters.empty())
        {
            throw std::runtime_error("No display adapter found.");
        }
        for(auto& name : adapters)
        {
            name = utf8(name);
        }
        settings.adapter = std::clamp(settings.adapter, 0, static_cast<int>(adapters.size()) - 1);
        auto adapter = combo(content, "Display adapter", adapters, settings.adapter);
        auto device = combo(content, "Direct3D device", { "Hardware accelerated", "Reference (software)" }, std::clamp(settings.device, 0, 1));
        auto resolutions = video_resolutions(settings.adapter);
        auto found = std::find(resolutions.begin(), resolutions.end(), settings.resolution);
        int selected = found == resolutions.end() ? 0 : static_cast<int>(found - resolutions.begin());
        auto resolution = combo(content, "Resolution", resolutions, selected);
        (void)adapter->on_selection_changed().connect([resolution](int index)
                                                      {
            resolution->set_items(video_resolutions(index)); resolution->set_selected_index(0); });
        auto fullscreen = nk::CheckBox::create("Fullscreen");
        fullscreen->set_checked(settings.fullscreen);
        content->append_child(fullscreen);
        auto vsync = nk::CheckBox::create("Synchronize with vertical refresh");
        vsync->set_checked(settings.vsync);
        content->append_child(vsync);
        auto dialog = nk::Dialog::create("Video settings");
        dialog->set_content(content);
        dialog->set_minimum_panel_width(500);
        dialog->add_button("Cancel", response::Cancel);
        dialog->add_button("Save", response::Accept);
        show_dialog(dialog, [this, adapter, device, resolution, fullscreen, vsync](response result)
                    {
            if(result != response::Accept) { return; }
            auto values = video_resolutions(adapter->selected_index());
            int index = resolution->selected_index();
            if(index < 0 || static_cast<size_t>(index) >= values.size()) { throw std::runtime_error("Choose a video resolution."); }
            write_video_settings({adapter->selected_index(), device->selected_index(), values[index], fullscreen->is_checked(), vsync->is_checked()});
            message("Video settings saved."); });
    }

    void controller_dialog()
    {
        controller_ = std::make_shared<controller_settings>();
        auto content = box();
        auto objects = controller_settings::objects();
        auto input = combo(content, "Xbox input", objects, 0);
        auto capture = nk::Button::create("Map selected input");
        content->append_child(capture);
        capture_status_ = nk::Label::create("Choose an input, then press a key or move a control.");
        content->append_child(capture_status_);
        (void)capture->on_clicked().connect([this, input]
                                            {
            try {
                controller_->begin(nk::window_hwnd(window_), input->selected_index());
                capture_deadline_ = std::chrono::steady_clock::now() + 5s;
                capture_status_->set_text("Listening for input (5 seconds)...");
            } catch(const std::exception& error) { capture_status_->set_text(utf8(error.what())); } });
        auto cancel = nk::Button::create("Cancel capture");
        content->append_child(cancel);
        (void)cancel->on_clicked().connect([this]
                                           { controller_->cancel(); capture_deadline_ = {}; capture_status_->set_text("Capture cancelled."); });
        auto dialog = nk::Dialog::create("Controller settings");
        dialog->set_content(content);
        dialog->set_minimum_panel_width(520);
        dialog->add_button("Cancel", response::Cancel);
        dialog->add_button("Save", response::Accept);
        show_dialog(dialog, [this](response result)
                    {
            controller_->cancel();
            if(result == response::Accept) { controller_->accept(); message("Controller settings saved."); }
            controller_.reset(); capture_status_.reset(); capture_deadline_ = {}; });
    }

    void apply_logging()
    {
        const auto& settings = session_.preferences;
        if(settings.launcher_debug == 2 && !settings.launcher_log.empty())
        {
            configure_log_file(settings.launcher_log.c_str());
        }
        else if(settings.launcher_debug == 1)
        {
            if(!GetConsoleWindow())
            {
                AllocConsole();
            }
            (void)freopen("CONOUT$", "w", stdout);
            (void)freopen("CONOUT$", "w", stderr);
            SetEnvironmentVariableA("CXBX_LOG_FILE", nullptr);
        }
        // An explicit --log setting remains active until the user changes debug preferences.
    }

    void preferences_dialog()
    {
        auto content = box();
        const auto& preferences = session_.preferences;
        auto generation = combo(content, "Generated executable location", { "Choose every time", "XBE directory", "Windows temporary directory" }, preferences.generation);
        auto launcher = combo(content, "Launcher debug output", { "None", "Console", "File" }, preferences.launcher_debug);
        auto launcher_file = nk::TextField::create(utf8(preferences.launcher_log));
        launcher_file->set_placeholder("Launcher log path");
        content->append_child(launcher_file);
        auto kernel = combo(content, "Kernel debug output", { "None", "Console", "File" }, preferences.kernel_debug);
        auto kernel_file = nk::TextField::create(utf8(preferences.kernel_log));
        kernel_file->set_placeholder("Kernel log path");
        content->append_child(kernel_file);
        auto dialog = nk::Dialog::create("Generation and debug output");
        dialog->set_content(content);
        dialog->set_minimum_panel_width(550);
        dialog->add_button("Cancel", response::Cancel);
        dialog->add_button("Save", response::Accept);
        show_dialog(dialog, [this, generation, launcher, launcher_file, kernel, kernel_file](response result)
                    {
            if(result != response::Accept) { return; }
            auto candidate = session_.preferences;
            candidate.generation = generation->selected_index(); candidate.launcher_debug = launcher->selected_index(); candidate.kernel_debug = kernel->selected_index();
            candidate.launcher_log = ansi(std::string(launcher_file->text())); candidate.kernel_log = ansi(std::string(kernel_file->text()));
            if((candidate.launcher_debug == 2 && candidate.launcher_log.empty()) || (candidate.kernel_debug == 2 && candidate.kernel_log.empty())) {
                throw std::runtime_error("File debug output requires a log path.");
            }
            session_.preferences = std::move(candidate);
            session_.persist_preferences();
            if(session_.preferences.launcher_debug == 0) {
                (void)freopen("NUL", "w", stdout); (void)freopen("NUL", "w", stderr);
                SetEnvironmentVariableA("CXBX_LOG_FILE", nullptr);
            }
            apply_logging(); message("Preferences saved."); });
    }

    void tail_log()
    {
        if(tail_path_.empty())
        {
            return;
        }
        HANDLE file = CreateFileA(tail_path_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if(file == INVALID_HANDLE_VALUE)
        {
            return;
        }
        LARGE_INTEGER size{};
        GetFileSizeEx(file, &size);
        if(static_cast<std::uint64_t>(size.QuadPart) < tail_offset_)
        {
            tail_offset_ = 0;
            pending_line_.clear();
        }
        LARGE_INTEGER position{};
        position.QuadPart = static_cast<LONGLONG>(tail_offset_);
        SetFilePointerEx(file, position, nullptr, FILE_BEGIN);
        char buffer[16384];
        DWORD bytes = 0;
        if(ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr))
        {
            tail_offset_ += bytes;
            pending_line_.append(buffer, bytes);
        }
        CloseHandle(file);
        size_t start = 0, end;
        while((end = pending_line_.find('\n', start)) != std::string::npos)
        {
            log_->append_line(utf8(pending_line_.substr(start, end - start)));
            start = end + 1;
        }
        pending_line_.erase(0, start);
        if(pending_line_.size() > 16384)
        {
            log_->append_line(utf8(pending_line_));
            pending_line_.clear();
        }
    }

    void tick()
    {
        try
        {
            if(busy_ && job_.wait_for(0ms) == std::future_status::ready)
            {
                auto error = job_.get();
                busy_ = false;
                auto completed = std::move(completion_);
                completion_ = {};
                refresh();
                if(error.empty())
                {
                    message("Ready");
                    if(completed)
                    {
                        completed();
                    }
                }
                else
                {
                    report_error(error);
                }
            }
            if(!busy_)
            {
                if(auto exit = session_.poll_exit())
                {
                    if(smoke_test_ && *exit != 0)
                    {
                        smoke_failed_ = true;
                    }
                    message("Guest exited with code " + std::to_string(*exit) + ".");
                    refresh();
                }
            }
            if(controller_ && capture_deadline_ != std::chrono::steady_clock::time_point{})
            {
                if(auto input = controller_->poll())
                {
                    capture_status_->set_text(utf8(*input));
                    capture_deadline_ = {};
                }
                else if(std::chrono::steady_clock::now() >= capture_deadline_)
                {
                    controller_->cancel();
                    capture_status_->set_text("No input detected. Try again.");
                    capture_deadline_ = {};
                }
            }
            tail_log();
            if(smoke_pending_ && !busy_)
            {
                smoke_pending_ = false;
                finish_smoke();
            }
        }
        catch(const std::exception& error)
        {
            if(controller_)
            {
                controller_->cancel();
                capture_deadline_ = {};
            }
            report_error(error.what());
        }
    }

    void finish_smoke()
    {
        printf("cxbx: UI smoke step %zu\n", smoke_step_);
        fflush(stdout);
        if(smoke_step_ == 0)
        {
            if(const char* screenshot = std::getenv("CXBX_UI_SMOKE_SCREENSHOT"))
            {
                if(!window_.inspector().save_debug_screenshot_ppm_file(screenshot))
                {
                    smoke_failed_ = true;
                }
            }
        }
        if(smoke_failed_)
        {
            app_.quit(1);
            return;
        }
        if(session_.running())
        {
            smoke_pending_ = true;
            return;
        }
        const char* actions[] = { "about", "preferences", "video", "controller" };
        if(smoke_step_ < std::size(actions))
        {
            dispatch(actions[smoke_step_]);
        }
        else if(smoke_step_ == std::size(actions) && session_.loaded())
        {
            dispatch("patch-memory");
            dispatch("close");
        }
        else
        {
            window_.close();
            return;
        }
        (void)app_.event_loop().set_timeout(100ms, [this]
                                            {
            if(!dialog_ || !dialog_->is_presented()) { app_.quit(1); return; }
            window_.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape});
            window_.dispatch_key_event({.type = nk::KeyEvent::Type::Release, .key = nk::KeyCode::Escape});
            (void)app_.event_loop().set_timeout(100ms, [this] {
                if(dialog_ || controller_) { app_.quit(1); return; }
                if(smoke_step_ == 4 && (!session_.loaded() || !session_.dirty())) { app_.quit(1); return; }
                ++smoke_step_;
                finish_smoke();
            }, "launcher-smoke-next"); }, "launcher-smoke-dialog");
    }
};
} // namespace

int run_nodalkit(const char* initial_path, const char* log_file, bool smoke_test)
{
    try
    {
        launcher_ui ui(log_file, smoke_test);
        return ui.run(initial_path);
    }
    catch(const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "CXBX launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
}
} // namespace cxbx::frontend
