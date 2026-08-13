#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "backup_engine.hpp"
#include "source_watcher.hpp"
#include "state_store.hpp"

namespace {

class WatcherTemporaryDirectory final {
public:
    WatcherTemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("back-it-up-tool-watcher-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~WatcherTemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    REQUIRE(output.is_open());
    output << text;
    output.close();
}

}  // namespace

TEST_CASE("a watched change becomes a pending mirror and is reconciled") {
    WatcherTemporaryDirectory directory;
    const std::filesystem::path sourceFolder = directory.path() / "source";
    const std::filesystem::path destinationFolder = directory.path() / "destination";
    std::filesystem::create_directories(sourceFolder);
    std::filesystem::create_directories(destinationFolder);
    const std::filesystem::path sourceFile = sourceFolder / "note.txt";
    writeText(sourceFile, "before");

    BackupConfig config;
    config.manualSources.push_back(ManualSource{"source-one", sourceFolder, ManualSourceKind::folder});
    config.destinations.push_back(
        Destination{"destination-one", "Test Destination", DestinationKind::path, destinationFolder, 0, {}});
    config.routes.push_back(BackupRoute{"source-one", "destination-one", true, false, {}});
    StateStore stateStore{directory.path() / "state.db"};

    std::mutex notificationMutex;
    std::condition_variable notificationCondition;
    bool wasNotified = false;
    SourceWatcher watcher;
    watcher.start(config.manualSources, 1, [&](const std::string& sourceId) {
        stateStore.markRouteDirty(sourceId, "destination-one");
        {
            const std::scoped_lock lock(notificationMutex);
            wasNotified = true;
        }
        notificationCondition.notify_one();
    });

    writeText(sourceFile, "after automatic mirror");
    {
        std::unique_lock lock(notificationMutex);
        REQUIRE(notificationCondition.wait_for(lock, std::chrono::seconds{5}, [&] { return wasNotified; }));
    }
    watcher.stop();

    const auto pendingState = stateStore.routeState("source-one", "destination-one");
    REQUIRE(pendingState.has_value());
    REQUIRE(pendingState->isDirty);

    BackupEngine engine;
    const BackupRunSummary summary = engine.runPendingMirrors(config, stateStore, directory.path() / "logs");
    REQUIRE(summary.succeeded == 1);
    REQUIRE(summary.failed == 0);

    const std::filesystem::path mirroredFile = destinationFolder / "BackItUpTool" / "Mirrors" /
                                               buildMirrorRelativePath(sourceFolder) / "note.txt";
    std::ifstream input{mirroredFile, std::ios::binary};
    REQUIRE(input.is_open());
    const std::string mirroredText{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    REQUIRE(mirroredText == "after automatic mirror");

    const auto completedState = stateStore.routeState("source-one", "destination-one");
    REQUIRE(completedState.has_value());
    REQUIRE_FALSE(completedState->isDirty);
    REQUIRE(completedState->status == RouteStatus::synced);
}
