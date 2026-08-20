#pragma once

#include "app_state.h"

#include <filesystem>
#include <string>

namespace desktopnote {

std::string SerializeAppState(const AppState& state);
AppState DeserializeAppState(const std::string& json_text);
AppState ImportLegacyAppState(const std::string& encoded_legacy_data);

class DataStore {
public:
    DataStore();
    explicit DataStore(std::filesystem::path root_directory);

    AppState Load();
    void Save(const AppState& state, bool create_backup = false);
    void CreateBackup(const std::wstring& prefix = L"data");

    const std::filesystem::path& root_directory() const { return root_directory_; }
    const std::filesystem::path& data_path() const { return data_path_; }
    const std::filesystem::path& legacy_path() const { return legacy_path_; }
    const std::filesystem::path& backup_directory() const { return backup_directory_; }

    void BackupFile(const std::filesystem::path& source, const std::wstring& prefix);
    void RotateBackups(const std::wstring& prefix, std::size_t keep_count = 10);

private:
    std::filesystem::path root_directory_;
    std::filesystem::path data_path_;
    std::filesystem::path legacy_path_;
    std::filesystem::path backup_directory_;

    static std::string ReadUtf8File(const std::filesystem::path& path);
};

}  // namespace desktopnote
