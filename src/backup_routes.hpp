#pragma once

#include <algorithm>
#include <string_view>
#include <vector>

#include "backup_config.hpp"
#include "projects_scanner.hpp"

[[nodiscard]] inline bool hasExplicitProjectRoutes(const BackupConfig& config, std::string_view projectId) {
    return std::ranges::any_of(config.watchedProjects, [&](const WatchedProject& project) {
        return project.id == projectId;
    }) || std::ranges::any_of(config.routes, [&](const BackupRoute& route) {
        return route.sourceId == projectId;
    });
}

[[nodiscard]] inline std::vector<BackupRoute> effectiveBackupRoutes(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projects) {
    std::vector<BackupRoute> result;
    for (const BackupRoute& route : config.routes) {
        if (std::ranges::find(config.destinations, route.destinationId, &Destination::id) == config.destinations.end()) {
            continue;
        }
        if (std::ranges::find(config.manualSources, route.sourceId, &ManualSource::id) != config.manualSources.end()) {
            result.push_back(route);
            continue;
        }
        for (const ConfiguredProjectsSource& project : projects) {
            if (std::ranges::find(config.projectsRoots, project.rootId, &ProjectsRoot::id) == config.projectsRoots.end()) {
                continue;
            }
            if (project.source.id == route.sourceId ||
                (project.rootId == route.sourceId && !hasExplicitProjectRoutes(config, project.source.id))) {
                result.push_back({project.source.id, route.destinationId});
            }
        }
    }
    return result;
}

[[nodiscard]] inline bool isActiveBackupRoute(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projects,
    std::string_view sourceId, std::string_view destinationId) {
    return std::ranges::any_of(effectiveBackupRoutes(config, projects), [&](const BackupRoute& route) {
        return route.sourceId == sourceId && route.destinationId == destinationId;
    });
}
