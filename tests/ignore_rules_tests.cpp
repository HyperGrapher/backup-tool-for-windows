#include "ignore_rules.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

class IgnoreFile final {
public:
    explicit IgnoreFile(std::string_view content) {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() / ("back-it-up-tool-ignore-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(directory_);
        std::ofstream output{directory_ / ".backup-ignore", std::ios::binary};
        output << content;
    }

    ~IgnoreFile() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    [[nodiscard]] std::filesystem::path path() const {
        return directory_ / ".backup-ignore";
    }

private:
    std::filesystem::path directory_;
};

}  // namespace

TEST_CASE("ignore rules match names at any depth and root-anchored paths") {
    IgnoreFile file{"*.tmp\n/root-only.txt\n"};
    const IgnoreRules rules = IgnoreRules::load(file.path());

    REQUIRE(rules.isIgnored("nested/cache.tmp", false));
    REQUIRE(rules.isIgnored("root-only.txt", false));
    REQUIRE_FALSE(rules.isIgnored("nested/root-only.txt", false));
}

TEST_CASE("ignore rules support directory-only patterns and negation") {
    IgnoreFile file{"cache/\n*.log\n!keep.log\n"};
    const IgnoreRules rules = IgnoreRules::load(file.path());

    REQUIRE(rules.isIgnored("output/cache", true));
    REQUIRE_FALSE(rules.isIgnored("output/cache", false));
    REQUIRE(rules.isIgnored("output/error.log", false));
    REQUIRE_FALSE(rules.isIgnored("output/keep.log", false));
}

TEST_CASE("ignore rules support double-star and single-character wildcards") {
    IgnoreFile file{"docs/**/draft?.md\n"};
    const IgnoreRules rules = IgnoreRules::load(file.path());

    REQUIRE(rules.isIgnored("docs/draft1.md", false));
    REQUIRE(rules.isIgnored("docs/reviews/old/draftA.md", false));
    REQUIRE_FALSE(rules.isIgnored("other/draft1.md", false));
    REQUIRE_FALSE(rules.isIgnored("docs/draft12.md", false));
}

