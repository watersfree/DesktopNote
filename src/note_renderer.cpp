#include "note_renderer.h"

#include "rich_edit_host.h"

#include <d2d1helper.h>
#include <algorithm>
#include <cstring>

namespace desktopnote {
namespace {

constexpr double kColorBarHeightDip = 2.0;
constexpr double kCollapsedTabSizeDip = 18.0;
constexpr double kCollapsedTabLengthDip = 52.0;

}  // namespace

NoteRenderer::NoteRenderer() = default;

NoteRenderer::~NoteRenderer() {
    ReleaseResources();
    if (d2d_factory_) {
        d2d_factory_->Release();
        d2d_factory_ = nullptr;
    }
}

bool NoteRenderer::Initialize() {
    if (d2d_factory_) return true;
    return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_));
}

void NoteRenderer::ReleaseResources() {
    if (render_target_) {
        render_target_->Release();
        render_target_ = nullptr;
    }
    if (memory_dc_ && old_bitmap_) {
        SelectObject(memory_dc_, old_bitmap_);
        old_bitmap_ = nullptr;
    }
    if (surface_bitmap_) {
        DeleteObject(surface_bitmap_);
        surface_bitmap_ = nullptr;
    }
    if (memory_dc_) {
        DeleteDC(memory_dc_);
        memory_dc_ = nullptr;
    }
    surface_bits_ = nullptr;
    surface_width_ = 0;
    surface_height_ = 0;
}

bool NoteRenderer::EnsureDrawingResources(HWND window, int width, int height) {
    if (width <= 0 || height <= 0 || !d2d_factory_) return false;
    if (surface_width_ == width && surface_height_ == height && render_target_) return true;
    ReleaseResources();

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    memory_dc_ = CreateCompatibleDC(nullptr);
    surface_bitmap_ = CreateDIBSection(memory_dc_, &info, DIB_RGB_COLORS, &surface_bits_, nullptr, 0);
    if (!memory_dc_ || !surface_bitmap_ || !surface_bits_) return false;
    old_bitmap_ = SelectObject(memory_dc_, surface_bitmap_);

    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(DpiForWindowOrSystem(window)),
        static_cast<float>(DpiForWindowOrSystem(window)),
        D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    if (FAILED(d2d_factory_->CreateDCRenderTarget(&properties, &render_target_))) return false;
    surface_width_ = width;
    surface_height_ = height;
    return true;
}

bool NoteRenderer::Render(HWND window, const Note& note, RichEditHost* rich_edit,
                          bool is_collapsed, int collapsed_edge) {
    if (!window || !d2d_factory_) return false;
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right;
    const int height = client.bottom;
    if (!EnsureDrawingResources(window, width, height)) return false;

    std::memset(surface_bits_, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    if (FAILED(render_target_->BindDC(memory_dc_, &client))) return false;

    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0, 0.0F));
    ID2D1SolidColorBrush* brush = nullptr;
    const UINT dpi = DpiForWindowOrSystem(window);
    const float pixel_to_dip = 96.0F / static_cast<float>(dpi ? dpi : 96);
    const float logical_width = static_cast<float>(width) * pixel_to_dip;
    const float logical_height = static_cast<float>(height) * pixel_to_dip;
    const float color_bar_height = static_cast<float>(kColorBarHeightDip);

    if (SUCCEEDED(render_target_->CreateSolidColorBrush(
            D2DColor(note.appearance.background_color,
                     static_cast<float>(note.appearance.background_alpha)), &brush))) {
        if (is_collapsed) {
            // Only draw the translucent glass capsule; the rest of the layered window remains 100% transparent
            const float tab_length = static_cast<float>(kCollapsedTabLengthDip);
            const float tab_depth = static_cast<float>(kCollapsedTabSizeDip);
            const float corner_radius = tab_depth * 0.5F;

            D2D1_RECT_F tab_rect{};
            D2D1_RECT_F handle_rect{};

            if (collapsed_edge == 1) { // Top edge -> tab hangs at bottom center of window
                const float cx = logical_width * 0.5F;
                tab_rect = D2D1::RectF(cx - tab_length * 0.5F, logical_height - tab_depth,
                                      cx + tab_length * 0.5F, logical_height);
                handle_rect = D2D1::RectF(cx - 14.0F, logical_height - 10.0F, cx + 14.0F, logical_height - 7.0F);
            } else if (collapsed_edge == 2) { // Left edge -> tab protrudes at right center of window
                const float cy = logical_height * 0.5F;
                tab_rect = D2D1::RectF(logical_width - tab_depth, cy - tab_length * 0.5F,
                                      logical_width, cy + tab_length * 0.5F);
                handle_rect = D2D1::RectF(logical_width - 10.0F, cy - 14.0F, logical_width - 7.0F, cy + 14.0F);
            } else if (collapsed_edge == 3) { // Right edge -> tab protrudes at left center of window
                const float cy = logical_height * 0.5F;
                tab_rect = D2D1::RectF(0.0F, cy - tab_length * 0.5F,
                                      tab_depth, cy + tab_length * 0.5F);
                handle_rect = D2D1::RectF(7.0F, cy - 14.0F, 10.0F, cy + 14.0F);
            }

            // 1. Draw smooth translucent glass capsule body
            const float base_alpha = static_cast<float>(std::clamp(note.appearance.background_alpha * 0.75, 0.45, 0.75));
            brush->SetColor(D2DColor(note.appearance.background_color, base_alpha));
            render_target_->FillRoundedRectangle(
                D2D1::RoundedRect(tab_rect, corner_radius, corner_radius), brush);

            // 2. Draw whisper-thin specular edge highlight (Mica/glass specular rim, no harsh solid borders)
            brush->SetColor(D2DColor(0xFFFFFF, 0.18F));
            render_target_->DrawRoundedRectangle(
                D2D1::RoundedRect(tab_rect, corner_radius, corner_radius), brush, 1.0F);

            // 3. Draw luminous minimalist capsule pill handle
            brush->SetColor(D2DColor(note.appearance.border_color, 0.90F));
            render_target_->FillRoundedRectangle(
                D2D1::RoundedRect(handle_rect, 1.5F, 1.5F), brush);
        } else {
            // Draw note main background across full window
            render_target_->FillRectangle(
                D2D1::RectF(0.0F, 0.0F, logical_width, logical_height), brush);

            // Draw accent bar at top of note
            brush->SetColor(D2DColor(note.appearance.border_color, 0.95F));
            render_target_->FillRectangle(
                D2D1::RectF(0.0F, 0.0F, logical_width, color_bar_height), brush);

            // Draw resize grip dots/lines if not locked
            if (!note.window.locked) {
                brush->SetColor(D2DColor(0xFFFFFF, 0.40F));
                const float x = logical_width - 3.0F;
                const float y = logical_height - 3.0F;
                for (int offset = 0; offset < 3; ++offset) {
                    const float length = 12.0F - static_cast<float>(offset) * 4.0F;
                    render_target_->DrawLine(D2D1::Point2F(x - length, y),
                                             D2D1::Point2F(x, y - length), brush, 1.5F);
                }
            }

            // Draw active status badges in top-right corner (Lock, Click-Through, TopMost, Desktop)
            const auto badges = GetActiveStatusBadges(note, logical_width);
            for (const auto& badge : badges) {
                const D2D1_RECT_F pill_rect = D2D1::RectF(badge.left_dip, badge.top_dip, badge.right_dip, badge.bottom_dip);
                const float cx = (badge.left_dip + badge.right_dip) * 0.5F;
                const float cy = (badge.top_dip + badge.bottom_dip) * 0.5F;

                // 1. Subtle translucent glass background pill
                brush->SetColor(D2DColor(0x000000, 0.35F));
                render_target_->FillRoundedRectangle(D2D1::RoundedRect(pill_rect, 4.0F, 4.0F), brush);

                // 2. Specular glass rim highlight
                brush->SetColor(D2DColor(0xFFFFFF, 0.22F));
                render_target_->DrawRoundedRectangle(D2D1::RoundedRect(pill_rect, 4.0F, 4.0F), brush, 1.0F);

                // 3. Vector glyph based on badge type
                if (badge.type == StatusBadgeType::Lock) {
                    // Shackle (upper arch)
                    brush->SetColor(D2DColor(0xFFFFFF, 0.95F));
                    const D2D1_RECT_F shackle_rect = D2D1::RectF(cx - 3.5F, cy - 5.5F, cx + 3.5F, cy + 0.5F);
                    render_target_->DrawRoundedRectangle(D2D1::RoundedRect(shackle_rect, 2.5F, 2.5F), brush, 1.4F);

                    // Lock Body (lower box)
                    const D2D1_RECT_F body_rect = D2D1::RectF(cx - 5.0F, cy - 0.5F, cx + 5.0F, cy + 5.5F);
                    render_target_->FillRoundedRectangle(D2D1::RoundedRect(body_rect, 1.5F, 1.5F), brush);

                    // Keyhole dot/line
                    brush->SetColor(D2DColor(0x000000, 0.60F));
                    render_target_->DrawLine(D2D1::Point2F(cx, cy + 1.2F), D2D1::Point2F(cx, cy + 3.8F), brush, 1.2F);
                } else if (badge.type == StatusBadgeType::TopMost) {
                    // Pushpin / Thumbtack icon
                    brush->SetColor(D2DColor(0xFFFFFF, 0.95F));
                    // Pin head cap
                    render_target_->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(cx - 3.5F, cy - 6.0F, cx + 3.5F, cy - 3.5F), 1.0F, 1.0F), brush);
                    // Pin barrel
                    render_target_->FillRectangle(D2D1::RectF(cx - 2.0F, cy - 3.5F, cx + 2.0F, cy + 0.5F), brush);
                    // Pin flange rim
                    render_target_->DrawLine(D2D1::Point2F(cx - 4.5F, cy + 0.5F), D2D1::Point2F(cx + 4.5F, cy + 0.5F), brush, 1.4F);
                    // Pin needle tip
                    render_target_->DrawLine(D2D1::Point2F(cx, cy + 0.5F), D2D1::Point2F(cx, cy + 6.0F), brush, 1.3F);
                } else if (badge.type == StatusBadgeType::Desktop) {
                    // Monitor / Desktop Screen icon
                    brush->SetColor(D2DColor(0xFFFFFF, 0.95F));
                    // Screen display frame
                    render_target_->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(cx - 5.5F, cy - 5.0F, cx + 5.5F, cy + 2.5F), 1.5F, 1.5F), brush, 1.3F);
                    // Stand neck
                    render_target_->DrawLine(D2D1::Point2F(cx, cy + 2.5F), D2D1::Point2F(cx, cy + 5.0F), brush, 1.3F);
                    // Stand base bar
                    render_target_->DrawLine(D2D1::Point2F(cx - 3.5F, cy + 5.0F), D2D1::Point2F(cx + 3.5F, cy + 5.0F), brush, 1.3F);
                } else if (badge.type == StatusBadgeType::ClickThrough) {
                    // Mouse / Pass-through icon
                    brush->SetColor(D2DColor(0xFFFFFF, 0.95F));
                    // Mouse body
                    render_target_->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(cx - 3.5F, cy - 5.5F, cx + 3.5F, cy + 3.0F), 2.5F, 2.5F), brush, 1.3F);
                    // Middle button / wheel
                    render_target_->DrawLine(D2D1::Point2F(cx, cy - 4.5F), D2D1::Point2F(cx, cy - 2.0F), brush, 1.2F);
                    // Pass-through ripple line
                    brush->SetColor(D2DColor(0xFFFFFF, 0.70F));
                    render_target_->DrawLine(D2D1::Point2F(cx - 4.0F, cy + 5.5F), D2D1::Point2F(cx + 4.0F, cy + 5.5F), brush, 1.2F);
                }
            }
        }
        brush->Release();
    }

    const HRESULT text_draw_result = (rich_edit && !is_collapsed) ? rich_edit->Draw(render_target_) : S_OK;
    const HRESULT draw_result = render_target_->EndDraw();
    if (draw_result == D2DERR_RECREATE_TARGET) {
        render_target_->Release();
        render_target_ = nullptr;
        return false;
    }
    if (FAILED(draw_result) || FAILED(text_draw_result)) return false;

    RECT window_rect{};
    GetWindowRect(window, &window_rect);
    POINT destination{window_rect.left, window_rect.top};
    POINT source{0, 0};
    SIZE size{width, height};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};

    const BOOL updated = UpdateLayeredWindow(
        window, nullptr, &destination, &size, memory_dc_, &source, 0, &blend, ULW_ALPHA);
    ValidateRect(window, nullptr);
    return updated != FALSE;
}

}  // namespace desktopnote
