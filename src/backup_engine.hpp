#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "backup_config.hpp"

class StateStore;

[[nodiscard]] std::filesystem::path buildMirrorRelativePath(const std::filesystem::path& sourcePath);

struct MirrorPlan {
    std::string sourceId;
    std::string destinationId;
    std::filesystem::path source;
    std::filesystem::path destination;
};

struct BackupRunSummary {
    std::size_t succeeded{};
    std::size_t failed{};
    std::vector<std::string> messages;
};

class BackupEngine final {
public:
    [[nodiscard]] std::vector<MirrorPlan> previewMirrors(const BackupConfig& config) const;
    [[nodiscard]] BackupRunSummary runMirrors(const BackupConfig& config, StateStore& stateStore,
                                              const std::filesystem::path& logDirectory) const;
};
