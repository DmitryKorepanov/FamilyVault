// IndexSyncManager.h — Синхронизация индекса файлов между устройствами
// См. SPECIFICATIONS.md, раздел 7.4

#pragma once

#include "../export.h"
#include "../Models.h"
#include "../Database.h"
#include "NetworkProtocol.h"
#include "PeerConnection.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>

namespace FamilyVault {

// Forward declarations
class IndexManager;

// ═══════════════════════════════════════════════════════════
// Константы синхронизации
// ═══════════════════════════════════════════════════════════

constexpr int SYNC_INTERVAL_SEC = 300;          // 5 минут между delta sync
constexpr int SYNC_BATCH_SIZE = 100;            // Файлов в одном сообщении
constexpr int64_t FULL_SYNC_TIMESTAMP = 0;      // Для запроса полного индекса

// ═══════════════════════════════════════════════════════════
// SyncProgress — прогресс синхронизации
// ═══════════════════════════════════════════════════════════

struct FV_API SyncProgress {
    std::string deviceId;
    int64_t totalFiles;
    int64_t receivedFiles;
    int64_t sentFiles;
    bool isComplete;
    std::string error;

    double progress() const {
        return totalFiles > 0 ? static_cast<double>(receivedFiles) / totalFiles : 0.0;
    }
};

// ═══════════════════════════════════════════════════════════
// RemoteFileRecord — запись о файле с удалённого устройства
// ═══════════════════════════════════════════════════════════

struct FV_API RemoteFileRecord {
    int64_t localId;            // ID в локальной БД (может отличаться)
    int64_t remoteId;           // ID на исходном устройстве
    std::string sourceDeviceId; // UUID устройства-источника
    
    std::string path;           // Путь на исходном устройстве
    std::string name;
    std::string mimeType;
    int64_t size;
    int64_t modifiedAt;
    std::string checksum;
    std::string extractedText;
    
    int64_t syncedAt;           // Когда получена запись
    bool isDeleted;             // Помечен как удалённый
};

// ═══════════════════════════════════════════════════════════
// IndexSyncManager — управление синхронизацией индекса
// ═══════════════════════════════════════════════════════════

class FV_API IndexSyncManager {
public:
    /// Создать менеджер синхронизации
    /// @param db База данных
    /// @param deviceId ID этого устройства
    IndexSyncManager(std::shared_ptr<Database> db, const std::string& deviceId);
    ~IndexSyncManager();

    // Запрет копирования
    IndexSyncManager(const IndexSyncManager&) = delete;
    IndexSyncManager& operator=(const IndexSyncManager&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Синхронизация
    // ═══════════════════════════════════════════════════════════

    /// Запросить синхронизацию с устройством
    /// @param peer Соединение с пиром
    /// @param fullSync true для полной синхронизации
    void requestSync(std::shared_ptr<PeerConnection> peer, bool fullSync = false);

    /// Обработать входящий запрос синхронизации
    void handleSyncRequest(std::shared_ptr<PeerConnection> peer, const Message& request);

    /// Обработать входящие данные синхронизации
    void handleSyncResponse(std::shared_ptr<PeerConnection> peer, const Message& response);

    /// Обработать delta (одну запись)
    void handleIndexDelta(std::shared_ptr<PeerConnection> peer, const Message& delta);

    // ═══════════════════════════════════════════════════════════
    // Локальные данные для отправки
    // ═══════════════════════════════════════════════════════════

    /// Получить локальные Family файлы изменённые после timestamp
    /// @param sinceTimestamp Timestamp (0 = все файлы)
    /// @return Список файлов для отправки
    std::vector<FileRecord> getLocalChangesSince(int64_t sinceTimestamp) const;

    /// Получить timestamp последней синхронизации с устройством
    int64_t getLastSyncTimestamp(const std::string& deviceId) const;

    /// Установить timestamp последней синхронизации
    void setLastSyncTimestamp(const std::string& deviceId, int64_t timestamp);

    // ═══════════════════════════════════════════════════════════
    // Удалённые файлы
    // ═══════════════════════════════════════════════════════════

    /// Получить удалённые файлы с устройства
    std::vector<RemoteFileRecord> getRemoteFiles(const std::string& deviceId) const;

    /// Получить все удалённые файлы
    std::vector<RemoteFileRecord> getAllRemoteFiles() const;

    /// Получить удалённый файл по ID
    std::optional<RemoteFileRecord> getRemoteFile(int64_t localId) const;

    /// Поиск по удалённым файлам
    std::vector<RemoteFileRecord> searchRemoteFiles(const std::string& query, int limit = 50) const;

    /// Количество удалённых файлов
    int64_t getRemoteFileCount() const;

    /// Количество удалённых файлов с устройства
    int64_t getRemoteFileCount(const std::string& deviceId) const;

    // ═══════════════════════════════════════════════════════════
    // Прогресс и состояние
    // ═══════════════════════════════════════════════════════════

    /// Получить прогресс синхронизации с устройством
    SyncProgress getSyncProgress(const std::string& deviceId) const;

    /// Проверить, идёт ли синхронизация
    bool isSyncing() const;

    /// Проверить, идёт ли синхронизация с устройством
    bool isSyncingWith(const std::string& deviceId) const;

    // ═══════════════════════════════════════════════════════════
    // Callbacks
    // ═══════════════════════════════════════════════════════════

    using ProgressCallback = std::function<void(const SyncProgress&)>;
    using CompleteCallback = std::function<void(const std::string& deviceId, int64_t filesReceived)>;
    using ErrorCallback = std::function<void(const std::string& deviceId, const std::string& error)>;

    void onProgress(ProgressCallback callback);
    void onComplete(CompleteCallback callback);
    void onError(ErrorCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault


