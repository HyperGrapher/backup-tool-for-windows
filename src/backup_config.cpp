#include "backup_config.hpp"

#include <array>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    std::string text;
    text.reserve(bytes.size());
    for (const char8_t byte : bytes) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
    std::u8string bytes;
    bytes.reserve(text.size());
    for (const char byte : text) {
        bytes.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path{bytes};
}

[[nodiscard]] std::string_view toString(ManualSourceKind kind) {
    switch (kind) {
    case ManualSourceKind::file:
        return "file";
    case ManualSourceKind::folder:
        return "folder";
    }
    throw std::invalid_argument("Unknown manual source kind.");
}

[[nodiscard]] ManualSourceKind manualSourceKindFromString(std::string_view value) {
    if (value == "file") {
        return ManualSourceKind::file;
    }
    if (value == "folder") {
        return ManualSourceKind::folder;
    }
    throw std::invalid_argument("Unknown manual source kind: " + std::string{value});
}

[[nodiscard]] std::string_view toString(DestinationKind kind) {
    switch (kind) {
    case DestinationKind::path:
        return "path";
    case DestinationKind::removable:
        return "removable";
    }
    throw std::invalid_argument("Unknown destination kind.");
}

[[nodiscard]] DestinationKind destinationKindFromString(std::string_view value) {
    if (value == "path") {
        return DestinationKind::path;
    }
    if (value == "removable") {
        return DestinationKind::removable;
    }
    throw std::invalid_argument("Unknown destination kind: " + std::string{value});
}

[[nodiscard]] Json toJson(const ManualSource& source) {
    return Json{{"id", source.id}, {"path", pathToUtf8(source.path)}, {"kind", toString(source.kind)}};
}

[[nodiscard]] Json toJson(const ProjectsRoot& root) {
    return Json{{"id", root.id}, {"path", pathToUtf8(root.path)}};
}

[[nodiscard]] Json toJson(const Destination& destination) {
    Json result{
        {"id", destination.id},
        {"name", destination.name},
        {"kind", toString(destination.kind)},
        {"root", pathToUtf8(destination.root)},
    };
    if (destination.kind == DestinationKind::removable) {
        result["volumeSerial"] = destination.volumeSerial;
        result["volumeLabel"] = destination.volumeLabel;
    }
    return result;
}

[[nodiscard]] Json toJson(const SnapshotPolicy& policy) {
    return Json{
        {"intervalHours", policy.intervalHours},
        {"retainDaily", policy.retainDaily},
        {"retainMonthly", policy.retainMonthly},
    };
}

[[nodiscard]] Json toJson(const BackupRoute& route) {
    return Json{
        {"sourceId", route.sourceId},
        {"destinationId", route.destinationId},
        {"mirrorEnabled", route.isMirrorEnabled},
        {"snapshotsEnabled", route.areSnapshotsEnabled},
        {"snapshotPolicy", toJson(route.snapshotPolicy)},
    };
}

[[nodiscard]] Json toJson(const BackupSettings& settings) {
    return Json{
        {"debounceSeconds", settings.debounceSeconds},
        {"largeFileThresholdBytes", settings.largeFileThresholdBytes},
        {"projectSizeThresholdBytes", settings.projectSizeThresholdBytes},
    };
}

[[nodiscard]] ManualSource manualSourceFromJson(const Json& json) {
    return ManualSource{
        json.at("id").get<std::string>(),
        pathFromUtf8(json.at("path").get<std::string>()),
        manualSourceKindFromString(json.at("kind").get<std::string>()),
    };
}

[[nodiscard]] ProjectsRoot projectsRootFromJson(const Json& json) {
    return ProjectsRoot{json.at("id").get<std::string>(), pathFromUtf8(json.at("path").get<std::string>())};
}

[[nodiscard]] Destination destinationFromJson(const Json& json) {
    Destination destination;
    destination.id = json.at("id").get<std::string>();
    destination.name = json.at("name").get<std::string>();
    destination.kind = destinationKindFromString(json.at("kind").get<std::string>());
    destination.root = pathFromUtf8(json.at("root").get<std::string>());
    destination.volumeSerial = json.value("volumeSerial", std::uint32_t{});
    destination.volumeLabel = json.value("volumeLabel", std::string{});
    return destination;
}

[[nodiscard]] SnapshotPolicy snapshotPolicyFromJson(const Json& json) {
    SnapshotPolicy policy;
    policy.intervalHours = json.value("intervalHours", policy.intervalHours);
    policy.retainDaily = json.value("retainDaily", policy.retainDaily);
    policy.retainMonthly = json.value("retainMonthly", policy.retainMonthly);
    return policy;
}

[[nodiscard]] BackupRoute routeFromJson(const Json& json) {
    BackupRoute route;
    route.sourceId = json.at("sourceId").get<std::string>();
    route.destinationId = json.at("destinationId").get<std::string>();
    route.isMirrorEnabled = json.value("mirrorEnabled", route.isMirrorEnabled);
    route.areSnapshotsEnabled = json.value("snapshotsEnabled", route.areSnapshotsEnabled);
    if (const auto policy = json.find("snapshotPolicy"); policy != json.end()) {
        route.snapshotPolicy = snapshotPolicyFromJson(*policy);
    }
    return route;
}

[[nodiscard]] BackupSettings settingsFromJson(const Json& json) {
    BackupSettings settings;
    settings.debounceSeconds = json.value("debounceSeconds", settings.debounceSeconds);
    settings.largeFileThresholdBytes = json.value("largeFileThresholdBytes", settings.largeFileThresholdBytes);
    settings.projectSizeThresholdBytes = json.value("projectSizeThresholdBytes", settings.projectSizeThresholdBytes);
    return settings;
}

template <typename Collection, typename GetId>
void requireUniqueIds(const Collection& values, GetId getId, std::string_view description) {
    std::unordered_set<std::string> ids;
    for (const auto& value : values) {
        const std::string& id = getId(value);
        if (id.empty()) {
            throw std::invalid_argument(std::string{description} + " ID cannot be empty.");
        }
        if (!ids.insert(id).second) {
            throw std::invalid_argument("Duplicate " + std::string{description} + " ID: " + id);
        }
    }
}

}  // namespace

std::string generateUuid() {
    std::array<std::uint8_t, 16> bytes{};
    std::random_device random;
    for (std::uint8_t& byte : bytes) {
        byte = static_cast<std::uint8_t>(random());
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        result << std::setw(2) << static_cast<unsigned int>(bytes[index]);
        if (index == 3 || index == 5 || index == 7 || index == 9) {
            result << '-';
        }
    }
    return result.str();
}

std::string generateStableId(std::string_view prefix) {
    if (prefix.empty()) {
        throw std::invalid_argument("Stable ID prefix cannot be empty.");
    }
    return std::string{prefix} + '-' + generateUuid();
}

void validateBackupConfig(const BackupConfig& config) {
    if (config.schemaVersion != 1) {
        throw std::invalid_argument("Unsupported backup configuration schema version.");
    }
    if (config.settings.debounceSeconds <= 0 || config.settings.largeFileThresholdBytes == 0 ||
        config.settings.projectSizeThresholdBytes == 0) {
        throw std::invalid_argument("Backup setting values must be positive.");
    }

    requireUniqueIds(config.manualSources, [](const ManualSource& source) -> const std::string& { return source.id; },
                     "manual source");
    requireUniqueIds(config.projectsRoots, [](const ProjectsRoot& root) -> const std::string& { return root.id; },
                     "Projects Root");
    requireUniqueIds(config.destinations, [](const Destination& destination) -> const std::string& {
        return destination.id;
    }, "destination");

    std::unordered_set<std::string> sourceIds;
    for (const ManualSource& source : config.manualSources) {
        if (source.path.empty()) {
            throw std::invalid_argument("Manual Source path cannot be empty.");
        }
        sourceIds.insert(source.id);
    }
    for (const ProjectsRoot& root : config.projectsRoots) {
        if (root.path.empty()) {
            throw std::invalid_argument("Projects Root path cannot be empty.");
        }
        if (!sourceIds.insert(root.id).second) {
            throw std::invalid_argument("Source IDs must be unique across Manual Sources and Projects Roots: " + root.id);
        }
    }

    std::unordered_set<std::string> destinationIds;
    for (const Destination& destination : config.destinations) {
        if (destination.name.empty() || destination.root.empty()) {
            throw std::invalid_argument("Destination name and root cannot be empty.");
        }
        if (destination.kind == DestinationKind::removable && destination.volumeSerial == 0) {
            throw std::invalid_argument("Removable Destination volume serial cannot be zero.");
        }
        destinationIds.insert(destination.id);
    }

    std::unordered_set<std::string> routeKeys;
    for (const BackupRoute& route : config.routes) {
        if (!sourceIds.contains(route.sourceId)) {
            throw std::invalid_argument("Backup Route references an unknown Source: " + route.sourceId);
        }
        if (!destinationIds.contains(route.destinationId)) {
            throw std::invalid_argument("Backup Route references an unknown Destination: " + route.destinationId);
        }
        if (!route.isMirrorEnabled && !route.areSnapshotsEnabled) {
            throw std::invalid_argument("Backup Route must enable Mirror, Snapshots, or both.");
        }
        if (route.snapshotPolicy.intervalHours <= 0 || route.snapshotPolicy.retainDaily < 0 ||
            route.snapshotPolicy.retainMonthly < 0) {
            throw std::invalid_argument("Snapshot policy values are invalid.");
        }
        const std::string key = route.sourceId + '\n' + route.destinationId;
        if (!routeKeys.insert(key).second) {
            throw std::invalid_argument("Only one Backup Route may exist for a Source and Destination pair.");
        }
    }
}

std::string serializeBackupConfig(const BackupConfig& config) {
    validateBackupConfig(config);
    Json json{
        {"schemaVersion", config.schemaVersion},
        {"manualSources", Json::array()},
        {"projectsRoots", Json::array()},
        {"destinations", Json::array()},
        {"routes", Json::array()},
        {"settings", toJson(config.settings)},
    };
    for (const ManualSource& source : config.manualSources) {
        json["manualSources"].push_back(toJson(source));
    }
    for (const ProjectsRoot& root : config.projectsRoots) {
        json["projectsRoots"].push_back(toJson(root));
    }
    for (const Destination& destination : config.destinations) {
        json["destinations"].push_back(toJson(destination));
    }
    for (const BackupRoute& route : config.routes) {
        json["routes"].push_back(toJson(route));
    }
    return json.dump(2) + '\n';
}

BackupConfig deserializeBackupConfig(std::string_view jsonText) {
    const Json json = Json::parse(jsonText);
    BackupConfig config;
    config.schemaVersion = json.value("schemaVersion", config.schemaVersion);
    for (const Json& source : json.value("manualSources", Json::array())) {
        config.manualSources.push_back(manualSourceFromJson(source));
    }
    for (const Json& root : json.value("projectsRoots", Json::array())) {
        config.projectsRoots.push_back(projectsRootFromJson(root));
    }
    for (const Json& destination : json.value("destinations", Json::array())) {
        config.destinations.push_back(destinationFromJson(destination));
    }
    for (const Json& route : json.value("routes", Json::array())) {
        config.routes.push_back(routeFromJson(route));
    }
    if (const auto settings = json.find("settings"); settings != json.end()) {
        config.settings = settingsFromJson(*settings);
    }
    validateBackupConfig(config);
    return config;
}
