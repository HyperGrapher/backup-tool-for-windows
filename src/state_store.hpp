#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

enum class RouteStatus {
    pending,
    running,
    synced,
    error,
};

enum class ProjectBackupDecision {
    alwaysAllow,
    ignorePermanently,
};

struct RouteRuntimeState {
    std::string sourceId;
    std::string destinationId;
    RouteStatus status{RouteStatus::pending};
    bool isDirty{true};
    std::optional<std::string> lastAttemptUtc;
    std::optional<std::string> lastSuccessUtc;
    std::optional<std::string> lastError;

    bool operator==(const RouteRuntimeState&) const = default;
};

struct ActivityRecord {
    std::int64_t id{};
    std::string occurredUtc;
    std::string severity;
    std::optional<std::string> sourceId;
    std::optional<std::string> destinationId;
    std::string message;

    bool operator==(const ActivityRecord&) const = default;
};

struct SnapshotRecord {
    std::int64_t id{};
    std::string sourceId;
    std::string destinationId;
    std::string createdUtc;
    std::filesystem::path archivePath;
    std::uintmax_t archiveBytes{};
};

class StateStore final {
public:
    explicit StateStore(const std::filesystem::path& path);
    ~StateStore() = default;

    StateStore(const StateStore&) = delete;
    StateStore& operator=(const StateStore&) = delete;
    StateStore(StateStore&&) = delete;
    StateStore& operator=(StateStore&&) = delete;

    [[nodiscard]] std::optional<RouteRuntimeState> routeState(std::string_view sourceId,
                                                               std::string_view destinationId) const;
    [[nodiscard]] std::vector<RouteRuntimeState> routeStates() const;
    void setRouteState(const RouteRuntimeState& state);
    void markRouteDirty(std::string_view sourceId, std::string_view destinationId);
    void recoverInterruptedRoutes();
    void beginRouteAttempt(std::string_view sourceId, std::string_view destinationId, std::string_view attemptUtc);
    void completeRouteSuccess(std::string_view sourceId, std::string_view destinationId,
                              std::string_view successUtc);
    void completeRouteFailure(std::string_view sourceId, std::string_view destinationId, std::string_view error);

    [[nodiscard]] std::optional<std::string> latestSnapshotUtc(std::string_view sourceId,
                                                                std::string_view destinationId) const;
    [[nodiscard]] std::optional<std::string> latestSnapshotUtc() const;
    [[nodiscard]] std::vector<SnapshotRecord> snapshotRecords(std::string_view sourceId,
                                                               std::string_view destinationId) const;
    void recordSnapshot(std::string_view sourceId, std::string_view destinationId, std::string_view createdUtc,
                        const std::filesystem::path& archivePath, std::uintmax_t archiveBytes);
    void removeSnapshotRecord(std::int64_t id);

    void appendActivity(std::string_view occurredUtc, std::string_view severity, std::string_view message,
                        std::optional<std::string_view> sourceId = std::nullopt,
                        std::optional<std::string_view> destinationId = std::nullopt);
    [[nodiscard]] std::vector<ActivityRecord> recentActivity(std::size_t limit) const;

    [[nodiscard]] std::optional<ProjectBackupDecision> projectBackupDecision(std::string_view projectId) const;
    void setProjectBackupDecision(std::string_view projectId, ProjectBackupDecision decision,
                                  std::string_view decidedUtc);
    void clearRoutePending(std::string_view sourceId, std::string_view destinationId);

private:
    struct SQLiteCloser {
        void operator()(sqlite3* database) const noexcept;
    };

    using DatabaseHandle = std::unique_ptr<sqlite3, SQLiteCloser>;

    void execute(std::string_view sql) const;

    DatabaseHandle database_;
};
