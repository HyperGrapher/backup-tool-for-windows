#include "projects_scanner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("back-it-up-tool-project-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    void write(const std::filesystem::path& relativePath, std::string_view content) const {
        const std::filesystem::path path = path_ / relativePath;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{path, std::ios::binary};
        output << content;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("Projects discovery opts in immediate children and gives empty markers stable UUIDs") {
    TemporaryDirectory directory;
    directory.write("Included/.backup-watch", "");
    directory.write("NotIncluded/notes.txt", "notes");
    const ProjectsRoot root{"projects-root", directory.path()};

    const ProjectsDiscovery first = discoverProjects(root);
    const ProjectsDiscovery second = discoverProjects(root);

    const std::string firstProblem = first.problems.empty() ? std::string{} : first.problems.front().message;
    INFO(firstProblem);
    REQUIRE(first.problems.empty());
    REQUIRE(first.sources.size() == 1);
    REQUIRE(first.sources.front().path.filename() == "Included");
    REQUIRE(first.sources.front().id.size() == 36);
    REQUIRE(second.sources.front().id == first.sources.front().id);
}

TEST_CASE("Projects discovery reports invalid and duplicate marker UUIDs") {
    TemporaryDirectory directory;
    directory.write("Invalid/.backup-watch", "not-an-id");
    directory.write("First/.backup-watch", "01234567-89ab-4def-8123-456789abcdef");
    directory.write("Second/.backup-watch", "01234567-89ab-4def-8123-456789abcdef");

    const ProjectsDiscovery discovery = discoverProjects(ProjectsRoot{"projects-root", directory.path()});

    REQUIRE(discovery.sources.size() == 1);
    REQUIRE(discovery.problems.size() == 2);
}

TEST_CASE("project preflight excludes repositories generated trees tool metadata and ignore matches") {
    TemporaryDirectory directory;
    directory.write("Project/.backup-watch", "01234567-89ab-4def-8123-456789abcdef");
    directory.write("Project/.backup-ignore", "ignored.bin\ncache/\n");
    directory.write("Project/notes.bin", std::string(60, 'n'));
    directory.write("Project/assets.bin", std::string(100, 'a'));
    directory.write("Project/ignored.bin", std::string(500, 'i'));
    directory.write("Project/cache/data.bin", std::string(500, 'c'));
    directory.write("Project/build/output.bin", std::string(500, 'b'));
    directory.write("Project/node_modules/package/data.bin", std::string(500, 'n'));
    directory.write("Project/Code/.git/HEAD", "ref: refs/heads/main");
    directory.write("Project/Code/object.bin", std::string(500, 'g'));
    const ProjectsSource source{"01234567-89ab-4def-8123-456789abcdef", directory.path() / "Project"};
    BackupSettings settings;
    settings.largeFileThresholdBytes = 50;
    settings.projectSizeThresholdBytes = 150;

    const ProjectPreflight preflight = scanProject(source, settings);

    REQUIRE(preflight.eligibleFileCount == 2);
    REQUIRE(preflight.eligibleSizeBytes == 160);
    REQUIRE(preflight.largeFiles.size() == 2);
    REQUIRE(preflight.doesProjectExceedThreshold);
    REQUIRE(preflight.requiresSizeApproval(false));
    REQUIRE_FALSE(preflight.requiresSizeApproval(true));
}

TEST_CASE("a Projects Source that is itself a Git repository has no Eligible Items") {
    TemporaryDirectory directory;
    directory.write("Project/.backup-watch", "01234567-89ab-4def-8123-456789abcdef");
    directory.write("Project/.git/HEAD", "ref: refs/heads/main");
    directory.write("Project/notes.bin", std::string(100, 'n'));
    const ProjectsSource source{"01234567-89ab-4def-8123-456789abcdef", directory.path() / "Project"};

    const ProjectPreflight preflight = scanProject(source, BackupSettings{});

    REQUIRE(preflight.isGitRepository);
    REQUIRE(preflight.eligibleFileCount == 0);
    REQUIRE_FALSE(preflight.requiresSizeApproval(false));
}
