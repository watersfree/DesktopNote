#include "app_state.h"
#include "base64.h"
#include "data_store.h"
#include "win_util.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace desktopnote;

void Check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                                 " check failed: " + expression);
    }
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

std::filesystem::path NewTestDirectory() {
    wchar_t temporary[MAX_PATH]{};
    CHECK(GetTempPathW(MAX_PATH, temporary) > 0);
    const auto path = std::filesystem::path(temporary) / L"DesktopNoteTests" / Utf8ToWide(NewId());
    std::filesystem::create_directories(path);
    return path;
}

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const wchar_t* name, const std::wstring& value) : name_(name) {
        const DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
        if (required > 0) {
            previous_.resize(required);
            const DWORD written = GetEnvironmentVariableW(
                name_.c_str(), previous_.data(), required);
            previous_.resize(written);
            existed_ = true;
        }
        CHECK(SetEnvironmentVariableW(name_.c_str(), value.c_str()));
    }

    ~ScopedEnvironmentVariable() {
        SetEnvironmentVariableW(
            name_.c_str(), existed_ ? previous_.c_str() : nullptr);
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
    ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

private:
    std::wstring name_;
    std::wstring previous_;
    bool existed_ = false;
};

void WriteFile(const std::filesystem::path& path, const std::string& data) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    CHECK(stream.good());
}

void TestBase64() {
    const std::string source = "DesktopNote 中文 \xF0\x9F\x93\x9D";
    const std::vector<std::uint8_t> bytes(source.begin(), source.end());
    const auto encoded = EncodeBase64(bytes);
    const auto decoded = DecodeBase64(encoded);
    CHECK(decoded == bytes);
    bool rejected = false;
    try {
        static_cast<void>(DecodeBase64("not valid ***"));
    } catch (...) {
        rejected = true;
    }
    CHECK(rejected);
}

void TestVersionTwoRoundTrip() {
    auto state = CreateDefaultState();
    auto& note = state.notes.front();
    note.title = "测试便签";
    const std::string mixed_rtf = R"({\rtf1\ansi\ansicpg65001 {\b DesktopNote} \u20013?\u25991? \u-10179?\u-8691?})";
    note.content_rtf_base64 = EncodeBase64(
        std::vector<std::uint8_t>(mixed_rtf.begin(), mixed_rtf.end()));
    note.window.x_dip = -120.5;
    note.window.mode = WindowMode::TopMost;
    note.window.auto_hide = true;
    note.appearance.background_alpha = 0.35;
    note.appearance.border_color = 0x2196F3;
    note.appearance.font_family = L"Microsoft YaHei UI";

    const auto restored = DeserializeAppState(SerializeAppState(state));
    CHECK(restored.schema_version == 2);
    CHECK(restored.notes.size() == 1);
    CHECK(restored.notes.front().title == note.title);
    CHECK(restored.notes.front().content_rtf_base64 == note.content_rtf_base64);
    const auto restored_rtf = DecodeBase64(restored.notes.front().content_rtf_base64);
    CHECK(std::string(restored_rtf.begin(), restored_rtf.end()) == mixed_rtf);
    CHECK(restored.notes.front().window.mode == WindowMode::TopMost);
    CHECK(restored.notes.front().window.auto_hide == true);
    CHECK(restored.notes.front().appearance.border_color == 0x2196F3);
    CHECK(restored.notes.front().appearance.font_family == L"Microsoft YaHei UI");
}

void TestLegacyMigration() {
    const std::string legacy_json = R"({
      "settings": {
        "windowX": 50, "windowY": 60, "width": 500, "height": 260,
        "backgroundOpacity": 0.42, "backgroundMode": "white",
        "fontFamily": "Microsoft YaHei", "fontSize": 18, "fontColor": "#112233",
        "paddingSize": 9, "lineSpacing": 5, "isToolbarPinned": true,
        "windowMode": "TopMost", "isLocked": true, "isClickThrough": false,
        "activeNoteId": "same"
      },
      "notes": [
        {"id":"same","title":"一","contentRtf":"e1xccnRmMX0=","createdAt":"2025-12-08T10:20:30.1234567+08:00","modifiedAt":"2025-12-08T10:20:31.000"},
        {"id":"same","title":"二","contentRtf":"","createdAt":"2025-12-08T10:20:30.000","modifiedAt":"2025-12-08T10:20:31.000"}
      ]
    })";
    const std::vector<std::uint8_t> bytes(legacy_json.begin(), legacy_json.end());
    auto migrated = ImportLegacyAppState(EncodeBase64(bytes));
    CHECK(migrated.notes.size() == 2);
    CHECK(migrated.notes[0].id != migrated.notes[1].id);
    CHECK(migrated.notes[0].window.x_dip == 50);
    CHECK(migrated.notes[1].window.x_dip == 80);
    CHECK(migrated.notes[0].window.mode == WindowMode::TopMost);
    CHECK(migrated.notes[0].window.locked);
    CHECK(migrated.notes[0].appearance.background_color == 0xFFFFFF);
    CHECK(migrated.notes[0].appearance.font_color == 0x112233);
    CHECK(migrated.notes[0].appearance.toolbar_pinned);
    CHECK(migrated.notes[0].created_at_utc == "2025-12-08T02:20:30.123Z");
}

void TestLegacyDefaultsMatchWpf() {
    const std::string legacy_json = R"({
      "settings": {"activeNoteId": "old"},
      "notes": [{"id":"old","title":"旧数据","contentRtf":"","createdAt":"","modifiedAt":""}]
    })";
    const std::vector<std::uint8_t> bytes(legacy_json.begin(), legacy_json.end());
    const auto migrated = ImportLegacyAppState(EncodeBase64(bytes));
    CHECK(migrated.notes.size() == 1);
    CHECK(migrated.notes[0].window.x_dip == 100.0);
    CHECK(migrated.notes[0].window.y_dip == 100.0);
    CHECK(migrated.notes[0].window.width_dip == 300.0);
    CHECK(migrated.notes[0].window.height_dip == 350.0);
    CHECK(migrated.notes[0].appearance.background_alpha == 0.0);
    CHECK(migrated.notes[0].appearance.background_color == 0xFFFFFF);
}

void TestNormalization() {
    AppState state;
    Note first;
    first.id = "duplicate";
    first.appearance.background_alpha = 9.0;
    first.window.width_dip = 10.0;
    first.window.height_dip = 99999.0;
    Note second = first;
    state.notes = {first, second};
    Normalize(state);
    CHECK(state.notes[0].id != state.notes[1].id);
    CHECK(state.notes[0].appearance.background_alpha == 1.0);
    CHECK(state.notes[0].window.width_dip == 300.0);
    CHECK(state.notes[0].window.height_dip == 1800.0);
    CHECK(!state.last_active_note_id.empty());
}

void TestStoreMigrationAndRecovery() {
    const auto directory = NewTestDirectory();
    try {
        const std::string legacy_json = R"({"settings":{"activeNoteId":"old"},"notes":[{"id":"old","title":"旧数据","contentRtf":"","createdAt":"","modifiedAt":""}]})";
        const std::vector<std::uint8_t> legacy_bytes(legacy_json.begin(), legacy_json.end());
        WriteFile(directory / L"data.dat", EncodeBase64(legacy_bytes));

        DataStore store(directory);
        auto state = store.Load();
        CHECK(state.notes.front().title == "旧数据");
        CHECK(std::filesystem::exists(directory / L"data.json"));
        CHECK(std::filesystem::exists(directory / L"data.dat"));

        bool migration_backup_found = false;
        for (const auto& entry : std::filesystem::directory_iterator(directory / L"backups")) {
            migration_backup_found |= entry.path().filename().wstring().starts_with(L"legacy_data_");
        }
        CHECK(migration_backup_found);

        state.notes.front().title = "第一次保存";
        store.Save(state, true);
        state.notes.front().title = "第二次保存";
        store.Save(state, true);
        CHECK(!std::filesystem::exists(directory / L"data.json.tmp"));
        CHECK(std::filesystem::exists(directory / L"data.json.bak"));
        WriteFile(directory / L"data.json", "{broken");

        const auto recovered = store.Load();
        CHECK(recovered.notes.front().title == "第一次保存");
        WriteFile(directory / L"data.json.bak", "{also broken");
        CHECK(store.Load().notes.front().title == "第一次保存");
        bool corrupt_backup_found = false;
        for (const auto& entry : std::filesystem::directory_iterator(directory / L"backups")) {
            corrupt_backup_found |= entry.path().filename().wstring().starts_with(L"data_corrupted_");
        }
        CHECK(corrupt_backup_found);
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}

void TestCorruptVersionTwoDoesNotResurrectLegacy() {
    const auto directory = NewTestDirectory();
    try {
        DataStore store(directory);
        auto state = CreateDefaultState();
        state.notes.front().title = "当前数据";
        store.Save(state);
        state.notes.front().title = "更新数据";
        store.Save(state);

        WriteFile(directory / L"data.json", "{broken");
        WriteFile(directory / L"data.json.bak", "{also broken");
        const std::string legacy_json = R"({"settings":{"activeNoteId":"old"},"notes":[{"id":"old","title":"绝不能恢复","contentRtf":"","createdAt":"","modifiedAt":""}]})";
        const std::vector<std::uint8_t> legacy_bytes(legacy_json.begin(), legacy_json.end());
        WriteFile(directory / L"data.dat", EncodeBase64(legacy_bytes));

        const auto recovered = store.Load();
        CHECK(recovered.notes.size() == 1);
        CHECK(recovered.notes.front().title != "绝不能恢复");
        CHECK(std::filesystem::exists(directory / L"data.dat"));
        CHECK(store.Load().notes.front().title == recovered.notes.front().title);
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}

void TestCorruptLegacyRecovery() {
    const auto directory = NewTestDirectory();
    try {
        const std::string corrupt_legacy = "not valid base64 ***";
        WriteFile(directory / L"data.dat", corrupt_legacy);

        DataStore store(directory);
        const auto state = store.Load();
        CHECK(state.schema_version == 2);
        CHECK(state.notes.size() == 1);
        CHECK(std::filesystem::exists(directory / L"data.json"));
        CHECK(std::filesystem::exists(directory / L"data.dat"));
        CHECK(std::filesystem::file_size(directory / L"data.dat") == corrupt_legacy.size());

        bool migration_backup_found = false;
        for (const auto& entry : std::filesystem::directory_iterator(directory / L"backups")) {
            migration_backup_found |= entry.path().filename().wstring().starts_with(L"legacy_data_");
        }
        CHECK(migration_backup_found);
        CHECK(store.Load().notes.size() == 1);
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}

void TestBackupRotation() {
    const auto directory = NewTestDirectory();
    try {
        DataStore store(directory);
        auto state = CreateDefaultState();
        for (int index = 0; index < 16; ++index) {
            state.notes.front().title = "backup-" + std::to_string(index);
            store.Save(state, true);
        }
        std::size_t backups = 0;
        for (const auto& entry : std::filesystem::directory_iterator(directory / L"backups")) {
            if (entry.path().filename().wstring().starts_with(L"data_")) ++backups;
        }
        CHECK(backups == 10);
        CHECK(!std::filesystem::exists(directory / L"data.json.tmp"));
        CHECK(store.Load().notes.front().title == "backup-15");
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}

void TestDpiConversions() {
    CHECK(DipToPixel(96.0, 144) == 144);
    CHECK(PixelToDip(144, 144) == 96.0);
    CHECK(DipToPixel(-96.0, 144) == -144);
}

void TestDataRootOverrideAndInstanceIdentity() {
    const auto directory = NewTestDirectory();
    try {
        ScopedEnvironmentVariable override(L"DESKTOPNOTE_DATA_DIR", directory.wstring());
        CHECK(DesktopNoteDataRoot() == directory);
        DataStore store;
        CHECK(store.root_directory() == directory);

        const std::wstring first_identifier = DesktopNoteInstanceIdentifier();
        std::wstring alternate_spelling = directory.wstring();
        std::transform(alternate_spelling.begin(), alternate_spelling.end(),
                       alternate_spelling.begin(), [](wchar_t character) {
                           return static_cast<wchar_t>(std::towupper(character));
                       });
        CHECK(SetEnvironmentVariableW(L"DESKTOPNOTE_DATA_DIR", alternate_spelling.c_str()));
        CHECK(DesktopNoteInstanceIdentifier() == first_identifier);

        LogDebug("data root override regression test");
        CHECK(std::filesystem::exists(directory / L"debug.log"));
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}

void TestResilientDeserialization() {
    const std::string json_with_bad_note = R"({
      "schemaVersion": 2,
      "lastActiveNoteId": "good-1",
      "notes": [
        {"id": "good-1", "title": "有效便签1", "contentRtfBase64": "", "createdAtUtc": "2026-01-01T00:00:00.000Z"},
        {"id": "bad-2", "title": 12345, "appearance": "not-an-object"},
        {"id": "good-3", "title": "有效便签3", "contentRtfBase64": "", "createdAtUtc": "2026-01-01T00:00:00.000Z"}
      ]
    })";
    const auto state = DeserializeAppState(json_with_bad_note);
    CHECK(state.notes.size() >= 2);
    CHECK(state.notes[0].title == "有效便签1");
}

void TestResilientUtfConversions() {
    const std::string raw_utf8 = "正常文本与特殊符号 \xF0\x9F\x93\x9D";
    const auto wide = Utf8ToWide(raw_utf8);
    CHECK(!wide.empty());
    const auto roundtrip = WideToUtf8(wide);
    CHECK(roundtrip == raw_utf8);

    const std::string broken_utf8 = "broken \xFF\xFE utf8";
    const auto converted = Utf8ToWide(broken_utf8);
    CHECK(!converted.empty());
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"base64", TestBase64},
        {"v2 round trip", TestVersionTwoRoundTrip},
        {"legacy migration", TestLegacyMigration},
        {"legacy defaults match WPF", TestLegacyDefaultsMatchWpf},
        {"normalization", TestNormalization},
        {"store migration and recovery", TestStoreMigrationAndRecovery},
        {"corrupt v2 does not resurrect legacy", TestCorruptVersionTwoDoesNotResurrectLegacy},
        {"corrupt legacy recovery", TestCorruptLegacyRecovery},
        {"backup rotation", TestBackupRotation},
        {"DPI conversions", TestDpiConversions},
        {"data root override and instance identity", TestDataRootOverrideAndInstanceIdentity},
        {"resilient deserialization", TestResilientDeserialization},
        {"resilient UTF conversions", TestResilientUtfConversions},
    };

    int failed = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - static_cast<std::size_t>(failed) << "/" << tests.size()
              << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
