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

    REQUIRE(engine.previewPendingMirrors(config, stateStore).empty());
    REQUIRE(stateStore.routeState("source-one", "destination-one")->isDirty);

    std::filesystem::create_directories(destination);
    REQUIRE(engine.previewPendingMirrors(config, stateStore).size() == 1);
    const BackupRunSummary summary = engine.runPendingMirrors(config, stateStore, testRoot / "logs");
    REQUIRE(summary.succeeded == 1);
    REQUIRE_FALSE(stateStore.routeState("source-one", "destination-one")->isDirty);

    std::filesystem::remove_all(testRoot, cleanupError);
}
