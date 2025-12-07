#pragma once

#include "Database.h"
#include "Models.h"
#include <memory>
#include <vector>
#include <functional>

namespace FamilyVault {

// Forward declaration
class IndexManager;

// ═══════════════════════════════════════════════════════════
// Статистика дубликатов
// ═══════════════════════════════════════════════════════════

struct DuplicateStats {
    int64_t totalGroups = 0;        // Групп дубликатов
    int64_t totalDuplicates = 0;    // Всего файлов-дубликатов
    int64_t potentialSavings = 0;   // Можно освободить байт
};

struct BackupStatus {
    int64_t filesWithBackup = 0;    // Файлы с копией на другом устройстве
    int64_t filesWithoutBackup = 0; // Файлы только на этом устройстве
    int64_t sizeWithoutBackup = 0;  // Размер файлов без бэкапа
};

// ═══════════════════════════════════════════════════════════
// DuplicateFinder
// ═══════════════════════════════════════════════════════════

class DuplicateFinder {
public:
    /// @param db База данных
    /// @param indexMgr Опционально: IndexManager для централизованного удаления файлов
    explicit DuplicateFinder(std::shared_ptr<Database> db, IndexManager* indexMgr = nullptr);
    ~DuplicateFinder();

    // ═══════════════════════════════════════════════════════════
    // Поиск дубликатов
    // ═══════════════════════════════════════════════════════════

    /// Найти локальные дубликаты (файлы с одинаковым checksum на ЭТОМ устройстве)
    std::vector<DuplicateGroup> findLocalDuplicates();

    /// Статистика дубликатов
    DuplicateStats getDuplicateStats();

    // ═══════════════════════════════════════════════════════════
    // Файлы без бэкапа
    // ═══════════════════════════════════════════════════════════

    /// Найти файлы без бэкапа (только на этом устройстве)
    std::vector<FileRecord> findFilesWithoutBackup();

    /// Статистика бэкапа
    BackupStatus getBackupStatus();

    // ═══════════════════════════════════════════════════════════
    // Действия
    // ═══════════════════════════════════════════════════════════

    /// Удалить файл (из индекса и с диска)
    void deleteFile(int64_t fileId);

    /// Оставить только один файл из группы дубликатов
    void keepOnlyOne(const DuplicateGroup& group, int64_t keepFileId);

    // ═══════════════════════════════════════════════════════════
    // Checksum
    // ═══════════════════════════════════════════════════════════

    /// Пересчитать checksum для файлов, где он отсутствует
    void computeMissingChecksums(std::function<void(int, int)> onProgress = nullptr);

    /// Вычислить checksum для конкретного файла
    void computeChecksumForFile(int64_t fileId);

private:
    std::shared_ptr<Database> m_db;
    IndexManager* m_indexMgr;  // Optional, not owned

    /// Вычислить SHA-256 checksum
    std::string computeChecksum(const std::string& filePath);

    /// Получить полный путь к файлу
    std::string getFilePath(int64_t fileId);

    /// Маппер FileRecord
    static FileRecord mapFileRecord(sqlite3_stmt* stmt);
};

} // namespace FamilyVault

