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

TEST_CASE("Deselected and removed routes do not contribute pending work or errors") {
    BackupConfig config;
    config.manualSources.push_back({"source", "C:/source"});
    config.destinations.push_back({"selected", "Drive", DestinationKind::path, "D:/copies"});
    config.destinations.push_back({"deselected", "Other", DestinationKind::path, "E:/copies"});
    config.routes.push_back({"source", "selected"});
    RouteRuntimeState current{"source", "selected"};
    current.isDirty = false;
    current.status = RouteStatus::synced;
    current.lastSuccessUtc = "2026-09-07T10:00:00Z";
    RouteRuntimeState stale{"source", "deselected"};
    stale.isDirty = true;
    stale.status = RouteStatus::error;
    RouteRuntimeState removed{"removed-source", "selected"};
    removed.isDirty = true;
    removed.status = RouteStatus::error;
    const auto health = backupHealth(config, {}, {current, stale, removed});
    REQUIRE(health.expected == 1);
    REQUIRE(health.pending == 0);
    REQUIRE(health.failed == 0);
    REQUIRE(isActiveBackupRoute(config, {}, "source", "selected"));
    REQUIRE_FALSE(isActiveBackupRoute(config, {}, "source", "deselected"));
    REQUIRE_FALSE(isActiveBackupRoute(config, {}, "removed-source", "selected"));
    config.destinations.clear();
    REQUIRE(effectiveBackupRoutes(config, {}).empty());
}

TEST_CASE("Projects inherit parent routes only until explicitly configured") {
    BackupConfig config;
    config.projectsRoots.push_back({"root", "C:/projects"});
    config.destinations.push_back({"first", "First", DestinationKind::path, "D:/copies"});
    config.destinations.push_back({"second", "Second", DestinationKind::path, "E:/copies"});
    config.routes = {{"root", "first"}, {"root", "second"}};
    const std::vector<ConfiguredProjectsSource> projects{{"root", {"project", "C:/projects/project"}}};
    REQUIRE(effectiveBackupRoutes(config, projects).size() == 2);
    REQUIRE(isActiveBackupRoute(config, projects, "project", "first"));
    REQUIRE_FALSE(isActiveBackupRoute(config, projects, "root", "first"));
    config.watchedProjects.push_back({"project"});
    REQUIRE(effectiveBackupRoutes(config, projects).empty());
    config.routes.push_back({"project", "second"});
    REQUIRE(effectiveBackupRoutes(config, projects).size() == 1);
    REQUIRE_FALSE(isActiveBackupRoute(config, projects, "project", "first"));
    REQUIRE(isActiveBackupRoute(config, projects, "project", "second"));
    REQUIRE(backupHealth(config, projects, {}).expected == 1);
    config.projectsRoots.clear();
    REQUIRE(effectiveBackupRoutes(config, projects).empty());
}
