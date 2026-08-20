#pragma once

#include "app_state.h"
#include "desktop_embedder.h"
#include "note_renderer.h"
#include "note_toolbar.h"
#include "rich_edit_host.h"

#include <windows.h>

#include <functional>
#include <memory>
#include <string>

namespace desktopnote {

class NoteWindow;

struct NoteWindowCallbacks {
    std::function<void(NoteWindow*)> activated;
    std::function<void()> dirty;
    std::function<void()> save_now;
    std::function<void(NoteWindow*)> request_delete;
    std::function<void(NoteWindow*)> request_new;
};

class NoteWindow {
public:
    NoteWindow(HINSTANCE instance, Note note, NoteWindowCallbacks callbacks);
    ~NoteWindow();

    NoteWindow(const NoteWindow&) = delete;
    NoteWindow& operator=(const NoteWindow&) = delete;

    bool Create();
    void Destroy();
    void Show(bool activate = false);
    void Hide();
    void Activate();
    bool IsVisible() const;

    bool SyncNote();
    void ReleaseIdleResources();
    const Note& note() const noexcept { return note_; }
    Note& note() noexcept { return note_; }
    HWND hwnd() const noexcept { return window_; }
    std::wstring DisplayTitle() const;

    void SetWindowMode(WindowMode mode);
    void ReapplyDesktopMode();
    void SetClickThrough(bool enabled);
    void SetLocked(bool locked);
    void SetAutoHide(bool enabled);
    bool IsAutoHide() const noexcept { return note_.window.auto_hide; }
    bool IsCollapsed() const noexcept { return is_collapsed_; }
    bool last_render_succeeded() const noexcept { return last_render_succeeded_; }
    void ShowContextMenu(POINT screen_point);
    void ClampToVisibleWorkArea();

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static bool RegisterWindowClass(HINSTANCE instance);

    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void UpdateEditorBounds();
    void UpdateWindowState();
    void MarkDirty();
    void MarkTextDirty();
    void ApplyModeInternal(WindowMode mode, bool update_model);
    bool Render();
    void CheckAutoHide();
    void CollapseToEdge();
    void ExpandFromEdge();

    void SetFontColor(std::uint32_t color);
    void SetBackgroundColor(std::uint32_t color);
    void SetBorderColor(std::uint32_t color);
    void ScrollFromMouseWheel(WPARAM wparam);

    HINSTANCE instance_ = nullptr;
    Note note_;
    NoteWindowCallbacks callbacks_;
    HWND window_ = nullptr;

    std::unique_ptr<NoteRenderer> renderer_;
    std::unique_ptr<NoteToolbar> toolbar_;
    std::unique_ptr<DesktopEmbedder> desktop_embedder_;
    std::unique_ptr<RichEditHost> rich_edit_;

    RECT editor_bounds_{};
    bool text_dirty_ = false;
    bool destroying_ = false;
    bool is_collapsed_ = false;
    int collapsed_edge_ = 0;
    RECT expanded_rect_{};
    int wheel_delta_remainder_ = 0;
    bool last_render_succeeded_ = false;
};

}  // namespace desktopnote
