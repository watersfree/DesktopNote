#include "note_window.h"
#include "resource.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace desktopnote {
namespace {

constexpr wchar_t kNoteWindowClass[] = L"DesktopNote.NoteWindow.v2";

constexpr int kMenuNormal = 3101;
constexpr int kMenuTopMost = 3102;
constexpr int kMenuDesktop = 3103;
constexpr int kMenuClickThrough = 3104;
constexpr int kMenuLocked = 3105;
constexpr int kMenuNew = 3106;
constexpr int kMenuDelete = 3107;
constexpr int kMenuAutoHide = 3108;

constexpr UINT_PTR kAutoHideTimerId = 9102;
constexpr UINT kAutoHideDelayMs = 350;
constexpr UINT_PTR kToolbarHideTimerId = 9103;
constexpr UINT kToolbarHideDelayMs = 250;
constexpr double kEdgeSnapThresholdDip = 18.0;
constexpr double kCollapsedTabSizeDip = 18.0;
constexpr double kCollapsedTabLengthDip = 52.0;

int DetectScreenEdge(const RECT& window_rect, const RECT& work_rect, UINT dpi) {
    const int threshold = DipToPixel(kEdgeSnapThresholdDip, dpi);
    if (window_rect.top <= work_rect.top + threshold) return 1; // Top
    if (window_rect.left <= work_rect.left + threshold) return 2; // Left
    if (window_rect.right >= work_rect.right - threshold) return 3; // Right
    return 0; // None
}

RECT GetCollapsedTabRect(const RECT& client, int collapsed_edge, UINT dpi) {
    const int tab_depth = DipToPixel(kCollapsedTabSizeDip, dpi);
    const int tab_length = DipToPixel(kCollapsedTabLengthDip, dpi);
    if (collapsed_edge == 1) { // Top edge -> tab is at bottom center of window
        const int cx = (client.right - client.left) / 2;
        return {cx - tab_length / 2, client.bottom - tab_depth, cx + tab_length / 2, client.bottom};
    } else if (collapsed_edge == 2) { // Left edge -> tab is at right center of window
        const int cy = (client.bottom - client.top) / 2;
        return {client.right - tab_depth, cy - tab_length / 2, client.right, cy + tab_length / 2};
    } else if (collapsed_edge == 3) { // Right edge -> tab is at left center of window
        const int cy = (client.bottom - client.top) / 2;
        return {client.left, cy - tab_length / 2, client.left + tab_depth, cy + tab_length / 2};
    }
    return client;
}

constexpr double kColorBarHeightDip = 2.0;
constexpr double kMinimumWindowWidthDip = 300.0;
constexpr double kMinimumWindowHeightDip = 150.0;
constexpr double kMinimumEditorHeightDip = 72.0;
constexpr double kToolbarWideMinimumWidthDip = 620.0;
constexpr double kToolbarMediumMinimumWidthDip = 400.0;
constexpr double kToolbarWideHeightDip = 76.0;
constexpr double kToolbarMediumHeightDip = 112.0;
constexpr double kToolbarNarrowHeightDip = 174.0;

double GetToolbarHeightDipForWidth(int width, UINT dpi) {
    if (width >= DipToPixel(kToolbarWideMinimumWidthDip, dpi)) return kToolbarWideHeightDip;
    if (width >= DipToPixel(kToolbarMediumMinimumWidthDip, dpi)) return kToolbarMediumHeightDip;
    return kToolbarNarrowHeightDip;
}

struct MonitorSearch {
    std::wstring name;
    HMONITOR monitor = nullptr;
    RECT work{};
    bool found = false;
};

BOOL CALLBACK FindMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM value) {
    auto& search = *reinterpret_cast<MonitorSearch*>(value);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) && search.name == info.szDevice) {
        search.monitor = monitor;
        search.work = info.rcWork;
        search.found = true;
        return FALSE;
    }
    return TRUE;
}

}  // namespace

NoteWindow::NoteWindow(HINSTANCE instance, Note note, NoteWindowCallbacks callbacks)
    : instance_(instance), note_(std::move(note)), callbacks_(std::move(callbacks)) {}

NoteWindow::~NoteWindow() {
    Destroy();
}

bool NoteWindow::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW note_class{};
    note_class.cbSize = sizeof(note_class);
    note_class.style = CS_DBLCLKS;
    note_class.lpfnWndProc = WindowProcedure;
    note_class.hInstance = instance;
    note_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_DESKTOPNOTE));
    note_class.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_DESKTOPNOTE), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    note_class.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    note_class.lpszClassName = kNoteWindowClass;
    return RegisterClassExW(&note_class) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool NoteWindow::Create() {
    LogDebug("NoteWindow::Create starting for note id=" + note_.id + " (x=" + std::to_string(note_.window.x_dip) + ", y=" + std::to_string(note_.window.y_dip) + ")");
    if (!RegisterWindowClass(instance_)) {
        LogDebug("NoteWindow::RegisterWindowClass failed, error=" + std::to_string(GetLastError()));
        return false;
    }
    UINT dpi = GetDpiForSystem();
    MonitorSearch saved_monitor{note_.window.monitor_device};
    if (!saved_monitor.name.empty()) {
        EnumDisplayMonitors(nullptr, nullptr, FindMonitor, reinterpret_cast<LPARAM>(&saved_monitor));
        UINT dpi_x = 0;
        UINT dpi_y = 0;
        if (saved_monitor.found &&
            SUCCEEDED(GetDpiForMonitor(saved_monitor.monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
            dpi = dpi_x;
        }
    }
    const int x = DipToPixel(note_.window.x_dip, dpi);
    const int y = DipToPixel(note_.window.y_dip, dpi);
    const int width = DipToPixel(note_.window.width_dip, dpi);
    const int height = DipToPixel(note_.window.height_dip, dpi);

    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, kNoteWindowClass, L"DesktopNote 桌面便签",
                              WS_POPUP, x, y, width, height, nullptr, nullptr, instance_, this);
    if (!window_) {
        LogDebug("NoteWindow::CreateWindowExW failed, error=" + std::to_string(GetLastError()));
        return false;
    }
    LogDebug("NoteWindow created HWND=" + std::to_string(reinterpret_cast<uintptr_t>(window_)));

    HICON icon_big = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_DESKTOPNOTE), IMAGE_ICON,
                                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    HICON icon_sm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_DESKTOPNOTE), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (icon_big) SendMessageW(window_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_big));
    if (icon_sm) SendMessageW(window_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_sm));

    ClampToVisibleWorkArea();

    renderer_ = std::make_unique<NoteRenderer>();
    if (!renderer_->Initialize()) {
        LogDebug("NoteRenderer::Initialize failed!");
        return false;
    }

    desktop_embedder_ = std::make_unique<DesktopEmbedder>();

    rich_edit_ = std::make_unique<RichEditHost>(window_, [this] { MarkTextDirty(); });
    if (!rich_edit_->Initialize(note_.appearance)) {
        LogDebug("RichEditHost::Initialize failed!");
        return false;
    }

    NoteToolbarCallbacks toolbar_callbacks{
        [this](const std::wstring& family) {
            note_.appearance.font_family = family;
            rich_edit_->ApplyFontFamily(family);
            MarkDirty();
        },
        [this](double size) {
            note_.appearance.font_size_dip = size;
            rich_edit_->ApplyFontSize(size);
            MarkDirty();
        },
        [this](std::uint32_t color) { SetBackgroundColor(color); },
        [this](std::uint32_t color) { SetFontColor(color); },
        [this](std::uint32_t color) { SetBorderColor(color); },
        [this](double alpha) {
            note_.appearance.background_alpha = alpha;
            Render();
            MarkDirty();
        },
        [this](double padding) {
            note_.appearance.padding_dip = padding;
            UpdateEditorBounds();
            Render();
            MarkDirty();
        },
        [this](double spacing) {
            note_.appearance.paragraph_spacing_dip = spacing;
            rich_edit_->ApplyParagraphSpacing(spacing);
            MarkDirty();
        },
        [this] {
            note_.appearance.toolbar_pinned = !note_.appearance.toolbar_pinned;
            if (toolbar_) {
                if (note_.appearance.toolbar_pinned && !is_collapsed_ && note_.window.mode != WindowMode::Desktop) {
                    toolbar_->PositionToolbar();
                    toolbar_->Show();
                }
            }
            MarkDirty();
        },
        [this](POINT point) { ShowContextMenu(point); },
        [this] { SetFocus(window_); },
    };

    toolbar_ = std::make_unique<NoteToolbar>(instance_, window_, note_, std::move(toolbar_callbacks));
    if (!toolbar_->Create()) {
        LogDebug("NoteToolbar::Create failed!");
        return false;
    }

    toolbar_->PositionToolbar();
    UpdateEditorBounds();
    rich_edit_->LoadRtfBase64(note_.content_rtf_base64);
    UpdateEditorBounds();
    ApplyModeInternal(note_.window.mode, false);
    SetClickThrough(note_.window.click_through);
    Render();
    ShowWindow(window_, note_.window.click_through ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (note_.appearance.toolbar_pinned && !note_.window.click_through && note_.window.mode != WindowMode::Desktop) {
        toolbar_->Show();
    }
    LogDebug("NoteWindow::Create succeeded for note id=" + note_.id);
    return true;
}

void NoteWindow::Destroy() {
    if (destroying_) return;
    destroying_ = true;
    SyncNote();
    if (toolbar_) {
        toolbar_->Destroy();
        toolbar_.reset();
    }
    rich_edit_.reset();
    if (window_) {
        if (desktop_embedder_ && desktop_embedder_->is_embedded()) {
            desktop_embedder_->Detach(window_);
        }
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (renderer_) {
        renderer_->ReleaseResources();
    }
}

void NoteWindow::Show(bool activate) {
    if (!window_) return;
    LogDebug("NoteWindow::Show called for note id=" + note_.id + ", activate=" + (activate ? "true" : "false"));
    if (IsIconic(window_)) {
        ShowWindow(window_, SW_RESTORE);
    } else {
        ShowWindow(window_, activate && !note_.window.click_through ? SW_SHOW : SW_SHOWNOACTIVATE);
    }

    ClampToVisibleWorkArea();

    if (note_.window.mode == WindowMode::TopMost) {
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | (activate ? 0 : SWP_NOACTIVATE));
    } else if (note_.window.mode == WindowMode::Normal) {
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | (activate ? 0 : SWP_NOACTIVATE));
        SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | (activate ? 0 : SWP_NOACTIVATE));
    } else if (note_.window.mode == WindowMode::Desktop) {
        if (!desktop_embedder_ || !desktop_embedder_->Attach(window_)) {
            LogDebug("Desktop embedding failed while showing note " + note_.id + "; using normal mode");
            ApplyModeInternal(WindowMode::Normal, true);
        }
    }

    if (activate && !note_.window.click_through && note_.window.mode != WindowMode::Desktop) {
        HWND fg_window = GetForegroundWindow();
        DWORD fg_thread = fg_window ? GetWindowThreadProcessId(fg_window, nullptr) : 0;
        DWORD current_thread = GetCurrentThreadId();
        if (fg_thread != 0 && fg_thread != current_thread) {
            AttachThreadInput(current_thread, fg_thread, TRUE);
            SetForegroundWindow(window_);
            BringWindowToTop(window_);
            SetActiveWindow(window_);
            SetFocus(window_);
            AttachThreadInput(current_thread, fg_thread, FALSE);
        } else {
            SetForegroundWindow(window_);
            BringWindowToTop(window_);
            SetActiveWindow(window_);
            SetFocus(window_);
        }
    }
    Render();
    if (toolbar_) {
        toolbar_->PositionToolbar();
        if (note_.appearance.toolbar_pinned || activate) toolbar_->Show();
    }
}

void NoteWindow::Hide() {
    if (toolbar_) toolbar_->Hide(true);
    if (window_) ShowWindow(window_, SW_HIDE);
}

void NoteWindow::Activate() { Show(true); }
bool NoteWindow::IsVisible() const { return window_ && IsWindowVisible(window_); }

std::wstring NoteWindow::DisplayTitle() const {
    if (!note_.title.empty() && note_.title != "我的便签") return Utf8ToWide(note_.title);
    if (rich_edit_) {
        auto text = rich_edit_->PlainText();
        const auto end = text.find_first_of(L"\r\n");
        if (end != std::wstring::npos) text.resize(end);
        if (text.size() > 24) text = text.substr(0, 24) + L"…";
        if (!text.empty()) return text;
    }
    return L"空便签";
}

bool NoteWindow::SyncNote() {
    if (text_dirty_ && rich_edit_) {
        const auto content = rich_edit_->SaveRtfBase64();
        if (!content) return false;
        note_.content_rtf_base64 = *content;
        note_.modified_at_utc = UtcNowIso8601();
        text_dirty_ = false;
        if (window_) {
            const std::wstring title = L"DesktopNote - " + DisplayTitle();
            SetWindowTextW(window_, title.c_str());
        }
    }
    UpdateWindowState();
    return true;
}

void NoteWindow::ReleaseIdleResources() {
    SyncNote();
    if (renderer_) {
        renderer_->ReleaseResources();
    }
    if (toolbar_ && !toolbar_->IsVisible()) {
        toolbar_->ReleaseIdleResources();
    }
    note_.content_rtf_base64.shrink_to_fit();
    note_.title.shrink_to_fit();
}

void NoteWindow::MarkDirty() {
    if (callbacks_.dirty) callbacks_.dirty();
}

void NoteWindow::MarkTextDirty() {
    text_dirty_ = true;
    MarkDirty();
}

void NoteWindow::UpdateEditorBounds() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const UINT dpi = DpiForWindowOrSystem(window_);
    const int padding = DipToPixel(note_.appearance.padding_dip, dpi);
    const int content_top = DipToPixel(kColorBarHeightDip, dpi);
    editor_bounds_ = {padding, padding + content_top,
                      std::max<LONG>(padding + 1, client.right - padding),
                      std::max<LONG>(padding + content_top + 1, client.bottom - padding)};
    if (rich_edit_) rich_edit_->SetBounds(editor_bounds_);
}

void NoteWindow::UpdateWindowState() {
    if (!window_ || IsIconic(window_)) return;
    RECT rect{};
    if (!GetWindowRect(window_, &rect)) return;
    const UINT dpi = DpiForWindowOrSystem(window_);
    note_.window.x_dip = PixelToDip(rect.left, dpi);
    note_.window.y_dip = PixelToDip(rect.top, dpi);
    note_.window.width_dip = PixelToDip(rect.right - rect.left, dpi);
    note_.window.height_dip = PixelToDip(rect.bottom - rect.top, dpi);
    MONITORINFOEXW monitor_info{};
    monitor_info.cbSize = sizeof(monitor_info);
    if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor_info)) {
        note_.window.monitor_device = monitor_info.szDevice;
    }
}

void NoteWindow::ClampToVisibleWorkArea() {
    if (!window_ || is_collapsed_) return;
    RECT window_rect{};
    GetWindowRect(window_, &window_rect);
    RECT work{};
    MonitorSearch saved{note_.window.monitor_device};
    if (!saved.name.empty()) EnumDisplayMonitors(nullptr, nullptr, FindMonitor, reinterpret_cast<LPARAM>(&saved));
    if (saved.found) {
        work = saved.work;
    } else {
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromRect(&window_rect, MONITOR_DEFAULTTONEAREST), &info);
        work = info.rcWork;
    }
    const UINT dpi = DpiForWindowOrSystem(window_);
    const int work_width = work.right - work.left;
    const int work_height = work.bottom - work.top;
    const int minimum_width = std::min(DipToPixel(kMinimumWindowWidthDip, dpi), work_width);
    const int current_width = static_cast<int>(window_rect.right - window_rect.left);
    const int width = std::clamp(current_width, minimum_width, work_width);
    const double minimum_height_dip = kColorBarHeightDip + kMinimumEditorHeightDip;
    const int minimum_height = std::min(
        std::max(DipToPixel(kMinimumWindowHeightDip, dpi), DipToPixel(minimum_height_dip, dpi)),
        work_height);
    const int current_height = static_cast<int>(window_rect.bottom - window_rect.top);
    const int height = std::clamp(current_height, minimum_height, work_height);
    int x = std::clamp(window_rect.left, work.left, work.right - width);
    int y = std::clamp(window_rect.top, work.top, work.bottom - height);
    SetWindowPos(window_, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
}

bool NoteWindow::Render() {
    last_render_succeeded_ = renderer_ && window_ &&
        renderer_->Render(window_, note_, rich_edit_.get(), is_collapsed_, collapsed_edge_);
    return last_render_succeeded_;
}

void NoteWindow::SetAutoHide(bool enabled) {
    note_.window.auto_hide = enabled;
    if (!enabled && is_collapsed_) {
        ExpandFromEdge();
    } else if (enabled) {
        CheckAutoHide();
    }
    MarkDirty();
}

void NoteWindow::CheckAutoHide() {
    if (!window_ || !note_.window.auto_hide || note_.window.locked ||
        note_.window.mode == WindowMode::Desktop || is_collapsed_) {
        return;
    }
    RECT window_rect{};
    GetWindowRect(window_, &window_rect);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
    const UINT dpi = DpiForWindowOrSystem(window_);
    const int edge = DetectScreenEdge(window_rect, monitor.rcWork, dpi);
    if (edge != 0) {
        CollapseToEdge();
    }
}

void NoteWindow::CollapseToEdge() {
    if (!window_ || is_collapsed_ || !note_.window.auto_hide ||
        note_.window.mode == WindowMode::Desktop) return;

    RECT window_rect{};
    GetWindowRect(window_, &window_rect);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
    const UINT dpi = DpiForWindowOrSystem(window_);
    const int edge = DetectScreenEdge(window_rect, monitor.rcWork, dpi);
    if (edge == 0) return;

    expanded_rect_ = window_rect;
    collapsed_edge_ = edge;
    is_collapsed_ = true;

    if (toolbar_) toolbar_->Hide(true);

    const int width = window_rect.right - window_rect.left;
    const int height = window_rect.bottom - window_rect.top;
    const int tab_size = DipToPixel(kCollapsedTabSizeDip, dpi);

    int target_x = window_rect.left;
    int target_y = window_rect.top;

    if (edge == 1) { // Top
        target_y = monitor.rcWork.top - (height - tab_size);
    } else if (edge == 2) { // Left
        target_x = monitor.rcWork.left - (width - tab_size);
    } else if (edge == 3) { // Right
        target_x = monitor.rcWork.right - tab_size;
    }

    SetWindowPos(window_, note_.window.mode == WindowMode::TopMost ? HWND_TOPMOST : HWND_TOP,
                 target_x, target_y, width, height,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    Render();
}

void NoteWindow::ExpandFromEdge() {
    if (!window_ || !is_collapsed_) return;

    is_collapsed_ = false;
    const int edge = collapsed_edge_;
    collapsed_edge_ = 0;

    const UINT dpi = DpiForWindowOrSystem(window_);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);

    const int width = std::max<int>(expanded_rect_.right - expanded_rect_.left, DipToPixel(kMinimumWindowWidthDip, dpi));
    const int height = std::max<int>(expanded_rect_.bottom - expanded_rect_.top, DipToPixel(kMinimumWindowHeightDip, dpi));
    int target_x = expanded_rect_.left;
    int target_y = expanded_rect_.top;

    if (edge == 1) { // Top
        target_y = monitor.rcWork.top;
    } else if (edge == 2) { // Left
        target_x = monitor.rcWork.left;
    } else if (edge == 3) { // Right
        target_x = monitor.rcWork.right - width;
    }

    SetWindowPos(window_, note_.window.mode == WindowMode::TopMost ? HWND_TOPMOST : HWND_TOP,
                 target_x, target_y, width, height,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    UpdateEditorBounds();
    Render();
    if (toolbar_ && (note_.appearance.toolbar_pinned || !note_.window.locked) && note_.window.mode != WindowMode::Desktop) {
        toolbar_->PositionToolbar();
        toolbar_->Show();
    }
}

void NoteWindow::SetFontColor(std::uint32_t color) {
    note_.appearance.font_color = color;
    if (rich_edit_) rich_edit_->ApplyFontColor(note_.appearance.font_color);
    if (toolbar_) toolbar_->Invalidate();
    MarkDirty();
}

void NoteWindow::SetBackgroundColor(std::uint32_t color) {
    note_.appearance.background_color = color;
    Render();
    if (toolbar_) toolbar_->Invalidate();
    MarkDirty();
}

void NoteWindow::SetBorderColor(std::uint32_t color) {
    note_.appearance.border_color = color;
    Render();
    if (toolbar_) toolbar_->Invalidate();
    MarkDirty();
}

void NoteWindow::SetWindowMode(WindowMode mode) {
    ApplyModeInternal(mode, true);
}

void NoteWindow::ApplyModeInternal(WindowMode mode, bool update_model) {
    if (!window_) return;
    if (desktop_embedder_ && desktop_embedder_->is_embedded()) {
        if (!desktop_embedder_->Detach(window_)) {
            LogDebug("Cannot detach note " + note_.id + " from the desktop host");
            return;
        }
    }
    WindowMode effective_mode = mode;
    SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (mode == WindowMode::TopMost) {
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else if (mode == WindowMode::Normal) {
        SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else if (mode == WindowMode::Desktop) {
        if (!desktop_embedder_ || !desktop_embedder_->Attach(window_)) {
            effective_mode = WindowMode::Normal;
            LogDebug("Desktop embedding failed for note " + note_.id + "; using normal mode");
        }
    }
    const bool mode_changed = note_.window.mode != effective_mode;
    note_.window.mode = effective_mode;
    if (effective_mode == WindowMode::TopMost && note_.window.click_through) {
        SetClickThrough(false);
    }
    if (update_model || mode_changed) {
        MarkDirty();
    }
    if (toolbar_) toolbar_->PositionToolbar();
    UpdateEditorBounds();
    Render();
    if (effective_mode == WindowMode::Desktop && toolbar_) toolbar_->Hide(true);
    if (effective_mode != WindowMode::Desktop && IsWindowVisible(window_) &&
        note_.appearance.toolbar_pinned && toolbar_) {
        toolbar_->Show();
    }
}

void NoteWindow::ReapplyDesktopMode() {
    if (note_.window.mode == WindowMode::Desktop) {
        ApplyModeInternal(WindowMode::Desktop, false);
    }
}

void NoteWindow::SetClickThrough(bool enabled) {
    if (!window_) return;
    note_.window.click_through = enabled;
    LONG_PTR style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    style &= ~(static_cast<LONG_PTR>(WS_EX_TRANSPARENT) | WS_EX_NOACTIVATE);
    if (enabled) {
        style |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    } else if (note_.window.mode == WindowMode::Desktop) {
        style |= WS_EX_NOACTIVATE;
    }
    SetWindowLongPtrW(window_, GWL_EXSTYLE, style);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (rich_edit_) rich_edit_->SetReadOnly(enabled);
    if (enabled && toolbar_) {
        toolbar_->Hide(true);
    } else if (!enabled && toolbar_ && note_.appearance.toolbar_pinned && !is_collapsed_ && note_.window.mode != WindowMode::Desktop) {
        toolbar_->PositionToolbar();
        toolbar_->Show();
    }
    Render();
    MarkDirty();
}

void NoteWindow::SetLocked(bool locked) {
    note_.window.locked = locked;
    if (locked && toolbar_ && !note_.appearance.toolbar_pinned) {
        toolbar_->Hide(true);
    } else if (toolbar_ && note_.appearance.toolbar_pinned && !is_collapsed_ && note_.window.mode != WindowMode::Desktop) {
        toolbar_->PositionToolbar();
        toolbar_->Show();
    }
    Render();
    MarkDirty();
}

void NoteWindow::ScrollFromMouseWheel(WPARAM wparam) {
    if (!rich_edit_) return;
    wheel_delta_remainder_ += GET_WHEEL_DELTA_WPARAM(wparam);
    const int detents = wheel_delta_remainder_ / WHEEL_DELTA;
    wheel_delta_remainder_ %= WHEEL_DELTA;
    if (detents == 0) return;

    UINT lines = 3;
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0) || lines == 0) return;
    const bool page_scroll = lines == WHEEL_PAGESCROLL;
    const UINT repetitions = page_scroll
        ? static_cast<UINT>(std::abs(detents))
        : static_cast<UINT>(std::abs(detents)) * std::min(lines, 100U);
    const WPARAM command = detents > 0
        ? (page_scroll ? SB_PAGEUP : SB_LINEUP)
        : (page_scroll ? SB_PAGEDOWN : SB_LINEDOWN);
    for (UINT index = 0; index < repetitions; ++index) {
        rich_edit_->ForwardMessage(WM_VSCROLL, command, 0);
    }
}

void NoteWindow::ShowContextMenu(POINT screen_point) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"【窗口模式】");
    AppendMenuW(menu, MF_STRING | (note_.window.mode == WindowMode::Normal ? MF_CHECKED : 0), kMenuNormal, L"普通模式");
    AppendMenuW(menu, MF_STRING | (note_.window.mode == WindowMode::TopMost ? MF_CHECKED : 0), kMenuTopMost, L"置顶模式\tCtrl+T");
    AppendMenuW(menu, MF_STRING | (note_.window.mode == WindowMode::Desktop ? MF_CHECKED : 0), kMenuDesktop, L"嵌入桌面");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"【其他选项】");
    AppendMenuW(menu, MF_STRING | (note_.window.click_through ? MF_CHECKED : 0), kMenuClickThrough, L"鼠标穿透");
    AppendMenuW(menu, MF_STRING | (note_.window.locked ? MF_CHECKED : 0), kMenuLocked, L"锁定位置\tCtrl+L");
    AppendMenuW(menu, MF_STRING | (note_.window.auto_hide ? MF_CHECKED : 0), kMenuAutoHide, L"贴边自动隐藏");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"【便签管理】");
    AppendMenuW(menu, MF_STRING, kMenuNew, L"新建便签\tCtrl+N");
    AppendMenuW(menu, MF_STRING, kMenuDelete, L"删除此便签\tCtrl+W");
    SetForegroundWindow(window_);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen_point.x,
                                       screen_point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    PostMessageW(window_, WM_NULL, 0, 0);
    switch (command) {
        case kMenuNormal: SetWindowMode(WindowMode::Normal); break;
        case kMenuTopMost: SetWindowMode(WindowMode::TopMost); break;
        case kMenuDesktop: SetWindowMode(WindowMode::Desktop); break;
        case kMenuClickThrough: SetClickThrough(!note_.window.click_through); break;
        case kMenuLocked: SetLocked(!note_.window.locked); break;
        case kMenuAutoHide: SetAutoHide(!note_.window.auto_hide); break;
        case kMenuNew: if (callbacks_.request_new) callbacks_.request_new(this); break;
        case kMenuDelete: if (callbacks_.request_delete) callbacks_.request_delete(this); break;
    }
}

LRESULT CALLBACK NoteWindow::WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    NoteWindow* self = reinterpret_cast<NoteWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<NoteWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT NoteWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            if (!limits) return 0;
            const UINT dpi = DpiForWindowOrSystem(window_);
            limits->ptMinTrackSize.x = DipToPixel(kMinimumWindowWidthDip, dpi);
            limits->ptMinTrackSize.y = DipToPixel(kMinimumWindowHeightDip, dpi);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor)) {
                limits->ptMaxTrackSize.x = monitor.rcWork.right - monitor.rcWork.left;
                limits->ptMaxTrackSize.y = monitor.rcWork.bottom - monitor.rcWork.top;
            }
            return 0;
        }
        case WM_MOVING: {
            if (desktop_embedder_ && desktop_embedder_->is_embedded()) return TRUE;
            auto* moving = reinterpret_cast<RECT*>(lparam);
            if (!moving) return TRUE;
            const int v_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            const int v_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int v_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            const int v_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            const int width = moving->right - moving->left;
            const int height = moving->bottom - moving->top;
            constexpr LONG kMargin = 32;
            moving->left = std::clamp<LONG>(moving->left, v_left - width + kMargin, v_left + v_width - kMargin);
            moving->top = std::clamp<LONG>(moving->top, v_top, v_top + v_height - kMargin);
            moving->right = moving->left + width;
            moving->bottom = moving->top + height;
            return TRUE;
        }
        case WM_SIZING: {
            auto* sizing = reinterpret_cast<RECT*>(lparam);
            if (!sizing) return TRUE;
            const UINT dpi = DpiForWindowOrSystem(window_);
            const int minimum_width = DipToPixel(kMinimumWindowWidthDip, dpi);
            int width = sizing->right - sizing->left;
            if (width < minimum_width) {
                if (wparam == WMSZ_LEFT || wparam == WMSZ_TOPLEFT || wparam == WMSZ_BOTTOMLEFT) {
                    sizing->left = sizing->right - minimum_width;
                } else {
                    sizing->right = sizing->left + minimum_width;
                }
                width = minimum_width;
            }
            const double minimum_height_dip = GetToolbarHeightDipForWidth(width, dpi) +
                                              kColorBarHeightDip + kMinimumEditorHeightDip;
            const int minimum_height = std::max(
                DipToPixel(kMinimumWindowHeightDip, dpi), DipToPixel(minimum_height_dip, dpi));
            if (sizing->bottom - sizing->top < minimum_height) {
                if (wparam == WMSZ_TOP || wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT) {
                    sizing->top = sizing->bottom - minimum_height;
                } else {
                    sizing->bottom = sizing->top + minimum_height;
                }
            }
            return TRUE;
        }
        case WM_NCHITTEST: {
            if (note_.window.click_through) return HTTRANSPARENT;

            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(window_, &point);
            RECT client{};
            GetClientRect(window_, &client);
            const UINT dpi = DpiForWindowOrSystem(window_);

            if (is_collapsed_) {
                const RECT tab_rect = GetCollapsedTabRect(client, collapsed_edge_, dpi);
                if (PtInRect(&tab_rect, point)) {
                    return HTCLIENT;
                }
                return HTTRANSPARENT;
            }

            const float pixel_to_dip = 96.0F / static_cast<float>(dpi ? dpi : 96);
            const float logical_width = static_cast<float>(client.right) * pixel_to_dip;
            const auto badges = GetActiveStatusBadges(note_, logical_width);
            for (const auto& badge : badges) {
                const RECT rc = BadgeDipToPixel(badge, dpi);
                if (PtInRect(&rc, point)) {
                    return HTCLIENT;
                }
            }

            if (note_.window.locked) return HTCLIENT;
            const int corner = DipToPixel(16.0, dpi);
            if (point.x >= client.right - corner && point.y >= client.bottom - corner) return HTBOTTOMRIGHT;
            if (point.x < corner && point.y >= client.bottom - corner) return HTBOTTOMLEFT;
            if (point.x < corner && point.y < corner) return HTTOPLEFT;
            if (point.x >= client.right - corner && point.y < corner) return HTTOPRIGHT;

            const int edge = DipToPixel(8.0, dpi);
            if (point.y < edge) return HTCAPTION;
            if (point.y >= client.bottom - edge) return HTBOTTOM;
            if (point.x < edge) return HTLEFT;
            if (point.x >= client.right - edge) return HTRIGHT;
            return HTCLIENT;
        }
        case WM_NCMOUSEMOVE: {
            KillTimer(window_, kToolbarHideTimerId);
            if (toolbar_ && !toolbar_->IsVisible() && !is_collapsed_ &&
                (note_.appearance.toolbar_pinned || (!note_.window.locked && !note_.window.click_through)) &&
                note_.window.mode != WindowMode::Desktop) {
                toolbar_->Show();
            }
            break;
        }
        case WM_EXITSIZEMOVE: {
            UpdateWindowState();
            if (note_.window.auto_hide && !is_collapsed_ && !note_.window.locked &&
                note_.window.mode != WindowMode::Desktop) {
                SetTimer(window_, kAutoHideTimerId, kAutoHideDelayMs, nullptr);
            }
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lparam) == HTCLIENT) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(window_, &pt);
                RECT client{};
                GetClientRect(window_, &client);
                const UINT dpi = DpiForWindowOrSystem(window_);
                const float pixel_to_dip = 96.0F / static_cast<float>(dpi ? dpi : 96);
                const float logical_width = static_cast<float>(client.right) * pixel_to_dip;
                const auto badges = GetActiveStatusBadges(note_, logical_width);
                for (const auto& badge : badges) {
                    const RECT rc = BadgeDipToPixel(badge, dpi);
                    if (PtInRect(&rc, pt)) {
                        SetCursor(LoadCursorW(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
            }
            const bool is_focused = (GetFocus() == window_);
            if (LOWORD(lparam) == HTCLIENT && rich_edit_ && !note_.window.click_through && is_focused) {
                if (rich_edit_->ForwardMessage(message, wparam, lparam)) return TRUE;
            }
            if (LOWORD(lparam) == HTCLIENT && !is_focused) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            return DefWindowProcW(window_, message, wparam, lparam);
        }
        case WM_PAINT:
            Render();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOVE:
            if (toolbar_ && toolbar_->PositionToolbar()) {
                UpdateEditorBounds();
                Render();
            }
            UpdateWindowState();
            MarkDirty();
            break;
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                UpdateEditorBounds();
                if (toolbar_ && toolbar_->PositionToolbar()) {
                    UpdateEditorBounds();
                }
                Render();
                UpdateWindowState();
                MarkDirty();
            }
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
        case WM_WINDOWPOSCHANGING: {
            if (note_.window.mode == WindowMode::Desktop) {
                auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
                if (pos && (pos->flags & SWP_HIDEWINDOW)) {
                    pos->flags &= ~SWP_HIDEWINDOW;
                }
            }
            break;
        }
        case WM_ACTIVATE:
            if (LOWORD(wparam) != WA_INACTIVE) {
                if (callbacks_.activated) callbacks_.activated(this);
                if (toolbar_) {
                    toolbar_->Show();
                    const HWND insert_after = note_.window.mode == WindowMode::TopMost ? HWND_TOPMOST : HWND_TOP;
                    SetWindowPos(toolbar_->hwnd(), insert_after, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            } else {
                if (note_.window.auto_hide && !note_.window.locked &&
                    note_.window.mode != WindowMode::Desktop && !is_collapsed_) {
                    POINT pt{};
                    GetCursorPos(&pt);
                    RECT note_rect{};
                    RECT toolbar_rect{};
                    GetWindowRect(window_, &note_rect);
                    const bool in_note = PtInRect(&note_rect, pt);
                    const bool in_toolbar = toolbar_ && toolbar_->hwnd() &&
                                            GetWindowRect(toolbar_->hwnd(), &toolbar_rect) &&
                                            PtInRect(&toolbar_rect, pt);
                    if (!in_note && !in_toolbar) {
                        SetTimer(window_, kAutoHideTimerId, 150, nullptr);
                    }
                }
            }
            break;
        case WM_SETFOCUS:
            KillTimer(window_, kToolbarHideTimerId);
            if (rich_edit_) rich_edit_->ForwardMessage(message, wparam, lparam);
            if (toolbar_) {
                toolbar_->Show();
                const HWND insert_after = note_.window.mode == WindowMode::TopMost ? HWND_TOPMOST : HWND_TOP;
                SetWindowPos(toolbar_->hwnd(), insert_after, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            return 0;
        case WM_KILLFOCUS: {
            if (rich_edit_) rich_edit_->ForwardMessage(message, wparam, lparam);
            SyncNote();
            if (callbacks_.save_now) callbacks_.save_now();
            HWND next = reinterpret_cast<HWND>(wparam);
            const bool to_toolbar = toolbar_ && (next == toolbar_->hwnd() || IsChild(toolbar_->hwnd(), next));
            POINT pt{};
            GetCursorPos(&pt);
            RECT note_rect{};
            RECT toolbar_rect{};
            GetWindowRect(window_, &note_rect);
            const bool in_note = PtInRect(&note_rect, pt);
            const bool in_toolbar = toolbar_ && toolbar_->hwnd() &&
                                    GetWindowRect(toolbar_->hwnd(), &toolbar_rect) &&
                                    PtInRect(&toolbar_rect, pt);
            if (!to_toolbar && toolbar_ && !note_.appearance.toolbar_pinned) {
                if (!in_note && !in_toolbar) {
                    SetTimer(window_, kToolbarHideTimerId, kToolbarHideDelayMs, nullptr);
                }
            }
            if (!to_toolbar && note_.window.auto_hide && !note_.window.locked &&
                note_.window.mode != WindowMode::Desktop && !is_collapsed_) {
                if (!in_note && !in_toolbar) {
                    SetTimer(window_, kAutoHideTimerId, 150, nullptr);
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            RECT client{};
            GetClientRect(window_, &client);
            const UINT dpi = DpiForWindowOrSystem(window_);
            const float pixel_to_dip = 96.0F / static_cast<float>(dpi ? dpi : 96);
            const float logical_width = static_cast<float>(client.right) * pixel_to_dip;
            const auto badges = GetActiveStatusBadges(note_, logical_width);
            for (const auto& badge : badges) {
                const RECT rc = BadgeDipToPixel(badge, dpi);
                if (PtInRect(&rc, pt)) {
                    if (badge.type == StatusBadgeType::Lock) {
                        SetLocked(false);
                    } else if (badge.type == StatusBadgeType::ClickThrough) {
                        SetClickThrough(false);
                    } else if (badge.type == StatusBadgeType::TopMost) {
                        SetWindowMode(WindowMode::Normal);
                    } else if (badge.type == StatusBadgeType::Desktop) {
                        SetWindowMode(WindowMode::Normal);
                    }
                    if (toolbar_ && !is_collapsed_ &&
                        (note_.appearance.toolbar_pinned || (!note_.window.click_through && !note_.window.locked)) &&
                        note_.window.mode != WindowMode::Desktop) {
                        toolbar_->PositionToolbar();
                        toolbar_->Show();
                    }
                    return 0;
                }
            }

            if (note_.window.click_through) {
                return 0;
            }

            SetForegroundWindow(window_);
            SetFocus(window_);

            if (toolbar_ && toolbar_->IsVisible()) {
                const HWND insert_after = note_.window.mode == WindowMode::TopMost ? HWND_TOPMOST : HWND_TOP;
                SetWindowPos(toolbar_->hwnd(), insert_after, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            if (rich_edit_) {
                const LRESULT res = rich_edit_->ForwardMessage(message, wparam, lparam);
                Render();
                return res;
            }
            break;
        }
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (ctrl) {
                if (wparam == 'N' || wparam == 'n') {
                    if (callbacks_.request_new) callbacks_.request_new(this);
                    return 0;
                } else if (wparam == 'W' || wparam == 'w') {
                    if (callbacks_.request_delete) callbacks_.request_delete(this);
                    return 0;
                } else if (wparam == 'T' || wparam == 't') {
                    SetWindowMode(note_.window.mode == WindowMode::TopMost ? WindowMode::Normal : WindowMode::TopMost);
                    return 0;
                } else if (wparam == 'L' || wparam == 'l') {
                    SetLocked(!note_.window.locked);
                    return 0;
                }
            }
            if (rich_edit_) {
                const LRESULT res = rich_edit_->ForwardMessage(message, wparam, lparam);
                Render();
                return res;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            if (is_collapsed_) {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                RECT client{};
                GetClientRect(window_, &client);
                const UINT dpi = DpiForWindowOrSystem(window_);
                const RECT tab_rect = GetCollapsedTabRect(client, collapsed_edge_, dpi);
                if (PtInRect(&tab_rect, point)) {
                    ExpandFromEdge();
                } else {
                    break;
                }
            }
            KillTimer(window_, kAutoHideTimerId);
            KillTimer(window_, kToolbarHideTimerId);
            if (toolbar_ && !toolbar_->IsVisible() && !is_collapsed_) {
                toolbar_->Show();
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
            TrackMouseEvent(&tracking);
            if (rich_edit_) {
                const LRESULT res = rich_edit_->ForwardMessage(message, wparam, lparam);
                if (wparam & MK_LBUTTON) Render();
                return res;
            }
            break;
        }
        case WM_MOUSELEAVE: {
            if (toolbar_ && !note_.appearance.toolbar_pinned) {
                POINT pt{};
                GetCursorPos(&pt);
                RECT note_rect{};
                RECT toolbar_rect{};
                GetWindowRect(window_, &note_rect);
                const bool in_note = PtInRect(&note_rect, pt);
                const bool in_toolbar = toolbar_->hwnd() &&
                                        GetWindowRect(toolbar_->hwnd(), &toolbar_rect) &&
                                        PtInRect(&toolbar_rect, pt);
                if (!in_note && !in_toolbar) {
                    SetTimer(window_, kToolbarHideTimerId, kToolbarHideDelayMs, nullptr);
                }
            }
            if (note_.window.auto_hide && !note_.window.locked &&
                note_.window.mode != WindowMode::Desktop && !is_collapsed_) {
                SetTimer(window_, kAutoHideTimerId, kAutoHideDelayMs, nullptr);
            }
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1) {
                RECT rect{}; GetWindowRect(window_, &rect); point = {rect.left + 12, rect.top + 12};
            }
            ShowContextMenu(point);
            return 0;
        }
        case WM_TIMER:
            if (wparam == RichEditHost::kCaretTimerId) {
                if (rich_edit_) rich_edit_->ToggleCaretBlink();
                return 0;
            }
            if (wparam == kToolbarHideTimerId) {
                KillTimer(window_, kToolbarHideTimerId);
                if (toolbar_ && !note_.appearance.toolbar_pinned) {
                    POINT pt{};
                    GetCursorPos(&pt);
                    RECT note_rect{};
                    RECT toolbar_rect{};
                    GetWindowRect(window_, &note_rect);
                    const bool in_note = PtInRect(&note_rect, pt);
                    const bool in_toolbar = toolbar_->hwnd() &&
                                            GetWindowRect(toolbar_->hwnd(), &toolbar_rect) &&
                                            PtInRect(&toolbar_rect, pt);
                    if (!in_note && !in_toolbar) {
                        toolbar_->Hide();
                    }
                }
                return 0;
            }
            if (wparam == kAutoHideTimerId) {
                KillTimer(window_, kAutoHideTimerId);
                POINT pt{};
                GetCursorPos(&pt);
                RECT note_rect{};
                GetWindowRect(window_, &note_rect);
                RECT toolbar_rect{};
                const bool in_toolbar = toolbar_ && toolbar_->hwnd() &&
                                        GetWindowRect(toolbar_->hwnd(), &toolbar_rect) &&
                                        PtInRect(&toolbar_rect, pt);
                if (!PtInRect(&note_rect, pt) && !in_toolbar) {
                    CheckAutoHide();
                }
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            ScrollFromMouseWheel(wparam);
            return 0;
        case WM_CLOSE:
            if (callbacks_.request_delete) callbacks_.request_delete(this);
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            break;
    }

    if (rich_edit_) {
        switch (message) {
            case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP:
            case WM_KEYUP: case WM_CHAR: case WM_DEADCHAR:
            case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_SYSCHAR:
            case WM_IME_STARTCOMPOSITION: case WM_IME_COMPOSITION: case WM_IME_ENDCOMPOSITION:
            case WM_IME_SETCONTEXT: case WM_INPUTLANGCHANGE: case WM_TIMER:
            case WM_VSCROLL: case WM_HSCROLL: {
                const LRESULT res = rich_edit_->ForwardMessage(message, wparam, lparam);
                Render();
                return res;
            }
            default:
                break;
        }
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace desktopnote
