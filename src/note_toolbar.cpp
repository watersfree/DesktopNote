#include "note_toolbar.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <set>

namespace desktopnote {
namespace {

constexpr int kFontCombo = 2101;
constexpr int kSizeCombo = 2102;
constexpr int kPinToolbar = 2106;
constexpr int kWindowMenu = 2107;
constexpr int kOpacity = 2110;
constexpr int kPadding = 2111;
constexpr int kSpacing = 2112;
constexpr int kPaddingLabel = 2121;
constexpr int kSpacingLabel = 2122;
constexpr int kHandleLabel = 2123;
constexpr int kTransparencyLabel = 2124;
constexpr int kBackgroundLabel = 2125;
constexpr int kFontLabel = 2126;
constexpr int kSizeLabel = 2127;
constexpr int kFontColorLabel = 2128;
constexpr int kBorderLabel = 2129;
constexpr int kBackgroundColorBase = 2200;
constexpr int kFontColorBase = 2210;
constexpr int kBorderColorBase = 2220;

// Curated 6-color palettes for modern stickies
constexpr std::array<std::uint32_t, 6> kBackgroundColors{
    0x1E1E24,  // Obsidian Dark
    0xFAF6EE,  // Warm Cream Paper
    0xEEF5FB,  // Nordic Ice Blue
    0xEDF6EE,  // Sage Green
    0xF4EEF8,  // Velvet Mist
    0x2B303C   // Deep Slate
};

constexpr std::array<std::uint32_t, 6> kFontColors{
    0xFFFFFF,  // Pure White
    0x1E293B,  // Slate Dark
    0x38BDF8,  // Sky Blue
    0x34D399,  // Mint Emerald
    0xFBBF24,  // Amber Gold
    0xFB7185   // Rose Coral
};

constexpr std::array<std::uint32_t, 6> kBorderColors{
    0xFF9800,  // Sunset Amber
    0x10B981,  // Emerald
    0x0288D1,  // Cyber Blue
    0xF43F5E,  // Coral Rose
    0x8B5CF6,  // Iris Violet
    0x64748B   // Graphite Slate
};

// Modern Dark Slate UI Color System
constexpr COLORREF kToolbarBgColor = RGB(30, 34, 42);         // #1E222A
constexpr COLORREF kToolbarBorderColor = RGB(47, 54, 68);     // #2F3644
constexpr COLORREF kToolbarControlBg = RGB(40, 45, 56);       // #282D38
constexpr COLORREF kToolbarControlHover = RGB(54, 62, 78);    // #363E4E
constexpr COLORREF kToolbarControlActive = RGB(2, 136, 209);  // #0288D1
constexpr COLORREF kToolbarTextPrimary = RGB(241, 245, 249);  // #F1F5F9
constexpr COLORREF kToolbarTextSecondary = RGB(148, 163, 184);// #94A3B8
constexpr COLORREF kToolbarTextMuted = RGB(100, 116, 139);    // #64748B

constexpr double kToolbarNominalWidthDip = 760.0;
constexpr double kToolbarWideMinimumWidthDip = 620.0;
constexpr double kToolbarMediumMinimumWidthDip = 400.0;
constexpr double kToolbarWideHeightDip = 76.0;
constexpr double kToolbarMediumHeightDip = 112.0;
constexpr double kToolbarNarrowHeightDip = 174.0;

BOOL CALLBACK CollectFonts(const LOGFONTW* font, const TEXTMETRICW*, DWORD type, LPARAM value) {
    if (!(type & TRUETYPE_FONTTYPE) || font->lfFaceName[0] == L'@') return TRUE;
    auto* fonts = reinterpret_cast<std::set<std::wstring>*>(value);
    fonts->insert(font->lfFaceName);
    return TRUE;
}

void SetControlFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

struct ToolbarDimensions {
    int width = 1;
    int height = 1;
};

enum class ToolbarLayout {
    Wide,
    Medium,
    Narrow,
};

ToolbarLayout GetToolbarLayout(int width, UINT dpi) {
    if (width >= DipToPixel(kToolbarWideMinimumWidthDip, dpi)) return ToolbarLayout::Wide;
    if (width >= DipToPixel(kToolbarMediumMinimumWidthDip, dpi)) return ToolbarLayout::Medium;
    return ToolbarLayout::Narrow;
}

double GetToolbarHeightDip(ToolbarLayout layout) {
    if (layout == ToolbarLayout::Wide) return kToolbarWideHeightDip;
    if (layout == ToolbarLayout::Medium) return kToolbarMediumHeightDip;
    return kToolbarNarrowHeightDip;
}

ToolbarDimensions GetToolbarDimensions(HWND reference, UINT dpi) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const bool monitor_found = reference &&
        GetMonitorInfoW(MonitorFromWindow(reference, MONITOR_DEFAULTTONEAREST), &monitor);
    const int work_width = monitor_found
        ? std::max(1L, monitor.rcWork.right - monitor.rcWork.left)
        : std::max(1, GetSystemMetrics(SM_CXSCREEN));
    int reference_width = DipToPixel(kToolbarNominalWidthDip, dpi);
    RECT reference_rect{};
    if (reference && GetWindowRect(reference, &reference_rect)) {
        reference_width = std::max(1L, reference_rect.right - reference_rect.left);
    }
    const int width = std::min(reference_width, work_width);
    const ToolbarLayout layout = GetToolbarLayout(width, dpi);
    return {
        width,
        DipToPixel(GetToolbarHeightDip(layout), dpi),
    };
}

}  // namespace

NoteToolbar::NoteToolbar(HINSTANCE instance, HWND parent_window, Note& note, NoteToolbarCallbacks callbacks)
    : instance_(instance), parent_window_(parent_window), note_(note), callbacks_(std::move(callbacks)) {}

NoteToolbar::~NoteToolbar() {
    Destroy();
}

bool NoteToolbar::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW toolbar_class{};
    toolbar_class.cbSize = sizeof(toolbar_class);
    toolbar_class.lpfnWndProc = ToolbarProcedure;
    toolbar_class.hInstance = instance;
    toolbar_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    toolbar_class.hbrBackground = CreateSolidBrush(kToolbarBgColor);
    toolbar_class.lpszClassName = kToolbarWindowClass;
    return RegisterClassExW(&toolbar_class) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool NoteToolbar::Create() {
    if (!RegisterWindowClass(instance_)) return false;
    const UINT dpi = DpiForWindowOrSystem(parent_window_);
    const auto dimensions = GetToolbarDimensions(parent_window_, dpi);
    toolbar_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, kToolbarWindowClass,
                               L"DesktopNote 工具栏",
                               WS_POPUP, 0, 0, dimensions.width, dimensions.height, parent_window_, nullptr,
                               instance_, this);
    if (!toolbar_) return false;

    toolbar_background_brush_.reset(CreateSolidBrush(kToolbarBgColor));
    toolbar_control_brush_.reset(CreateSolidBrush(kToolbarControlBg));
    constexpr BYTE kToolbarAlpha = 185;
    SetLayeredWindowAttributes(toolbar_, 0, kToolbarAlpha, LWA_ALPHA);

    const auto create_label = [this](int id, const wchar_t* text) {
        return CreateWindowExW(0, WC_STATICW, text, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                               0, 0, 0, 0, toolbar_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    create_label(kHandleLabel, L"⠿");
    create_label(kTransparencyLabel, L"透明");
    create_label(kBackgroundLabel, L"背景");
    create_label(kFontLabel, L"字体");
    create_label(kSizeLabel, L"字号");
    create_label(kFontColorLabel, L"字色");
    create_label(kPaddingLabel, L"边距");
    create_label(kSpacingLabel, L"行距");
    create_label(kBorderLabel, L"边框");

    font_combo_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                                  CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL, 0, 0, 0, 0, toolbar_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontCombo)), instance_, nullptr);
    size_combo_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
                                  CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
                                  0, 0, 0, 0, toolbar_,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSizeCombo)), instance_, nullptr);

    const auto create_palette = [this](int base, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            CreateWindowExW(0, WC_BUTTONW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                            0, 0, 0, 0, toolbar_,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(base + index)),
                            instance_, nullptr);
        }
    };
    create_palette(kBackgroundColorBase, kBackgroundColors.size());
    create_palette(kFontColorBase, kFontColors.size());
    create_palette(kBorderColorBase, kBorderColors.size());

    opacity_slider_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
                                      0, 0, 0, 0, toolbar_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpacity)), instance_, nullptr);
    padding_slider_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
                                      0, 0, 0, 0, toolbar_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPadding)), instance_, nullptr);
    spacing_slider_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_NOTICKS,
                                      0, 0, 0, 0, toolbar_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSpacing)), instance_, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"\xE718",
                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, toolbar_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPinToolbar)), instance_, nullptr);
    CreateWindowExW(0, WC_BUTTONW, L"\xE713", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                    0, 0, 0, 0, toolbar_,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWindowMenu)), instance_, nullptr);

    UpdateToolbarFonts(dpi);
    LayoutToolbar(dpi);

    HDC dc = GetDC(toolbar_);
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    std::set<std::wstring> fonts;
    EnumFontFamiliesExW(dc, &query, reinterpret_cast<FONTENUMPROCW>(CollectFonts),
                        reinterpret_cast<LPARAM>(&fonts), 0);
    ReleaseDC(toolbar_, dc);

    int selected_font = -1;
    int index = 0;
    for (const auto& font : fonts) {
        SendMessageW(font_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(font.c_str()));
        if (_wcsicmp(font.c_str(), note_.appearance.font_family.c_str()) == 0) selected_font = index;
        ++index;
    }
    if (selected_font < 0 && !note_.appearance.font_family.empty()) {
        selected_font = static_cast<int>(SendMessageW(
            font_combo_, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(note_.appearance.font_family.c_str())));
    }
    SendMessageW(font_combo_, CB_SETCURSEL, selected_font >= 0 ? selected_font : 0, 0);

    const std::array<int, 12> sizes{10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36, 48};
    int selected_size = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const auto text = std::to_wstring(sizes[i]);
        SendMessageW(size_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (std::abs(note_.appearance.font_size_dip - sizes[i]) < 0.5) selected_size = static_cast<int>(i);
    }
    SendMessageW(size_combo_, CB_SETCURSEL, selected_size, 0);
    SendMessageW(opacity_slider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(opacity_slider_, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::lround(note_.appearance.background_alpha * 100.0)));
    SendMessageW(padding_slider_, TBM_SETRANGE, TRUE, MAKELPARAM(4, 30));
    SendMessageW(padding_slider_, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(note_.appearance.padding_dip)));
    SendMessageW(spacing_slider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 20));
    SendMessageW(spacing_slider_, TBM_SETPOS, TRUE,
                 static_cast<LPARAM>(std::lround(note_.appearance.paragraph_spacing_dip)));

    PositionToolbar();
    return true;
}

void NoteToolbar::Destroy() {
    if (toolbar_) {
        DestroyWindow(toolbar_);
        toolbar_ = nullptr;
    }
    toolbar_font_.reset();
    toolbar_icon_font_.reset();
    toolbar_background_brush_.reset();
    toolbar_control_brush_.reset();
    toolbar_visible_ = false;
}

void NoteToolbar::UpdateToolbarFonts(UINT dpi) {
    toolbar_font_.reset(CreateFontW(-DipToPixel(11.0, dpi), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI"));
    toolbar_icon_font_.reset(CreateFontW(-DipToPixel(14.0, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets"));
    for (HWND child = GetWindow(toolbar_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const int id = GetDlgCtrlID(child);
        SetControlFont(child, id == kPinToolbar || id == kWindowMenu ? toolbar_icon_font_.get() : toolbar_font_.get());
    }
}

void NoteToolbar::LayoutToolbar(UINT dpi) {
    if (!toolbar_) return;
    const auto s = [dpi](double dip) { return DipToPixel(dip, dpi); };
    constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS;
    RECT client{};
    GetClientRect(toolbar_, &client);
    const ToolbarLayout layout = GetToolbarLayout(client.right, dpi);

    if (layout == ToolbarLayout::Narrow) {
        // Row 1: Handle, Opacity Label, Opacity Slider, Background Label, 6 Bg Swatches
        SetWindowPos(GetDlgItem(toolbar_, kHandleLabel), nullptr, s(6), s(6), s(22), s(22), flags);
        SetWindowPos(GetDlgItem(toolbar_, kTransparencyLabel), nullptr, s(30), s(8), s(30), s(20), flags);
        SetWindowPos(opacity_slider_, nullptr, s(62), s(3), s(64), s(30), flags);
        SetWindowPos(GetDlgItem(toolbar_, kBackgroundLabel), nullptr, s(128), s(8), s(30), s(20), flags);
        for (std::size_t index = 0; index < kBackgroundColors.size(); ++index) {
            const int offset = static_cast<int>(index) * 19;
            SetWindowPos(GetDlgItem(toolbar_, kBackgroundColorBase + static_cast<int>(index)), nullptr,
                         s(160 + offset), s(9), s(16), s(16), flags);
        }

        // Row 2: Font Label, Font Combo, Size Label, Size Combo
        SetWindowPos(GetDlgItem(toolbar_, kFontLabel), nullptr, s(6), s(42), s(30), s(20), flags);
        SetWindowPos(font_combo_, nullptr, s(38), s(38), s(106), s(280), flags);
        SetWindowPos(GetDlgItem(toolbar_, kSizeLabel), nullptr, s(150), s(42), s(30), s(20), flags);
        SetWindowPos(size_combo_, nullptr, s(182), s(38), s(52), s(240), flags);

        // Row 3: Font Color Label, 6 Font Color Swatches, Action buttons
        SetWindowPos(GetDlgItem(toolbar_, kFontColorLabel), nullptr, s(6), s(76), s(30), s(20), flags);
        for (std::size_t index = 0; index < kFontColors.size(); ++index) {
            const int offset = static_cast<int>(index) * 19;
            SetWindowPos(GetDlgItem(toolbar_, kFontColorBase + static_cast<int>(index)), nullptr,
                         s(38 + offset), s(77), s(16), s(16), flags);
        }

        // Row 4: Padding Label, Padding Slider, Spacing Label, Spacing Slider, Border Label, 6 Border Swatches, Pin, Menu
        SetWindowPos(GetDlgItem(toolbar_, kPaddingLabel), nullptr, s(6), s(110), s(30), s(20), flags);
        SetWindowPos(padding_slider_, nullptr, s(38), s(104), s(60), s(31), flags);
        SetWindowPos(GetDlgItem(toolbar_, kSpacingLabel), nullptr, s(102), s(110), s(30), s(20), flags);
        SetWindowPos(spacing_slider_, nullptr, s(134), s(104), s(60), s(31), flags);

        // Row 5: Border Label + 6 Swatches + Pin & Menu
        SetWindowPos(GetDlgItem(toolbar_, kBorderLabel), nullptr, s(6), s(144), s(30), s(20), flags);
        for (std::size_t index = 0; index < kBorderColors.size(); ++index) {
            const int offset = static_cast<int>(index) * 19;
            SetWindowPos(GetDlgItem(toolbar_, kBorderColorBase + static_cast<int>(index)), nullptr,
                         s(38 + offset), s(145), s(16), s(16), flags);
        }
        SetWindowPos(GetDlgItem(toolbar_, kPinToolbar), nullptr, client.right - s(56), s(140), s(26), s(26), flags);
        SetWindowPos(GetDlgItem(toolbar_, kWindowMenu), nullptr, client.right - s(28), s(140), s(26), s(26), flags);
        return;
    }

    if (layout == ToolbarLayout::Medium) {
        // Row 1: Handle, Opacity, Background 6 Swatches, Pin & Menu
        SetWindowPos(GetDlgItem(toolbar_, kHandleLabel), nullptr, s(6), s(6), s(22), s(22), flags);
        SetWindowPos(GetDlgItem(toolbar_, kTransparencyLabel), nullptr, s(30), s(8), s(30), s(20), flags);
        SetWindowPos(opacity_slider_, nullptr, s(62), s(3), s(64), s(30), flags);
        SetWindowPos(GetDlgItem(toolbar_, kBackgroundLabel), nullptr, s(128), s(8), s(30), s(20), flags);
        for (std::size_t index = 0; index < kBackgroundColors.size(); ++index) {
            const int offset = static_cast<int>(index) * 19;
            SetWindowPos(GetDlgItem(toolbar_, kBackgroundColorBase + static_cast<int>(index)), nullptr,
                         s(160 + offset), s(9), s(16), s(16), flags);
        }
        SetWindowPos(GetDlgItem(toolbar_, kPinToolbar), nullptr, client.right - s(56), s(4), s(26), s(26), flags);
        SetWindowPos(GetDlgItem(toolbar_, kWindowMenu), nullptr, client.right - s(28), s(4), s(26), s(26), flags);

        // Row 2: Font, Size, Font Color 6 Swatches
        SetWindowPos(GetDlgItem(toolbar_, kFontLabel), nullptr, s(6), s(42), s(30), s(20), flags);
        SetWindowPos(font_combo_, nullptr, s(38), s(38), s(106), s(280), flags);
        SetWindowPos(GetDlgItem(toolbar_, kSizeLabel), nullptr, s(150), s(42), s(30), s(20), flags);
        SetWindowPos(size_combo_, nullptr, s(182), s(38), s(52), s(240), flags);
        SetWindowPos(GetDlgItem(toolbar_, kFontColorLabel), nullptr, s(242), s(42), s(30), s(20), flags);
        for (std::size_t index = 0; index < kFontColors.size(); ++index) {
            const int offset = static_cast<int>(index) * 19;
            SetWindowPos(GetDlgItem(toolbar_, kFontColorBase + static_cast<int>(index)), nullptr,
                         s(274 + offset), s(43), s(16), s(16), flags);
        }

        // Row 3: Padding, Spacing, Border 6 Swatches
        SetWindowPos(GetDlgItem(toolbar_, kPaddingLabel), nullptr, s(6), s(76), s(30), s(20), flags);
        SetWindowPos(padding_slider_, nullptr, s(38), s(71), s(60), s(30), flags);
        SetWindowPos(GetDlgItem(toolbar_, kSpacingLabel), nullptr, s(102), s(76), s(30), s(20), flags);
        SetWindowPos(spacing_slider_, nullptr, s(134), s(71), s(60), s(30), flags);
        SetWindowPos(GetDlgItem(toolbar_, kBorderLabel), nullptr, s(204), s(76), s(30), s(20), flags);
        for (std::size_t index = 0; index < kBorderColors.size(); ++index) {
            const int offset = static_cast<int>(index) * 19;
            SetWindowPos(GetDlgItem(toolbar_, kBorderColorBase + static_cast<int>(index)), nullptr,
                         s(236 + offset), s(77), s(16), s(16), flags);
        }
        return;
    }

    // Wide Layout (>= 620 DIP): 2 Clean Structured Rows
    // Row 1: Handle, Opacity, 6 Background Swatches, Font, Size, Pin, Menu
    SetWindowPos(GetDlgItem(toolbar_, kHandleLabel), nullptr, s(8), s(5), s(22), s(26), flags);
    SetWindowPos(GetDlgItem(toolbar_, kTransparencyLabel), nullptr, s(34), s(8), s(30), s(20), flags);
    SetWindowPos(opacity_slider_, nullptr, s(66), s(2), s(66), s(29), flags);
    SetWindowPos(GetDlgItem(toolbar_, kBackgroundLabel), nullptr, s(136), s(8), s(30), s(20), flags);
    for (std::size_t index = 0; index < kBackgroundColors.size(); ++index) {
        const int offset = static_cast<int>(index) * 19;
        SetWindowPos(GetDlgItem(toolbar_, kBackgroundColorBase + static_cast<int>(index)), nullptr,
                     s(168 + offset), s(9), s(16), s(16), flags);
    }
    SetWindowPos(GetDlgItem(toolbar_, kFontLabel), nullptr, s(292), s(8), s(30), s(20), flags);
    SetWindowPos(font_combo_, nullptr, s(324), s(4), s(116), s(280), flags);
    SetWindowPos(GetDlgItem(toolbar_, kSizeLabel), nullptr, s(448), s(8), s(30), s(20), flags);
    SetWindowPos(size_combo_, nullptr, s(480), s(4), s(54), s(240), flags);
    SetWindowPos(GetDlgItem(toolbar_, kPinToolbar), nullptr, client.right - s(58), s(4), s(26), s(26), flags);
    SetWindowPos(GetDlgItem(toolbar_, kWindowMenu), nullptr, client.right - s(28), s(4), s(26), s(26), flags);

    // Row 2: Font Color 6 Swatches, Padding Slider, Spacing Slider, Border 6 Swatches
    SetWindowPos(GetDlgItem(toolbar_, kFontColorLabel), nullptr, s(8), s(45), s(30), s(20), flags);
    for (std::size_t index = 0; index < kFontColors.size(); ++index) {
        const int offset = static_cast<int>(index) * 19;
        SetWindowPos(GetDlgItem(toolbar_, kFontColorBase + static_cast<int>(index)), nullptr,
                     s(40 + offset), s(46), s(16), s(16), flags);
    }

    SetWindowPos(GetDlgItem(toolbar_, kPaddingLabel), nullptr, s(164), s(45), s(30), s(20), flags);
    SetWindowPos(padding_slider_, nullptr, s(196), s(39), s(66), s(30), flags);
    SetWindowPos(GetDlgItem(toolbar_, kSpacingLabel), nullptr, s(268), s(45), s(30), s(20), flags);
    SetWindowPos(spacing_slider_, nullptr, s(300), s(39), s(66), s(30), flags);
    SetWindowPos(GetDlgItem(toolbar_, kBorderLabel), nullptr, s(376), s(45), s(30), s(20), flags);
    for (std::size_t index = 0; index < kBorderColors.size(); ++index) {
        const int offset = static_cast<int>(index) * 19;
        SetWindowPos(GetDlgItem(toolbar_, kBorderColorBase + static_cast<int>(index)), nullptr,
                     s(408 + offset), s(46), s(16), s(16), flags);
    }
}

bool NoteToolbar::PositionToolbar() {
    if (!toolbar_ || !parent_window_) return false;
    RECT note_rect{};
    RECT toolbar_rect{};
    GetWindowRect(parent_window_, &note_rect);
    GetWindowRect(toolbar_, &toolbar_rect);
    const UINT dpi = DpiForWindowOrSystem(parent_window_);
    const auto dimensions = GetToolbarDimensions(parent_window_, dpi);
    const int old_width = toolbar_rect.right - toolbar_rect.left;
    const int old_height = toolbar_rect.bottom - toolbar_rect.top;
    const int width = dimensions.width;
    const int height = dimensions.height;
    const bool size_changed = old_width != width || old_height != height;

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromWindow(parent_window_, MONITOR_DEFAULTTONEAREST), &monitor);

    // Smart external placement:
    // If there is enough room ABOVE the note, dock ABOVE.
    // Otherwise, if there is room BELOW the note (e.g. note is near top of screen), dock BELOW.
    // Otherwise, place above or top.
    const int space_above = note_rect.top - monitor.rcWork.top;
    const int space_below = monitor.rcWork.bottom - note_rect.bottom;

    const bool was_at_bottom = at_bottom_;
    if (space_above >= height) {
        at_bottom_ = false;
    } else if (space_below >= height) {
        at_bottom_ = true;
    } else {
        at_bottom_ = space_below > space_above;
    }

    const int target_x = note_rect.left;
    const int target_y = at_bottom_ ? note_rect.bottom : (note_rect.top - height);

    const HWND insert_after = note_.window.mode == WindowMode::TopMost ? HWND_TOPMOST : HWND_TOP;
    SetWindowPos(toolbar_, insert_after, target_x, target_y, width, height,
                 SWP_NOACTIVATE | (toolbar_visible_ ? SWP_SHOWWINDOW : 0));
    if (size_changed) {
        UpdateToolbarFonts(dpi);
        LayoutToolbar(dpi);
        InvalidateRect(toolbar_, nullptr, FALSE);
    }
    return size_changed || (was_at_bottom != at_bottom_);
}

void NoteToolbar::EnsureBrushes() {
    if (!toolbar_background_brush_) {
        toolbar_background_brush_.reset(CreateSolidBrush(kToolbarBgColor));
    }
    if (!toolbar_control_brush_) {
        toolbar_control_brush_.reset(CreateSolidBrush(kToolbarControlBg));
    }
}

void NoteToolbar::ReleaseIdleResources() {
    if (!toolbar_visible_) {
        toolbar_background_brush_.reset();
        toolbar_control_brush_.reset();
    }
}

void NoteToolbar::Show() {
    if (!toolbar_ || note_.window.locked || note_.window.click_through ||
        note_.window.mode == WindowMode::Desktop) {
        return;
    }
    EnsureBrushes();
    if (!toolbar_visible_ || !IsWindowVisible(toolbar_)) {
        toolbar_visible_ = true;
        PositionToolbar();
        const HWND insert_after = note_.window.mode == WindowMode::TopMost ? HWND_TOPMOST : HWND_TOP;
        SetWindowPos(toolbar_, insert_after, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    }
}

void NoteToolbar::Hide(bool force) {
    if (!toolbar_) return;
    if (!force && note_.appearance.toolbar_pinned) return;
    if (toolbar_visible_ || IsWindowVisible(toolbar_)) {
        ShowWindow(toolbar_, SW_HIDE);
        toolbar_visible_ = false;
        ReleaseIdleResources();
    }
}

int NoteToolbar::ReservedHeight() const {
    return 0;
}

bool NoteToolbar::IsVisible() const {
    return toolbar_ && IsWindowVisible(toolbar_);
}

void NoteToolbar::Invalidate() {
    if (toolbar_) {
        InvalidateRect(toolbar_, nullptr, FALSE);
    }
}

LRESULT CALLBACK NoteToolbar::ToolbarProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    NoteToolbar* self = reinterpret_cast<NoteToolbar*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<NoteToolbar*>(create->lpCreateParams);
        self->toolbar_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleToolbarMessage(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT NoteToolbar::HandleToolbarMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_ERASEBKGND) {
        EnsureBrushes();
        RECT client{};
        GetClientRect(toolbar_, &client);
        FillRect(reinterpret_cast<HDC>(wparam), &client, toolbar_background_brush_.get());
        return 1;
    } else if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(toolbar_, &ps);
        EnsureBrushes();
        FillRect(dc, &ps.rcPaint, toolbar_background_brush_.get());
        EndPaint(toolbar_, &ps);
        return 0;
    } else if (message == WM_WINDOWPOSCHANGED) {
        auto* pos = reinterpret_cast<WINDOWPOS*>(lparam);
        if (pos && !(pos->flags & SWP_NOSIZE)) {
            LayoutToolbar(DpiForWindowOrSystem(toolbar_));
            RedrawWindow(toolbar_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
    } else if (message == WM_CTLCOLORSTATIC) {
        HDC dc = reinterpret_cast<HDC>(wparam);
        HWND control = reinterpret_cast<HWND>(lparam);
        const int id = GetDlgCtrlID(control);
        SetBkMode(dc, TRANSPARENT);
        if (id == kHandleLabel) {
            SetTextColor(dc, kToolbarTextMuted);
        } else {
            SetTextColor(dc, kToolbarTextSecondary);
        }
        return reinterpret_cast<LRESULT>(toolbar_background_brush_.get());
    } else if (message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLOREDIT) {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetBkColor(dc, kToolbarControlBg);
        SetTextColor(dc, kToolbarTextPrimary);
        return reinterpret_cast<LRESULT>(toolbar_control_brush_.get());
    } else if (message == WM_MEASUREITEM) {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure && (measure->CtlID == kFontCombo || measure->CtlID == kSizeCombo)) {
            measure->itemHeight = static_cast<UINT>(DipToPixel(22.0, DpiForWindowOrSystem(toolbar_)));
            return TRUE;
        }
    } else if (message == WM_NCHITTEST) {
        POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(toolbar_, &pt);
        HWND child = ChildWindowFromPointEx(toolbar_, pt, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
        if (child && child != toolbar_) {
            const int id = GetDlgCtrlID(child);
            if (id == kHandleLabel) return HTCAPTION;
            return HTCLIENT;
        }
        return HTCAPTION;
    } else if (message == WM_SETCURSOR) {
        if (LOWORD(lparam) == HTCAPTION) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
    } else if (message == WM_NCLBUTTONDOWN && wparam == HTCAPTION) {
        SetForegroundWindow(parent_window_);
        SendMessageW(parent_window_, WM_NCLBUTTONDOWN, HTCAPTION, lparam);
        return 0;
    } else if (message == WM_LBUTTONDOWN) {
        SetForegroundWindow(parent_window_);
        POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ClientToScreen(toolbar_, &pt);
        SendMessageW(parent_window_, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
        return 0;
    } else if (message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE) {
        KillTimer(parent_window_, 9103);
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, toolbar_, 0};
        TrackMouseEvent(&tracking);
        return 0;
    } else if (message == WM_MOUSELEAVE) {
        if (!note_.appearance.toolbar_pinned) {
            POINT pt{};
            GetCursorPos(&pt);
            RECT note_rect{};
            RECT toolbar_rect{};
            GetWindowRect(parent_window_, &note_rect);
            GetWindowRect(toolbar_, &toolbar_rect);
            const bool in_note = PtInRect(&note_rect, pt);
            const bool in_toolbar = PtInRect(&toolbar_rect, pt);
            if (!in_note && !in_toolbar) {
                SetTimer(parent_window_, 9103, 250, nullptr);
            }
        }
        return 0;
    } else if (message == WM_DRAWITEM) {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (!draw) return FALSE;
        const int id = static_cast<int>(draw->CtlID);

        // 1. Draw Dropdown Combos (Font & Size)
        if (id == kFontCombo || id == kSizeCombo) {
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            HBRUSH fill = CreateSolidBrush(selected ? kToolbarControlHover : kToolbarControlBg);
            FillRect(draw->hDC, &draw->rcItem, fill);
            DeleteObject(fill);

            // Subtle border
            HPEN border_pen = CreatePen(PS_SOLID, 1, selected ? RGB(79, 195, 247) : kToolbarBorderColor);
            HGDIOBJ old_pen = SelectObject(draw->hDC, border_pen);
            HGDIOBJ old_brush = SelectObject(draw->hDC, GetStockObject(HOLLOW_BRUSH));
            Rectangle(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom);
            SelectObject(draw->hDC, old_brush);
            SelectObject(draw->hDC, old_pen);
            DeleteObject(border_pen);

            if (draw->itemID != static_cast<UINT>(-1)) {
                const LRESULT length = SendMessageW(draw->hwndItem, CB_GETLBTEXTLEN, draw->itemID, 0);
                if (length >= 0) {
                    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
                    SendMessageW(draw->hwndItem, CB_GETLBTEXT, draw->itemID,
                                 reinterpret_cast<LPARAM>(value.data()));
                    value.resize(static_cast<std::size_t>(length));
                    const int saved = SaveDC(draw->hDC);
                    SelectObject(draw->hDC, toolbar_font_.get());
                    SetBkMode(draw->hDC, TRANSPARENT);
                    SetTextColor(draw->hDC, selected ? RGB(255, 255, 255) : kToolbarTextPrimary);
                    RECT text_rect = draw->rcItem;
                    text_rect.left += DipToPixel(6.0, DpiForWindowOrSystem(toolbar_));
                    DrawTextW(draw->hDC, value.c_str(), -1, &text_rect,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    RestoreDC(draw->hDC, saved);
                }
            }
            if ((draw->itemState & ODS_FOCUS) != 0) DrawFocusRect(draw->hDC, &draw->rcItem);
            return TRUE;
        }

        // 2. Draw Action Buttons (Pin & Menu)
        if (id == kPinToolbar || id == kWindowMenu) {
            const bool is_pinned = (id == kPinToolbar && note_.appearance.toolbar_pinned);
            const bool is_hover = (draw->itemState & ODS_SELECTED) != 0;
            COLORREF bg = is_pinned ? kToolbarControlActive : (is_hover ? kToolbarControlHover : kToolbarControlBg);
            HBRUSH fill = CreateSolidBrush(bg);
            HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(draw->hDC, fill));
            HPEN border_pen = CreatePen(PS_SOLID, 1, is_pinned ? RGB(56, 189, 248) : kToolbarBorderColor);
            HPEN old_pen = static_cast<HPEN>(SelectObject(draw->hDC, border_pen));
            const int radius = DipToPixel(4.0, DpiForWindowOrSystem(toolbar_));
            RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom,
                      radius, radius);
            SelectObject(draw->hDC, old_brush);
            SelectObject(draw->hDC, old_pen);
            DeleteObject(fill);
            DeleteObject(border_pen);

            wchar_t glyph[4]{};
            GetWindowTextW(draw->hwndItem, glyph, static_cast<int>(std::size(glyph)));
            const int saved = SaveDC(draw->hDC);
            SelectObject(draw->hDC, toolbar_icon_font_.get());
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, is_pinned || is_hover ? RGB(255, 255, 255) : kToolbarTextSecondary);

            if (id == kPinToolbar && is_pinned) {
                SetGraphicsMode(draw->hDC, GM_ADVANCED);
                const float cx = static_cast<float>(draw->rcItem.left + draw->rcItem.right) / 2.0f;
                const float cy = static_cast<float>(draw->rcItem.top + draw->rcItem.bottom) / 2.0f;
                constexpr float kAngleRad = -45.0f * 3.14159265358979323846f / 180.0f; // -45 deg counter-clockwise
                const float cos_a = std::cos(kAngleRad);
                const float sin_a = std::sin(kAngleRad);
                XFORM xform{};
                xform.eM11 = cos_a;
                xform.eM12 = sin_a;
                xform.eM21 = -sin_a;
                xform.eM22 = cos_a;
                xform.eDx = cx - (cos_a * cx - sin_a * cy);
                xform.eDy = cy - (sin_a * cx + cos_a * cy);
                SetWorldTransform(draw->hDC, &xform);
            }

            RECT text_rect = draw->rcItem;
            DrawTextW(draw->hDC, glyph, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RestoreDC(draw->hDC, saved);
            return TRUE;
        }

        // 3. Draw Modern Circular Micro-Swatches
        std::uint32_t color = 0;
        bool selected = false;
        bool palette_button = false;
        if (id >= kBackgroundColorBase &&
            id < kBackgroundColorBase + static_cast<int>(kBackgroundColors.size())) {
            color = kBackgroundColors[static_cast<std::size_t>(id - kBackgroundColorBase)];
            selected = color == note_.appearance.background_color;
            palette_button = true;
        } else if (id >= kFontColorBase &&
                   id < kFontColorBase + static_cast<int>(kFontColors.size())) {
            color = kFontColors[static_cast<std::size_t>(id - kFontColorBase)];
            selected = color == note_.appearance.font_color;
            palette_button = true;
        } else if (id >= kBorderColorBase &&
                   id < kBorderColorBase + static_cast<int>(kBorderColors.size())) {
            color = kBorderColors[static_cast<std::size_t>(id - kBorderColorBase)];
            selected = color == note_.appearance.border_color;
            palette_button = true;
        }
        if (!palette_button) return FALSE;

        RECT rc = draw->rcItem;
        HBRUSH bg_brush = CreateSolidBrush(kToolbarBgColor);
        FillRect(draw->hDC, &rc, bg_brush);
        DeleteObject(bg_brush);

        if (selected) {
            // Draw glowing accent ring
            HPEN glow_pen = CreatePen(PS_SOLID, 1, RGB(56, 189, 248));
            HBRUSH glow_brush = CreateSolidBrush(kToolbarBgColor);
            HGDIOBJ old_pen = SelectObject(draw->hDC, glow_pen);
            HGDIOBJ old_br = SelectObject(draw->hDC, glow_brush);
            Ellipse(draw->hDC, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(draw->hDC, old_br);
            SelectObject(draw->hDC, old_pen);
            DeleteObject(glow_pen);
            DeleteObject(glow_brush);
            InflateRect(&rc, -2, -2);
        } else {
            HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(60, 68, 84));
            HGDIOBJ old_pen = SelectObject(draw->hDC, border_pen);
            HGDIOBJ old_br = SelectObject(draw->hDC, GetStockObject(HOLLOW_BRUSH));
            Ellipse(draw->hDC, rc.left, rc.top, rc.right, rc.bottom);
            SelectObject(draw->hDC, old_br);
            SelectObject(draw->hDC, old_pen);
            DeleteObject(border_pen);
            InflateRect(&rc, -1, -1);
        }

        // Fill circle with the swatch color
        HBRUSH fill = CreateSolidBrush(RgbToColorRef(color));
        HPEN fill_pen = CreatePen(PS_SOLID, 1, RgbToColorRef(color));
        HGDIOBJ old_fill = SelectObject(draw->hDC, fill);
        HGDIOBJ old_pen = SelectObject(draw->hDC, fill_pen);
        Ellipse(draw->hDC, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(draw->hDC, old_pen);
        SelectObject(draw->hDC, old_fill);
        DeleteObject(fill_pen);
        DeleteObject(fill);

        if ((draw->itemState & ODS_FOCUS) != 0) {
            RECT focus = draw->rcItem;
            DrawFocusRect(draw->hDC, &focus);
        }
        return TRUE;
    } else if (message == WM_COMMAND) {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (id == kFontCombo && notification == CBN_SELCHANGE) {
            wchar_t text[LF_FACESIZE]{};
            GetWindowTextW(font_combo_, text, LF_FACESIZE);
            if (callbacks_.font_family_changed) callbacks_.font_family_changed(text);
            if (callbacks_.focus_main_window) callbacks_.focus_main_window();
            return 0;
        }
        if (id == kSizeCombo && notification == CBN_SELCHANGE) {
            wchar_t text[16]{};
            GetWindowTextW(size_combo_, text, 16);
            if (callbacks_.font_size_changed) callbacks_.font_size_changed(_wtof(text));
            if (callbacks_.focus_main_window) callbacks_.focus_main_window();
            return 0;
        }
        if (notification == BN_CLICKED && id >= kBackgroundColorBase &&
            id < kBackgroundColorBase + static_cast<int>(kBackgroundColors.size())) {
            if (callbacks_.background_color_changed) {
                callbacks_.background_color_changed(kBackgroundColors[static_cast<std::size_t>(id - kBackgroundColorBase)]);
            }
            if (callbacks_.focus_main_window) callbacks_.focus_main_window();
            return 0;
        }
        if (notification == BN_CLICKED && id >= kFontColorBase &&
            id < kFontColorBase + static_cast<int>(kFontColors.size())) {
            if (callbacks_.font_color_changed) {
                callbacks_.font_color_changed(kFontColors[static_cast<std::size_t>(id - kFontColorBase)]);
            }
            if (callbacks_.focus_main_window) callbacks_.focus_main_window();
            return 0;
        }
        if (notification == BN_CLICKED && id >= kBorderColorBase &&
            id < kBorderColorBase + static_cast<int>(kBorderColors.size())) {
            if (callbacks_.border_color_changed) {
                callbacks_.border_color_changed(kBorderColors[static_cast<std::size_t>(id - kBorderColorBase)]);
            }
            if (callbacks_.focus_main_window) callbacks_.focus_main_window();
            return 0;
        }
        if (notification == BN_CLICKED && id == kPinToolbar) {
            KillTimer(parent_window_, 9103);
            if (callbacks_.pin_toggled) callbacks_.pin_toggled();
            InvalidateRect(GetDlgItem(toolbar_, kPinToolbar), nullptr, FALSE);
            if (callbacks_.focus_main_window) callbacks_.focus_main_window();
            return 0;
        }
        if (notification == BN_CLICKED && id == kWindowMenu) {
            RECT rect{};
            GetWindowRect(GetDlgItem(toolbar_, kWindowMenu), &rect);
            if (callbacks_.show_menu) callbacks_.show_menu(POINT{rect.left, rect.bottom});
            return 0;
        }
    } else if (message == WM_HSCROLL) {
        HWND slider = reinterpret_cast<HWND>(lparam);
        const int pos = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
        if (slider == opacity_slider_ && callbacks_.opacity_changed) {
            callbacks_.opacity_changed(static_cast<double>(pos) / 100.0);
        } else if (slider == padding_slider_ && callbacks_.padding_changed) {
            callbacks_.padding_changed(static_cast<double>(pos));
        } else if (slider == spacing_slider_ && callbacks_.spacing_changed) {
            callbacks_.spacing_changed(static_cast<double>(pos));
        }
        return 0;
    }
    return DefWindowProcW(toolbar_, message, wparam, lparam);
}

}  // namespace desktopnote
