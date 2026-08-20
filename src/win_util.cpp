#include "win_util.h"

#include <shlobj.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace desktopnote {

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                          static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}

std::filesystem::path RoamingAppDataPath() {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &raw);
    if (FAILED(hr) || raw == nullptr) throw std::runtime_error("cannot locate roaming AppData");
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

std::filesystem::path DesktopNoteDataRoot() {
    const DWORD required = GetEnvironmentVariableW(L"DESKTOPNOTE_DATA_DIR", nullptr, 0);
    if (required > 1) {
        std::wstring value(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(
            L"DESKTOPNOTE_DATA_DIR", value.data(), required);
        if (written > 0 && written < required) {
            value.resize(written);
            return value;
        }
    }
    return RoamingAppDataPath() / L"DesktopNote";
}

std::wstring DesktopNoteInstanceIdentifier() {
    constexpr wchar_t base_name[] = L"Local\\DesktopNote.Native.v2.SingleInstance";
    std::error_code error;
    auto data_directory = std::filesystem::weakly_canonical(
        DesktopNoteDataRoot(), error);
    if (error) {
        error.clear();
        data_directory = std::filesystem::absolute(DesktopNoteDataRoot(), error);
    }
    if (error) data_directory = DesktopNoteDataRoot().lexically_normal();

    std::wstring identity = data_directory.lexically_normal().wstring();
    if (!identity.empty()) {
        CharLowerBuffW(identity.data(), static_cast<DWORD>(identity.size()));
    }

    std::uint64_t hash = 1469598103934665603ULL;
    for (const wchar_t character : identity) {
        hash ^= static_cast<std::uint16_t>(character);
        hash *= 1099511628211ULL;
    }
    std::array<wchar_t, 18> suffix{};
    swprintf_s(suffix.data(), suffix.size(), L".%016llx",
               static_cast<unsigned long long>(hash));
    return std::wstring(base_name) + suffix.data();
}

std::wstring GetLastErrorMessage(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"unknown Windows error";
    if (buffer) LocalFree(buffer);
    return result;
}

UINT DpiForWindowOrSystem(HWND window) {
    if (window) return GetDpiForWindow(window);
    return GetDpiForSystem();
}

int DipToPixel(double dip, UINT dpi) {
    return static_cast<int>(dip * static_cast<double>(dpi) / 96.0 + (dip >= 0 ? 0.5 : -0.5));
}

double PixelToDip(int pixel, UINT dpi) {
    return static_cast<double>(pixel) * 96.0 / static_cast<double>(dpi ? dpi : 96);
}

std::uint32_t ColorRefToRgb(COLORREF color) {
    return (static_cast<std::uint32_t>(GetRValue(color)) << 16U) |
           (static_cast<std::uint32_t>(GetGValue(color)) << 8U) |
           static_cast<std::uint32_t>(GetBValue(color));
}

COLORREF RgbToColorRef(std::uint32_t rgb) {
    return RGB((rgb >> 16U) & 0xFFU, (rgb >> 8U) & 0xFFU, rgb & 0xFFU);
}

void LogDebug(const std::string& message) {
    try {
        const auto log_path = DesktopNoteDataRoot() / L"debug.log";
        std::filesystem::create_directories(log_path.parent_path());
        std::ofstream file(log_path, std::ios::app);
        if (file) {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            char time_buf[64]{};
            sprintf_s(time_buf, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                      st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
            file << time_buf << message << "\n";
            file.flush();
        }
    } catch (...) {}
}

}  // namespace desktopnote
