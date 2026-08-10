#include "backup_config.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

[[nodiscard]] BackupConfig populatedConfig() {
    BackupConfig config;
    config.manualSources.push_back(ManualSource{"source-notes", L"C:\\Users\\Burak\\Notes", ManualSourceKind::folder});
    config.manualSources.push_back(ManualSource{"source-list", L"C:\\Users\\Burak\\todo.txt", ManualSourceKind::file});
    config.projectsRoots.push_back(ProjectsRoot{"projects-main", L"D:\\Projects"});
    config.destinations.push_back(Destination{
        "destination-folder",
        "Local backup",
        DestinationKind::path,
        L"D:\\Backups",
        0,
        {},
    });
    config.destinations.push_back(Destination{
        "destination-flash",
        "Flash drive",
        DestinationKind::removable,
        L"BackItUpTool",
        0xA1B2C3D4,
        "BACKUP",
    });
    config.routes.push_back(BackupRoute{"source-notes", "destination-folder", true, true, {24, 30, 12}});
    config.routes.push_back(BackupRoute{"projects-main", "destination-flash", true, false, {24, 30, 12}});
    return config;
}

}  // namespace

TEST_CASE("backup configuration round-trips through JSON") {
    const BackupConfig expected = populatedConfig();

    const BackupConfig actual = deserializeBackupConfig(serializeBackupConfig(expected));

    REQUIRE(actual == expected);
}

TEST_CASE("backup configuration uses operational defaults when optional JSON fields are absent") {
    const BackupConfig config = deserializeBackupConfig("{}");

    REQUIRE(config.schemaVersion == 1);
    REQUIRE(config.settings.debounceSeconds == 8);
    REQUIRE(config.settings.projectsRescanMinutes == 5);
    REQUIRE(config.manualSources.empty());
    REQUIRE(config.projectsRoots.empty());
}

TEST_CASE("backup configuration rejects a route with an unknown Source") {
    BackupConfig config = populatedConfig();
    config.routes.front().sourceId = "missing-source";

    REQUIRE_THROWS_AS(validateBackupConfig(config), std::invalid_argument);
}

TEST_CASE("backup configuration permits only one route for a Source and Destination") {
    BackupConfig config = populatedConfig();
    config.routes.push_back(config.routes.front());

    REQUIRE_THROWS_AS(validateBackupConfig(config), std::invalid_argument);
}

TEST_CASE("generated stable IDs retain the requested domain prefix") {
    const std::string first = generateStableId("source");
    const std::string second = generateStableId("source");

    REQUIRE(first.starts_with("source-"));
    REQUIRE(first != second);
}

