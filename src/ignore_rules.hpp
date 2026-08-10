#pragma once

#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <vector>

class IgnoreRules final {
public:
    IgnoreRules() = default;

    [[nodiscard]] static IgnoreRules load(const std::filesystem::path& path);
    [[nodiscard]] bool isIgnored(const std::filesystem::path& relativePath, bool isDirectory) const;

private:
    struct Rule {
        std::regex matcher;
        bool isNegated{};
        bool isDirectoryOnly{};
    };

    explicit IgnoreRules(std::vector<Rule> rules);
    [[nodiscard]] static std::optional<Rule> parseRule(std::string pattern);

    std::vector<Rule> rules_;
};
