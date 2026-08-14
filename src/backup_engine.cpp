#include "backup_engine.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "connected_volume.hpp"
#include "projects_scanner.hpp"
#include "state_store.hpp"

namespace {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    std::string text;
    text.reserve(bytes.size());
    for (const char8_t byte : bytes) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

[[nodiscard]] std::string utcNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return text.str();
}

[[nodiscard]] std::wstring quoteArgument(const std::filesystem::path& path) {
    return L"\"" + path.native() + L"\"";
}

[[nodiscard]] bool isSameOrInside(const std::filesystem::path& candidate, const std::filesystem::path& parent) {
    std::wstring candidateText = std::filesystem::absolute(candidate).lexically_normal().native();
    std::wstring parentText = std::filesystem::absolute(parent).lexically_normal().native();
    std::replace(candidateText.begin(), candidateText.end(), L'/', L'\\');
    std::replace(parentText.begin(), parentText.end(), L'/', L'\\');
    std::transform(candidateText.begin(), candidateText.end(), candidateText.begin(), ::towlower);
    std::transform(parentText.begin(), parentText.end(), parentText.begin(), ::towlower);
    if (!parentText.empty() && parentText.back() != L'\\') {
        parentText.push_back(L'\\');
    }
    return candidateText == parentText.substr(0, parentText.size() - 1) || candidateText.starts_with(parentText);
}

[[nodiscard]] const ManualSource& findManualSource(const BackupConfig& config, const std::string& id) {
    const auto source = std::ranges::find(config.manualSources, id, &ManualSource::id);
    if (source == config.manualSources.end()) {
        throw std::runtime_error("This initial mirror only supports Manual Sources.");
    }
    return *source;
}

[[nodiscard]] const ProjectsRoot* findProjectsRoot(const BackupConfig& config, const std::string& id) {
    const auto root = std::ranges::find(config.projectsRoots, id, &ProjectsRoot::id);
    return root == config.projectsRoots.end() ? nullptr : &*root;
}

[[nodiscard]] std::filesystem::path destinationRoot(const Destination& destination) {
    if (destination.kind == DestinationKind::path) {
        if (!std::filesystem::exists(destination.root)) {
            throw std::runtime_error("Destination folder is unavailable: " + pathToUtf8(destination.root));
        }
        return destination.root;
    }
    const auto volume = findConnectedRemovableVolume(destination.volumeSerial);
    if (!volume.has_value()) {
        throw std::runtime_error("Removable Destination is unavailable: " + destination.name);
    }
    return volume->root;
}

[[nodiscard]] DWORD runRobocopy(const std::filesystem::path& source, const std::filesystem::path& target,
                                 const std::filesystem::path& logPath) {
    std::wstring command = L"robocopy.exe " + quoteArgument(source) + L" " + quoteArgument(target);
    command += L" /MIR /XJ /FFT /R:1 /W:2 /NFL /NDL /NP /NJH /NJS /LOG+:" + quoteArgument(logPath);
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, writable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process) == FALSE) {
        throw std::runtime_error("Unable to start robocopy.");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode{};
    const BOOL receivedCode = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (receivedCode == FALSE) {
        throw std::runtime_error("Unable to read robocopy's result.");
    }
    return exitCode;
}

void runTarZip(const std::filesystem::path& source, const std::filesystem::path& archivePath) {
    const std::filesystem::path parent = source.parent_path();
    const std::filesystem::path name = source.filename();
    std::wstring command = L"tar.exe -a -c -f " + quoteArgument(archivePath) + L" -C " + quoteArgument(parent) +
                           L" " + quoteArgument(name);
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, writable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process) == FALSE) {
        throw std::runtime_error("Unable to start Windows tar for the ZIP snapshot.");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode{};
    const BOOL receivedCode = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (receivedCode == FALSE || exitCode != 0) {
        throw std::runtime_error("ZIP snapshot creation failed.");
    }
}

[[nodiscard]] std::string utcFilenameStamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%d_%H%M%S");
    return text.str();
}

[[nodiscard]] bool isSnapshotDue(const std::optional<std::string>& latestSnapshot, int intervalHours) {
    if (!latestSnapshot.has_value()) {
        return true;
    }
    std::tm timestamp{};
    std::istringstream input{*latestSnapshot};
    input >> std::get_time(&timestamp, "%Y-%m-%dT%H:%M:%SZ");
    if (input.fail()) {
        return true;
    }
    const std::time_t previous = _mkgmtime(&timestamp);
    const std::time_t current = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return std::difftime(current, previous) >= static_cast<double>(intervalHours) * 60.0 * 60.0;
}

void pruneSnapshots(const BackupRoute& route, std::string_view sourceId, std::string_view destinationId,
                    StateStore& stateStore) {
    const std::vector<SnapshotRecord> records = stateStore.snapshotRecords(sourceId, destinationId);
    std::set<std::string> keptDays;
    std::set<std::string> keptMonths;
    for (const SnapshotRecord& record : records) {
        const std::string day = record.createdUtc.substr(0, 10);
        const std::string month = record.createdUtc.substr(0, 7);
        bool keep = false;
        if (keptDays.size() < static_cast<std::size_t>(route.snapshotPolicy.retainDaily) && keptDays.insert(day).second) {
            keep = true;
        } else if (keptMonths.size() < static_cast<std::size_t>(route.snapshotPolicy.retainMonthly) &&
                   keptMonths.insert(month).second) {
            keep = true;
        }
        if (keep) {
            continue;
        }
        std::error_code error;
        std::filesystem::remove(record.archivePath, error);
        if (!error || !std::filesystem::exists(record.archivePath)) {
            stateStore.removeSnapshotRecord(record.id);
        }
    }
}

void mirrorProjectContents(const ProjectsSource& source, const std::filesystem::path& destination) {
    const ProjectContents contents = collectProjectContents(source);
    std::filesystem::create_directories(destination);
    std::set<std::filesystem::path> eligiblePaths;
    for (const EligibleProjectFile& file : contents.files) {
        const std::filesystem::path normalizedRelativePath = file.relativePath.lexically_normal();
        eligiblePaths.insert(normalizedRelativePath);
        const std::filesystem::path target = destination / normalizedRelativePath;
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(source.path / normalizedRelativePath, target,
                                   std::filesystem::copy_options::overwrite_existing);
    }

    std::error_code iterationError;
    for (std::filesystem::recursive_directory_iterator iterator{destination, iterationError}, end; iterator != end;
         iterator.increment(iterationError)) {
        if (iterationError) {
            throw std::runtime_error("Unable to reconcile the Project mirror.");
        }
        std::error_code typeError;
        if (!iterator->is_regular_file(typeError) || typeError) {
            continue;
        }
        const std::filesystem::path relativePath = iterator->path().lexically_relative(destination).lexically_normal();
        if (!eligiblePaths.contains(relativePath)) {
            std::filesystem::remove(iterator->path());
        }
    }
    if (iterationError) {
        throw std::runtime_error("Unable to reconcile the Project mirror.");
    }

    std::vector<std::filesystem::path> directories;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{destination}) {
        if (entry.is_directory()) {
            directories.push_back(entry.path());
        }
    }
    std::ranges::sort(directories, [](const auto& left, const auto& right) {
        return std::ranges::distance(left) > std::ranges::distance(right);
    });
    for (const std::filesystem::path& directory : directories) {
        std::error_code ignoredError;
        if (std::filesystem::is_empty(directory, ignoredError) && !ignoredError) {
            std::filesystem::remove(directory, ignoredError);
        }
    }
}

}  // namespace

std::filesystem::path buildMirrorRelativePath(const std::filesystem::path& sourcePath) {
    const std::filesystem::path absolutePath = std::filesystem::absolute(sourcePath).lexically_normal();
    const std::wstring rootName = absolutePath.root_name().native();
    if (rootName.size() == 2 && rootName[1] == L':') {
        return std::filesystem::path{rootName.substr(0, 1)} / absolutePath.relative_path();
    }
    if (rootName.starts_with(L"\\\\") && rootName.size() > 2) {
        return std::filesystem::path{L"UNC"} / std::filesystem::path{rootName.substr(2)} /
               absolutePath.relative_path();
    }
    throw std::runtime_error("Source must use an absolute drive or network path.");
}

std::vector<MirrorPlan> BackupEngine::previewMirrors(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources) const {
    std::vector<MirrorPlan> plans;
    for (const BackupRoute& route : config.routes) {
        if (!route.isMirrorEnabled) {
            continue;
        }
        const auto destination = std::ranges::find(config.destinations, route.destinationId, &Destination::id);
        if (destination == config.destinations.end()) {
            throw std::runtime_error("Route has an unavailable Destination.");
        }
        const std::filesystem::path availableDestinationRoot = destinationRoot(*destination);
        const auto manualSource = std::ranges::find(config.manualSources, route.sourceId, &ManualSource::id);
        if (manualSource != config.manualSources.end()) {
            if (!std::filesystem::exists(manualSource->path)) {
                throw std::runtime_error("Source is unavailable: " + pathToUtf8(manualSource->path));
            }
            const std::filesystem::path target = availableDestinationRoot / L"BackItUpTool" / L"Mirrors" /
                                                 buildMirrorRelativePath(manualSource->path);
            const std::filesystem::path sourceRoot = manualSource->kind == ManualSourceKind::folder
                                                         ? manualSource->path
                                                         : manualSource->path.parent_path();
            if (isSameOrInside(target, sourceRoot) || isSameOrInside(sourceRoot, target)) {
                throw std::runtime_error("Refusing a mirror whose Source and Destination overlap.");
            }
            plans.push_back(MirrorPlan{route.sourceId, manualSource->id, destination->id, manualSource->path, target,
                                       manualSource->kind, false});
            continue;
        }

        if (findProjectsRoot(config, route.sourceId) == nullptr) {
            throw std::runtime_error("Route has an unavailable Source.");
        }
        for (const ConfiguredProjectsSource& configuredSource : projectsSources) {
            if (configuredSource.rootId != route.sourceId) {
                continue;
            }
            const ProjectsSource& source = configuredSource.source;
            if (!std::filesystem::is_directory(source.path) ||
                std::filesystem::exists(source.path / L".git")) {
                continue;
            }
            const std::filesystem::path target = availableDestinationRoot / L"BackItUpTool" / L"Mirrors" /
                                                 buildMirrorRelativePath(source.path);
            if (isSameOrInside(target, source.path) || isSameOrInside(source.path, target)) {
                throw std::runtime_error("Refusing a mirror whose Source and Destination overlap.");
            }
            plans.push_back(MirrorPlan{route.sourceId, source.id, destination->id, source.path, target,
                                       ManualSourceKind::folder, true});
        }
    }
    return plans;
}

std::vector<MirrorPlan> BackupEngine::previewPendingMirrors(const BackupConfig& config,
                                                            const std::vector<ConfiguredProjectsSource>& projectsSources,
                                                            const StateStore& stateStore) const {
    std::vector<MirrorPlan> pendingPlans;
    for (const BackupRoute& route : config.routes) {
        if (!route.isMirrorEnabled) {
            continue;
        }
        BackupConfig singleRouteConfig = config;
        singleRouteConfig.routes = {route};
        try {
            std::vector<MirrorPlan> routePlans = previewMirrors(singleRouteConfig, projectsSources);
            for (MirrorPlan& plan : routePlans) {
                const std::optional<RouteRuntimeState> state =
                    stateStore.routeState(plan.sourceId, plan.destinationId);
                if (state.has_value() && state->isDirty) {
                    pendingPlans.push_back(std::move(plan));
                }
            }
        } catch (const std::exception&) {
            // Pending work stays in SQLite until its Source and Destination are available again.
        }
    }
    return pendingPlans;
}

std::vector<SizeWarning> BackupEngine::findSizeWarnings(const BackupConfig& config,
                                                        const std::vector<MirrorPlan>& plans,
                                                        const StateStore& stateStore) const {
    std::vector<SizeWarning> warnings;
    for (const MirrorPlan& plan : plans) {
        if (plan.isProjectsSource) {
            const ProjectsSource source{plan.sourceId, plan.source};
            const ProjectPreflight preflight = scanProject(source, config.settings);
            if (!preflight.requiresSizeApproval(stateStore.hasPermanentSizeApproval(plan.sourceId))) {
                continue;
            }
            warnings.push_back(SizeWarning{plan.sourceId, plan.destinationId, plan.source,
                                           preflight.eligibleSizeBytes, preflight.largeFiles, true});
            continue;
        }
    }
    return warnings;
}

BackupRunSummary BackupEngine::runMirrors(const BackupConfig& config, const std::vector<MirrorPlan>& plans,
                                          StateStore& stateStore, const std::filesystem::path& logDirectory) const {
    std::filesystem::create_directories(logDirectory);
    BackupRunSummary summary;
    for (const MirrorPlan& plan : plans) {
        const std::string attemptTime = utcNow();
        stateStore.beginRouteAttempt(plan.sourceId, plan.destinationId, attemptTime);
        try {
            DWORD exitCode = 1;
            if (plan.isProjectsSource) {
                mirrorProjectContents(ProjectsSource{plan.sourceId, plan.source}, plan.destination);
            } else if (plan.sourceKind == ManualSourceKind::file) {
                std::filesystem::create_directories(plan.destination.parent_path());
                std::filesystem::copy_file(plan.source, plan.destination,
                                           std::filesystem::copy_options::overwrite_existing);
            } else {
                std::filesystem::create_directories(plan.destination);
                exitCode = runRobocopy(plan.source, plan.destination,
                                       logDirectory / (plan.sourceId + "-" + plan.destinationId + ".log"));
            }
            if (exitCode >= 8) {
                const std::string message = "Mirror failed for " + pathToUtf8(plan.source) + " (robocopy " + std::to_string(exitCode) + ").";
                stateStore.completeRouteFailure(plan.sourceId, plan.destinationId, message);
                stateStore.appendActivity(attemptTime, "error", message, plan.sourceId, plan.destinationId);
                summary.messages.push_back(message);
                ++summary.failed;
                continue;
            }
            const std::string message = "Mirror completed for " + pathToUtf8(plan.source) + ".";
            stateStore.completeRouteSuccess(plan.sourceId, plan.destinationId, utcNow());
            stateStore.appendActivity(attemptTime, "info", message, plan.sourceId, plan.destinationId);
            summary.messages.push_back(message);
            ++summary.succeeded;
            const auto route = std::ranges::find_if(config.routes, [&](const BackupRoute& candidate) {
                return candidate.sourceId == plan.routeSourceId && candidate.destinationId == plan.destinationId;
            });
            const auto destination = std::ranges::find(config.destinations, plan.destinationId, &Destination::id);
            if (route != config.routes.end() && destination != config.destinations.end() && route->areSnapshotsEnabled) {
                try {
                    createDueSnapshot(*route, plan, *destination, stateStore);
                } catch (const std::exception& error) {
                    const std::string snapshotError = "Snapshot failed for " + pathToUtf8(plan.source) + ": " + error.what();
                    stateStore.appendActivity(utcNow(), "error", snapshotError, plan.sourceId, plan.destinationId);
                    summary.messages.push_back(snapshotError);
                    ++summary.failed;
                }
            }
        } catch (const std::exception& error) {
            const std::string message = "Mirror failed for " + pathToUtf8(plan.source) + ": " + error.what();
            stateStore.completeRouteFailure(plan.sourceId, plan.destinationId, message);
            stateStore.appendActivity(attemptTime, "error", message, plan.sourceId, plan.destinationId);
            summary.messages.push_back(message);
            ++summary.failed;
        }
    }
    return summary;
}

void BackupEngine::createDueSnapshot(const BackupRoute& route, const MirrorPlan& plan,
                                     const Destination& destination, StateStore& stateStore) const {
    if (!isSnapshotDue(stateStore.latestSnapshotUtc(plan.sourceId, plan.destinationId),
                       route.snapshotPolicy.intervalHours)) {
        return;
    }
    const std::filesystem::path snapshotFolder = destinationRoot(destination) / L"BackItUpTool" / L"Snapshots" /
                                                 buildMirrorRelativePath(plan.source).parent_path();
    std::filesystem::create_directories(snapshotFolder);
    const std::filesystem::path archivePath = snapshotFolder /
                                              (plan.source.filename().wstring() + L"_" +
                                               std::filesystem::path{utcFilenameStamp()}.wstring() + L".zip");
    runTarZip(plan.isProjectsSource ? plan.destination : plan.source, archivePath);
    stateStore.recordSnapshot(plan.sourceId, plan.destinationId, utcNow(), archivePath,
                              std::filesystem::file_size(archivePath));
    pruneSnapshots(route, plan.sourceId, plan.destinationId, stateStore);
    stateStore.appendActivity(utcNow(), "info", "Snapshot created at " + pathToUtf8(archivePath) + ".",
                              plan.sourceId, plan.destinationId);
}
