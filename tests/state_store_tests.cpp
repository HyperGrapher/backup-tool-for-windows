#include "state_store.hpp"

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

