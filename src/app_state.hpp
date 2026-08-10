#pragma once

#include <string>

struct AppState {
    std::string displayName{"Windows Backup Tool"};
    int sampleCount{0};

    bool operator==(const AppState&) const = default;
};

[[nodiscard]] std::string serializeAppState(const AppState& state);
[[nodiscard]] AppState deserializeAppState(const std::string& jsonText);

