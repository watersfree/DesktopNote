#include "app_state.h"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <unordered_set>

namespace desktopnote {

std::string NewId() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        const auto ticks = GetTickCount64();
        return "note-" + std::to_string(ticks);
    }
    std::array<char, 33> text{};
    std::snprintf(text.data(), text.size(),
                  "%08lx%04x%04x%02x%02x%02x%02x%02x%02x%02x%02x",
                  guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
                  guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
                  guid.Data4[6], guid.Data4[7]);
    return text.data();
}

std::string UtcNowIso8601() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
                  time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
                  time.wSecond, time.wMilliseconds);
    return text.data();
}

std::string WindowModeToString(WindowMode mode) {
    switch (mode) {
        case WindowMode::TopMost: return "TopMost";
        case WindowMode::Desktop: return "Desktop";
        default: return "Normal";
    }
}

WindowMode WindowModeFromString(const std::string& value) {
    if (value == "TopMost") return WindowMode::TopMost;
    if (value == "Desktop") return WindowMode::Desktop;
    return WindowMode::Normal;
}

void Normalize(AppState& state) {
    state.schema_version = 2;
    std::unordered_set<std::string> ids;
    for (auto& note : state.notes) {
        if (note.id.empty() || ids.contains(note.id)) note.id = NewId();
        ids.insert(note.id);
        if (note.title.empty()) note.title = "我的便签";
        if (note.created_at_utc.empty()) note.created_at_utc = UtcNowIso8601();
        if (note.modified_at_utc.empty()) note.modified_at_utc = note.created_at_utc;
        note.window.width_dip = std::clamp(note.window.width_dip, 300.0, 2400.0);
        note.window.height_dip = std::clamp(note.window.height_dip, 150.0, 1800.0);
        note.appearance.background_alpha = std::clamp(note.appearance.background_alpha, 0.0, 1.0);
        note.appearance.font_size_dip = std::clamp(note.appearance.font_size_dip, 8.0, 96.0);
        note.appearance.padding_dip = std::clamp(note.appearance.padding_dip, 0.0, 64.0);
        note.appearance.paragraph_spacing_dip = std::clamp(note.appearance.paragraph_spacing_dip, 0.0, 48.0);
        note.appearance.background_color &= 0xFFFFFF;
        note.appearance.border_color &= 0xFFFFFF;
        note.appearance.font_color &= 0xFFFFFF;
        if (note.appearance.font_family.empty()) note.appearance.font_family = L"Microsoft YaHei";
    }
    if (state.notes.empty()) {
        state = CreateDefaultState();
        return;
    }
    if (!ids.contains(state.last_active_note_id)) state.last_active_note_id = state.notes.front().id;
}

AppState CreateDefaultState() {
    AppState state;
    Note note;
    note.id = NewId();
    note.created_at_utc = UtcNowIso8601();
    note.modified_at_utc = note.created_at_utc;
    state.last_active_note_id = note.id;
    state.notes.push_back(std::move(note));
    return state;
}

}  // namespace desktopnote
