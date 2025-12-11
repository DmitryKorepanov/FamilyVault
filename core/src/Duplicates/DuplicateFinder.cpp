#include "familyvault/DuplicateFinder.h"
#include "familyvault/IndexManager.h"
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace FamilyVault {

DuplicateFinder::DuplicateFinder(std::shared_ptr<Database> db, IndexManager* indexMgr)
    : m_db(std::move(db))
    , m_indexMgr(indexMgr) {
}

DuplicateFinder::~DuplicateFinder() = default;

std::string DuplicateFinder::computeChecksum(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file for checksum: " + filePath);
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to init SHA256");
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
             EVP_MD_CTX_free(ctx);
             throw std::runtime_error("Failed to update SHA256");
        }
    }
    // Последний блок
    if (file.gcount() > 0) {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
             EVP_MD_CTX_free(ctx);
             throw std::runtime_error("Failed to update SHA256");
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &length) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize SHA256");
    }
    EVP_MD_CTX_free(ctx);

    // Конвертируем в hex строку
    std::ostringstream oss;
    oss << "sha256:";
    for (unsigned int i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return oss.str();
}

std::string DuplicateFinder::getFilePath(int64_t fileId) {
    // Query folder path and relative path separately to avoid SQL path concatenation issues
    auto result = m_db->queryOne<std::pair<std::string, std::string>>(
        R"SQL(
        SELECT wf.path, f.relative_path
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.id = ?
        )SQL",
        [](sqlite3_stmt* stmt) { 
            return std::make_pair(
                Database::getString(stmt, 0),
                Database::getString(stmt, 1)
            );
        },
        fileId
    );

    if (!result) {
        return "";
    }

    // Use std::filesystem::path for proper cross-platform path handling
    fs::path basePath(result->first);
    fs::path relativePath(result->second);
    return (basePath / relativePath).string();
}

std::vector<DuplicateGroup> DuplicateFinder::findLocalDuplicates() {
    // Находим группы файлов с одинаковым checksum
    auto groups = m_db->query<std::pair<std::string, int64_t>>(
        R"SQL(
        SELECT checksum, size
        FROM files
        WHERE checksum IS NOT NULL 
          AND source_device_id IS NULL
        GROUP BY checksum
        HAVING COUNT(*) > 1
        ORDER BY size * (COUNT(*) - 1) DESC
        )SQL",
        [](sqlite3_stmt* stmt) {
            return std::make_pair(
                Database::getString(stmt, 0),
                Database::getInt64(stmt, 1)
            );
        }
    );

    std::vector<DuplicateGroup> result;
    result.reserve(groups.size());

    for (const auto& [checksum, size] : groups) {
        DuplicateGroup group;
        group.checksum = checksum;
        group.fileSize = size;

        // Локальные копии
        group.localCopies = m_db->query<FileRecord>(
            R"SQL(
            SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
                   f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
                   f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
                   f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
            FROM files f
            JOIN watched_folders wf ON f.folder_id = wf.id
            WHERE f.checksum = ? AND f.source_device_id IS NULL
            ORDER BY f.indexed_at
            )SQL",
            mapFileRecord,
            checksum
        );

        // Remote копии (информационно)
        group.remoteCopies = m_db->query<FileRecord>(
            R"SQL(
            SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
                   f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
                   f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
                   f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
            FROM files f
            JOIN watched_folders wf ON f.folder_id = wf.id
            WHERE f.checksum = ? AND f.source_device_id IS NOT NULL
            )SQL",
            mapFileRecord,
            checksum
        );

        result.push_back(std::move(group));
    }

    spdlog::info("Found {} duplicate groups", result.size());
    return result;
}

DuplicateStats DuplicateFinder::getDuplicateStats() {
    DuplicateStats stats;

    auto data = m_db->queryOne<std::tuple<int64_t, int64_t, int64_t>>(
        R"SQL(
        SELECT 
            COUNT(DISTINCT checksum) as groups,
            SUM(copies - 1) as duplicates,
            SUM(size * (copies - 1)) as savings
        FROM (
            SELECT checksum, size, COUNT(*) as copies
            FROM files
            WHERE checksum IS NOT NULL AND source_device_id IS NULL
            GROUP BY checksum
            HAVING COUNT(*) > 1
        )
        )SQL",
        [](sqlite3_stmt* stmt) {
            return std::make_tuple(
                Database::getInt64(stmt, 0),
                Database::getInt64(stmt, 1),
                Database::getInt64(stmt, 2)
            );
        }
    );

    if (data) {
        stats.totalGroups = std::get<0>(*data);
        stats.totalDuplicates = std::get<1>(*data);
        stats.potentialSavings = std::get<2>(*data);
    }

    return stats;
}

std::vector<FileRecord> DuplicateFinder::findFilesWithoutBackup() {
    return m_db->query<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.source_device_id IS NULL
          AND f.checksum IS NOT NULL
          AND NOT EXISTS (
              SELECT 1 FROM files f2
              WHERE f2.checksum = f.checksum
                AND f2.source_device_id IS NOT NULL
          )
        ORDER BY f.size DESC
        )SQL",
        mapFileRecord
    );
}

BackupStatus DuplicateFinder::getBackupStatus() {
    BackupStatus status;

    // Файлы с бэкапом (есть remote копия с таким же checksum)
    status.filesWithBackup = m_db->queryScalar(
        R"SQL(
        SELECT COUNT(DISTINCT f.id)
        FROM files f
        WHERE f.source_device_id IS NULL
          AND f.checksum IS NOT NULL
          AND EXISTS (
              SELECT 1 FROM files f2
              WHERE f2.checksum = f.checksum
                AND f2.source_device_id IS NOT NULL
          )
        )SQL"
    );

    // Файлы без бэкапа
    auto noBackup = m_db->queryOne<std::pair<int64_t, int64_t>>(
        R"SQL(
        SELECT COUNT(*), COALESCE(SUM(size), 0)
        FROM files f
        WHERE f.source_device_id IS NULL
          AND f.checksum IS NOT NULL
          AND NOT EXISTS (
              SELECT 1 FROM files f2
              WHERE f2.checksum = f.checksum
                AND f2.source_device_id IS NOT NULL
          )
        )SQL",
        [](sqlite3_stmt* stmt) {
            return std::make_pair(
                Database::getInt64(stmt, 0),
                Database::getInt64(stmt, 1)
            );
        }
    );

    if (noBackup) {
        status.filesWithoutBackup = noBackup->first;
        status.sizeWithoutBackup = noBackup->second;
    }

    return status;
}

void DuplicateFinder::deleteFile(int64_t fileId) {
    // Use IndexManager if available for centralized deletion
    // (updates folder stats, FTS, etc.)
    if (m_indexMgr) {
        m_indexMgr->deleteFile(fileId, true);
        return;
    }
    
    // Fallback: direct deletion (legacy behavior)
    std::string filePath = getFilePath(fileId);

    if (filePath.empty()) {
        throw std::runtime_error("File not found in database");
    }

    // Удаляем с диска
    if (fs::exists(filePath)) {
        fs::remove(filePath);
        spdlog::info("Deleted file from disk: {}", filePath);
    }

    // Удаляем из БД (CASCADE удалит связи)
    m_db->execute("DELETE FROM files WHERE id = ?", fileId);
    spdlog::warn("File deleted without IndexManager - folder stats may be inconsistent");
}

void DuplicateFinder::keepOnlyOne(const DuplicateGroup& group, int64_t keepFileId) {
    if (group.localCopies.size() <= 1) {
        return; // Нечего удалять
    }

    // Проверяем что keepFileId в группе
    bool found = false;
    for (const auto& file : group.localCopies) {
        if (file.id == keepFileId) {
            found = true;
            break;
        }
    }

    if (!found) {
        throw std::runtime_error("keepFileId not found in duplicate group");
    }

    // Удаляем все кроме выбранного
    Database::Transaction tx(*m_db);

    for (const auto& file : group.localCopies) {
        if (file.id != keepFileId) {
            deleteFile(file.id);
        }
    }

    tx.commit();
    spdlog::info("Kept file {} and deleted {} duplicates",
                 keepFileId, group.localCopies.size() - 1);
}

void DuplicateFinder::computeMissingChecksums(std::function<void(int, int)> onProgress) {
    // Получаем файлы без checksum (folder path and relative path separately)
    auto files = m_db->query<std::tuple<int64_t, std::string, std::string>>(
        R"SQL(
        SELECT f.id, wf.path, f.relative_path
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.checksum IS NULL AND f.source_device_id IS NULL
        )SQL",
        [](sqlite3_stmt* stmt) {
            return std::make_tuple(
                Database::getInt64(stmt, 0),
                Database::getString(stmt, 1),
                Database::getString(stmt, 2)
            );
        }
    );

    int total = static_cast<int>(files.size());
    int processed = 0;

    spdlog::info("Computing checksums for {} files", total);

    for (const auto& [fileId, folderPath, relativePath] : files) {
        // Construct full path using std::filesystem for proper cross-platform handling
        fs::path fullPath = fs::path(folderPath) / fs::path(relativePath);
        std::string filePathStr = fullPath.string();
        
        try {
            if (fs::exists(fullPath)) {
                std::string checksum = computeChecksum(filePathStr);
                m_db->execute("UPDATE files SET checksum = ? WHERE id = ?",
                              checksum, fileId);
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to compute checksum for {}: {}", filePathStr, e.what());
        }

        processed++;
        if (onProgress && processed % 10 == 0) {
            onProgress(processed, total);
        }
    }

    spdlog::info("Computed checksums for {} files", processed);
}

void DuplicateFinder::computeChecksumForFile(int64_t fileId) {
    std::string filePath = getFilePath(fileId);

    if (filePath.empty() || !fs::exists(filePath)) {
        throw std::runtime_error("File not found");
    }

    std::string checksum = computeChecksum(filePath);
    m_db->execute("UPDATE files SET checksum = ? WHERE id = ?", checksum, fileId);
}

FileRecord DuplicateFinder::mapFileRecord(sqlite3_stmt* stmt) {
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

} // namespace FamilyVault

