#include "familyvault/Database.h"
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace FamilyVault {

Database::Database(const std::string& dbPath) : m_dbPath(dbPath) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(dbPath.c_str(), &m_db, flags, nullptr);

    if (rc != SQLITE_OK) {
        std::string error = sqlite3_errmsg(m_db);
        sqlite3_close(m_db);
        m_db = nullptr;
        throw DatabaseException("Failed to open database: " + error);
    }

    // Настройки для производительности и надёжности
    execute("PRAGMA journal_mode = DELETE");  // DELETE режим — VACUUM работает корректно
    execute("PRAGMA busy_timeout = 30000");   // 30 seconds for large folders
    execute("PRAGMA foreign_keys = ON");
    execute("PRAGMA synchronous = NORMAL");
    execute("PRAGMA cache_size = -64000");    // 64MB cache

    spdlog::info("Database opened: {}", dbPath);
}

Database::~Database() {
    if (m_db) {
        sqlite3_close(m_db);
        spdlog::debug("Database closed: {}", m_dbPath);
    }
}

Database::Database(Database&& other) noexcept
    : m_db(other.m_db), m_dbPath(std::move(other.m_dbPath)) {
    other.m_db = nullptr;
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        if (m_db) {
            sqlite3_close(m_db);
        }
        m_db = other.m_db;
        m_dbPath = std::move(other.m_dbPath);
        other.m_db = nullptr;
    }
    return *this;
}

void Database::initialize() {
    applyMigrations();
}

void Database::execute(const std::string& sql) {
    char* errorMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errorMsg);

    if (rc != SQLITE_OK) {
        std::string error = errorMsg ? errorMsg : "Unknown error";
        sqlite3_free(errorMsg);
        throw DatabaseException("SQL execution failed: " + error + "\nSQL: " + sql);
    }
}

sqlite3_stmt* Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        throw DatabaseException("Failed to prepare statement: " +
                                std::string(sqlite3_errmsg(m_db)) + "\nSQL: " + sql);
    }
    return stmt;
}

void Database::step(sqlite3_stmt* stmt) {
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw DatabaseException("Failed to execute statement: " +
                                std::string(sqlite3_errmsg(m_db)));
    }
}

bool Database::stepRow(sqlite3_stmt* stmt) {
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        return true;
    } else if (rc == SQLITE_DONE) {
        return false;
    }
    throw DatabaseException("Failed to step statement: " +
                            std::string(sqlite3_errmsg(m_db)));
}

void Database::finalize(sqlite3_stmt* stmt) {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

int64_t Database::lastInsertId() const {
    return sqlite3_last_insert_rowid(m_db);
}

int Database::changesCount() const {
    return sqlite3_changes(m_db);
}

void Database::beginTransaction() {
    execute("BEGIN TRANSACTION");
}

void Database::commit() {
    execute("COMMIT");
}

void Database::rollback() {
    execute("ROLLBACK");
}

// Привязка параметров
void Database::bindParameter(sqlite3_stmt* stmt, int index, int value) {
    sqlite3_bind_int(stmt, index, value);
}

void Database::bindParameter(sqlite3_stmt* stmt, int index, int64_t value) {
    sqlite3_bind_int64(stmt, index, value);
}

void Database::bindParameter(sqlite3_stmt* stmt, int index, double value) {
    sqlite3_bind_double(stmt, index, value);
}

void Database::bindParameter(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void Database::bindParameter(sqlite3_stmt* stmt, int index, const char* value) {
    if (value) {
        sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

void Database::bindParameter(sqlite3_stmt* stmt, int index, std::nullptr_t) {
    sqlite3_bind_null(stmt, index);
}

// Хелперы для чтения
int Database::getInt(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int(stmt, col);
}

int64_t Database::getInt64(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int64(stmt, col);
}

double Database::getDouble(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_double(stmt, col);
}

std::string Database::getString(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    if (text) {
        return std::string(reinterpret_cast<const char*>(text));
    }
    return "";
}

std::optional<std::string> Database::getStringOpt(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return getString(stmt, col);
}

std::optional<int64_t> Database::getInt64Opt(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return getInt64(stmt, col);
}

std::optional<double> Database::getDoubleOpt(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return getDouble(stmt, col);
}

bool Database::isNull(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_type(stmt, col) == SQLITE_NULL;
}

// Transaction RAII
Database::Transaction::Transaction(Database& db) : m_db(&db) {
    m_db->beginTransaction();
}

Database::Transaction::~Transaction() {
    if (!m_finished && m_db) {
        try {
            m_db->rollback();
        } catch (...) {
            // Игнорируем ошибки в деструкторе
        }
    }
}

void Database::Transaction::commit() {
    if (!m_finished && m_db) {
        m_db->commit();
        m_finished = true;
    }
}

void Database::Transaction::rollback() {
    if (!m_finished && m_db) {
        m_db->rollback();
        m_finished = true;
    }
}

} // namespace FamilyVault
