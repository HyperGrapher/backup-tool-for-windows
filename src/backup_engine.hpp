#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "backup_config.hpp"
<<<<<<< HEAD
#include "projects_scanner.hpp"
=======
>>>>>>> ca638f856d93a6c06c654f048bf27327bda35525

class StateStore;

[[nodiscard]] std::filesystem::path buildMirrorRelativePath(const std::filesystem::path& sourcePath);

struct MirrorPlan {
<<<<<<< HEAD
    std::string routeSourceId;
=======
>>>>>>> ca638f856d93a6c06c654f048bf27327bda35525
    std::string sourceId;
    std::string destinationId;
    std::filesystem::path source;
    std::filesystem::path destination;
<<<<<<< HEAD
    ManualSourceKind sourceKind{ManualSourceKind::folder};
    bool isProjectsSource{};
=======
>>>>>>> ca638f856d93a6c06c654f048bf27327bda35525
};

struct BackupRunSummary {
    std::size_t succeeded{};
    std::size_t failed{};
    std::vector<std::string> messages;
};

<<<<<<< HEAD
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
    [[nodiscard]] std::vector<MirrorPlan> previewMirrors(
        const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources) const;
    [[nodiscard]] std::vector<MirrorPlan> previewPendingMirrors(const BackupConfig& config,
                                                                const std::vector<ConfiguredProjectsSource>& projectsSources,
                                                                const StateStore& stateStore) const;
    [[nodiscard]] std::vector<SizeWarning> findSizeWarnings(const BackupConfig& config,
                                                            const std::vector<MirrorPlan>& plans,
                                                            const StateStore& stateStore) const;
    [[nodiscard]] BackupRunSummary runMirrors(const BackupConfig& config, const std::vector<MirrorPlan>& plans,
                                              StateStore& stateStore,
                                              const std::filesystem::path& logDirectory) const;

private:
    void createDueSnapshot(const BackupRoute& route, const MirrorPlan& plan, const Destination& destination,
                           StateStore& stateStore) const;
=======
class BackupEngine final {
public:
    [[nodiscard]] std::vector<MirrorPlan> previewMirrors(const BackupConfig& config) const;
    [[nodiscard]] BackupRunSummary runMirrors(const BackupConfig& config, StateStore& stateStore,
                                              const std::filesystem::path& logDirectory) const;
>>>>>>> ca638f856d93a6c06c654f048bf27327bda35525
};
