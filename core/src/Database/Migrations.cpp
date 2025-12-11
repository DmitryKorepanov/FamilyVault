#include "familyvault/Database.h"
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <array>

namespace FamilyVault {

struct Migration {
    int version;
    const char* description;
    const char* sql;
};

// ═══════════════════════════════════════════════════════════
// МИГРАЦИИ СХЕМЫ БД
// ═══════════════════════════════════════════════════════════

static const std::array MIGRATIONS = {
    Migration{1, "Initial schema", R"SQL(
-- Версионирование схемы
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at INTEGER DEFAULT (strftime('%s', 'now')),
    description TEXT
);

-- Настройки приложения
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT
);

-- Отслеживаемые папки
CREATE TABLE IF NOT EXISTS watched_folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT UNIQUE NOT NULL,
    name TEXT NOT NULL,
    enabled INTEGER DEFAULT 1,
    last_scan_at INTEGER,
    file_count INTEGER DEFAULT 0,
    total_size INTEGER DEFAULT 0,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    visibility INTEGER DEFAULT 1
);

-- Файлы
CREATE TABLE IF NOT EXISTS files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    folder_id INTEGER NOT NULL,
    relative_path TEXT NOT NULL,
    name TEXT NOT NULL,
    extension TEXT,
    size INTEGER NOT NULL,
    mime_type TEXT,
    content_type INTEGER DEFAULT 0,
    checksum TEXT,
    created_at INTEGER,
    modified_at INTEGER,
    indexed_at INTEGER DEFAULT (strftime('%s', 'now')),
    visibility INTEGER DEFAULT NULL,
    source_device_id TEXT DEFAULT NULL,
    is_remote INTEGER DEFAULT 0,
    sync_version INTEGER DEFAULT 0,
    last_modified_by TEXT DEFAULT NULL,
    
    UNIQUE(folder_id, relative_path),
    FOREIGN KEY (folder_id) REFERENCES watched_folders(id) ON DELETE CASCADE
);

-- Tombstones для удалённых файлов (P2P sync)
CREATE TABLE IF NOT EXISTS deleted_files (
    checksum TEXT PRIMARY KEY,
    deleted_at INTEGER DEFAULT (strftime('%s', 'now')),
    deleted_by TEXT
);

-- Индексы файлов
CREATE INDEX IF NOT EXISTS idx_files_folder ON files(folder_id);
CREATE INDEX IF NOT EXISTS idx_files_name ON files(name);
CREATE INDEX IF NOT EXISTS idx_files_extension ON files(extension);
CREATE INDEX IF NOT EXISTS idx_files_content_type ON files(content_type);
CREATE INDEX IF NOT EXISTS idx_files_modified ON files(modified_at DESC);
CREATE INDEX IF NOT EXISTS idx_files_checksum ON files(checksum);
CREATE INDEX IF NOT EXISTS idx_files_source ON files(source_device_id);
CREATE INDEX IF NOT EXISTS idx_files_indexed ON files(indexed_at DESC);

-- Метаданные изображений
CREATE TABLE IF NOT EXISTS image_metadata (
    file_id INTEGER PRIMARY KEY,
    width INTEGER,
    height INTEGER,
    taken_at INTEGER,
    camera_make TEXT,
    camera_model TEXT,
    latitude REAL,
    longitude REAL,
    orientation INTEGER DEFAULT 1,
    
    FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE
);

-- Извлечённый текст из файлов
CREATE TABLE IF NOT EXISTS file_content (
    file_id INTEGER PRIMARY KEY,
    content TEXT,
    extraction_method TEXT,
    language TEXT,
    extracted_at INTEGER DEFAULT (strftime('%s', 'now')),
    
    FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE
);

-- Теги
CREATE TABLE IF NOT EXISTS tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT UNIQUE NOT NULL,
    source INTEGER DEFAULT 0,
    created_at INTEGER DEFAULT (strftime('%s', 'now'))
);

-- Связь файлов и тегов
CREATE TABLE IF NOT EXISTS file_tags (
    file_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    
    PRIMARY KEY (file_id, tag_id),
    FOREIGN KEY (file_id) REFERENCES files(id) ON DELETE CASCADE,
    FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_file_tags_tag ON file_tags(tag_id);
CREATE INDEX IF NOT EXISTS idx_tags_name ON tags(name);

-- Full-text search (обычная таблица, не contentless — поддерживает rebuild)
CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5(
    name,
    relative_path,
    content,
    tokenize='unicode61 remove_diacritics 2'
);

-- Триггер: добавление файла → создаём запись в FTS
CREATE TRIGGER IF NOT EXISTS files_fts_insert AFTER INSERT ON files BEGIN
    INSERT INTO files_fts(rowid, name, relative_path, content)
    VALUES (new.id, new.name, new.relative_path, '');
END;

-- Триггер: удаление файла → удаляем из FTS
CREATE TRIGGER IF NOT EXISTS files_fts_delete AFTER DELETE ON files BEGIN
    DELETE FROM files_fts WHERE rowid = old.id;
END;

-- View для эффективной visibility
CREATE VIEW IF NOT EXISTS files_effective AS
SELECT 
    f.*,
    COALESCE(f.visibility, wf.visibility) as effective_visibility
FROM files f
JOIN watched_folders wf ON f.folder_id = wf.id;

-- Sync state для P2P
CREATE TABLE IF NOT EXISTS sync_state (
    device_id TEXT PRIMARY KEY,
    last_sync_version INTEGER DEFAULT 0,
    last_sync_at INTEGER,
    needs_full_resync INTEGER DEFAULT 0
);

-- Облачные аккаунты (метаданные)
CREATE TABLE IF NOT EXISTS cloud_accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type TEXT NOT NULL,
    email TEXT NOT NULL,
    display_name TEXT,
    avatar_url TEXT,
    change_token TEXT,
    last_sync_at INTEGER,
    file_count INTEGER DEFAULT 0,
    enabled INTEGER DEFAULT 1,
    created_at INTEGER DEFAULT (strftime('%s', 'now')),
    UNIQUE(type, email)
);

-- Отслеживаемые папки в облаке
CREATE TABLE IF NOT EXISTS cloud_watched_folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    cloud_id TEXT NOT NULL,
    name TEXT NOT NULL,
    path TEXT,
    enabled INTEGER DEFAULT 1,
    last_sync_at INTEGER,
    UNIQUE(account_id, cloud_id),
    FOREIGN KEY (account_id) REFERENCES cloud_accounts(id) ON DELETE CASCADE
);

-- Файлы из облака (индекс)
CREATE TABLE IF NOT EXISTS cloud_files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    cloud_id TEXT NOT NULL,
    name TEXT NOT NULL,
    mime_type TEXT,
    size INTEGER DEFAULT 0,
    created_at INTEGER,
    modified_at INTEGER,
    parent_cloud_id TEXT,
    path TEXT,
    thumbnail_url TEXT,
    web_view_url TEXT,
    checksum TEXT,
    indexed_at INTEGER DEFAULT (strftime('%s', 'now')),
    extension TEXT,
    content_type INTEGER DEFAULT 0,
    UNIQUE(account_id, cloud_id),
    FOREIGN KEY (account_id) REFERENCES cloud_accounts(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_cloud_files_account ON cloud_files(account_id);
CREATE INDEX IF NOT EXISTS idx_cloud_files_name ON cloud_files(name);
CREATE INDEX IF NOT EXISTS idx_cloud_files_modified ON cloud_files(modified_at DESC);
CREATE INDEX IF NOT EXISTS idx_cloud_files_extension ON cloud_files(extension);
CREATE INDEX IF NOT EXISTS idx_cloud_files_content_type ON cloud_files(content_type);

-- Полнотекстовый поиск по cloud_files
CREATE VIRTUAL TABLE IF NOT EXISTS cloud_files_fts USING fts5(
    name,
    path,
    tokenize='unicode61 remove_diacritics 2'
);

CREATE TRIGGER IF NOT EXISTS cloud_files_fts_insert AFTER INSERT ON cloud_files BEGIN
    INSERT INTO cloud_files_fts(rowid, name, path)
    VALUES (new.id, new.name, COALESCE(new.path, ''));
END;

CREATE TRIGGER IF NOT EXISTS cloud_files_fts_delete AFTER DELETE ON cloud_files BEGIN
    DELETE FROM cloud_files_fts WHERE rowid = old.id;
END;

CREATE TRIGGER IF NOT EXISTS cloud_files_fts_update AFTER UPDATE OF name, path ON cloud_files BEGIN
    DELETE FROM cloud_files_fts WHERE rowid = old.id;
    INSERT INTO cloud_files_fts(rowid, name, path)
    VALUES (new.id, new.name, COALESCE(new.path, ''));
END;
    )SQL"}
};

// ═══════════════════════════════════════════════════════════
// Реализация миграций
// ═══════════════════════════════════════════════════════════

int Database::getCurrentVersion() {
    // Проверяем существование таблицы schema_version
    auto result = queryOne<int>(
        "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='schema_version'",
        [](sqlite3_stmt* stmt) { return getInt(stmt, 0); }
    );

    if (!result || *result == 0) {
        return 0; // Таблицы нет — версия 0
    }

    auto version = queryOne<int>(
        "SELECT MAX(version) FROM schema_version",
        [](sqlite3_stmt* stmt) {
            if (isNull(stmt, 0)) return 0;
            return getInt(stmt, 0);
        }
    );
    
    return version.value_or(0);
}

void Database::applyMigrations() {
    int currentVersion = getCurrentVersion();
    spdlog::info("Database version: {}, latest: {}", currentVersion, MIGRATIONS.size());

    for (const auto& migration : MIGRATIONS) {
        if (migration.version <= currentVersion) {
            continue;
        }

        spdlog::info("Applying migration {}: {}", migration.version, migration.description);
        
        try {
            execute("BEGIN EXCLUSIVE TRANSACTION");
            
            // Выполняем SQL миграции
            char* errMsg = nullptr;
            int rc = sqlite3_exec(m_db, migration.sql, nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                std::string error = errMsg ? errMsg : "Unknown error";
                sqlite3_free(errMsg);
                execute("ROLLBACK");
                throw std::runtime_error("Migration failed: " + error);
            }
            
            // Записываем версию
            execute(
                "INSERT INTO schema_version (version, description) VALUES (?, ?)",
                migration.version, migration.description
            );
            
            execute("COMMIT");
            spdlog::info("Migration {} completed", migration.version);
            
        } catch (const std::exception& e) {
            spdlog::error("Migration {} failed: {}", migration.version, e.what());
            throw;
        }
    }
    
    spdlog::info("Database schema is up to date (version {})", MIGRATIONS.size());
}

} // namespace FamilyVault
