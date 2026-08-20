#include "desktop_embedder.h"

namespace desktopnote {
namespace {

struct DesktopHosts {
    HWND icons = nullptr;
    HWND wallpaper = nullptr;
};

BOOL CALLBACK FindIconHost(HWND top_level, LPARAM value) {
    if (!IsWindowVisible(top_level)) return TRUE;
    const HWND icon_view = FindWindowExW(top_level, nullptr, L"SHELLDLL_DefView", nullptr);
    if (!icon_view || !IsWindowVisible(icon_view)) return TRUE;
    const HWND wallpaper_host = FindWindowExW(nullptr, top_level, L"WorkerW", nullptr);
    if (!wallpaper_host || !IsWindowVisible(wallpaper_host)) return TRUE;
    auto& hosts = *reinterpret_cast<DesktopHosts*>(value);
    hosts.icons = top_level;
    hosts.wallpaper = wallpaper_host;
    return FALSE;
}

DesktopHosts FindDesktopHosts() {
    HWND progman = FindWindowW(L"Progman", L"Program Manager");
    if (!progman) progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return {};

    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &ignored);

    DesktopHosts hosts;
    EnumWindows(FindIconHost, reinterpret_cast<LPARAM>(&hosts));
    return hosts;
}

bool SetExtendedStyle(HWND window, LONG_PTR style) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

}  // namespace

HWND DesktopEmbedder::FindDesktopHost() {
    return FindDesktopHosts().wallpaper;
}

bool DesktopEmbedder::is_embedded() const noexcept {
    return is_embedded_ && desktop_host_ && IsWindow(desktop_host_);
}

bool DesktopEmbedder::Detach(HWND window) {
    if (!window || !is_embedded_) return true;
    if (!SetExtendedStyle(window, original_extended_style_) ||
        !SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                          SWP_SHOWWINDOW | SWP_FRAMECHANGED)) {
        return false;
    }

    desktop_host_ = nullptr;
    original_extended_style_ = 0;
    is_embedded_ = false;
    return true;
}

bool DesktopEmbedder::Attach(HWND window) {
    if (!window) return false;
    const DesktopHosts hosts = FindDesktopHosts();
    if (!hosts.icons || !hosts.wallpaper) return false;

    if (is_embedded_) {
        if (desktop_host_ == hosts.wallpaper) {
            return SetWindowPos(window, hosts.wallpaper, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                                    SWP_SHOWWINDOW) != FALSE;
        }
        if (!Detach(window)) return false;
    }

    const LONG_PTR extended_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (!SetExtendedStyle(window, extended_style | WS_EX_NOACTIVATE) ||
        !SetWindowPos(window, hosts.wallpaper, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE |
                          SWP_SHOWWINDOW | SWP_FRAMECHANGED)) {
        SetExtendedStyle(window, extended_style);
        return false;
    }

    if (GetForegroundWindow() == window) {
        SetFocus(nullptr);
        SetActiveWindow(nullptr);
    }
    desktop_host_ = hosts.wallpaper;
    original_extended_style_ = extended_style;
    is_embedded_ = true;
    return true;
}

}  // namespace desktopnote
