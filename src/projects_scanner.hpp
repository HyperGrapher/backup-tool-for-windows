#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
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

struct ConfiguredProjectsSource {
    std::string rootId;
    ProjectsSource source;

    bool operator==(const ConfiguredProjectsSource&) const = default;
};

struct ConfiguredProjectsDiscovery {
    std::vector<ConfiguredProjectsSource> sources;
    std::vector<ProjectDiscoveryProblem> problems;
};

struct EligibleProjectFile {
    std::filesystem::path relativePath;
    std::uint64_t sizeBytes{};

    bool operator==(const EligibleProjectFile&) const = default;
};

struct ProjectContents {
    ProjectsSource source;
    std::vector<EligibleProjectFile> files;
    bool isGitRepository{};
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

    [[nodiscard]] bool requiresApproval() const noexcept;
};

class ProjectChangeFilter final {
public:
    explicit ProjectChangeFilter(std::filesystem::path sourceRoot);
    ~ProjectChangeFilter();

    ProjectChangeFilter(const ProjectChangeFilter&) = delete;
    ProjectChangeFilter& operator=(const ProjectChangeFilter&) = delete;

    [[nodiscard]] bool operator()(const std::filesystem::path& relativePath);

private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};

[[nodiscard]] ProjectsDiscovery discoverProjects(const ProjectsRoot& root);
[[nodiscard]] ConfiguredProjectsDiscovery discoverConfiguredProjects(const std::vector<ProjectsRoot>& roots);
[[nodiscard]] bool isProjectsRootDiscoveryChange(const std::filesystem::path& relativePath);
[[nodiscard]] ProjectContents collectProjectContents(const ProjectsSource& source);
[[nodiscard]] ProjectPreflight scanProject(const ProjectsSource& source, const BackupSettings& settings);
