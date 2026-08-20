#include "data_store.h"

#include "base64.h"
#include "win_util.h"
#include <nlohmann/json.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

namespace desktopnote {
namespace {

using json = nlohmann::json;

template <typename T>
T ValueOr(const json& object, const char* name, T fallback) {
    const auto iterator = object.find(name);
    if (iterator == object.end() || iterator->is_null()) return fallback;
    try {
        return iterator->get<T>();
    } catch (...) {
        return fallback;
    }
}

std::uint32_t ParseColor(const json& object, const char* name, std::uint32_t fallback) {
    const auto text = ValueOr<std::string>(object, name, {});
    if (text.size() != 7 || text.front() != '#') return fallback;
    try {
        return static_cast<std::uint32_t>(std::stoul(text.substr(1), nullptr, 16)) & 0xFFFFFFU;
    } catch (...) {
        return fallback;
    }
}

std::string ColorText(std::uint32_t color) {
    std::array<char, 8> text{};
    std::snprintf(text.data(), text.size(), "#%06X", color & 0xFFFFFFU);
    return text.data();
}

std::string FormatUtc(const SYSTEMTIME& time) {
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                  time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                  time.wSecond, time.wMilliseconds);
    return text.data();
}

std::string LegacyTimestampToUtc(const std::string& value) {
    if (value.empty()) return UtcNowIso8601();
    if (value.size() < 19 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':') {
        return value;
    }
    const auto number = [&value](std::size_t offset, std::size_t count) -> int {
        int result = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const unsigned char character = static_cast<unsigned char>(value[offset + index]);
            if (!std::isdigit(character)) throw std::invalid_argument("invalid timestamp");
            result = result * 10 + (character - '0');
        }
        return result;
    };

    SYSTEMTIME local{};
    std::size_t position = 19;
    try {
        local.wYear = static_cast<WORD>(number(0, 4));
        local.wMonth = static_cast<WORD>(number(5, 2));
        local.wDay = static_cast<WORD>(number(8, 2));
        local.wHour = static_cast<WORD>(number(11, 2));
        local.wMinute = static_cast<WORD>(number(14, 2));
        local.wSecond = static_cast<WORD>(number(17, 2));
        if (position < value.size() && value[position] == '.') {
            ++position;
            unsigned int milliseconds = 0;
            unsigned int digits = 0;
            while (position < value.size() && std::isdigit(static_cast<unsigned char>(value[position]))) {
                if (digits < 3) milliseconds = milliseconds * 10 + (value[position] - '0');
                ++digits;
                ++position;
            }
            while (digits < 3) {
                milliseconds *= 10;
                ++digits;
            }
            local.wMilliseconds = static_cast<WORD>(milliseconds);
        }
    } catch (...) {
        return value;
    }

    if (position < value.size() && (value[position] == 'Z' || value[position] == 'z')) {
        FILETIME validation{};
        return SystemTimeToFileTime(&local, &validation) ? FormatUtc(local) : value;
    }
    if (position + 6 == value.size() && (value[position] == '+' || value[position] == '-') &&
        value[position + 3] == ':') {
        try {
            const int offset_minutes = number(position + 1, 2) * 60 + number(position + 4, 2);
            FILETIME local_file_time{};
            if (!SystemTimeToFileTime(&local, &local_file_time)) return value;
            ULARGE_INTEGER ticks{};
            ticks.LowPart = local_file_time.dwLowDateTime;
            ticks.HighPart = local_file_time.dwHighDateTime;
            const LONGLONG delta = static_cast<LONGLONG>(offset_minutes) * 60LL * 10'000'000LL;
            if (value[position] == '+') {
                if (ticks.QuadPart < static_cast<ULONGLONG>(delta)) return value;
                ticks.QuadPart -= static_cast<ULONGLONG>(delta);
            } else {
                ticks.QuadPart += static_cast<ULONGLONG>(delta);
            }
            FILETIME utc_file_time{ticks.LowPart, ticks.HighPart};
            SYSTEMTIME utc{};
            return FileTimeToSystemTime(&utc_file_time, &utc) ? FormatUtc(utc) : value;
        } catch (...) {
            return value;
        }
    }

    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) return value;
    return FormatUtc(utc);
}

json ToJson(const WindowState& value) {
    return {
        {"xDip", value.x_dip},
        {"yDip", value.y_dip},
        {"widthDip", value.width_dip},
        {"heightDip", value.height_dip},
        {"monitorDevice", WideToUtf8(value.monitor_device)},
        {"mode", WindowModeToString(value.mode)},
        {"locked", value.locked},
        {"clickThrough", value.click_through},
        {"autoHide", value.auto_hide},
    };
}

json ToJson(const Appearance& value) {
    return {
        {"backgroundAlpha", value.background_alpha},
        {"backgroundColor", ColorText(value.background_color)},
        {"borderColor", ColorText(value.border_color)},
        {"fontFamily", WideToUtf8(value.font_family)},
        {"fontSizeDip", value.font_size_dip},
        {"fontColor", ColorText(value.font_color)},
        {"paddingDip", value.padding_dip},
        {"paragraphSpacingDip", value.paragraph_spacing_dip},
        {"toolbarPinned", value.toolbar_pinned},
    };
}

WindowState ParseWindow(const json& value) {
    WindowState result;
    if (!value.is_object()) return result;
    result.x_dip = ValueOr<double>(value, "xDip", result.x_dip);
    result.y_dip = ValueOr<double>(value, "yDip", result.y_dip);
    result.width_dip = ValueOr<double>(value, "widthDip", result.width_dip);
    result.height_dip = ValueOr<double>(value, "heightDip", result.height_dip);
    result.monitor_device = Utf8ToWide(ValueOr<std::string>(value, "monitorDevice", {}));
    result.mode = WindowModeFromString(ValueOr<std::string>(value, "mode", "Normal"));
    result.locked = ValueOr<bool>(value, "locked", false);
    result.click_through = ValueOr<bool>(value, "clickThrough", false);
    result.auto_hide = ValueOr<bool>(value, "autoHide", false);
    return result;
}

Appearance ParseAppearance(const json& value) {
    Appearance result;
    if (!value.is_object()) return result;
    result.background_alpha = ValueOr<double>(value, "backgroundAlpha", result.background_alpha);
    result.background_color = ParseColor(value, "backgroundColor", result.background_color);
    result.border_color = ParseColor(value, "borderColor", result.border_color);
    result.font_family = Utf8ToWide(ValueOr<std::string>(value, "fontFamily", WideToUtf8(result.font_family)));
    result.font_size_dip = ValueOr<double>(value, "fontSizeDip", result.font_size_dip);
    result.font_color = ParseColor(value, "fontColor", result.font_color);
    result.padding_dip = ValueOr<double>(value, "paddingDip", result.padding_dip);
    result.paragraph_spacing_dip = ValueOr<double>(value, "paragraphSpacingDip", result.paragraph_spacing_dip);
    result.toolbar_pinned = ValueOr<bool>(value, "toolbarPinned", false);
    return result;
}

std::wstring BackupTimestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::array<wchar_t, 32> text{};
    swprintf_s(text.data(), text.size(), L"%04u%02u%02u_%02u%02u%02u_%03u",
               time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
               time.wSecond, time.wMilliseconds);
    return text.data();
}

std::string ReadAll(HANDLE file) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 64LL * 1024LL * 1024LL) {
        throw std::runtime_error("invalid data file size");
    }
    std::string data(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    if (!data.empty() && (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr) ||
                          read != data.size())) {
        throw std::runtime_error("failed to read data file");
    }
    return data;
}

void WriteAllAndFlush(const std::filesystem::path& path, const std::string& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create temporary data file");
    DWORD written = 0;
    const bool ok = (data.empty() ||
                     (WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                      written == data.size())) &&
                    FlushFileBuffers(file);
    const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path.c_str());
        throw std::runtime_error("cannot flush temporary data file: " + std::to_string(error));
    }
}

void TryRepairPrimaryFile(const std::filesystem::path& data_path, const AppState& state) noexcept {
    const auto temporary = data_path.wstring() + L".tmp";
    try {
        WriteAllAndFlush(temporary, SerializeAppState(state));
        if (!ReplaceFileW(data_path.c_str(), temporary.c_str(), nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            DeleteFileW(temporary.c_str());
        }
    } catch (...) {
        DeleteFileW(temporary.c_str());
    }
}

}  // namespace

std::string SerializeAppState(const AppState& state) {
    json root;
    root["schemaVersion"] = 2;
    root["lastActiveNoteId"] = state.last_active_note_id;
    root["notes"] = json::array();
    for (const auto& note : state.notes) {
        root["notes"].push_back({
            {"id", note.id},
            {"title", note.title},
            {"contentRtfBase64", note.content_rtf_base64},
            {"createdAtUtc", note.created_at_utc},
            {"modifiedAtUtc", note.modified_at_utc},
            {"window", ToJson(note.window)},
            {"appearance", ToJson(note.appearance)},
        });
    }
    return root.dump(2);
}

AppState DeserializeAppState(const std::string& json_text) {
    const json root = json::parse(json_text);
    if (!root.is_object() || ValueOr<int>(root, "schemaVersion", 0) != 2) {
        throw std::runtime_error("unsupported DesktopNote data schema");
    }
    AppState state;
    state.last_active_note_id = ValueOr<std::string>(root, "lastActiveNoteId", {});
    const auto notes = root.find("notes");
    if (notes != root.end() && notes->is_array()) {
        for (const auto& value : *notes) {
            if (!value.is_object()) continue;
            try {
                Note note;
                note.id = ValueOr<std::string>(value, "id", {});
                note.title = ValueOr<std::string>(value, "title", "我的便签");
                note.content_rtf_base64 = ValueOr<std::string>(value, "contentRtfBase64", {});
                note.created_at_utc = ValueOr<std::string>(value, "createdAtUtc", {});
                note.modified_at_utc = ValueOr<std::string>(value, "modifiedAtUtc", {});
                if (const auto item = value.find("window"); item != value.end()) note.window = ParseWindow(*item);
                if (const auto item = value.find("appearance"); item != value.end()) note.appearance = ParseAppearance(*item);
                state.notes.push_back(std::move(note));
            } catch (...) {
                // Keep healthy notes if one note payload is corrupt
            }
        }
    }
    Normalize(state);
    return state;
}

AppState ImportLegacyAppState(const std::string& encoded_legacy_data) {
    const auto decoded = DecodeBase64(encoded_legacy_data);
    const json root = json::parse(std::string(decoded.begin(), decoded.end()));
    if (!root.is_object()) throw std::runtime_error("invalid legacy DesktopNote data");

    AppState state;
    const json settings = root.contains("settings") && root["settings"].is_object()
                              ? root["settings"] : json::object();
    WindowState base_window;
    base_window.x_dip = 100.0;
    base_window.y_dip = 100.0;
    base_window.width_dip = 300.0;
    base_window.height_dip = 350.0;
    base_window.x_dip = ValueOr<double>(settings, "windowX", base_window.x_dip);
    base_window.y_dip = ValueOr<double>(settings, "windowY", base_window.y_dip);
    base_window.width_dip = ValueOr<double>(settings, "width", base_window.width_dip);
    base_window.height_dip = ValueOr<double>(settings, "height", base_window.height_dip);
    base_window.mode = WindowModeFromString(ValueOr<std::string>(settings, "windowMode", "Normal"));
    base_window.locked = ValueOr<bool>(settings, "isLocked", false);
    base_window.click_through = ValueOr<bool>(settings, "isClickThrough", false);

    Appearance base_appearance;
    base_appearance.background_alpha = 0.0;
    base_appearance.background_color = 0xFFFFFFU;
    base_appearance.background_alpha = ValueOr<double>(settings, "backgroundOpacity", base_appearance.background_alpha);
    const auto background_mode = ValueOr<std::string>(settings, "backgroundMode", "white");
    base_appearance.background_color = background_mode == "white" ? 0xFFFFFFU
                                         : background_mode == "gray" ? 0x808080U : 0x202020U;
    base_appearance.font_family = Utf8ToWide(ValueOr<std::string>(settings, "fontFamily", "Microsoft YaHei"));
    base_appearance.font_size_dip = ValueOr<double>(settings, "fontSize", base_appearance.font_size_dip);
    base_appearance.font_color = ParseColor(settings, "fontColor", base_appearance.font_color);
    base_appearance.padding_dip = ValueOr<double>(settings, "paddingSize", base_appearance.padding_dip);
    base_appearance.paragraph_spacing_dip = ValueOr<double>(settings, "lineSpacing", base_appearance.paragraph_spacing_dip);
    base_appearance.toolbar_pinned = ValueOr<bool>(settings, "isToolbarPinned", false);

    const auto notes = root.find("notes");
    std::size_t index = 0;
    if (notes != root.end() && notes->is_array()) {
        for (const auto& value : *notes) {
            if (!value.is_object()) continue;
            Note note;
            note.id = ValueOr<std::string>(value, "id", {});
            note.title = ValueOr<std::string>(value, "title", "我的便签");
            note.content_rtf_base64 = ValueOr<std::string>(value, "contentRtf", {});
            note.created_at_utc = LegacyTimestampToUtc(ValueOr<std::string>(value, "createdAt", {}));
            note.modified_at_utc = LegacyTimestampToUtc(ValueOr<std::string>(value, "modifiedAt", {}));
            note.window = base_window;
            note.window.x_dip += static_cast<double>(index * 30);
            note.window.y_dip += static_cast<double>(index * 30);
            note.appearance = base_appearance;
            state.notes.push_back(std::move(note));
            ++index;
        }
    }
    state.last_active_note_id = ValueOr<std::string>(settings, "activeNoteId", {});
    Normalize(state);
    return state;
}

DataStore::DataStore() : DataStore(DesktopNoteDataRoot()) {}

DataStore::DataStore(std::filesystem::path root_directory)
    : root_directory_(std::move(root_directory)),
      data_path_(root_directory_ / L"data.json"),
      legacy_path_(root_directory_ / L"data.dat"),
      backup_directory_(root_directory_ / L"backups") {}

std::string DataStore::ReadUtf8File(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open data file");
    try {
        auto result = ReadAll(file);
        CloseHandle(file);
        return result;
    } catch (...) {
        CloseHandle(file);
        throw;
    }
}

void DataStore::BackupFile(const std::filesystem::path& source, const std::wstring& prefix) {
    if (!std::filesystem::exists(source)) return;
    std::filesystem::create_directories(backup_directory_);
    auto target = backup_directory_ / (prefix + L"_" + BackupTimestamp() + source.extension().wstring());
    for (unsigned int suffix = 1; std::filesystem::exists(target); ++suffix) {
        target = backup_directory_ /
                 (prefix + L"_" + BackupTimestamp() + L"_" + std::to_wstring(suffix) +
                  source.extension().wstring());
    }
    if (!CopyFileW(source.c_str(), target.c_str(), TRUE)) {
        throw std::runtime_error("cannot create DesktopNote backup");
    }
    RotateBackups(prefix, 10);
}

void DataStore::RotateBackups(const std::wstring& prefix, std::size_t keep_count) {
    std::vector<std::filesystem::directory_entry> matches;
    if (!std::filesystem::exists(backup_directory_)) return;
    for (const auto& entry : std::filesystem::directory_iterator(backup_directory_)) {
        if (entry.is_regular_file() && entry.path().filename().wstring().starts_with(prefix + L"_")) {
            matches.push_back(entry);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        return left.path().filename() > right.path().filename();
    });
    for (std::size_t index = keep_count; index < matches.size(); ++index) {
        std::error_code ignored;
        std::filesystem::remove(matches[index].path(), ignored);
    }
}

void DataStore::CreateBackup(const std::wstring& prefix) {
    if (std::filesystem::exists(data_path_)) {
        BackupFile(data_path_, prefix);
    }
}

AppState DataStore::Load() {
    std::filesystem::create_directories(root_directory_);
    std::filesystem::create_directories(backup_directory_);

    const bool version_two_exists = std::filesystem::exists(data_path_);
    if (version_two_exists) {
        try {
            return DeserializeAppState(ReadUtf8File(data_path_));
        } catch (...) {
            try {
                BackupFile(data_path_, L"data_corrupted");
            } catch (...) {
            }
            const auto backup = data_path_.wstring() + L".bak";
            if (std::filesystem::exists(backup)) {
                try {
                    auto recovered = DeserializeAppState(ReadUtf8File(backup));
                    TryRepairPrimaryFile(data_path_, recovered);
                    return recovered;
                } catch (...) {
                }
            }
        }

        AppState state = CreateDefaultState();
        Save(state);
        return state;
    }

    if (!version_two_exists && std::filesystem::exists(legacy_path_)) {
        BackupFile(legacy_path_, L"legacy_data");
        AppState migrated;
        try {
            migrated = ImportLegacyAppState(ReadUtf8File(legacy_path_));
        } catch (...) {
            AppState state = CreateDefaultState();
            Save(state);
            return state;
        }
        Save(migrated);
        return migrated;
    }

    AppState state = CreateDefaultState();
    Save(state);
    return state;
}

void DataStore::Save(const AppState& state, bool create_backup) {
    std::filesystem::create_directories(root_directory_);
    if (create_backup && std::filesystem::exists(data_path_)) {
        BackupFile(data_path_, L"data");
    }
    const auto temporary = data_path_.wstring() + L".tmp";
    WriteAllAndFlush(temporary, SerializeAppState(state));

    if (std::filesystem::exists(data_path_)) {
        const auto backup = data_path_.wstring() + L".bak";
        if (!ReplaceFileW(data_path_.c_str(), temporary.c_str(), backup.c_str(),
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            const DWORD error = GetLastError();
            DeleteFileW(temporary.c_str());
            throw std::runtime_error("ReplaceFileW failed: " + std::to_string(error));
        }
    } else if (!MoveFileExW(temporary.c_str(), data_path_.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD error = GetLastError();
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("MoveFileExW failed: " + std::to_string(error));
    }
}

}  // namespace desktopnote
