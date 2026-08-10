#include "config_store.hpp"

#define NOMINMAX
#include <windows.h>

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

ConfigStore::ConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

BackupConfig ConfigStore::load() const {
    if (!std::filesystem::exists(path_)) {
        return {};
    }

    std::ifstream input{path_, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open the backup configuration.");
    }
    const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return deserializeBackupConfig(text);
}

void ConfigStore::save(const BackupConfig& config) const {
    const std::string text = serializeBackupConfig(config);
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }

    std::filesystem::path temporaryPath = path_;
    temporaryPath += L".tmp";
    {
        std::ofstream output{temporaryPath, std::ios::binary | std::ios::trunc};
        if (!output) {
            throw std::runtime_error("Unable to create the temporary backup configuration.");
        }
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output) {
            throw std::runtime_error("Unable to write the temporary backup configuration.");
        }
    }

    if (MoveFileExW(temporaryPath.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const DWORD error = GetLastError();
        std::error_code ignoredError;
        std::filesystem::remove(temporaryPath, ignoredError);
        throw std::runtime_error("Unable to publish the backup configuration. Windows error " +
                                 std::to_string(error) + '.');
    }
}

const std::filesystem::path& ConfigStore::path() const noexcept {
    return path_;
}
