#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "backup_config.hpp"

struct ProjectsSource {
    std::string id;
    std::filesystem::path path;

    bool operator==(const ProjectsSource&) const = default;
};

struct ProjectDiscoveryProblem {
    std::filesystem::path path;
    std::string message;

    bool operator==(const ProjectDiscoveryProblem&) const = default;
};

struct ProjectsDiscovery {
    std::vector<ProjectsSource> sources;
    std::vector<ProjectDiscoveryProblem> problems;
};

struct LargeEligibleFile {
    std::filesystem::path relativePath;
    std::uint64_t sizeBytes{};

    bool operator==(const LargeEligibleFile&) const = default;
};

struct ProjectPreflight {
    ProjectsSource source;
    std::uint64_t eligibleSizeBytes{};
    std::size_t eligibleFileCount{};
    std::vector<LargeEligibleFile> largeFiles;
    bool doesProjectExceedThreshold{};
    bool isGitRepository{};

    [[nodiscard]] bool requiresSizeApproval(bool isPermanentlyApproved) const noexcept;
};

[[nodiscard]] ProjectsDiscovery discoverProjects(const ProjectsRoot& root);
[[nodiscard]] ProjectPreflight scanProject(const ProjectsSource& source, const BackupSettings& settings);

