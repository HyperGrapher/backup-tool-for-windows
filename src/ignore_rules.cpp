#include "ignore_rules.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

[[nodiscard]] std::string pathToGenericUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.generic_u8string();
    std::string text;
    text.reserve(bytes.size());
    for (const char8_t byte : bytes) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

[[nodiscard]] bool isEscaped(const std::string& text, std::size_t index) {
    std::size_t slashCount = 0;
    while (index > slashCount && text[index - slashCount - 1] == '\\') {
        ++slashCount;
    }
    return (slashCount % 2) != 0;
}

void trimUnescapedTrailingSpaces(std::string& pattern) {
    while (!pattern.empty() && (pattern.back() == ' ' || pattern.back() == '\t') &&
           !isEscaped(pattern, pattern.size() - 1)) {
        pattern.pop_back();
    }
}

[[nodiscard]] bool isRegexSpecial(char character) {
    constexpr std::string_view specialCharacters = R"(.^$|(){}+)";
    return specialCharacters.find(character) != std::string_view::npos;
}

void appendLiteral(std::string& regex, char character) {
    if (isRegexSpecial(character) || character == '[' || character == ']' || character == '\\') {
        regex.push_back('\\');
    }
    regex.push_back(character);
}

[[nodiscard]] std::string wildcardToRegex(std::string_view pattern) {
    std::string regex;
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        const char character = pattern[index];
        if (character == '\\') {
            if (index + 1 >= pattern.size()) {
                throw std::invalid_argument("Ignore pattern ends with an escape character.");
            }
            appendLiteral(regex, pattern[++index]);
            continue;
        }
        if (character == '?') {
            regex += "[^/]";
            continue;
        }
        if (character == '*') {
            const bool isDoubleStar = index + 1 < pattern.size() && pattern[index + 1] == '*';
            if (!isDoubleStar) {
                regex += "[^/]*";
                continue;
            }

            while (index + 1 < pattern.size() && pattern[index + 1] == '*') {
                ++index;
            }
            if (index + 1 < pattern.size() && pattern[index + 1] == '/') {
                ++index;
                regex += "(?:.*/)?";
            } else {
                regex += ".*";
            }
            continue;
        }
        if (character == '[') {
            const std::size_t closing = pattern.find(']', index + 1);
            if (closing == std::string_view::npos) {
                appendLiteral(regex, character);
                continue;
            }
            regex.push_back('[');
            std::size_t classIndex = index + 1;
            if (classIndex < closing && (pattern[classIndex] == '!' || pattern[classIndex] == '^')) {
                regex.push_back('^');
                ++classIndex;
            }
            for (; classIndex < closing; ++classIndex) {
                if (pattern[classIndex] == '\\') {
                    regex += "\\\\";
                } else {
                    regex.push_back(pattern[classIndex]);
                }
            }
            regex.push_back(']');
            index = closing;
            continue;
        }
        appendLiteral(regex, character);
    }
    return regex;
}

}  // namespace

std::optional<IgnoreRules::Rule> IgnoreRules::parseRule(std::string pattern) {
    if (pattern.ends_with('\r')) {
        pattern.pop_back();
    }
    trimUnescapedTrailingSpaces(pattern);
    if (pattern.empty() || pattern.front() == '#') {
        return std::nullopt;
    }

    bool isNegated = false;
    if (pattern.front() == '!') {
        isNegated = true;
        pattern.erase(pattern.begin());
    } else if (pattern.starts_with("\\#") || pattern.starts_with("\\!")) {
        pattern.erase(pattern.begin());
    }
    if (pattern.empty()) {
        return std::nullopt;
    }

    const bool isDirectoryOnly = pattern.ends_with('/') && !isEscaped(pattern, pattern.size() - 1);
    if (isDirectoryOnly) {
        pattern.pop_back();
    }
    const bool isAnchored = pattern.starts_with('/');
    if (isAnchored) {
        pattern.erase(pattern.begin());
    }
    const bool containsSeparator = pattern.find('/') != std::string::npos;

    std::string regex = isAnchored || containsSeparator ? "^" : "(?:^|.*/)";
    regex += wildcardToRegex(pattern);
    regex += '$';
    return IgnoreRules::Rule{std::regex{regex, std::regex::ECMAScript | std::regex::icase}, isNegated,
                             isDirectoryOnly};
}

IgnoreRules::IgnoreRules(std::vector<Rule> rules) : rules_(std::move(rules)) {}

IgnoreRules IgnoreRules::load(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        return {};
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open .backup-ignore.");
    }

    std::vector<Rule> rules;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }
        try {
            if (auto rule = parseRule(std::move(line)); rule.has_value()) {
                rules.push_back(std::move(*rule));
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("Invalid .backup-ignore pattern on line " + std::to_string(lineNumber) + ": " +
                                     error.what());
        }
    }
    if (!input.eof()) {
        throw std::runtime_error("Unable to read .backup-ignore.");
    }
    return IgnoreRules{std::move(rules)};
}

bool IgnoreRules::isIgnored(const std::filesystem::path& relativePath, bool isDirectory) const {
    const std::string path = pathToGenericUtf8(relativePath);
    bool isIgnored = false;
    for (const Rule& rule : rules_) {
        if (rule.isDirectoryOnly && !isDirectory) {
            continue;
        }
        if (std::regex_match(path, rule.matcher)) {
            isIgnored = !rule.isNegated;
        }
    }
    return isIgnored;
}
