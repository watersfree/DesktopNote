#pragma once

#include <windows.h>

namespace desktopnote {

class DesktopEmbedder {
public:
    DesktopEmbedder() = default;
    ~DesktopEmbedder() = default;

    DesktopEmbedder(const DesktopEmbedder&) = delete;
    DesktopEmbedder& operator=(const DesktopEmbedder&) = delete;

    bool Attach(HWND window);
    bool Detach(HWND window);
    bool is_embedded() const noexcept;
    HWND desktop_host() const noexcept { return desktop_host_; }

    static HWND FindDesktopHost();

private:
    HWND desktop_host_ = nullptr;
    LONG_PTR original_extended_style_ = 0;
    bool is_embedded_ = false;
};

}  // namespace desktopnote
