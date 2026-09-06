#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "backup_config.hpp"
#include "projects_scanner.hpp"

class StateStore;

[[nodiscard]] std::filesystem::path buildMirrorRelativePath(const std::filesystem::path& sourcePath);

struct BackupPlan {
    std::string routeSourceId;
    std::string sourceId;
    std::string destinationId;
    std::filesystem::path source;
    std::filesystem::path destination;
    ManualSourceKind sourceKind{ManualSourceKind::folder};
    bool isProjectsSource{};
};

struct BackupRunSummary {
    std::size_t succeeded{};
    std::size_t failed{};
    std::vector<std::string> messages;
};

struct SizeWarning {
    std::string sourceId;
    std::filesystem::path sourcePath;
    std::uint64_t eligibleSizeBytes{};
    std::vector<LargeEligibleFile> largeFiles;
};

class BackupEngine final {
public:
    [[nodiscard]] std::vector<BackupPlan> previewMirrors(
        const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources) const;
    [[nodiscard]] std::vector<BackupPlan> previewPendingMirrors(
        const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
        const StateStore& stateStore) const;
    [[nodiscard]] std::vector<BackupPlan> previewPendingArchives(
        const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
        const StateStore& stateStore) const;
    [[nodiscard]] std::vector<SizeWarning> findSizeWarnings(const BackupConfig& config,
                                                            const std::vector<BackupPlan>& plans,
                                                            const StateStore& stateStore) const;
    [[nodiscard]] BackupRunSummary runMirrors(const std::vector<BackupPlan>& plans, StateStore& stateStore,
                                              const std::filesystem::path& logDirectory,
                                              std::uint64_t largeFileThresholdBytes) const;
    [[nodiscard]] BackupRunSummary runArchives(const std::vector<BackupPlan>& plans, StateStore& stateStore,
                                               std::uint64_t largeFileThresholdBytes) const;

private:
    [[nodiscard]] std::vector<BackupPlan> previewBackups(
        const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
        BackupMode backupMode) const;
    [[nodiscard]] std::vector<BackupPlan> previewPendingBackups(
        const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
        const StateStore& stateStore, BackupMode backupMode) const;
    void createArchive(const BackupPlan& plan, StateStore& stateStore,
                       std::uint64_t largeFileThresholdBytes) const;
};
