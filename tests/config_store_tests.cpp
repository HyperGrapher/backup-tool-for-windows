#include "config_store.hpp"

#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("back-it-up-tool-config-tests-" + std::to_string(suffix));
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

TEST_CASE("missing configuration loads as an empty valid configuration") {
    TemporaryDirectory directory;
    const ConfigStore store{directory.path() / "config.json"};

    const BackupConfig config = store.load();

    REQUIRE(config == BackupConfig{});
}

TEST_CASE("configuration is atomically replaced and persists") {
    TemporaryDirectory directory;
    const ConfigStore store{directory.path() / "config.json"};
    BackupConfig expected;
    expected.manualSources.push_back(ManualSource{"source-one", L"C:\\Notes", ManualSourceKind::folder});

    store.save(BackupConfig{});
    store.save(expected);

    REQUIRE(store.load() == expected);
    REQUIRE_FALSE(std::filesystem::exists(directory.path() / "config.json.tmp"));
}

