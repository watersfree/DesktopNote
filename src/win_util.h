#pragma once

#include <windows.h>
#include <d2d1.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace desktopnote {

inline D2D1_COLOR_F D2DColor(std::uint32_t rgb, float alpha = 1.0F) {
    const float red = static_cast<float>((rgb >> 16) & 0xFF) / 255.0F;
    const float green = static_cast<float>((rgb >> 8) & 0xFF) / 255.0F;
    const float blue = static_cast<float>(rgb & 0xFF) / 255.0F;
    return D2D1::ColorF(red, green, blue, alpha);
}

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::filesystem::path RoamingAppDataPath();
std::filesystem::path DesktopNoteDataRoot();
std::wstring DesktopNoteInstanceIdentifier();
std::wstring GetLastErrorMessage(DWORD error = GetLastError());
UINT DpiForWindowOrSystem(HWND window);
int DipToPixel(double dip, UINT dpi);
double PixelToDip(int pixel, UINT dpi);
std::uint32_t ColorRefToRgb(COLORREF color);
COLORREF RgbToColorRef(std::uint32_t rgb);
void LogDebug(const std::string& message);

template <typename T>
class ScopedGdiObject {
public:
    ScopedGdiObject() noexcept : handle_(nullptr) {}
    explicit ScopedGdiObject(T handle) noexcept : handle_(handle) {}
    ~ScopedGdiObject() noexcept { reset(); }

    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;

    ScopedGdiObject(ScopedGdiObject&& other) noexcept : handle_(other.release()) {}
    ScopedGdiObject& operator=(ScopedGdiObject&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    T get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    operator T() const noexcept { return handle_; }

    T release() noexcept {
        T temp = handle_;
        handle_ = nullptr;
        return temp;
    }

    void reset(T handle = nullptr) noexcept {
        if (handle_) {
            DeleteObject(handle_);
        }
        handle_ = handle;
    }

private:
    T handle_ = nullptr;
};

using ScopedBrush = ScopedGdiObject<HBRUSH>;
using ScopedFont = ScopedGdiObject<HFONT>;
using ScopedBitmap = ScopedGdiObject<HBITMAP>;

class ScopedDC {
public:
    ScopedDC() noexcept : dc_(nullptr), window_(nullptr), is_created_(false) {}

    static ScopedDC FromWindow(HWND window) noexcept {
        ScopedDC s;
        s.dc_ = GetDC(window);
        s.window_ = window;
        s.is_created_ = false;
        return s;
    }

    static ScopedDC Compatible(HDC ref_dc) noexcept {
        ScopedDC s;
        s.dc_ = CreateCompatibleDC(ref_dc);
        s.window_ = nullptr;
        s.is_created_ = true;
        return s;
    }

    ~ScopedDC() noexcept { reset(); }

    ScopedDC(const ScopedDC&) = delete;
    ScopedDC& operator=(const ScopedDC&) = delete;

    ScopedDC(ScopedDC&& other) noexcept
        : dc_(other.dc_), window_(other.window_), is_created_(other.is_created_) {
        other.dc_ = nullptr;
        other.window_ = nullptr;
        other.is_created_ = false;
    }

    ScopedDC& operator=(ScopedDC&& other) noexcept {
        if (this != &other) {
            reset();
            dc_ = other.dc_;
            window_ = other.window_;
            is_created_ = other.is_created_;
            other.dc_ = nullptr;
            other.window_ = nullptr;
            other.is_created_ = false;
        }
        return *this;
    }

    HDC get() const noexcept { return dc_; }
    operator HDC() const noexcept { return dc_; }
    explicit operator bool() const noexcept { return dc_ != nullptr; }

    void reset() noexcept {
        if (dc_) {
            if (is_created_) {
                DeleteDC(dc_);
            } else if (window_) {
                ReleaseDC(window_, dc_);
            }
            dc_ = nullptr;
            window_ = nullptr;
            is_created_ = false;
        }
    }

private:
    HDC dc_ = nullptr;
    HWND window_ = nullptr;
    bool is_created_ = false;
};

class ScopedSelectObject {
public:
    ScopedSelectObject(HDC dc, HGDIOBJ new_obj) noexcept
        : dc_(dc), old_obj_(dc && new_obj ? SelectObject(dc, new_obj) : nullptr) {}
    ~ScopedSelectObject() noexcept {
        if (dc_ && old_obj_) {
            SelectObject(dc_, old_obj_);
        }
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

private:
    HDC dc_ = nullptr;
    HGDIOBJ old_obj_ = nullptr;
};

}  // namespace desktopnote
