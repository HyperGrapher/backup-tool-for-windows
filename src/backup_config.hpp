#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class ManualSourceKind {
    file,
    folder,
};

enum class DestinationKind {
    path,
    removable,
};

enum class BackupMode {
    mirror,
    zipped,
};

struct ManualSource {
    std::string id;
    std::filesystem::path path;
    ManualSourceKind kind{ManualSourceKind::folder};
    BackupMode backupMode{BackupMode::mirror};
    bool followSymbolicLinks{};

    bool operator==(const ManualSource&) const = default;
};

struct ProjectsRoot {
    std::string id;
    std::filesystem::path path;
    BackupMode backupMode{BackupMode::mirror};

    bool operator==(const ProjectsRoot&) const = default;
};

struct WatchedProject {
    std::string id;
    bool followSymbolicLinks{};

    bool operator==(const WatchedProject&) const = default;
};

struct Destination {
    std::string id;
    std::string name;
    DestinationKind kind{DestinationKind::path};
    std::filesystem::path root;
    std::uint32_t volumeSerial{};
    std::string volumeLabel;

    bool operator==(const Destination&) const = default;
};

struct BackupRoute {
    std::string sourceId;
    std::string destinationId;

    bool operator==(const BackupRoute&) const = default;
};

struct BackupSettings {
    int debounceSeconds{8};
    std::uint64_t largeFileThresholdBytes{50ULL * 1024ULL * 1024ULL};

    bool operator==(const BackupSettings&) const = default;
};

struct BackupConfig {
    int schemaVersion{2};
    std::vector<ManualSource> manualSources;
    std::vector<ProjectsRoot> projectsRoots;
    std::vector<WatchedProject> watchedProjects;
    std::vector<Destination> destinations;
    std::vector<BackupRoute> routes;
    BackupSettings settings;

    bool operator==(const BackupConfig&) const = default;
};

[[nodiscard]] std::string generateUuid();
[[nodiscard]] std::string generateStableId(std::string_view prefix);
void rebuildBackupRoutes(BackupConfig& config);
void validateBackupConfig(const BackupConfig& config);
[[nodiscard]] std::string serializeBackupConfig(const BackupConfig& config);
[[nodiscard]] BackupConfig deserializeBackupConfig(std::string_view jsonText);
