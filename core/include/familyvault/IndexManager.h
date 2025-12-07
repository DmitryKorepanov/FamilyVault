#pragma once

#include "Database.h"
#include "Models.h"
#include "FileScanner.h"
#include <memory>
#include <vector>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Статистика индекса
// ═══════════════════════════════════════════════════════════

struct IndexStats {
    int64_t totalFolders = 0;
    int64_t totalFiles = 0;
    int64_t totalSize = 0;
    int64_t imageCount = 0;
    int64_t videoCount = 0;
    int64_t documentCount = 0;
    int64_t audioCount = 0;
    int64_t archiveCount = 0;
    int64_t otherCount = 0;
};

// ═══════════════════════════════════════════════════════════
// IndexManager
// ═══════════════════════════════════════════════════════════

class IndexManager {
public:
    explicit IndexManager(std::shared_ptr<Database> db);
    ~IndexManager();

    // ═══════════════════════════════════════════════════════════
    // Управление папками
    // ═══════════════════════════════════════════════════════════

    /// Добавить папку для отслеживания
    int64_t addFolder(const std::string& path, const std::string& name = "",
                      Visibility visibility = Visibility::Family);

    /// Удалить папку и все её файлы из индекса
    void removeFolder(int64_t folderId);

    /// Включить/отключить папку
    void setFolderEnabled(int64_t folderId, bool enabled);

    /// Изменить видимость папки
    void setFolderVisibility(int64_t folderId, Visibility visibility);

    /// Получить все папки
    std::vector<WatchedFolder> getFolders() const;

    /// Получить папку по ID
    std::optional<WatchedFolder> getFolder(int64_t folderId) const;

    // ═══════════════════════════════════════════════════════════
    // Сканирование
    // ═══════════════════════════════════════════════════════════

    /// Сканировать одну папку
    void scanFolder(int64_t folderId, ScanProgressCallback onProgress = nullptr);

    /// Сканировать все включённые папки
    void scanAllFolders(ScanProgressCallback onProgress = nullptr);

    /// Остановить текущее сканирование
    void stopScan();

    /// Проверка статуса сканирования
    bool isScanning() const;

    // ═══════════════════════════════════════════════════════════
    // Получение файлов
    // ═══════════════════════════════════════════════════════════

    /// Получить файл по ID (полная версия)
    std::optional<FileRecord> getFile(int64_t fileId) const;

    /// Получить файл по пути
    std::optional<FileRecord> getFileByPath(int64_t folderId, const std::string& relativePath) const;

    /// Получить недавние файлы
    std::vector<FileRecord> getRecentFiles(int limit = 50) const;

    /// Получить файлы папки
    std::vector<FileRecord> getFilesByFolder(int64_t folderId, int limit = 1000, int offset = 0) const;

    /// Получить файлы (компактная версия для списков)
    std::vector<FileRecordCompact> getRecentFilesCompact(int limit = 50) const;

    /// Получить файлы папки (компактная версия)
    std::vector<FileRecordCompact> getFilesByFolderCompact(int64_t folderId, int limit = 1000, int offset = 0) const;

    /// Изменить видимость файла
    void setFileVisibility(int64_t fileId, std::optional<Visibility> visibility);

    /// Удалить файл из индекса (и опционально с диска)
    /// @param fileId ID файла
    /// @param deleteFromDisk true - удалить также с диска
    void deleteFile(int64_t fileId, bool deleteFromDisk = true);

    // ═══════════════════════════════════════════════════════════
    // Статистика
    // ═══════════════════════════════════════════════════════════

    IndexStats getStats() const;

private:
    std::shared_ptr<Database> m_db;
    std::unique_ptr<FileScanner> m_scanner;
    CancellationToken m_cancelToken;

    /// Добавить или обновить файл в индексе
    int64_t upsertFile(int64_t folderId, const ScannedFile& file);

    /// Удалить файлы, которых больше нет на диске
    void deleteRemovedFiles(int64_t folderId, int64_t scanStartTime);

    /// Обновить статистику папки
    void updateFolderStats(int64_t folderId);

    /// Маппер для FileRecord
    static FileRecord mapFileRecord(sqlite3_stmt* stmt);

    /// Маппер для FileRecordCompact
    static FileRecordCompact mapFileRecordCompact(sqlite3_stmt* stmt);

    /// Маппер для WatchedFolder
    static WatchedFolder mapWatchedFolder(sqlite3_stmt* stmt);
};

} // namespace FamilyVault

