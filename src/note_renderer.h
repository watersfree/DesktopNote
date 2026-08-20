#pragma once

#include "app_state.h"
#include "win_util.h"

#include <windows.h>
#include <d2d1.h>

namespace desktopnote {

class RichEditHost;

enum class StatusBadgeType {
    Lock,
    ClickThrough,
    TopMost,
    Desktop
};

struct StatusBadgeItem {
    StatusBadgeType type;
    float left_dip;
    float top_dip;
    float right_dip;
    float bottom_dip;
};

inline std::vector<StatusBadgeItem> GetActiveStatusBadges(const Note& note, float logical_width) {
    std::vector<StatusBadgeItem> badges;
    const float badge_size = 20.0F;
    const float margin = 4.0F;
    const float spacing = 4.0F;
    float current_right = logical_width - margin;

    if (note.window.locked) {
        badges.push_back({StatusBadgeType::Lock, current_right - badge_size, margin, current_right, margin + badge_size});
        current_right -= (badge_size + spacing);
    }
    if (note.window.click_through) {
        badges.push_back({StatusBadgeType::ClickThrough, current_right - badge_size, margin, current_right, margin + badge_size});
        current_right -= (badge_size + spacing);
    }
    if (note.window.mode == WindowMode::TopMost) {
        badges.push_back({StatusBadgeType::TopMost, current_right - badge_size, margin, current_right, margin + badge_size});
        current_right -= (badge_size + spacing);
    } else if (note.window.mode == WindowMode::Desktop) {
        badges.push_back({StatusBadgeType::Desktop, current_right - badge_size, margin, current_right, margin + badge_size});
        current_right -= (badge_size + spacing);
    }
    return badges;
}

inline RECT BadgeDipToPixel(const StatusBadgeItem& item, UINT dpi) {
    return RECT{
        DipToPixel(static_cast<double>(item.left_dip), dpi),
        DipToPixel(static_cast<double>(item.top_dip), dpi),
        DipToPixel(static_cast<double>(item.right_dip), dpi),
        DipToPixel(static_cast<double>(item.bottom_dip), dpi)
    };
}

class NoteRenderer {
public:
    NoteRenderer();
    ~NoteRenderer();

    NoteRenderer(const NoteRenderer&) = delete;
    NoteRenderer& operator=(const NoteRenderer&) = delete;

    bool Initialize();
    bool Render(HWND window, const Note& note, RichEditHost* rich_edit,
                bool is_collapsed = false, int collapsed_edge = 0);
    void ReleaseResources();

private:
    bool EnsureDrawingResources(HWND window, int width, int height);

    ID2D1Factory* d2d_factory_ = nullptr;
    ID2D1DCRenderTarget* render_target_ = nullptr;
    HDC memory_dc_ = nullptr;
    HBITMAP surface_bitmap_ = nullptr;
    HGDIOBJ old_bitmap_ = nullptr;
    void* surface_bits_ = nullptr;
    int surface_width_ = 0;
    int surface_height_ = 0;
};

}  // namespace desktopnote
