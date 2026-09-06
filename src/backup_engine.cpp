#include "backup_engine.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <map>
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

[[nodiscard]] std::string pathToGenericUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.generic_u8string();
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

[[nodiscard]] const ProjectsRoot* findProjectsRoot(const BackupConfig& config, const std::string& id) {
    const auto root = std::ranges::find(config.projectsRoots, id, &ProjectsRoot::id);
    return root == config.projectsRoots.end() ? nullptr : &*root;
}

[[nodiscard]] std::filesystem::path destinationRoot(const Destination& destination,
                                                    const std::vector<ConnectedVolume>& connectedVolumes) {
    if (destination.kind == DestinationKind::path) {
        if (!std::filesystem::exists(destination.root)) {
            throw std::runtime_error("Destination folder is unavailable: " + pathToUtf8(destination.root));
        }
        return destination.root;
    }
    const auto volume = std::ranges::find(connectedVolumes, destination.volumeSerial, &ConnectedVolume::serial);
    if (volume == connectedVolumes.end()) {
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
    std::wstring command = L"tar.exe -a -c --options zip:compression-level=9 -f " + quoteArgument(archivePath) +
                           L" -C " + quoteArgument(parent) + L" " + quoteArgument(name);
    std::vector<wchar_t> writable(command.begin(), command.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, writable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &process) == FALSE) {
        throw std::runtime_error("Unable to start Windows tar for the ZIP archive.");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode{};
    const BOOL receivedCode = GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (receivedCode == FALSE || exitCode != 0) {
        throw std::runtime_error("ZIP archive creation failed.");
    }
}

void runProjectTarZip(const ProjectsSource& source, const std::filesystem::path& archivePath,
                      std::optional<std::uint64_t> maximumFileSizeBytes) {
    const ProjectContents contents = collectProjectContents(source, maximumFileSizeBytes);
    const std::filesystem::path listPath = std::filesystem::temp_directory_path() /
                                           (L"BackItUpTool-" + std::filesystem::path{generateUuid()}.wstring() +
                                            L".txt");
    try {
        std::ofstream list{listPath, std::ios::binary | std::ios::trunc};
        if (!list) {
            throw std::runtime_error("Unable to create the Project archive file list.");
        }
        for (const EligibleProjectFile& file : contents.files) {
            list << pathToGenericUtf8(source.path.filename() / file.relativePath) << '\n';
        }
        list.close();
        if (!list) {
            throw std::runtime_error("Unable to write the Project archive file list.");
        }

        std::wstring command = L"tar.exe -a -c --options zip:compression-level=9 -f " +
                               quoteArgument(archivePath) + L" -C " + quoteArgument(source.path.parent_path()) +
                               L" -T " + quoteArgument(listPath);
        std::vector<wchar_t> writable(command.begin(), command.end());
        writable.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, writable.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process) == FALSE) {
            throw std::runtime_error("Unable to start Windows tar for the Project ZIP archive.");
        }
        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exitCode{};
        const BOOL receivedCode = GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (receivedCode == FALSE || exitCode != 0) {
            throw std::runtime_error("Project ZIP archive creation failed.");
        }
    } catch (...) {
        std::error_code ignoredError;
        std::filesystem::remove(listPath, ignoredError);
        throw;
    }
    std::error_code ignoredError;
    std::filesystem::remove(listPath, ignoredError);
}

[[nodiscard]] std::string utcFilenameStamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%d_%H%M%S");
    return text.str();
}

void pruneArchives(std::string_view sourceId, std::string_view destinationId, StateStore& stateStore) {
    constexpr std::size_t kRetainDailyArchives = 30;
    constexpr std::size_t kRetainMonthlyArchives = 12;
    const std::vector<ArchiveRecord> records = stateStore.archiveRecords(sourceId, destinationId);
    std::set<std::string> keptDays;
    std::set<std::string> keptMonths;
    for (const ArchiveRecord& record : records) {
        const std::string day = record.createdUtc.substr(0, 10);
        const std::string month = record.createdUtc.substr(0, 7);
        bool keep = false;
        if (keptDays.size() < kRetainDailyArchives && keptDays.insert(day).second) {
            keep = true;
        } else if (keptMonths.size() < kRetainMonthlyArchives && keptMonths.insert(month).second) {
            keep = true;
        }
        if (keep) {
            continue;
        }
        std::error_code error;
        std::filesystem::remove(record.archivePath, error);
        if (!error || !std::filesystem::exists(record.archivePath)) {
            stateStore.removeArchiveRecord(record.id);
        }
    }
}

[[nodiscard]] bool shouldCopyFile(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(target, error) || error) {
        return true;
    }
    const std::uintmax_t sourceSize = std::filesystem::file_size(source, error);
    if (error) {
        throw std::runtime_error("Unable to read a Project source file size.");
    }
    const std::uintmax_t targetSize = std::filesystem::file_size(target, error);
    if (error || sourceSize != targetSize) {
        return true;
    }
    const auto sourceWriteTime = std::filesystem::last_write_time(source, error);
    if (error) {
        throw std::runtime_error("Unable to read a Project source timestamp.");
    }
    const auto targetWriteTime = std::filesystem::last_write_time(target, error);
    if (error) {
        return true;
    }
    const auto difference = sourceWriteTime > targetWriteTime ? sourceWriteTime - targetWriteTime
                                                               : targetWriteTime - sourceWriteTime;
    return difference > std::chrono::seconds{2};
}

void mirrorProjectContents(const ProjectsSource& source, const std::filesystem::path& destination,
                           std::optional<std::uint64_t> maximumFileSizeBytes) {
    const ProjectContents contents = collectProjectContents(source, maximumFileSizeBytes);
    std::filesystem::create_directories(destination);
    std::set<std::filesystem::path> eligiblePaths;
    for (const EligibleProjectFile& file : contents.files) {
        const std::filesystem::path normalizedRelativePath = file.relativePath.lexically_normal();
        eligiblePaths.insert(normalizedRelativePath);
        const std::filesystem::path target = destination / normalizedRelativePath;
        const std::filesystem::path sourcePath = source.path / normalizedRelativePath;
        if (!shouldCopyFile(sourcePath, target)) {
            continue;
        }
        std::filesystem::create_directories(target.parent_path());
        std::filesystem::copy_file(sourcePath, target,
                                   std::filesystem::copy_options::overwrite_existing);
        std::error_code timestampError;
        const auto sourceWriteTime = std::filesystem::last_write_time(sourcePath, timestampError);
        if (!timestampError) {
            std::filesystem::last_write_time(target, sourceWriteTime, timestampError);
        }
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

[[nodiscard]] std::optional<std::uint64_t> projectMaximumFileSize(const BackupPlan& plan,
                                                                  const StateStore& stateStore,
                                                                  std::uint64_t largeFileThresholdBytes) {
    if (!plan.isProjectsSource ||
        stateStore.projectBackupDecision(plan.sourceId) != ProjectBackupDecision::ignoreLargeFiles) {
        return std::nullopt;
    }
    return largeFileThresholdBytes;
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

std::vector<BackupPlan> BackupEngine::previewBackups(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
    BackupMode backupMode) const {
    const bool hasMatchingSource =
        std::ranges::any_of(config.manualSources, [&](const ManualSource& source) {
            return source.backupMode == backupMode;
        }) || std::ranges::any_of(config.projectsRoots, [&](const ProjectsRoot& root) {
            return root.backupMode == backupMode;
        });
    if (!hasMatchingSource) {
        return {};
    }

    std::vector<ConnectedVolume> connectedVolumes;
    try {
        connectedVolumes = findConnectedRemovableVolumes();
    } catch (const std::exception&) {
    }

    std::map<std::string, std::filesystem::path> availableDestinations;
    for (const Destination& destination : config.destinations) {
        try {
            availableDestinations.emplace(destination.id, destinationRoot(destination, connectedVolumes));
        } catch (const std::exception&) {
            // Work remains pending until Windows reports that the Destination is available.
        }
    }

    std::vector<BackupPlan> plans;
    for (const BackupRoute& route : config.routes) {
        const auto destination = std::ranges::find(config.destinations, route.destinationId, &Destination::id);
        const auto availableDestination = availableDestinations.find(route.destinationId);
        if (destination == config.destinations.end() || availableDestination == availableDestinations.end()) {
            continue;
        }
        const std::filesystem::path& availableDestinationRoot = availableDestination->second;
        const auto manualSource = std::ranges::find(config.manualSources, route.sourceId, &ManualSource::id);
        if (manualSource != config.manualSources.end()) {
            if (manualSource->backupMode != backupMode) {
                continue;
            }
            if (!std::filesystem::exists(manualSource->path)) {
                continue;
            }
            const std::filesystem::path relativePath = buildMirrorRelativePath(manualSource->path);
            const std::filesystem::path target =
                backupMode == BackupMode::mirror
                    ? availableDestinationRoot / L"BackItUpTool" / L"Mirrors" / relativePath
                    : availableDestinationRoot / L"BackItUpTool" / L"Zipped" / relativePath.parent_path();
            const std::filesystem::path sourceRoot = manualSource->kind == ManualSourceKind::folder
                                                         ? manualSource->path
                                                         : manualSource->path.parent_path();
            if (isSameOrInside(target, sourceRoot) || isSameOrInside(sourceRoot, target)) {
                throw std::runtime_error("Refusing a backup whose Source and Destination overlap.");
            }
            plans.push_back(BackupPlan{route.sourceId, manualSource->id, destination->id, manualSource->path, target,
                                       manualSource->kind, false});
            continue;
        }

        const ProjectsRoot* projectsRoot = findProjectsRoot(config, route.sourceId);
        if (projectsRoot == nullptr || projectsRoot->backupMode != backupMode) {
            continue;
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
            const std::filesystem::path relativePath = buildMirrorRelativePath(source.path);
            const std::filesystem::path target =
                backupMode == BackupMode::mirror
                    ? availableDestinationRoot / L"BackItUpTool" / L"Mirrors" / relativePath
                    : availableDestinationRoot / L"BackItUpTool" / L"Zipped" / relativePath.parent_path();
            if (isSameOrInside(target, source.path) || isSameOrInside(source.path, target)) {
                throw std::runtime_error("Refusing a backup whose Source and Destination overlap.");
            }
            plans.push_back(BackupPlan{route.sourceId, source.id, destination->id, source.path, target,
                                       ManualSourceKind::folder, true});
        }
    }
    return plans;
}

std::vector<BackupPlan> BackupEngine::previewMirrors(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources) const {
    return previewBackups(config, projectsSources, BackupMode::mirror);
}

std::vector<BackupPlan> BackupEngine::previewPendingBackups(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
    const StateStore& stateStore, BackupMode backupMode) const {
    std::vector<BackupPlan> pendingPlans;
    for (BackupPlan& plan : previewBackups(config, projectsSources, backupMode)) {
        const std::optional<RouteRuntimeState> state = stateStore.routeState(plan.sourceId, plan.destinationId);
        if (state.has_value() && state->isDirty) {
            pendingPlans.push_back(std::move(plan));
        }
    }
    return pendingPlans;
}

std::vector<BackupPlan> BackupEngine::previewPendingMirrors(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
    const StateStore& stateStore) const {
    return previewPendingBackups(config, projectsSources, stateStore, BackupMode::mirror);
}

std::vector<BackupPlan> BackupEngine::previewPendingArchives(
    const BackupConfig& config, const std::vector<ConfiguredProjectsSource>& projectsSources,
    const StateStore& stateStore) const {
    return previewPendingBackups(config, projectsSources, stateStore, BackupMode::zipped);
}

std::vector<SizeWarning> BackupEngine::findSizeWarnings(const BackupConfig& config,
                                                        const std::vector<BackupPlan>& plans,
                                                        const StateStore& stateStore) const {
    std::vector<SizeWarning> warnings;
    std::map<std::string, ProjectPreflight> preflights;
    std::set<std::string> warnedSources;
    for (const BackupPlan& plan : plans) {
        if (plan.isProjectsSource) {
            const std::optional<ProjectBackupDecision> decision = stateStore.projectBackupDecision(plan.sourceId);
            if (decision.has_value() || !warnedSources.insert(plan.sourceId).second) {
                continue;
            }
            const auto [preflight, inserted] = preflights.try_emplace(plan.sourceId);
            if (inserted) {
                preflight->second = scanProject(ProjectsSource{plan.sourceId, plan.source}, config.settings);
            }
            if (!preflight->second.requiresApproval()) {
                continue;
            }
            warnings.push_back(SizeWarning{plan.sourceId, plan.source, preflight->second.eligibleSizeBytes,
                                           preflight->second.largeFiles});
            continue;
        }
    }
    return warnings;
}

BackupRunSummary BackupEngine::runMirrors(const std::vector<BackupPlan>& plans, StateStore& stateStore,
                                          const std::filesystem::path& logDirectory,
                                          std::uint64_t largeFileThresholdBytes) const {
    std::filesystem::create_directories(logDirectory);
    std::map<std::string, std::vector<BackupPlan>> plansByDestination;
    for (const BackupPlan& plan : plans) {
        plansByDestination[plan.destinationId].push_back(plan);
    }

    const auto runDestination = [&stateStore, &logDirectory,
                                 largeFileThresholdBytes](const std::vector<BackupPlan>& destinationPlans) {
        BackupRunSummary summary;
        for (const BackupPlan& plan : destinationPlans) {
            const std::string attemptTime = utcNow();
            stateStore.beginRouteAttempt(plan.sourceId, plan.destinationId, attemptTime);
            try {
                DWORD exitCode = 1;
                if (plan.isProjectsSource) {
                    mirrorProjectContents(ProjectsSource{plan.sourceId, plan.source}, plan.destination,
                                          projectMaximumFileSize(plan, stateStore, largeFileThresholdBytes));
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
                    const std::string message = "Mirror failed for " + pathToUtf8(plan.source) + " (robocopy " +
                                                std::to_string(exitCode) + ").";
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
            } catch (const std::exception& error) {
                const std::string message = "Mirror failed for " + pathToUtf8(plan.source) + ": " + error.what();
                stateStore.completeRouteFailure(plan.sourceId, plan.destinationId, message);
                stateStore.appendActivity(attemptTime, "error", message, plan.sourceId, plan.destinationId);
                summary.messages.push_back(message);
                ++summary.failed;
            }
        }
        return summary;
    };

    std::vector<std::future<BackupRunSummary>> runs;
    runs.reserve(plansByDestination.size());
    for (auto& [destinationId, destinationPlans] : plansByDestination) {
        static_cast<void>(destinationId);
        runs.push_back(std::async(std::launch::async, runDestination, std::move(destinationPlans)));
    }

    BackupRunSummary summary;
    for (std::future<BackupRunSummary>& run : runs) {
        BackupRunSummary destinationSummary = run.get();
        summary.succeeded += destinationSummary.succeeded;
        summary.failed += destinationSummary.failed;
        summary.messages.insert(summary.messages.end(),
                                std::make_move_iterator(destinationSummary.messages.begin()),
                                std::make_move_iterator(destinationSummary.messages.end()));
    }
    return summary;
}

BackupRunSummary BackupEngine::runArchives(const std::vector<BackupPlan>& plans, StateStore& stateStore,
                                           std::uint64_t largeFileThresholdBytes) const {
    BackupRunSummary summary;
    for (const BackupPlan& plan : plans) {
        const std::string attemptTime = utcNow();
        stateStore.beginRouteAttempt(plan.sourceId, plan.destinationId, attemptTime);
        try {
            createArchive(plan, stateStore, largeFileThresholdBytes);
            stateStore.completeRouteSuccess(plan.sourceId, plan.destinationId, utcNow());
            ++summary.succeeded;
        } catch (const std::exception& error) {
            const std::string message = "Zipped backup failed for " + pathToUtf8(plan.source) + ": " + error.what();
            stateStore.completeRouteFailure(plan.sourceId, plan.destinationId, message);
            stateStore.appendActivity(attemptTime, "error", message, plan.sourceId, plan.destinationId);
            summary.messages.push_back(message);
            ++summary.failed;
        }
    }
    return summary;
}

void BackupEngine::createArchive(const BackupPlan& plan, StateStore& stateStore,
                                 std::uint64_t largeFileThresholdBytes) const {
    std::filesystem::create_directories(plan.destination);
    std::filesystem::path archivePath =
        plan.destination / (plan.source.filename().wstring() + L"_" +
                            std::filesystem::path{utcFilenameStamp()}.wstring() + L".zip");
    if (std::filesystem::exists(archivePath)) {
        archivePath = plan.destination /
                      (plan.source.filename().wstring() + L"_" +
                       std::filesystem::path{utcFilenameStamp()}.wstring() + L"_" +
                       std::filesystem::path{generateUuid()}.wstring() + L".zip");
    }
    if (plan.isProjectsSource) {
        runProjectTarZip(ProjectsSource{plan.sourceId, plan.source}, archivePath,
                         projectMaximumFileSize(plan, stateStore, largeFileThresholdBytes));
    } else {
        runTarZip(plan.source, archivePath);
    }
    stateStore.recordArchive(plan.sourceId, plan.destinationId, utcNow(), archivePath,
                             std::filesystem::file_size(archivePath));
    pruneArchives(plan.sourceId, plan.destinationId, stateStore);
    stateStore.appendActivity(utcNow(), "info", "Zipped backup created at " + pathToUtf8(archivePath) + ".",
                              plan.sourceId, plan.destinationId);
}
