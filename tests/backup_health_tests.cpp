#include <catch2/catch_test_macros.hpp>

#include "backup_health.hpp"
#include "deferred_project_backups.hpp"

TEST_CASE("Backup health requires a configured successful copy") {
    BackupConfig config;
    REQUIRE(backupHealth(config, {}, {}).summary() == "Not set up");
    config.manualSources.push_back({"source", "C:/source", ManualSourceKind::folder, BackupMode::mirror});
    config.destinations.push_back({"destination", "Drive", DestinationKind::path, "D:/copies"});
    rebuildBackupRoutes(config);
    auto health = backupHealth(config, {}, {});
    REQUIRE(health.expected == 1);
    REQUIRE(health.neverRun == 1);
    REQUIRE(health.summary() == "First backup pending");
    RouteRuntimeState state{"source", "destination"};
    state.isDirty = false;
    state.status = RouteStatus::synced;
    REQUIRE(backupHealth(config, {}, {state}).summary() == "First backup pending");
    state.lastSuccessUtc = "2026-09-07T10:00:00Z";
    REQUIRE(backupHealth(config, {}, {state}).summary() == "Current");
    state.isDirty = true;
    REQUIRE(backupHealth(config, {}, {state}).summary() == "Changes are waiting");
    state.status = RouteStatus::error;
    REQUIRE(backupHealth(config, {}, {state}).summary() == "Action needed");
}

TEST_CASE("Removed routes and empty project roots cannot imply protection") {
    BackupConfig config;
    config.projectsRoots.push_back({"root", "C:/projects", BackupMode::mirror});
    config.destinations.push_back({"destination", "Drive", DestinationKind::path, "D:/copies"});
    rebuildBackupRoutes(config);
    RouteRuntimeState stale{"deleted-source", "destination"};
    stale.lastSuccessUtc = "2026-09-07T10:00:00Z";
    stale.status = RouteStatus::synced;
    stale.isDirty = false;
    const auto health = backupHealth(config, {}, {stale});
    REQUIRE(health.expected == 0);
    REQUIRE_FALSE(health.lastSuccess.has_value());
    REQUIRE(health.summary() == "Not set up");
}

TEST_CASE("Deferred projects wait across destinations without blocking unrelated work") {
    DeferredProjectBackups deferred;
    deferred.defer("project");
    BackupPlan first;
    first.sourceId = "project";
    first.destinationId = "usb";
    first.isProjectsSource = true;
    BackupPlan second = first;
    second.destinationId = "folder";
    BackupPlan other = first;
    other.sourceId = "other-project";
    BackupPlan manual = first;
    manual.isProjectsSource = false;
    std::vector<BackupPlan> plans{first, second, other, manual};
    deferred.removeFrom(plans);
    REQUIRE(plans.size() == 2);
    REQUIRE(plans[0].sourceId == "other-project");
    REQUIRE_FALSE(plans[1].isProjectsSource);
    deferred.clear();
    plans = {first, second};
    deferred.removeFrom(plans);
    REQUIRE(plans.size() == 2);
    REQUIRE(DeferredProjectBackups{}.empty());
}
