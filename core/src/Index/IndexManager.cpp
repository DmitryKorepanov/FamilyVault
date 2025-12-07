#include "familyvault/IndexManager.h"
#include "familyvault/MimeTypeDetector.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;

namespace FamilyVault {

IndexManager::IndexManager(std::shared_ptr<Database> db)
    : m_db(std::move(db))
    , m_scanner(std::make_unique<FileScanner>()) {
}

IndexManager::~IndexManager() {
    stopScan();
}

// ═══════════════════════════════════════════════════════════
// Управление папками
// ═══════════════════════════════════════════════════════════

int64_t IndexManager::addFolder(const std::string& path, const std::string& name,
                                 Visibility visibility) {
    // Нормализуем путь
    fs::path fsPath(path);
    std::string normalizedPath = fsPath.lexically_normal().string();

    // Используем имя папки если не задано
    std::string folderName = name.empty() ? fsPath.filename().string() : name;

    // Проверяем существование
    if (!fs::exists(fsPath)) {
        throw std::runtime_error("Folder does not exist: " + path);
    }

    if (!fs::is_directory(fsPath)) {
        throw std::runtime_error("Path is not a directory: " + path);
    }

    // Сначала пробуем найти существующую папку по пути
    auto existingId = m_db->queryOne<int64_t>(
        "SELECT id FROM watched_folders WHERE path = ?",
        [](sqlite3_stmt* stmt) { return Database::getInt64(stmt, 0); },
        normalizedPath
    );

    if (existingId) {
        // Папка уже существует — обновляем name и visibility, но сохраняем индекс
        m_db->execute(
            "UPDATE watched_folders SET name = ?, visibility = ? WHERE id = ?",
            folderName, static_cast<int>(visibility), *existingId
        );
        spdlog::info("Updated existing folder: {} (id={})", normalizedPath, *existingId);
        return *existingId;
    }

    // Папка не существует — вставляем новую
    m_db->execute(
        "INSERT INTO watched_folders (path, name, visibility) VALUES (?, ?, ?)",
        normalizedPath, folderName, static_cast<int>(visibility)
    );

    int64_t folderId = m_db->lastInsertId();
    spdlog::info("Added folder: {} (id={})", normalizedPath, folderId);

    return folderId;
}

void IndexManager::removeFolder(int64_t folderId) {
    // CASCADE удалит все файлы
    m_db->execute("DELETE FROM watched_folders WHERE id = ?", folderId);
    spdlog::info("Removed folder id={}", folderId);
}

void IndexManager::setFolderEnabled(int64_t folderId, bool enabled) {
    m_db->execute("UPDATE watched_folders SET enabled = ? WHERE id = ?",
                  enabled ? 1 : 0, folderId);
}

void IndexManager::setFolderVisibility(int64_t folderId, Visibility visibility) {
    m_db->execute("UPDATE watched_folders SET visibility = ? WHERE id = ?",
                  static_cast<int>(visibility), folderId);
}

std::vector<WatchedFolder> IndexManager::getFolders() const {
    return m_db->query<WatchedFolder>(
        "SELECT id, path, name, enabled, last_scan_at, file_count, total_size, created_at, visibility "
        "FROM watched_folders ORDER BY name",
        mapWatchedFolder
    );
}

std::optional<WatchedFolder> IndexManager::getFolder(int64_t folderId) const {
    return m_db->queryOne<WatchedFolder>(
        "SELECT id, path, name, enabled, last_scan_at, file_count, total_size, created_at, visibility "
        "FROM watched_folders WHERE id = ?",
        mapWatchedFolder,
        folderId
    );
}

// ═══════════════════════════════════════════════════════════
// Сканирование
// ═══════════════════════════════════════════════════════════

void IndexManager::scanFolder(int64_t folderId, ScanProgressCallback onProgress) {
    auto folder = getFolder(folderId);
    if (!folder) {
        spdlog::warn("Folder not found: {}", folderId);
        return;
    }

    if (!folder->enabled) {
        spdlog::info("Skipping disabled folder: {}", folder->path);
        return;
    }

    spdlog::info("Scanning folder: {} (id={})", folder->path, folderId);

    auto scanStartTime = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    m_cancelToken.reset();

    // Сканируем файлы
    m_scanner->scan(
        folder->path,
        [this, folderId](const ScannedFile& file) {
            upsertFile(folderId, file);
        },
        onProgress,
        ScanOptions{},
        m_cancelToken
    );

    // Удаляем файлы, которых больше нет
    if (!m_cancelToken.isCancelled()) {
        deleteRemovedFiles(folderId, scanStartTime);
        updateFolderStats(folderId);

        // Обновляем время последнего сканирования
        m_db->execute("UPDATE watched_folders SET last_scan_at = ? WHERE id = ?",
                      scanStartTime, folderId);
    }
}

void IndexManager::scanAllFolders(ScanProgressCallback onProgress) {
    auto folders = getFolders();
    for (const auto& folder : folders) {
        if (m_cancelToken.isCancelled()) {
            break;
        }
        if (folder.enabled) {
            scanFolder(folder.id, onProgress);
        }
    }
}

void IndexManager::stopScan() {
    m_cancelToken.cancel();
    m_scanner->cancelAll();
}

bool IndexManager::isScanning() const {
    return m_scanner->isRunning();
}

// ═══════════════════════════════════════════════════════════
// Файлы
// ═══════════════════════════════════════════════════════════

int64_t IndexManager::upsertFile(int64_t folderId, const ScannedFile& file) {
    m_db->execute(
        R"SQL(
        INSERT INTO files (folder_id, relative_path, name, extension, size, mime_type, 
                          content_type, created_at, modified_at, indexed_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now'))
        ON CONFLICT(folder_id, relative_path) DO UPDATE SET
            name = excluded.name,
            size = excluded.size,
            mime_type = excluded.mime_type,
            content_type = excluded.content_type,
            modified_at = excluded.modified_at,
            indexed_at = strftime('%s', 'now')
        )SQL",
        folderId,
        file.relativePath,
        file.name,
        file.extension,
        file.size,
        file.mimeType,
        static_cast<int>(file.contentType),
        file.createdAt,
        file.modifiedAt
    );

    return m_db->lastInsertId();
}

void IndexManager::deleteRemovedFiles(int64_t folderId, int64_t scanStartTime) {
    // Файлы, которые не были обновлены в этом сканировании — удалены с диска
    m_db->execute(
        "DELETE FROM files WHERE folder_id = ? AND indexed_at < ?",
        folderId, scanStartTime
    );

    int deleted = m_db->changesCount();
    if (deleted > 0) {
        spdlog::info("Removed {} files that no longer exist", deleted);
    }
}

void IndexManager::updateFolderStats(int64_t folderId) {
    m_db->execute(
        R"SQL(
        UPDATE watched_folders SET
            file_count = (SELECT COUNT(*) FROM files WHERE folder_id = ?),
            total_size = (SELECT COALESCE(SUM(size), 0) FROM files WHERE folder_id = ?)
        WHERE id = ?
        )SQL",
        folderId, folderId, folderId
    );
}

std::optional<FileRecord> IndexManager::getFile(int64_t fileId) const {
    return m_db->queryOne<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.id = ?
        )SQL",
        mapFileRecord,
        fileId
    );
}

std::optional<FileRecord> IndexManager::getFileByPath(int64_t folderId,
                                                       const std::string& relativePath) const {
    return m_db->queryOne<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.folder_id = ? AND f.relative_path = ?
        )SQL",
        mapFileRecord,
        folderId, relativePath
    );
}

std::vector<FileRecord> IndexManager::getRecentFiles(int limit) const {
    return m_db->query<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        ORDER BY f.indexed_at DESC
        LIMIT ?
        )SQL",
        mapFileRecord,
        limit
    );
}

std::vector<FileRecord> IndexManager::getFilesByFolder(int64_t folderId, int limit, int offset) const {
    return m_db->query<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.folder_id = ?
        ORDER BY f.name
        LIMIT ? OFFSET ?
        )SQL",
        mapFileRecord,
        folderId, limit, offset
    );
}

std::vector<FileRecordCompact> IndexManager::getRecentFilesCompact(int limit) const {
    return m_db->query<FileRecordCompact>(
        R"SQL(
        SELECT id, name, extension, size, content_type, modified_at, is_remote
        FROM files
        ORDER BY indexed_at DESC
        LIMIT ?
        )SQL",
        mapFileRecordCompact,
        limit
    );
}

std::vector<FileRecordCompact> IndexManager::getFilesByFolderCompact(int64_t folderId, int limit, int offset) const {
    return m_db->query<FileRecordCompact>(
        R"SQL(
        SELECT id, name, extension, size, content_type, modified_at, is_remote
        FROM files
        WHERE folder_id = ?
        ORDER BY name
        LIMIT ? OFFSET ?
        )SQL",
        mapFileRecordCompact,
        folderId, limit, offset
    );
}

void IndexManager::setFileVisibility(int64_t fileId, std::optional<Visibility> visibility) {
    if (visibility) {
        m_db->execute("UPDATE files SET visibility = ? WHERE id = ?",
                      static_cast<int>(*visibility), fileId);
    } else {
        m_db->execute("UPDATE files SET visibility = NULL WHERE id = ?", fileId);
    }
}

void IndexManager::deleteFile(int64_t fileId, bool deleteFromDisk) {
    // Get file info before deleting from DB
    auto result = m_db->queryOne<std::tuple<int64_t, std::string, std::string>>(
        R"SQL(
        SELECT f.folder_id, wf.path, f.relative_path
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.id = ?
        )SQL",
        [](sqlite3_stmt* stmt) {
            return std::make_tuple(
                Database::getInt64(stmt, 0),
                Database::getString(stmt, 1),
                Database::getString(stmt, 2)
            );
        },
        fileId
    );

    if (!result) {
        throw std::runtime_error("File not found in database");
    }

    auto [folderId, folderPath, relativePath] = *result;

    // Optionally delete from disk
    if (deleteFromDisk) {
        fs::path fullPath = fs::path(folderPath) / fs::path(relativePath);
        if (fs::exists(fullPath)) {
            fs::remove(fullPath);
            spdlog::info("Deleted file from disk: {}", fullPath.string());
        }
    }

    // Delete from database (CASCADE will delete file_tags, etc.)
    m_db->execute("DELETE FROM files WHERE id = ?", fileId);
    spdlog::info("Deleted file id={} from index", fileId);

    // Update folder stats
    updateFolderStats(folderId);
}

// ═══════════════════════════════════════════════════════════
// Статистика
// ═══════════════════════════════════════════════════════════

IndexStats IndexManager::getStats() const {
    IndexStats stats;

    stats.totalFolders = m_db->queryScalar("SELECT COUNT(*) FROM watched_folders");
    stats.totalFiles = m_db->queryScalar("SELECT COUNT(*) FROM files");
    stats.totalSize = m_db->queryScalar("SELECT COALESCE(SUM(size), 0) FROM files");

    stats.imageCount = m_db->queryScalar(
        "SELECT COUNT(*) FROM files WHERE content_type = ?",
        static_cast<int>(ContentType::Image));
    stats.videoCount = m_db->queryScalar(
        "SELECT COUNT(*) FROM files WHERE content_type = ?",
        static_cast<int>(ContentType::Video));
    stats.audioCount = m_db->queryScalar(
        "SELECT COUNT(*) FROM files WHERE content_type = ?",
        static_cast<int>(ContentType::Audio));
    stats.documentCount = m_db->queryScalar(
        "SELECT COUNT(*) FROM files WHERE content_type = ?",
        static_cast<int>(ContentType::Document));
    stats.archiveCount = m_db->queryScalar(
        "SELECT COUNT(*) FROM files WHERE content_type = ?",
        static_cast<int>(ContentType::Archive));
    stats.otherCount = m_db->queryScalar(
        "SELECT COUNT(*) FROM files WHERE content_type NOT IN (?, ?, ?, ?, ?)",
        static_cast<int>(ContentType::Image),
        static_cast<int>(ContentType::Video),
        static_cast<int>(ContentType::Audio),
        static_cast<int>(ContentType::Document),
        static_cast<int>(ContentType::Archive));

    return stats;
}

// ═══════════════════════════════════════════════════════════
// Mappers
// ═══════════════════════════════════════════════════════════

FileRecord IndexManager::mapFileRecord(sqlite3_stmt* stmt) {
    FileRecord r;
    r.id = Database::getInt64(stmt, 0);
    r.folderId = Database::getInt64(stmt, 1);
    r.relativePath = Database::getString(stmt, 2);
    r.name = Database::getString(stmt, 3);
    r.extension = Database::getString(stmt, 4);
    r.size = Database::getInt64(stmt, 5);
    r.mimeType = Database::getString(stmt, 6);
    r.contentType = static_cast<ContentType>(Database::getInt(stmt, 7));
    r.checksum = Database::getStringOpt(stmt, 8);
    r.createdAt = Database::getInt64(stmt, 9);
    r.modifiedAt = Database::getInt64(stmt, 10);
    r.indexedAt = Database::getInt64(stmt, 11);
    r.visibility = static_cast<Visibility>(Database::getInt(stmt, 12));
    r.sourceDeviceId = Database::getStringOpt(stmt, 13);
    r.isRemote = Database::getInt(stmt, 14) != 0;
    r.syncVersion = Database::getInt64(stmt, 15);
    r.lastModifiedBy = Database::getStringOpt(stmt, 16);
    return r;
}

FileRecordCompact IndexManager::mapFileRecordCompact(sqlite3_stmt* stmt) {
    FileRecordCompact r;
    r.id = Database::getInt64(stmt, 0);
    r.name = Database::getString(stmt, 1);
    r.extension = Database::getString(stmt, 2);
    r.size = Database::getInt64(stmt, 3);
    r.contentType = static_cast<ContentType>(Database::getInt(stmt, 4));
    r.modifiedAt = Database::getInt64(stmt, 5);
    r.isRemote = Database::getInt(stmt, 6) != 0;
    r.hasThumbnail = false; // TODO: check thumbnail cache
    return r;
}

WatchedFolder IndexManager::mapWatchedFolder(sqlite3_stmt* stmt) {
    WatchedFolder f;
    f.id = Database::getInt64(stmt, 0);
    f.path = Database::getString(stmt, 1);
    f.name = Database::getString(stmt, 2);
    f.enabled = Database::getInt(stmt, 3) != 0;
    f.lastScanAt = Database::getInt64(stmt, 4);
    f.fileCount = Database::getInt64(stmt, 5);
    f.totalSize = Database::getInt64(stmt, 6);
    f.createdAt = Database::getInt64(stmt, 7);
    f.defaultVisibility = static_cast<Visibility>(Database::getInt(stmt, 8));
    return f;
}

} // namespace FamilyVault
