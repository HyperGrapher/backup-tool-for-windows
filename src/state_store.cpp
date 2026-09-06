#include "state_store.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[nodiscard]] Statement prepare(sqlite3* database, std::string_view sql) {
    sqlite3_stmt* rawStatement = nullptr;
    const std::string sqlText{sql};
    if (sqlite3_prepare_v2(database, sqlText.c_str(), -1, &rawStatement, nullptr) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
    return Statement{rawStatement, sqlite3_finalize};
}

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

void bindText(sqlite3* database, sqlite3_stmt* statement, int index, std::string_view value) {
    if (sqlite3_bind_text(statement, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
}

void bindOptionalText(sqlite3* database, sqlite3_stmt* statement, int index,
                      const std::optional<std::string>& value) {
    if (value.has_value()) {
        bindText(database, statement, index, *value);
        return;
    }
    if (sqlite3_bind_null(statement, index) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
}

void bindOptionalView(sqlite3* database, sqlite3_stmt* statement, int index,
                      const std::optional<std::string_view>& value) {
    if (value.has_value()) {
        bindText(database, statement, index, *value);
        return;
    }
    if (sqlite3_bind_null(statement, index) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database));
    }
}

[[nodiscard]] std::optional<std::string> optionalColumnText(sqlite3_stmt* statement, int index) {
    if (sqlite3_column_type(statement, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    const auto* text = sqlite3_column_text(statement, index);
    return text == nullptr ? std::string{} : std::string{reinterpret_cast<const char*>(text)};
}

[[nodiscard]] std::string requiredColumnText(sqlite3_stmt* statement, int index) {
    const auto* text = sqlite3_column_text(statement, index);
    if (text == nullptr) {
        throw std::runtime_error("State database contains an unexpected null value.");
    }
    return std::string{reinterpret_cast<const char*>(text)};
}

[[nodiscard]] std::string_view toString(RouteStatus status) {
    switch (status) {
    case RouteStatus::pending:
        return "pending";
    case RouteStatus::running:
        return "running";
    case RouteStatus::synced:
        return "synced";
    case RouteStatus::error:
        return "error";
    }
    throw std::invalid_argument("Unknown route status.");
}

[[nodiscard]] RouteStatus routeStatusFromString(std::string_view value) {
    if (value == "pending") {
        return RouteStatus::pending;
    }
    if (value == "running") {
        return RouteStatus::running;
    }
    if (value == "synced") {
        return RouteStatus::synced;
    }
    if (value == "error") {
        return RouteStatus::error;
    }
    throw std::runtime_error("State database contains an unknown route status.");
}

void requireIdentifier(std::string_view value, std::string_view description) {
    if (value.empty()) {
        throw std::invalid_argument(std::string{description} + " cannot be empty.");
    }
}

}  // namespace

void StateStore::SQLiteCloser::operator()(sqlite3* database) const noexcept {
    if (database != nullptr) {
        sqlite3_close(database);
    }
}

StateStore::StateStore(const std::filesystem::path& path) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    sqlite3* rawDatabase = nullptr;
    const std::string pathText = pathToUtf8(path);
    const int result = sqlite3_open_v2(
        pathText.c_str(),
        &rawDatabase,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    database_.reset(rawDatabase);
    if (result != SQLITE_OK) {
        throw std::runtime_error(database_ != nullptr ? sqlite3_errmsg(database_.get()) : "Unable to open state database");
    }

    execute("PRAGMA journal_mode = WAL;");
    execute("PRAGMA synchronous = FULL;");
    execute(
        "CREATE TABLE IF NOT EXISTS route_state ("
        "source_id TEXT NOT NULL,"
        "destination_id TEXT NOT NULL,"
        "status TEXT NOT NULL,"
        "is_dirty INTEGER NOT NULL CHECK(is_dirty IN (0, 1)),"
        "last_attempt_utc TEXT,"
        "last_success_utc TEXT,"
        "last_error TEXT,"
        "PRIMARY KEY(source_id, destination_id)"
        ");");
    execute(
        "CREATE TABLE IF NOT EXISTS activity_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "occurred_utc TEXT NOT NULL,"
        "severity TEXT NOT NULL,"
        "source_id TEXT,"
        "destination_id TEXT,"
        "message TEXT NOT NULL"
        ");");
    execute("CREATE INDEX IF NOT EXISTS activity_history_recent ON activity_history(id DESC);");
    execute(
        "CREATE TABLE IF NOT EXISTS snapshot_history ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "source_id TEXT NOT NULL,"
        "destination_id TEXT NOT NULL,"
        "created_utc TEXT NOT NULL,"
        "archive_path TEXT NOT NULL,"
        "archive_bytes INTEGER NOT NULL"
        ");");
    execute("CREATE INDEX IF NOT EXISTS snapshot_history_route_recent "
            "ON snapshot_history(source_id, destination_id, created_utc DESC);");
    execute("DROP TABLE IF EXISTS project_size_approval;");
    execute(
        "CREATE TABLE IF NOT EXISTS project_backup_decision ("
        "project_id TEXT PRIMARY KEY,"
        "decision TEXT NOT NULL CHECK(decision IN ('allow', 'ignore')) ,"
        "decided_utc TEXT NOT NULL"
        ");");
}

std::optional<RouteRuntimeState> StateStore::routeState(std::string_view sourceId,
                                                        std::string_view destinationId) const {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    auto statement = prepare(
        database_.get(),
        "SELECT status, is_dirty, last_attempt_utc, last_success_utc, last_error "
        "FROM route_state WHERE source_id = ?1 AND destination_id = ?2;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);

    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }

    return RouteRuntimeState{
        std::string{sourceId},
        std::string{destinationId},
        routeStatusFromString(requiredColumnText(statement.get(), 0)),
        sqlite3_column_int(statement.get(), 1) != 0,
        optionalColumnText(statement.get(), 2),
        optionalColumnText(statement.get(), 3),
        optionalColumnText(statement.get(), 4),
    };
}

std::vector<RouteRuntimeState> StateStore::routeStates() const {
    auto statement = prepare(
        database_.get(),
        "SELECT source_id, destination_id, status, is_dirty, last_attempt_utc, last_success_utc, last_error "
        "FROM route_state ORDER BY source_id, destination_id;");
    std::vector<RouteRuntimeState> states;
    while (true) {
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            return states;
        }
        if (result != SQLITE_ROW) {
            throw std::runtime_error(sqlite3_errmsg(database_.get()));
        }
        states.push_back(RouteRuntimeState{
            requiredColumnText(statement.get(), 0),
            requiredColumnText(statement.get(), 1),
            routeStatusFromString(requiredColumnText(statement.get(), 2)),
            sqlite3_column_int(statement.get(), 3) != 0,
            optionalColumnText(statement.get(), 4),
            optionalColumnText(statement.get(), 5),
            optionalColumnText(statement.get(), 6),
        });
    }
}

void StateStore::setRouteState(const RouteRuntimeState& state) {
    requireIdentifier(state.sourceId, "Source ID");
    requireIdentifier(state.destinationId, "Destination ID");
    auto statement = prepare(
        database_.get(),
        "INSERT INTO route_state(source_id, destination_id, status, is_dirty, last_attempt_utc, last_success_utc, "
        "last_error) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7) "
        "ON CONFLICT(source_id, destination_id) DO UPDATE SET "
        "status = excluded.status, is_dirty = excluded.is_dirty, last_attempt_utc = excluded.last_attempt_utc, "
        "last_success_utc = excluded.last_success_utc, last_error = excluded.last_error;");
    bindText(database_.get(), statement.get(), 1, state.sourceId);
    bindText(database_.get(), statement.get(), 2, state.destinationId);
    bindText(database_.get(), statement.get(), 3, toString(state.status));
    if (sqlite3_bind_int(statement.get(), 4, state.isDirty ? 1 : 0) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
    bindOptionalText(database_.get(), statement.get(), 5, state.lastAttemptUtc);
    bindOptionalText(database_.get(), statement.get(), 6, state.lastSuccessUtc);
    bindOptionalText(database_.get(), statement.get(), 7, state.lastError);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::markRouteDirty(std::string_view sourceId, std::string_view destinationId) {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    auto statement = prepare(
        database_.get(),
        "INSERT INTO route_state(source_id, destination_id, status, is_dirty) VALUES(?1, ?2, 'pending', 1) "
        "ON CONFLICT(source_id, destination_id) DO UPDATE SET "
        "status = CASE WHEN route_state.status = 'running' THEN 'running' ELSE 'pending' END, is_dirty = 1;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::recoverInterruptedRoutes() {
    execute("UPDATE route_state SET status = 'pending', is_dirty = 1 "
            "WHERE status = 'running';");
}

void StateStore::beginRouteAttempt(std::string_view sourceId, std::string_view destinationId,
                                   std::string_view attemptUtc) {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    requireIdentifier(attemptUtc, "Attempt timestamp");
    auto statement = prepare(
        database_.get(),
        "INSERT INTO route_state(source_id, destination_id, status, is_dirty, last_attempt_utc) "
        "VALUES(?1, ?2, 'running', 0, ?3) "
        "ON CONFLICT(source_id, destination_id) DO UPDATE SET "
        "status = 'running', is_dirty = 0, last_attempt_utc = excluded.last_attempt_utc, last_error = NULL;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    bindText(database_.get(), statement.get(), 3, attemptUtc);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::completeRouteSuccess(std::string_view sourceId, std::string_view destinationId,
                                      std::string_view successUtc) {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    requireIdentifier(successUtc, "Success timestamp");
    auto statement = prepare(
        database_.get(),
        "UPDATE route_state SET status = CASE WHEN is_dirty = 1 THEN 'pending' ELSE 'synced' END, "
        "last_success_utc = ?3, last_error = NULL WHERE source_id = ?1 AND destination_id = ?2;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    bindText(database_.get(), statement.get(), 3, successUtc);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::completeRouteFailure(std::string_view sourceId, std::string_view destinationId,
                                      std::string_view error) {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    requireIdentifier(error, "Mirror error");
    auto statement = prepare(
        database_.get(),
        "UPDATE route_state SET status = 'error', is_dirty = 1, last_error = ?3 "
        "WHERE source_id = ?1 AND destination_id = ?2;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    bindText(database_.get(), statement.get(), 3, error);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

std::optional<std::string> StateStore::latestSnapshotUtc(std::string_view sourceId,
                                                          std::string_view destinationId) const {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    auto statement = prepare(database_.get(),
                             "SELECT created_utc FROM snapshot_history WHERE source_id = ?1 AND destination_id = ?2 "
                             "ORDER BY created_utc DESC LIMIT 1;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
    return requiredColumnText(statement.get(), 0);
}

std::optional<std::string> StateStore::latestSnapshotUtc() const {
    auto statement = prepare(database_.get(), "SELECT created_utc FROM snapshot_history ORDER BY created_utc DESC LIMIT 1;");
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
    return requiredColumnText(statement.get(), 0);
}

std::vector<SnapshotRecord> StateStore::snapshotRecords(std::string_view sourceId,
                                                         std::string_view destinationId) const {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    auto statement = prepare(database_.get(),
                             "SELECT id, source_id, destination_id, created_utc, archive_path, archive_bytes "
                             "FROM snapshot_history WHERE source_id = ?1 AND destination_id = ?2 "
                             "ORDER BY created_utc DESC;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    std::vector<SnapshotRecord> records;
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
        records.push_back(SnapshotRecord{sqlite3_column_int64(statement.get(), 0), requiredColumnText(statement.get(), 1),
                                         requiredColumnText(statement.get(), 2), requiredColumnText(statement.get(), 3),
                                         pathFromUtf8(requiredColumnText(statement.get(), 4)),
                                         static_cast<std::uintmax_t>(sqlite3_column_int64(statement.get(), 5))});
    }
    return records;
}

void StateStore::recordSnapshot(std::string_view sourceId, std::string_view destinationId, std::string_view createdUtc,
                                const std::filesystem::path& archivePath, std::uintmax_t archiveBytes) {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    requireIdentifier(createdUtc, "Snapshot timestamp");
    auto statement = prepare(database_.get(),
                             "INSERT INTO snapshot_history(source_id, destination_id, created_utc, archive_path, archive_bytes) "
                             "VALUES(?1, ?2, ?3, ?4, ?5);");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    bindText(database_.get(), statement.get(), 3, createdUtc);
    bindText(database_.get(), statement.get(), 4, pathToUtf8(archivePath));
    if (sqlite3_bind_int64(statement.get(), 5, static_cast<sqlite3_int64>(archiveBytes)) != SQLITE_OK ||
        sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::removeSnapshotRecord(std::int64_t id) {
    auto statement = prepare(database_.get(), "DELETE FROM snapshot_history WHERE id = ?1;");
    if (sqlite3_bind_int64(statement.get(), 1, id) != SQLITE_OK || sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::appendActivity(std::string_view occurredUtc, std::string_view severity, std::string_view message,
                                std::optional<std::string_view> sourceId,
                                std::optional<std::string_view> destinationId) {
    requireIdentifier(occurredUtc, "Activity timestamp");
    requireIdentifier(severity, "Activity severity");
    requireIdentifier(message, "Activity message");
    auto statement = prepare(
        database_.get(),
        "INSERT INTO activity_history(occurred_utc, severity, source_id, destination_id, message) "
        "VALUES(?1, ?2, ?3, ?4, ?5);");
    bindText(database_.get(), statement.get(), 1, occurredUtc);
    bindText(database_.get(), statement.get(), 2, severity);
    bindOptionalView(database_.get(), statement.get(), 3, sourceId);
    bindOptionalView(database_.get(), statement.get(), 4, destinationId);
    bindText(database_.get(), statement.get(), 5, message);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

std::vector<ActivityRecord> StateStore::recentActivity(std::size_t limit) const {
    if (limit > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Activity limit is too large.");
    }
    auto statement = prepare(
        database_.get(),
        "SELECT id, occurred_utc, severity, source_id, destination_id, message "
        "FROM activity_history ORDER BY id DESC LIMIT ?1;");
    if (sqlite3_bind_int(statement.get(), 1, static_cast<int>(limit)) != SQLITE_OK) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }

    std::vector<ActivityRecord> records;
    while (true) {
        const int result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE) {
            return records;
        }
        if (result != SQLITE_ROW) {
            throw std::runtime_error(sqlite3_errmsg(database_.get()));
        }
        records.push_back(ActivityRecord{
            sqlite3_column_int64(statement.get(), 0),
            requiredColumnText(statement.get(), 1),
            requiredColumnText(statement.get(), 2),
            optionalColumnText(statement.get(), 3),
            optionalColumnText(statement.get(), 4),
            requiredColumnText(statement.get(), 5),
        });
    }
}

std::optional<ProjectBackupDecision> StateStore::projectBackupDecision(std::string_view projectId) const {
    requireIdentifier(projectId, "Project ID");
    auto statement = prepare(database_.get(),
                             "SELECT decision FROM project_backup_decision WHERE project_id = ?1;");
    bindText(database_.get(), statement.get(), 1, projectId);
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) {
        const std::string decision = requiredColumnText(statement.get(), 0);
        if (decision == "allow") {
            return ProjectBackupDecision::alwaysAllow;
        }
        if (decision == "ignore") {
            return ProjectBackupDecision::ignorePermanently;
        }
        throw std::runtime_error("State database contains an unknown Project backup decision.");
    }
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    throw std::runtime_error(sqlite3_errmsg(database_.get()));
}

void StateStore::setProjectBackupDecision(std::string_view projectId, ProjectBackupDecision decision,
                                          std::string_view decidedUtc) {
    requireIdentifier(projectId, "Project ID");
    requireIdentifier(decidedUtc, "Decision timestamp");
    auto statement = prepare(
        database_.get(),
        "INSERT INTO project_backup_decision(project_id, decision, decided_utc) VALUES(?1, ?2, ?3) "
        "ON CONFLICT(project_id) DO UPDATE SET decision = excluded.decision, decided_utc = excluded.decided_utc;");
    bindText(database_.get(), statement.get(), 1, projectId);
    bindText(database_.get(), statement.get(), 2,
             decision == ProjectBackupDecision::alwaysAllow ? "allow" : "ignore");
    bindText(database_.get(), statement.get(), 3, decidedUtc);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::clearRoutePending(std::string_view sourceId, std::string_view destinationId) {
    requireIdentifier(sourceId, "Source ID");
    requireIdentifier(destinationId, "Destination ID");
    auto statement = prepare(database_.get(),
                             "UPDATE route_state SET status = 'synced', is_dirty = 0, last_error = NULL "
                             "WHERE source_id = ?1 AND destination_id = ?2;");
    bindText(database_.get(), statement.get(), 1, sourceId);
    bindText(database_.get(), statement.get(), 2, destinationId);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::execute(std::string_view sql) const {
    char* errorMessage = nullptr;
    const std::string sqlText{sql};
    if (sqlite3_exec(database_.get(), sqlText.c_str(), nullptr, nullptr, &errorMessage) != SQLITE_OK) {
        const std::string message = errorMessage == nullptr ? sqlite3_errmsg(database_.get()) : errorMessage;
        sqlite3_free(errorMessage);
        throw std::runtime_error(message);
    }
}
