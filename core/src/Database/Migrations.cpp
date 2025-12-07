#include "familyvault/Database.h"
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

-- Full-text search
CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5(
    name,
    relative_path,
    content,
    content='',
    tokenize='unicode61 remove_diacritics 2'
);

-- FTS триггеры
CREATE TRIGGER IF NOT EXISTS files_fts_insert AFTER INSERT ON files BEGIN
    INSERT INTO files_fts(rowid, name, relative_path, content)
    VALUES (new.id, new.name, new.relative_path, '');
END;

CREATE TRIGGER IF NOT EXISTS files_fts_delete AFTER DELETE ON files BEGIN
    INSERT INTO files_fts(files_fts, rowid, name, relative_path, content)
    VALUES ('delete', old.id, old.name, old.relative_path, '');
END;

CREATE TRIGGER IF NOT EXISTS files_fts_update AFTER UPDATE ON files BEGIN
    INSERT INTO files_fts(files_fts, rowid, name, relative_path, content)
    VALUES ('delete', old.id, old.name, old.relative_path, '');
    INSERT INTO files_fts(rowid, name, relative_path, content)
    VALUES (new.id, new.name, new.relative_path, 
            COALESCE((SELECT content FROM file_content WHERE file_id = new.id), ''));
END;

CREATE TRIGGER IF NOT EXISTS content_fts_insert AFTER INSERT ON file_content BEGIN
    UPDATE files_fts SET content = new.content WHERE rowid = new.file_id;
END;

CREATE TRIGGER IF NOT EXISTS content_fts_update AFTER UPDATE ON file_content BEGIN
    UPDATE files_fts SET content = new.content WHERE rowid = new.file_id;
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

void Database::setVersion(int version, const std::string& description) {
    execute("INSERT INTO schema_version (version, description) VALUES (?, ?)",
            version, description);
}

void Database::applyMigrations() {
    int currentVersion = getCurrentVersion();
    int targetVersion = MIGRATIONS.back().version;

    if (currentVersion >= targetVersion) {
        spdlog::info("Database schema is up to date (version {})", currentVersion);
        return;
    }

    spdlog::info("Applying database migrations: {} -> {}", currentVersion, targetVersion);

    for (const auto& migration : MIGRATIONS) {
        if (migration.version > currentVersion) {
            spdlog::info("Applying migration {}: {}", migration.version, migration.description);

            Transaction tx(*this);
            try {
                // Выполняем SQL миграции
                execute(migration.sql);

                // Записываем версию (если таблица уже создана в этой миграции)
                if (migration.version == 1) {
                    // Первая миграция создаёт таблицу, версия уже вставлена в SQL
                } else {
                    setVersion(migration.version, migration.description);
                }

                tx.commit();
                spdlog::info("Migration {} applied successfully", migration.version);
            } catch (const std::exception& e) {
                spdlog::error("Migration {} failed: {}", migration.version, e.what());
                throw;
            }
        }
    }

    // Вставляем запись о первой версии, если её ещё нет
    if (currentVersion == 0) {
        auto count = queryScalar("SELECT COUNT(*) FROM schema_version WHERE version = 1");
        if (count == 0) {
            setVersion(1, MIGRATIONS[0].description);
        }
    }

    spdlog::info("Database migrations completed. Current version: {}", targetVersion);
}

} // namespace FamilyVault

