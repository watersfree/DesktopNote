#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace desktopnote {

enum class WindowMode { Normal, TopMost, Desktop };

struct WindowState {
    double x_dip = 100.0;
    double y_dip = 100.0;
    double width_dip = 420.0;
    double height_dip = 320.0;
    std::wstring monitor_device;
    WindowMode mode = WindowMode::Normal;
    bool locked = false;
    bool click_through = false;
    bool auto_hide = false;
};

struct Appearance {
    double background_alpha = 0.88;
    std::uint32_t background_color = 0x1E1E24;
    std::uint32_t border_color = 0xFF9800;
    std::wstring font_family = L"Microsoft YaHei UI";
    double font_size_dip = 16.0;
    std::uint32_t font_color = 0xFFFFFF;
    double padding_dip = 10.0;
    double paragraph_spacing_dip = 4.0;
    bool toolbar_pinned = false;
};

struct Note {
    std::string id;
    std::string title = "我的便签";
    std::string content_rtf_base64;
    std::string created_at_utc;
    std::string modified_at_utc;
    WindowState window;
    Appearance appearance;
};

struct AppState {
    int schema_version = 2;
    std::string last_active_note_id;
    std::vector<Note> notes;
};

std::string NewId();
std::string UtcNowIso8601();
std::string WindowModeToString(WindowMode mode);
WindowMode WindowModeFromString(const std::string& value);
void Normalize(AppState& state);
AppState CreateDefaultState();

}  // namespace desktopnote
