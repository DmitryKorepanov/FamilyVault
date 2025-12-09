// RemoteFileAccess.h — Доступ к файлам на удалённых устройствах
// См. SPECIFICATIONS.md, раздел 7.5

#pragma once

#include "../export.h"
#include "../Models.h"
#include "NetworkProtocol.h"
#include "PeerConnection.h"
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <atomic>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Константы
// ═══════════════════════════════════════════════════════════

constexpr size_t FILE_CHUNK_SIZE = 64 * 1024;   // 64 KB chunks
constexpr int FILE_REQUEST_TIMEOUT_SEC = 60;

// ═══════════════════════════════════════════════════════════
// FileTransferStatus — статус передачи файла
// ═══════════════════════════════════════════════════════════

enum class FileTransferStatus {
    Pending,        // Ожидает начала
    InProgress,     // Передаётся
    Completed,      // Успешно завершена
    Failed,         // Ошибка
    Cancelled       // Отменена
};

// ═══════════════════════════════════════════════════════════
// FileTransferProgress — прогресс передачи
// ═══════════════════════════════════════════════════════════

struct FV_API FileTransferProgress {
    std::string requestId;   // Уникальный ID запроса
    std::string deviceId;    // ID устройства-источника
    int64_t fileId;
    std::string fileName;
    int64_t totalSize;
    int64_t transferredSize;
    FileTransferStatus status;
    std::string error;
    std::string localPath;  // Путь к локальному файлу (после завершения)

    double progress() const {
        return totalSize > 0 ? static_cast<double>(transferredSize) / totalSize : 0.0;
    }
};

// ═══════════════════════════════════════════════════════════
// RemoteFileAccess — доступ к удалённым файлам
// ═══════════════════════════════════════════════════════════

class FV_API RemoteFileAccess {
public:
    /// Создать менеджер доступа к файлам
    /// @param cacheDir Директория для кэширования файлов
    RemoteFileAccess(const std::string& cacheDir);
    ~RemoteFileAccess();

    // Запрет копирования
    RemoteFileAccess(const RemoteFileAccess&) = delete;
    RemoteFileAccess& operator=(const RemoteFileAccess&) = delete;

    // ═══════════════════════════════════════════════════════════
    // File Requests
    // ═══════════════════════════════════════════════════════════

    using ProgressCallback = std::function<void(const FileTransferProgress&)>;
    using CompleteCallback = std::function<void(const FileTransferProgress&)>;  // Full progress with localPath
    using ErrorCallback = std::function<void(const FileTransferProgress&)>;     // Full progress with error

    /// Запросить файл с удалённого устройства
    /// @param peer Соединение с устройством
    /// @param deviceId ID удалённого устройства (для уникальности кэша)
    /// @param fileId ID файла на удалённом устройстве
    /// @param fileName Имя файла (для сохранения)
    /// @param expectedSize Ожидаемый размер (для прогресса)
    /// @param checksum Ожидаемая контрольная сумма (опционально)
    /// @return Request ID для отслеживания
    std::string requestFile(
        std::shared_ptr<PeerConnection> peer,
        const std::string& deviceId,
        int64_t fileId,
        const std::string& fileName,
        int64_t expectedSize,
        const std::string& checksum = "");

    /// Отменить запрос файла
    void cancelRequest(const std::string& requestId);

    /// Отменить все запросы к устройству
    void cancelAllRequests(const std::string& deviceId);

    // ═══════════════════════════════════════════════════════════
    // Message Handling
    // ═══════════════════════════════════════════════════════════

    /// Обработать запрос файла (серверная сторона)
    /// @param peer Соединение с запрашивающим устройством
    /// @param request Сообщение запроса
    /// @param getFilePath Функция для получения локального пути по fileId
    void handleFileRequest(
        std::shared_ptr<PeerConnection> peer,
        const Message& request,
        std::function<std::string(int64_t fileId)> getFilePath);

    /// Обработать ответ на запрос файла
    void handleFileResponse(const Message& response);

    /// Обработать chunk файла
    void handleFileChunk(const Message& chunk);

    /// Обработать "файл не найден"
    void handleFileNotFound(const Message& msg);

    // ═══════════════════════════════════════════════════════════
    // Status & Progress
    // ═══════════════════════════════════════════════════════════

    /// Получить прогресс передачи
    FileTransferProgress getProgress(const std::string& requestId) const;

    /// Получить все активные передачи
    std::vector<FileTransferProgress> getActiveTransfers() const;

    /// Проверить, есть ли активные передачи
    bool hasActiveTransfers() const;

    // ═══════════════════════════════════════════════════════════
    // Cache
    // ═══════════════════════════════════════════════════════════

    /// Проверить, есть ли файл в кэше
    /// @param deviceId ID устройства-источника
    /// @param fileId ID файла
    /// @param checksum Контрольная сумма (опционально для верификации)
    bool isCached(const std::string& deviceId, int64_t fileId, const std::string& checksum = "") const;

    /// Получить путь к кэшированному файлу
    /// @param deviceId ID устройства-источника
    /// @param fileId ID файла
    std::string getCachedPath(const std::string& deviceId, int64_t fileId) const;

    /// Очистить кэш
    void clearCache();

    /// Получить размер кэша (в байтах)
    int64_t getCacheSize() const;

    // ═══════════════════════════════════════════════════════════
    // Callbacks
    // ═══════════════════════════════════════════════════════════

    void onProgress(ProgressCallback callback);
    void onComplete(CompleteCallback callback);
    void onError(ErrorCallback callback);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault

