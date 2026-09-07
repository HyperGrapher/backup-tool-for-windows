#include "state_store.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("back-it-up-tool-state-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("route runtime state persists across store instances") {
    TemporaryDirectory directory;
    const auto databasePath = directory.path() / "state.db";
    const RouteRuntimeState expected{
        "source-one",
        "destination-one",
        RouteStatus::error,
        true,
        "2026-08-10T16:00:00Z",
        std::nullopt,
        "Destination unavailable",
    };

    {
        StateStore store{databasePath};
        REQUIRE_FALSE(store.routeState(expected.sourceId, expected.destinationId).has_value());
        store.setRouteState(expected);
    }

    StateStore reopenedStore{databasePath};
    REQUIRE(reopenedStore.routeState(expected.sourceId, expected.destinationId) == expected);
}

TEST_CASE("recent activity is returned newest first and respects the limit") {
    TemporaryDirectory directory;
    StateStore store{directory.path() / "state.db"};
    store.appendActivity("2026-08-10T16:00:00Z", "info", "First");
    store.appendActivity("2026-08-10T16:01:00Z", "error", "Second", "source-one", "destination-one");

    const auto records = store.recentActivity(1);

    REQUIRE(records.size() == 1);
    REQUIRE(records.front().message == "Second");
    REQUIRE(records.front().sourceId == "source-one");
    REQUIRE(records.front().destinationId == "destination-one");
}

TEST_CASE("activity history can be cleared without changing route state") {
    TemporaryDirectory directory;
    StateStore store{directory.path() / "state.db"};
    store.appendActivity("2026-08-10T16:00:00Z", "info", "Completed");
    store.appendActivity("2026-08-10T16:01:00Z", "error", "Failed");
    store.markRouteDirty("source-one", "destination-one");

    store.clearActivity();

    REQUIRE(store.recentActivity(10).empty());
    const auto routeState = store.routeState("source-one", "destination-one");
    REQUIRE(routeState.has_value());
    REQUIRE(routeState->isDirty);
}

TEST_CASE("failures can be cleared while retaining other activity") {
    TemporaryDirectory directory;
    StateStore store{directory.path() / "state.db"};
    store.appendActivity("2026-08-10T16:00:00Z", "info", "Completed");
    store.appendActivity("2026-08-10T16:01:00Z", "error", "Failed");
    store.appendActivity("2026-08-10T16:02:00Z", "warning", "Waiting");

    store.clearFailures();

    const auto records = store.recentActivity(10);
    REQUIRE(records.size() == 2);
    REQUIRE(std::ranges::none_of(records, [](const ActivityRecord& record) {
        return record.severity == "error";
    }));
}

TEST_CASE("a change during a running mirror remains pending after success") {
    TemporaryDirectory directory;
    StateStore store{directory.path() / "state.db"};

    store.markRouteDirty("source-one", "destination-one");
    store.beginRouteAttempt("source-one", "destination-one", "2026-08-13T10:00:00Z");
    store.markRouteDirty("source-one", "destination-one");
    store.completeRouteSuccess("source-one", "destination-one", "2026-08-13T10:01:00Z");

    const auto state = store.routeState("source-one", "destination-one");
    REQUIRE(state.has_value());
    REQUIRE(state->status == RouteStatus::pending);
    REQUIRE(state->isDirty);
    REQUIRE(state->lastSuccessUtc == "2026-08-13T10:01:00Z");
}

TEST_CASE("interrupted routes are pending after recovery") {
    TemporaryDirectory directory;
    StateStore stateStore{directory.path() / "state.db"};
    stateStore.markRouteDirty("source-one", "destination-one");
    stateStore.beginRouteAttempt("source-one", "destination-one", "2026-09-06T10:00:00Z");

    stateStore.recoverInterruptedRoutes();

    const auto recovered = stateStore.routeState("source-one", "destination-one");
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->status == RouteStatus::pending);
    REQUIRE(recovered->isDirty);
}

TEST_CASE("a quiet successful mirror clears pending work") {
    TemporaryDirectory directory;
    StateStore store{directory.path() / "state.db"};

    store.markRouteDirty("source-one", "destination-one");
    store.beginRouteAttempt("source-one", "destination-one", "2026-08-13T10:00:00Z");
    store.completeRouteSuccess("source-one", "destination-one", "2026-08-13T10:01:00Z");

    const auto state = store.routeState("source-one", "destination-one");
    REQUIRE(state.has_value());
    REQUIRE(state->status == RouteStatus::synced);
    REQUIRE_FALSE(state->isDirty);
}

TEST_CASE("Project backup decisions persist") {
    TemporaryDirectory directory;
    StateStore store{directory.path() / "state.db"};

    REQUIRE_FALSE(store.projectBackupDecision("project-one").has_value());
    store.setProjectBackupDecision("project-one", ProjectBackupDecision::alwaysAllow,
                                   "2026-08-10T16:00:00Z");
    REQUIRE(store.projectBackupDecision("project-one") == ProjectBackupDecision::alwaysAllow);
    store.setProjectBackupDecision("project-one", ProjectBackupDecision::ignoreLargeFiles,
                                   "2026-08-10T16:01:00Z");
    REQUIRE(store.projectBackupDecision("project-one") == ProjectBackupDecision::ignoreLargeFiles);
}
