#pragma once

#include <string>

struct AppState {
    std::string displayName{"System Tray App"};
    int sampleCount{0};

    bool operator==(const AppState&) const = default;
};

[[nodiscard]] std::string serializeAppState(const AppState& state);
[[nodiscard]] AppState deserializeAppState(const std::string& jsonText);

