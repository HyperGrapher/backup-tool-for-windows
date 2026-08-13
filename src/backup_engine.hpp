#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "backup_config.hpp"
#include "projects_scanner.hpp"

class StateStore;

[[nodiscard]] std::filesystem::path buildMirrorRelativePath(const std::filesystem::path& sourcePath);

struct MirrorPlan {
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
    std::string destinationId;
    std::filesystem::path sourcePath;
    std::uint64_t eligibleSizeBytes{};
    std::vector<LargeEligibleFile> largeFiles;
    bool isProject{};
};

class BackupEngine final {
public:
    [[nodiscard]] std::vector<MirrorPlan> previewMirrors(const BackupConfig& config) const;
    [[nodiscard]] std::vector<MirrorPlan> previewPendingMirrors(const BackupConfig& config,
                                                                const StateStore& stateStore) const;
    [[nodiscard]] std::vector<SizeWarning> findSizeWarnings(const BackupConfig& config,
                                                            const std::vector<MirrorPlan>& plans,
                                                            const StateStore& stateStore) const;
    [[nodiscard]] BackupRunSummary runMirrors(const BackupConfig& config, StateStore& stateStore,
                                              const std::filesystem::path& logDirectory) const;
    [[nodiscard]] BackupRunSummary runPendingMirrors(const BackupConfig& config, StateStore& stateStore,
                                                     const std::filesystem::path& logDirectory) const;

private:
    [[nodiscard]] BackupRunSummary runPlans(const BackupConfig& config, const std::vector<MirrorPlan>& plans,
                                            StateStore& stateStore,
                                            const std::filesystem::path& logDirectory) const;
    void createDueSnapshot(const BackupRoute& route, const MirrorPlan& plan, const Destination& destination,
                           StateStore& stateStore) const;
};
