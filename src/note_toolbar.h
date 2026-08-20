#pragma once

#include "app_state.h"
#include "win_util.h"

#include <windows.h>
#include <functional>
#include <memory>
#include <string>

namespace desktopnote {

struct NoteToolbarCallbacks {
    std::function<void(const std::wstring&)> font_family_changed;
    std::function<void(double)> font_size_changed;
    std::function<void(std::uint32_t)> background_color_changed;
    std::function<void(std::uint32_t)> font_color_changed;
    std::function<void(std::uint32_t)> border_color_changed;
    std::function<void(double)> opacity_changed;
    std::function<void(double)> padding_changed;
    std::function<void(double)> spacing_changed;
    std::function<void()> pin_toggled;
    std::function<void(POINT)> show_menu;
    std::function<void()> focus_main_window;
};

class NoteToolbar {
public:
    static constexpr wchar_t kToolbarWindowClass[] = L"DesktopNote.ToolbarWindow.v2";

    NoteToolbar(HINSTANCE instance, HWND parent_window, Note& note, NoteToolbarCallbacks callbacks);
    ~NoteToolbar();

    NoteToolbar(const NoteToolbar&) = delete;
    NoteToolbar& operator=(const NoteToolbar&) = delete;

    static bool RegisterWindowClass(HINSTANCE instance);

    bool Create();
    void Destroy();
    void Show();
    void Hide(bool force = false);
    bool PositionToolbar();
    int ReservedHeight() const;
    bool IsVisible() const;
    bool is_at_bottom() const noexcept { return at_bottom_; }
    void Invalidate();
    void ReleaseIdleResources();
    HWND hwnd() const noexcept { return toolbar_; }

private:
    static LRESULT CALLBACK ToolbarProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleToolbarMessage(UINT message, WPARAM wparam, LPARAM lparam);

    void EnsureBrushes();
    void UpdateToolbarFonts(UINT dpi);
    void LayoutToolbar(UINT dpi);

    HINSTANCE instance_ = nullptr;
    HWND parent_window_ = nullptr;
    Note& note_;
    NoteToolbarCallbacks callbacks_;

    HWND toolbar_ = nullptr;
    HWND font_combo_ = nullptr;
    HWND size_combo_ = nullptr;
    HWND opacity_slider_ = nullptr;
    HWND padding_slider_ = nullptr;
    HWND spacing_slider_ = nullptr;

    ScopedFont toolbar_font_;
    ScopedFont toolbar_icon_font_;
    ScopedBrush toolbar_background_brush_;
    ScopedBrush toolbar_control_brush_;
    bool toolbar_visible_ = false;
    bool at_bottom_ = false;
};

}  // namespace desktopnote
