#include "base64.h"
#include "note_window.h"
#include "win_util.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <windows.h>

#include <cmath>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace desktopnote;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        throw std::runtime_error("line " + std::to_string(line) +
                                 " check failed: " + expression);
    }
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

HWND FindOwnedToolbar(HWND owner) {
    struct Search {
        HWND owner = nullptr;
        HWND toolbar = nullptr;
    } search{owner};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM value) -> BOOL {
        auto& result = *reinterpret_cast<Search*>(value);
        wchar_t class_name[128]{};
        GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
        if (GetWindow(window, GW_OWNER) == result.owner &&
            std::wstring_view(class_name) == L"DesktopNote.ToolbarWindow.v2") {
            result.toolbar = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.toolbar;
}

HWND FindDesktopIconHost() {
    HWND result = nullptr;
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        const HWND icon_view = FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr);
        if (IsWindowVisible(window) && icon_view && IsWindowVisible(icon_view)) {
            *reinterpret_cast<HWND*>(value) = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}

bool IsAboveInTopLevelZOrder(HWND higher, HWND lower) {
    for (HWND window = GetTopWindow(nullptr); window;
         window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == higher) return true;
        if (window == lower) return false;
    }
    return false;
}

RECT ControlRect(HWND parent, int identifier) {
    const HWND control = GetDlgItem(parent, identifier);
    CHECK(control != nullptr);
    RECT rect{};
    CHECK(GetWindowRect(control, &rect));
    return rect;
}

std::vector<std::uint8_t> CaptureScreenRegion(const RECT& region) {
    const int width = region.right - region.left;
    const int height = region.bottom - region.top;
    CHECK(width > 0 && height > 0);

    HDC screen = GetDC(nullptr);
    CHECK(screen != nullptr);
    HDC memory = CreateCompatibleDC(screen);
    CHECK(memory != nullptr);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    CHECK(bitmap != nullptr && pixels != nullptr);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    CHECK(previous != nullptr);
    CHECK(BitBlt(memory, 0, 0, width, height, screen, region.left, region.top,
                 SRCCOPY | CAPTUREBLT));

    const auto* begin = static_cast<const std::uint8_t*>(pixels);
    std::vector<std::uint8_t> result(begin, begin + static_cast<std::size_t>(width * height * 4));
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}

double ScreenDifference(const std::vector<std::uint8_t>& left,
                        const std::vector<std::uint8_t>& right) {
    CHECK(left.size() == right.size());
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < left.size(); index += 4) {
        total += static_cast<std::uint64_t>(std::abs(
            static_cast<int>(left[index]) - static_cast<int>(right[index])));
        total += static_cast<std::uint64_t>(std::abs(
            static_cast<int>(left[index + 1]) - static_cast<int>(right[index + 1])));
        total += static_cast<std::uint64_t>(std::abs(
            static_cast<int>(left[index + 2]) - static_cast<int>(right[index + 2])));
    }
    return static_cast<double>(total) /
           static_cast<double>((left.size() / 4) * 3);
}

void TestBoundaryLayout() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CHECK(SUCCEEDED(com_result));
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    Note note;
    note.id = "boundary-layout";
    note.window.x_dip = 100.0;
    note.window.y_dip = 100.0;
    note.window.width_dip = 500.0;
    note.window.height_dip = 400.0;
    note.appearance.background_alpha = 1.0;
    note.appearance.background_color = 0x00D060;
    const std::string rich_text = "{\\rtf1\\ansi Desktop embed text}";
    note.content_rtf_base64 = EncodeBase64(
        std::vector<std::uint8_t>(rich_text.begin(), rich_text.end()));
    NoteWindow window(GetModuleHandleW(nullptr), std::move(note), {});
    CHECK(window.Create());
    CHECK(window.DisplayTitle() == L"Desktop embed text");
    const HWND note_window = window.hwnd();
    const HWND toolbar = FindOwnedToolbar(note_window);
    CHECK(toolbar != nullptr);
    const UINT dpi = DpiForWindowOrSystem(note_window);

    RECT note_rect{};
    RECT toolbar_rect{};
    CHECK(GetWindowRect(note_window, &note_rect));
    CHECK(GetWindowRect(toolbar, &toolbar_rect));
    CHECK(toolbar_rect.top == note_rect.top - (toolbar_rect.bottom - toolbar_rect.top) ||
          toolbar_rect.top == note_rect.bottom);
    CHECK(toolbar_rect.right - toolbar_rect.left == note_rect.right - note_rect.left);
    CHECK(toolbar_rect.bottom - toolbar_rect.top == DipToPixel(112.0, dpi));

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    CHECK(GetMonitorInfoW(MonitorFromWindow(note_window, MONITOR_DEFAULTTONEAREST), &monitor));
    const int width = note_rect.right - note_rect.left;
    const int height = note_rect.bottom - note_rect.top;
    RECT moving{monitor.rcWork.right - width + DipToPixel(80.0, dpi), note_rect.top,
                monitor.rcWork.right + DipToPixel(80.0, dpi), note_rect.top + height};
    CHECK(SendMessageW(note_window, WM_MOVING, 0, reinterpret_cast<LPARAM>(&moving)) == TRUE);
    SetWindowPos(note_window, nullptr, moving.left, moving.top, width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    CHECK(GetWindowRect(toolbar, &toolbar_rect));
    CHECK(toolbar_rect.left == moving.left);

    const int wide_width = DipToPixel(620.0, dpi);
    SetWindowPos(note_window, nullptr, monitor.rcWork.left, moving.top, wide_width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    CHECK(GetWindowRect(toolbar, &toolbar_rect));
    CHECK(toolbar_rect.right - toolbar_rect.left == wide_width);
    CHECK(toolbar_rect.bottom - toolbar_rect.top == DipToPixel(76.0, dpi));
    const RECT last_background = ControlRect(toolbar, 2205);
    const RECT font_label = ControlRect(toolbar, 2126);
    const RECT size_combo = ControlRect(toolbar, 2102);
    const RECT pin_button = ControlRect(toolbar, 2106);
    const RECT font_color_label = ControlRect(toolbar, 2128);
    const RECT first_font_color = ControlRect(toolbar, 2210);
    const RECT last_font_color = ControlRect(toolbar, 2215);
    const RECT padding_label = ControlRect(toolbar, 2121);
    const RECT last_border_color = ControlRect(toolbar, 2225);
    CHECK(last_background.right < font_label.left);
    CHECK(size_combo.right < pin_button.left);
    CHECK(font_color_label.right <= first_font_color.left);
    CHECK(std::abs(font_color_label.top - first_font_color.top) <= DipToPixel(3.0, dpi));
    CHECK(last_font_color.right < padding_label.left);
    CHECK(last_border_color.right <= toolbar_rect.right);
    CHECK((GetWindowLongPtrW(GetDlgItem(toolbar, 2110), GWL_STYLE) & TBS_NOTICKS) != 0);

    RECT input_rect{};
    CHECK(GetWindowRect(note_window, &input_rect));
    const POINT center_point{(input_rect.left + input_rect.right) / 2,
                             (input_rect.top + input_rect.bottom) / 2};
    const POINT badge_point{input_rect.right - DipToPixel(10.0, dpi),
                            input_rect.top + DipToPixel(10.0, dpi)};
    window.SetClickThrough(true);
    LONG_PTR input_style = GetWindowLongPtrW(note_window, GWL_EXSTYLE);
    CHECK(window.note().window.click_through);
    CHECK((input_style & WS_EX_TRANSPARENT) != 0);
    CHECK((input_style & WS_EX_NOACTIVATE) != 0);
    CHECK(SendMessageW(note_window, WM_NCHITTEST, 0,
                       MAKELPARAM(center_point.x, center_point.y)) == HTTRANSPARENT);
    CHECK(SendMessageW(note_window, WM_NCHITTEST, 0,
                       MAKELPARAM(badge_point.x, badge_point.y)) == HTTRANSPARENT);
    window.SetClickThrough(false);
    input_style = GetWindowLongPtrW(note_window, GWL_EXSTYLE);
    CHECK(!window.note().window.click_through);
    CHECK((input_style & WS_EX_TRANSPARENT) == 0);
    CHECK((input_style & WS_EX_NOACTIVATE) == 0);

    CHECK(GetWindowRect(note_window, &note_rect));
    RECT visibility_sample{
        note_rect.left + (note_rect.right - note_rect.left) / 2 - 20,
        note_rect.top + (note_rect.bottom - note_rect.top) / 2 - 20,
        note_rect.left + (note_rect.right - note_rect.left) / 2 + 20,
        note_rect.top + (note_rect.bottom - note_rect.top) / 2 + 20,
    };
    window.Hide();
    DwmFlush();
    const auto hidden_pixels = CaptureScreenRegion(visibility_sample);
    window.Show(true);
    DwmFlush();
    const auto normal_pixels = CaptureScreenRegion(visibility_sample);
    CHECK(ScreenDifference(hidden_pixels, normal_pixels) > 8.0);

    RECT desktop_position{};
    CHECK(GetWindowRect(note_window, &desktop_position));
    const LONG_PTR normal_style = GetWindowLongPtrW(note_window, GWL_STYLE);
    window.SetWindowMode(WindowMode::Desktop);
    RECT embedded_position{};
    CHECK(GetWindowRect(note_window, &embedded_position));
    if (embedded_position.left != desktop_position.left || embedded_position.top != desktop_position.top) {
        RECT parent_rect{};
        GetWindowRect(GetAncestor(note_window, GA_PARENT), &parent_rect);
        std::cerr << "desktop position before=" << desktop_position.left << ',' << desktop_position.top
                  << " after=" << embedded_position.left << ',' << embedded_position.top
                  << " parent=" << parent_rect.left << ',' << parent_rect.top << '-'
                  << parent_rect.right << ',' << parent_rect.bottom << '\n';
    }
    CHECK(embedded_position.left == desktop_position.left);
    CHECK(embedded_position.top == desktop_position.top);
    CHECK(embedded_position.right == desktop_position.right);
    CHECK(embedded_position.bottom == desktop_position.bottom);
    const LONG_PTR desktop_style = GetWindowLongPtrW(note_window, GWL_STYLE);
    if (window.note().window.mode == WindowMode::Desktop) {
        CHECK((desktop_style & WS_CHILD) == 0);
        CHECK((desktop_style & WS_POPUP) != 0);
        const HWND desktop_host = DesktopEmbedder::FindDesktopHost();
        const HWND icon_host = FindDesktopIconHost();
        CHECK(desktop_host != nullptr);
        CHECK(icon_host != nullptr);
        CHECK(desktop_host != icon_host);
        CHECK(GetWindow(note_window, GW_OWNER) == nullptr);
        CHECK(IsAboveInTopLevelZOrder(note_window, icon_host));
        CHECK(IsAboveInTopLevelZOrder(icon_host, desktop_host));
        CHECK(window.last_render_succeeded());
        DwmFlush();
        const auto desktop_pixels = CaptureScreenRegion(visibility_sample);
        CHECK(ScreenDifference(hidden_pixels, desktop_pixels) > 8.0);

        window.ReapplyDesktopMode();
        CHECK(window.note().window.mode == WindowMode::Desktop);
        CHECK(GetWindow(note_window, GW_OWNER) == nullptr);
        CHECK(window.last_render_succeeded());
        DwmFlush();
        const auto reapplied_pixels = CaptureScreenRegion(visibility_sample);
        CHECK(ScreenDifference(hidden_pixels, reapplied_pixels) > 8.0);

        window.SetClickThrough(true);
        input_style = GetWindowLongPtrW(note_window, GWL_EXSTYLE);
        CHECK((input_style & WS_EX_TRANSPARENT) != 0);
        CHECK((input_style & WS_EX_NOACTIVATE) != 0);
        window.SetClickThrough(false);
        input_style = GetWindowLongPtrW(note_window, GWL_EXSTYLE);
        CHECK((input_style & WS_EX_TRANSPARENT) == 0);
        CHECK((input_style & WS_EX_NOACTIVATE) != 0);
    } else {
        CHECK(window.note().window.mode == WindowMode::Normal);
        CHECK((desktop_style & (WS_CHILD | WS_POPUP)) ==
              (normal_style & (WS_CHILD | WS_POPUP)));
    }
    RedrawWindow(note_window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RedrawWindow(note_window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RECT redrawn_position{};
    CHECK(GetWindowRect(note_window, &redrawn_position));
    CHECK(redrawn_position.left == desktop_position.left);
    CHECK(redrawn_position.top == desktop_position.top);
    window.SyncNote();
    CHECK(!window.note().content_rtf_base64.empty());
    window.SetWindowMode(WindowMode::Normal);
    RECT restored_position{};
    CHECK(GetWindowRect(note_window, &restored_position));
    CHECK(restored_position.left == desktop_position.left);
    CHECK(restored_position.top == desktop_position.top);
    CHECK(restored_position.right == desktop_position.right);
    CHECK(restored_position.bottom == desktop_position.bottom);
    CHECK((GetWindowLongPtrW(note_window, GWL_STYLE) & (WS_CHILD | WS_POPUP)) ==
          (normal_style & (WS_CHILD | WS_POPUP)));
    CHECK(GetWindow(note_window, GW_OWNER) == nullptr);

    window.SetClickThrough(true);
    window.SetWindowMode(WindowMode::TopMost);
    input_style = GetWindowLongPtrW(note_window, GWL_EXSTYLE);
    CHECK(window.note().window.mode == WindowMode::TopMost);
    CHECK(!window.note().window.click_through);
    CHECK((input_style & WS_EX_TRANSPARENT) == 0);
    CHECK((input_style & WS_EX_NOACTIVATE) == 0);
    window.SetWindowMode(WindowMode::Normal);

    window.note().appearance.toolbar_pinned = true;
    window.Hide();
    CHECK(!IsWindowVisible(toolbar));
    window.Show(false);
    CHECK(IsWindowVisible(toolbar));
    window.SetWindowMode(WindowMode::Desktop);
    if (window.note().window.mode == WindowMode::Desktop) {
        CHECK(!IsWindowVisible(toolbar));
    } else {
        CHECK(window.note().window.mode == WindowMode::Normal);
        CHECK(IsWindowVisible(toolbar));
    }
    window.SetWindowMode(WindowMode::Normal);
    CHECK(IsWindowVisible(toolbar));

    const int narrow_width = DipToPixel(350.0, dpi);
    SetWindowPos(note_window, nullptr, moving.left, moving.top, narrow_width, height,
                 SWP_NOACTIVATE | SWP_NOZORDER);
    CHECK(GetWindowRect(note_window, &note_rect));
    CHECK(GetWindowRect(toolbar, &toolbar_rect));
    CHECK(toolbar_rect.left == note_rect.left);
    CHECK(toolbar_rect.right - toolbar_rect.left == narrow_width);
    CHECK(toolbar_rect.bottom - toolbar_rect.top == DipToPixel(174.0, dpi));

    MINMAXINFO limits{};
    SendMessageW(note_window, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    CHECK(limits.ptMinTrackSize.x == DipToPixel(300.0, dpi));
    CHECK(limits.ptMinTrackSize.y == DipToPixel(150.0, dpi));

    RECT sizing{100, 100, 100 + DipToPixel(250.0, dpi), 100 + DipToPixel(100.0, dpi)};
    CHECK(SendMessageW(note_window, WM_SIZING, WMSZ_BOTTOMRIGHT,
                       reinterpret_cast<LPARAM>(&sizing)) == TRUE);
    CHECK(sizing.right - sizing.left == DipToPixel(300.0, dpi));
    CHECK(sizing.bottom - sizing.top >= DipToPixel(150.0, dpi));

    window.Destroy();

    Note compact_note;
    compact_note.id = "compact-restore";
    compact_note.window.width_dip = 350.0;
    compact_note.window.height_dip = 150.0;
    NoteWindow compact_window(GetModuleHandleW(nullptr), std::move(compact_note), {});
    CHECK(compact_window.Create());
    RECT compact_rect{};
    CHECK(GetWindowRect(compact_window.hwnd(), &compact_rect));
    CHECK(compact_rect.bottom - compact_rect.top >= DipToPixel(150.0, DpiForWindowOrSystem(compact_window.hwnd())));
    compact_window.Destroy();
    CoUninitialize();
}

}  // namespace

int main() {
    if (DesktopEmbedder::FindDesktopHost() == nullptr) {
        std::cout << "[SKIP] Interactive Explorer desktop is unavailable\n";
        return 0;
    }

    try {
        TestBoundaryLayout();
        std::cout << "[PASS] Note window boundary and responsive toolbar layout\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
