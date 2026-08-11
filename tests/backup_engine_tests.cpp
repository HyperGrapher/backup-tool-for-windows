#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "backup_engine.hpp"

TEST_CASE("mirror paths preserve the original local drive and folders") {
    const std::filesystem::path source = LR"(C:\Users\burak\Documents\Cinema 4D)";

    REQUIRE(buildMirrorRelativePath(source) == LR"(C\Users\burak\Documents\Cinema 4D)");
}

TEST_CASE("mirror paths preserve the original network server and share") {
    const std::filesystem::path source = LR"(\\server\share\folder\file.txt)";

    REQUIRE(buildMirrorRelativePath(source) == LR"(UNC\server\share\folder\file.txt)");
}
