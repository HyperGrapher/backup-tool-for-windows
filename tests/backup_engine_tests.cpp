#include <chrono>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "backup_engine.hpp"
#include "state_store.hpp"

TEST_CASE("mirror paths preserve the original local drive and folders") {
    const std::filesystem::path source = LR"(C:\Users\burak\Documents\Cinema 4D)";

    REQUIRE(buildMirrorRelativePath(source) == LR"(C\Users\burak\Documents\Cinema 4D)");
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
    config.routes.push_back(BackupRoute{"source-one", "destination-one", true, false, {}});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("source-one", "destination-one");
    BackupEngine engine;

    REQUIRE(engine.previewPendingMirrors(config, {}, stateStore).empty());
    REQUIRE(stateStore.routeState("source-one", "destination-one")->isDirty);

    std::filesystem::create_directories(destination);
    const std::vector<MirrorPlan> plans = engine.previewPendingMirrors(config, {}, stateStore);
    REQUIRE(plans.size() == 1);
    const BackupRunSummary summary = engine.runMirrors(config, plans, stateStore, testRoot / "logs");
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

    const std::vector<MirrorPlan> plans = engine.previewMirrors(config, {});

    REQUIRE(plans.size() == 1);
    REQUIRE(plans.front().destinationId == "available");
    const BackupRunSummary summary = engine.runMirrors(config, plans, stateStore, testRoot / "logs");
    REQUIRE(summary.succeeded == 1);
    REQUIRE(std::filesystem::exists(availableDestination / "BackItUpTool" / "Mirrors" /
                                    buildMirrorRelativePath(source) / "file.txt"));
    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("a changed route creates a ZIP snapshot beside the readable source path") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-snapshot-test-" + std::to_string(suffix));
    const std::filesystem::path source = testRoot / "Documents" / "Example";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(destination);
    {
        std::ofstream output{source / "file.txt"};
        output << "snapshot content";
    }

    BackupConfig config;
    config.manualSources.push_back(ManualSource{"source-one", source, ManualSourceKind::folder});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"source-one", "destination-one", true, true, {}});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("source-one", "destination-one");

    BackupEngine engine;
    const std::vector<MirrorPlan> plans = engine.previewPendingMirrors(config, {}, stateStore);
    const BackupRunSummary summary = engine.runMirrors(config, plans, stateStore, testRoot / "logs");
    REQUIRE(summary.succeeded == 1);
    const std::vector<MirrorPlan> snapshotPlans = engine.previewDueSnapshots(config, {}, stateStore);
    REQUIRE(snapshotPlans.size() == 1);
    REQUIRE(engine.runSnapshots(config, snapshotPlans, stateStore).succeeded == 1);
    const std::vector<SnapshotRecord> snapshots = stateStore.snapshotRecords("source-one", "destination-one");
    REQUIRE(snapshots.size() == 1);
    REQUIRE(std::filesystem::exists(snapshots.front().archivePath));
    REQUIRE(snapshots.front().archivePath.string().find("Snapshots") != std::string::npos);

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("a Projects Root mirrors only opted-in loose project files") {
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
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"root-one", "destination-one", true, false, {}});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");

    BackupEngine engine;
    const std::vector<ConfiguredProjectsSource> projectsSources =
        discoverConfiguredProjects(config.projectsRoots).sources;
    const std::vector<MirrorPlan> plans = engine.previewPendingMirrors(config, projectsSources, stateStore);
    REQUIRE(plans.size() == 1);
    const BackupRunSummary summary = engine.runMirrors(config, plans, stateStore, testRoot / "logs");
    REQUIRE(summary.succeeded == 1);
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
    config.routes.push_back(BackupRoute{"root-one", "destination-one", true, false, {}});
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");
    BackupEngine engine;
    const auto projectsSources = discoverConfiguredProjects(config.projectsRoots).sources;
    const auto firstPlans = engine.previewPendingMirrors(config, projectsSources, stateStore);
    REQUIRE(engine.runMirrors(config, firstPlans, stateStore, testRoot / "logs").succeeded == 1);

    const std::filesystem::path mirroredFile = firstPlans.front().destination / "notes.txt";
    std::filesystem::permissions(mirroredFile, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove);
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");
    const auto secondPlans = engine.previewPendingMirrors(config, projectsSources, stateStore);
    REQUIRE(engine.runMirrors(config, secondPlans, stateStore, testRoot / "logs").succeeded == 1);

    std::error_code cleanupError;
    std::filesystem::permissions(mirroredFile, std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, cleanupError);
    std::filesystem::remove_all(testRoot, cleanupError);
}

TEST_CASE("a due Project snapshot reads filtered source content without requiring a mirror") {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path testRoot =
        std::filesystem::temp_directory_path() / ("back-it-up-tool-project-snapshot-test-" + std::to_string(suffix));
    const std::filesystem::path projectsRoot = testRoot / "Projects";
    const std::filesystem::path project = projectsRoot / "LooseProject";
    const std::filesystem::path destination = testRoot / "destination";
    std::filesystem::create_directories(project / "build");
    std::filesystem::create_directories(destination);
    std::ofstream{project / ".backup-watch"} << "01234567-89ab-4def-8123-456789abcdef";
    std::ofstream{project / "notes.txt"} << "snapshot source";
    std::ofstream{project / "build" / "generated.txt"} << "excluded";

    BackupConfig config;
    config.projectsRoots.push_back(ProjectsRoot{"root-one", projectsRoot});
    config.destinations.push_back(Destination{"destination-one", "Destination", DestinationKind::path, destination, 0, {}});
    config.routes.push_back(BackupRoute{"root-one", "destination-one", false, true, {}});
    StateStore stateStore{testRoot / "state.db"};
    BackupEngine engine;
    const auto projectsSources = discoverConfiguredProjects(config.projectsRoots).sources;
    const auto snapshotPlans = engine.previewDueSnapshots(config, projectsSources, stateStore);
    REQUIRE(snapshotPlans.size() == 1);
    REQUIRE(engine.runSnapshots(config, snapshotPlans, stateStore).succeeded == 1);
    const auto snapshots = stateStore.snapshotRecords("01234567-89ab-4def-8123-456789abcdef", "destination-one");
    REQUIRE(snapshots.size() == 1);
    REQUIRE(std::filesystem::exists(snapshots.front().archivePath));

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
    config.routes.push_back(BackupRoute{"root-one", "destination-one", true, false, {}});
    config.settings.largeFileThresholdBytes = 50;
    config.settings.projectSizeThresholdBytes = 1000;
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("01234567-89ab-4def-8123-456789abcdef", "destination-one");

    BackupEngine engine;
    const std::vector<ConfiguredProjectsSource> projectsSources =
        discoverConfiguredProjects(config.projectsRoots).sources;
    const std::vector<MirrorPlan> plans = engine.previewPendingMirrors(config, projectsSources, stateStore);
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
    config.routes.push_back(BackupRoute{"source-one", "destination-one", true, false, {}});
    config.settings.largeFileThresholdBytes = 50;
    StateStore stateStore{testRoot / "state.db"};
    stateStore.markRouteDirty("source-one", "destination-one");

    BackupEngine engine;
    REQUIRE(engine.findSizeWarnings(config, engine.previewPendingMirrors(config, {}, stateStore), stateStore).empty());

    std::error_code cleanupError;
    std::filesystem::remove_all(testRoot, cleanupError);
}
