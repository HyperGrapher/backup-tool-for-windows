#pragma once

#include <filesystem>

#include "backup_config.hpp"

class ConfigStore final {
public:
    explicit ConfigStore(std::filesystem::path path);

    [[nodiscard]] BackupConfig load() const;
    void save(const BackupConfig& config) const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
};

