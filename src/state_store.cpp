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
        "CREATE TABLE IF NOT EXISTS project_size_approval ("
        "project_id TEXT PRIMARY KEY,"
        "approved_utc TEXT NOT NULL"
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

bool StateStore::hasPermanentSizeApproval(std::string_view projectId) const {
    requireIdentifier(projectId, "Project ID");
    auto statement = prepare(database_.get(), "SELECT 1 FROM project_size_approval WHERE project_id = ?1;");
    bindText(database_.get(), statement.get(), 1, projectId);
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) {
        return true;
    }
    if (result == SQLITE_DONE) {
        return false;
    }
    throw std::runtime_error(sqlite3_errmsg(database_.get()));
}

void StateStore::setPermanentSizeApproval(std::string_view projectId, std::string_view approvedUtc) {
    requireIdentifier(projectId, "Project ID");
    requireIdentifier(approvedUtc, "Approval timestamp");
    auto statement = prepare(
        database_.get(),
        "INSERT INTO project_size_approval(project_id, approved_utc) VALUES(?1, ?2) "
        "ON CONFLICT(project_id) DO UPDATE SET approved_utc = excluded.approved_utc;");
    bindText(database_.get(), statement.get(), 1, projectId);
    bindText(database_.get(), statement.get(), 2, approvedUtc);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
        throw std::runtime_error(sqlite3_errmsg(database_.get()));
    }
}

void StateStore::clearPermanentSizeApproval(std::string_view projectId) {
    requireIdentifier(projectId, "Project ID");
    auto statement = prepare(database_.get(), "DELETE FROM project_size_approval WHERE project_id = ?1;");
    bindText(database_.get(), statement.get(), 1, projectId);
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
