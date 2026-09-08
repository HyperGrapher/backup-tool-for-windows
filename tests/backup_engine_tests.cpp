#include <chrono>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <string>

#include <catch2/generators/catch_generators.hpp>

#include <catch2/catch_test_macros.hpp>

#include "backup_engine.hpp"
#include "state_store.hpp"

TEST_CASE("mirror paths preserve the original local drive and folders") {
    const std::filesystem::path source = LR"(C:\Users\TestUser\Documents\Cinema 4D)";

    REQUIRE(buildMirrorRelativePath(source) == LR"(C\Users\TestUser\Documents\Cinema 4D)");
}

TEST_CASE("mirror paths preserve the original network server and share") {
    const std::filesystem::path source = LR"(\\server\share\folder\file.txt)";

    REQUIRE(buildMirrorRelativePath(source) == LR"(UNC\server\share\folder\file.txt)");
}

TEST_CASE("pending work waits for a missing folder Destination and runs when it returns") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-reconnect-test-" + std::to_string(suffix));
    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
    const std::filesystem::path source = testRoot / "source";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(source);
    {
        std::ofstream output{source / "file.txt"};
        output << "queued while disconnected";
    }

    BackupConfig config;
    config.manualSources.push_back(ManualSource{"source-one", source, ManualSourceKind::folder});
    config.destinations.push_back(
        Destination{"destination-one", "Missing Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"source-one", "destination-one"});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("source-one", "destination-one");
    BackupEngine engine;

    REQUIRE(engine.previewPendingMirrors(config, {}, stateStore).empty());
    REQUIRE(stateStore.routeState("source-one", "destination-one")->isDirty);

    std::filesystem::create_directories(destination);
    const std::vector<BackupPlan> plans = engine.previewPendingMirrors(config, {}, stateStore);
    REQUIRE(plans.size() == 1);
    const BackupRunSummary summary =
        engine.runMirrors(plans, stateStore, testRoot / "logs", config.settings.largeFileThresholdBytes);
    REQUIRE(summary.succeeded == 1);
    REQUIRE_FALSE(stateStore.routeState("source-one", "destination-one")->isDirty);

    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("an unavailable Destination does not block an available folder Destination") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-multiple-destination-test-" + std::to_string(suffix));
    const std::filesystem::path source = testRoot / "source";
    const std::filesystem::path availableDestination = testRoot / "available";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(availableDestination);
    std::ofstream{source / "file.txt"} << "back up to the available folder";

    BackupConfig config;
    config.manualSources.push_back(ManualSource{"source-one", source, ManualSourceKind::folder});
    config.destinations.push_back(
        Destination{"missing", "Disconnected", DestinationKind::path, testRoot / "missing", 0, {}});
    config.destinations.push_back(
        Destination{"available", "Available", DestinationKind::path, availableDestination, 0, {}});
    rebuildBackupRoutes(config);
    StateStore stateStore{testRoot / "state.db"};
    BackupEngine engine;

    const std::vector<BackupPlan> plans = engine.previewMirrors(config, {});

    REQUIRE(plans.size() == 1);
    REQUIRE(plans.front().destinationId == "available");
    const BackupRunSummary summary =
        engine.runMirrors(plans, stateStore, testRoot / "logs", config.settings.largeFileThresholdBytes);
    REQUIRE(summary.succeeded == 1);
    REQUIRE(std::filesystem::exists(availableDestination / "BackItUpTool" / "Mirrors" /
                                    buildMirrorRelativePath(source) / "file.txt"));
    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("a changed Zipped Source creates a best-compression ZIP archive") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-archive-test-" + std::to_string(suffix));
    const std::filesystem::path source = testRoot / "Documents" / "Example";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(destination);
    {
        std::ofstream output{source / "file.txt"};
        output << "archive content";
    }

    BackupConfig config;
    config.manualSources.push_back(
        ManualSource{"source-one", source, ManualSourceKind::folder, BackupMode::zipped});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"source-one", "destination-one"});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("source-one", "destination-one");

    BackupEngine engine;
    REQUIRE(engine.previewPendingMirrors(config, {}, stateStore).empty());
    const std::vector<BackupPlan> archivePlans = engine.previewPendingArchives(config, {}, stateStore);
    REQUIRE(archivePlans.size() == 1);
    REQUIRE(engine.runArchives(archivePlans, stateStore, config.settings.largeFileThresholdBytes).succeeded == 1);
    const std::vector<ArchiveRecord> archives = stateStore.archiveRecords("source-one", "destination-one");
    REQUIRE(archives.size() == 1);
    REQUIRE(std::filesystem::exists(archives.front().archivePath));
    REQUIRE(archives.front().archivePath.string().find("Zipped") != std::string::npos);
    REQUIRE_FALSE(stateStore.routeState("source-one", "destination-one")->isDirty);

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("a Projects Root mirrors to an available folder while another Destination is unavailable") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-project-mirror-test-" + std::to_string(suffix));
    const std::filesystem::path projectsRoot = testRoot / "Projects";
    const std::filesystem::path project = projectsRoot / "desktop-apps" / "backup-tool";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(project / "Repository" / ".git");
    std::filesystem::create_directories(project / "build");
    std::filesystem::create_directories(project / "node_modules");
    std::filesystem::create_directories(destination);
    {
        std::ofstream{project / ".backup-watch"} << "01234567-89ab-4def-8123-456789abcdef";
        std::ofstream{project / ".backup-ignore"} << "ignored.txt\n";
        std::ofstream{project / "notes.txt"} << "keep";
        std::ofstream{project / "ignored.txt"} << "ignore";
        std::ofstream{project / "build" / "output.txt"} << "build";
        std::ofstream{project / "node_modules" / "package.txt"} << "package";
        std::ofstream{project / "Repository" / ".git" / "HEAD"} << "head";
        std::ofstream{project / "Repository" / "loose.txt"} << "repository";
    }

    BackupConfig config;
    config.projectsRoots.push_back(ProjectsRoot{"root-one", projectsRoot});
    config.destinations.push_back(
        Destination{"missing", "Disconnected", DestinationKind::path, testRoot / "missing", 0, {}});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    rebuildBackupRoutes(config);
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "missing");
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");

    BackupEngine engine;
    const std::vector<ConfiguredProjectsSource> projectsSources =
        discoverConfiguredProjects(config.projectsRoots).sources;
    const std::vector<BackupPlan> plans = engine.previewPendingMirrors(config, projectsSources, stateStore);
    REQUIRE(plans.size() == 1);
    const BackupRunSummary summary =
        engine.runMirrors(plans, stateStore, testRoot / "logs", config.settings.largeFileThresholdBytes);
    REQUIRE(summary.succeeded == 1);
    REQUIRE(stateStore.routeState("01234567-89ab-4def-8123-456789abcdef", "missing")->isDirty);
    REQUIRE(std::filesystem::exists(plans.front().destination / "notes.txt"));
    REQUIRE_FALSE(std::filesystem::exists(plans.front().destination / ".backup-watch"));
    REQUIRE_FALSE(std::filesystem::exists(plans.front().destination / ".backup-ignore"));
    REQUIRE_FALSE(std::filesystem::exists(plans.front().destination / "ignored.txt"));
    REQUIRE_FALSE(std::filesystem::exists(plans.front().destination / "build"));
    REQUIRE_FALSE(std::filesystem::exists(plans.front().destination / "node_modules"));
    REQUIRE_FALSE(std::filesystem::exists(plans.front().destination / "Repository"));

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("an unchanged Project file is not rewritten during a later mirror") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-project-incremental-test-" + std::to_string(suffix));
    const std::filesystem::path projectsRoot = testRoot / "Projects";
    const std::filesystem::path project = projectsRoot / "LooseProject";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(project);
    std::filesystem::create_directories(destination);
    std::ofstream{project / ".backup-watch"} << "01234567-89ab-4def-8123-456789abcdef";
    std::ofstream{project / "notes.txt"} << "keep this unchanged";

    BackupConfig config;
    config.projectsRoots.push_back(ProjectsRoot{"root-one", projectsRoot});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"root-one", "destination-one"});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");
    BackupEngine engine;
    const auto projectsSources = discoverConfiguredProjects(config.projectsRoots).sources;
    const auto firstPlans = engine.previewPendingMirrors(config, projectsSources, stateStore);
    REQUIRE(engine.runMirrors(firstPlans, stateStore, testRoot / "logs",
                              config.settings.largeFileThresholdBytes).succeeded == 1);

    const std::filesystem::path mirroredFile = firstPlans.front().destination / "notes.txt";
    std::filesystem::permissions(mirroredFile, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove);
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");
    const auto secondPlans = engine.previewPendingMirrors(config, projectsSources, stateStore);
    REQUIRE(engine.runMirrors(secondPlans, stateStore, testRoot / "logs",
                              config.settings.largeFileThresholdBytes).succeeded == 1);

    std::error_code cleanupError;
    std::filesystem::permissions(mirroredFile, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, cleanupError);
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("a Zipped Project backup reads filtered source content without creating a mirror") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-project-archive-test-" + std::to_string(suffix));
    const std::filesystem::path projectsRoot = testRoot / "Projects";
    const std::filesystem::path project = projectsRoot / "LooseProject";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(project / "build");
    std::filesystem::create_directories(destination);
    std::ofstream{project / ".backup-watch"} << "01234567-89ab-4def-8123-456789abcdef";
    std::ofstream{project / "notes.txt"} << "this large file is skipped";
    std::ofstream{project / "small.txt"} << "ok";
    std::ofstream{project / "build" / "generated.txt"} << "excluded";

    BackupConfig config;
    config.projectsRoots.push_back(ProjectsRoot{"root-one", projectsRoot, BackupMode::zipped});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"root-one", "destination-one"});
    config.settings.largeFileThresholdBytes = 10;
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");
    stateStore.setProjectBackupDecision("01234567-89ab-4def-8123-456789abcdef",
                                        ProjectBackupDecision::ignoreLargeFiles, "2026-09-06T12:00:00Z");
    BackupEngine engine;
    const auto projectsSources = discoverConfiguredProjects(config.projectsRoots).sources;
    const auto archivePlans = engine.previewPendingArchives(config, projectsSources, stateStore);
    REQUIRE(archivePlans.size() == 1);
    REQUIRE(engine.runArchives(archivePlans, stateStore, config.settings.largeFileThresholdBytes).succeeded == 1);
    const auto archives = stateStore.archiveRecords("01234567-89ab-4def-8123-456789abcdef", "destination-one");
    REQUIRE(archives.size() == 1);
    REQUIRE(std::filesystem::exists(archives.front().archivePath));
    REQUIRE(engine.previewPendingArchives(config, projectsSources, stateStore).empty());

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("size warnings use only eligible Project content") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-size-warning-test-" + std::to_string(suffix));
    const std::filesystem::path projectsRoot = testRoot / "Projects";
    const std::filesystem::path project = projectsRoot / "LooseProject";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(project / "build");
    std::filesystem::create_directories(destination);
    {
        std::ofstream{project / ".backup-watch"} << "01234567-89ab-4def-8123-456789abcdef";
        std::ofstream{project / "large.bin"} << std::string(60, 'l');
        std::ofstream{project / "build" / "ignored.bin"} << std::string(500, 'b');
    }

    BackupConfig config;
    config.projectsRoots.push_back(ProjectsRoot{"root-one", projectsRoot});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"root-one", "destination-one"});
    config.settings.largeFileThresholdBytes = 50;
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");

    BackupEngine engine;
    const std::vector<ConfiguredProjectsSource> projectsSources =
        discoverConfiguredProjects(config.projectsRoots).sources;
    const std::vector<BackupPlan> plans = engine.previewPendingMirrors(config, projectsSources, stateStore);
    const std::vector<SizeWarning> warnings = engine.findSizeWarnings(config, plans, stateStore);
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings.front().eligibleSizeBytes == 60);
    REQUIRE(warnings.front().largeFiles.size() == 1);
    stateStore.setProjectBackupDecision("01234567-89ab-4def-8123-456789abcdef",
                                        ProjectBackupDecision::alwaysAllow, "2026-09-06T12:00:00Z");
    REQUIRE(engine.findSizeWarnings(config, plans, stateStore).empty());

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("manual folder routes do not show the Project size warning") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-manual-size-test-" + std::to_string(suffix));
    const std::filesystem::path source = testRoot / "source";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(destination);
    std::ofstream{source / "large.bin"} << std::string(60, 'l');

    BackupConfig config;
    config.manualSources.push_back(ManualSource{"source-one", source, ManualSourceKind::folder});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"source-one", "destination-one"});
    config.settings.largeFileThresholdBytes = 50;
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("source-one", "destination-one");

    BackupEngine engine;
    REQUIRE(engine.findSizeWarnings(config, engine.previewPendingMirrors(config, {}, stateStore), stateStore).empty());

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("backups include link targets only when following links and stop directory cycles") {
    const BackupMode mode = GENERATE(BackupMode::mirror, BackupMode::zipped);
    const bool isProject = GENERATE(false, true);
    const bool followLinks = GENERATE(false, true);
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot = std::filesystem::temp_directory_path() /
                                          ("back-it-up-link-archive-test-" + std::to_string(suffix));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{testRoot};
    const auto source = testRoot / "source";
    const auto external = testRoot / "external";
    const auto destination = testRoot / "destination";
    const auto extracted = testRoot / "extracted";
    std::filesystem::create_directories(source / "empty");
    std::filesystem::create_directories(external);
    std::filesystem::create_directories(destination);
    std::filesystem::create_directories(extracted);
    std::ofstream{source / "ordinary.txt"} << "ordinary";
    std::ofstream{external / "linked.txt"} << "linked target contents";
    std::error_code linkError;
    std::filesystem::create_symlink(external / "linked.txt", source / "file-link.txt", linkError);
    if (linkError) {
        SKIP("Creating symbolic links requires Windows Developer Mode or administrator privileges.");
    }
    std::filesystem::create_directory_symlink(external, source / "folder-link");
    std::filesystem::create_directory_symlink(source, external / "cycle");

    BackupConfig config;
    std::vector<ConfiguredProjectsSource> projects;
    if (isProject) {
        config.projectsRoots.push_back(ProjectsRoot{"root", testRoot, mode});
        config.watchedProjects.push_back(WatchedProject{"source", followLinks});
        projects.push_back(ConfiguredProjectsSource{"root", ProjectsSource{"source", source}});
    } else {
        config.manualSources.push_back(
            ManualSource{"source", source, ManualSourceKind::folder, mode, followLinks});
    }
    config.destinations.push_back(Destination{"destination", "Destination", DestinationKind::path, destination});
    config.routes.push_back(BackupRoute{"source", "destination"});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("source", "destination");
    BackupEngine engine;
    std::filesystem::path restoredSource;
    if (mode == BackupMode::zipped) {
        const auto plans = engine.previewPendingArchives(config, projects, stateStore);
        REQUIRE(plans.size() == 1);
        REQUIRE(engine.runArchives(plans, stateStore, config.settings.largeFileThresholdBytes).succeeded == 1);
        const auto archives = stateStore.archiveRecords("source", "destination");
        REQUIRE(archives.size() == 1);
        REQUIRE(_wspawnlp(_P_WAIT, L"tar.exe", L"tar.exe", L"-x", L"-f", archives.front().archivePath.c_str(),
                          L"-C", extracted.c_str(), nullptr) == 0);
        restoredSource = extracted / "source";
    } else {
        const auto plans = engine.previewPendingMirrors(config, projects, stateStore);
        REQUIRE(plans.size() == 1);
        REQUIRE(engine.runMirrors(plans, stateStore, testRoot / "logs", config.settings.largeFileThresholdBytes)
                    .succeeded == 1);
        restoredSource = plans.front().destination;
    }
    REQUIRE(std::filesystem::is_regular_file(restoredSource / "ordinary.txt"));
    if (followLinks) {
        for (const auto& relative : {std::filesystem::path{"file-link.txt"},
                                     std::filesystem::path{"folder-link"} / "linked.txt"}) {
            const auto file = restoredSource / relative;
            REQUIRE_FALSE(std::filesystem::is_symlink(file));
            std::ifstream input{file};
            std::string contents;
            std::getline(input, contents);
            REQUIRE(contents == "linked target contents");
        }
        REQUIRE_FALSE(std::filesystem::exists(restoredSource / "folder-link" / "cycle"));
    } else {
        REQUIRE_FALSE(std::filesystem::exists(restoredSource / "file-link.txt"));
        REQUIRE_FALSE(std::filesystem::exists(restoredSource / "folder-link"));
    }
    if (!isProject) {
        REQUIRE(std::filesystem::is_directory(restoredSource / "empty"));
    }
}
