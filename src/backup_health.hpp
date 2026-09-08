#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "backup_config.hpp"
#include "backup_routes.hpp"
#include "projects_scanner.hpp"
#include "state_store.hpp"

struct BackupHealth {
    std::size_t expected{};
    std::size_t pending{};
    std::size_t failed{};
    std::size_t neverRun{};
    std::optional<std::string> lastSuccess;

    [[nodiscard]] std::string summary() const {
        if (expected == 0) { return "Not set up"; }
        if (failed > 0) { return "Action needed"; }
        if (neverRun > 0) { return "First backup pending"; }
        if (pending > 0) { return "Changes are waiting"; }
        return "Current";
    }
};

[[nodiscard]] inline BackupHealth backupHealth(const BackupConfig& config,
                                               const std::vector<ConfiguredProjectsSource>& projects,
                                               const std::vector<RouteRuntimeState>& states) {
    BackupHealth health;
    const auto inspect = [&](const std::string& sourceId, const std::string& destinationId) {
        ++health.expected;
        const auto state = std::ranges::find_if(states, [&](const RouteRuntimeState& entry) {
            return entry.sourceId == sourceId && entry.destinationId == destinationId;
        });
        if (state == states.end()) {
            ++health.neverRun;
            ++health.pending;
            return;
        }
        if (state->isDirty) { ++health.pending; }
        if (state->status == RouteStatus::error) { ++health.failed; }
        if (!state->lastSuccessUtc) { ++health.neverRun; }
        else if (!health.lastSuccess || *state->lastSuccessUtc > *health.lastSuccess) {
            health.lastSuccess = state->lastSuccessUtc;
        }
    };
    for (const BackupRoute& route : effectiveBackupRoutes(config, projects)) {
        inspect(route.sourceId, route.destinationId);
    }
    return health;
}
