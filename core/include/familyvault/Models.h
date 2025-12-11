#pragma once

#include "Types.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Метаданные изображений (EXIF)
// ═══════════════════════════════════════════════════════════

struct ImageMetadata {
    int32_t width = 0;
    int32_t height = 0;
    std::optional<int64_t> takenAt;         // Unix timestamp
    std::optional<std::string> cameraMake;
    std::optional<std::string> cameraModel;
    std::optional<double> latitude;
    std::optional<double> longitude;
    int32_t orientation = 1;
};

// ═══════════════════════════════════════════════════════════
// Запись о файле — компактная версия для списков
// ═══════════════════════════════════════════════════════════

struct FileRecordCompact {
    int64_t id = 0;
    int64_t folderId = 0;
    std::string relativePath;
    std::string folderPath;  // Путь к папке для построения полного пути
    std::string name;
    std::string extension;
    int64_t size = 0;
    ContentType contentType = ContentType::Unknown;
    int64_t modifiedAt = 0;
    bool isRemote = false;
    bool hasThumbnail = false;
};

// ═══════════════════════════════════════════════════════════
// Запись о файле — полная версия
// ═══════════════════════════════════════════════════════════

struct FileRecord {
    int64_t id = 0;
    int64_t folderId = 0;
    std::string relativePath;
    std::string folderPath;  // Путь к папке для построения полного пути
    std::string name;
    std::string extension;
    int64_t size = 0;
    std::string mimeType;
    ContentType contentType = ContentType::Unknown;
    std::optional<std::string> checksum;    // SHA-256
    int64_t createdAt = 0;                  // Unix timestamp
    int64_t modifiedAt = 0;
    int64_t indexedAt = 0;
    Visibility visibility = Visibility::Family;

    // P2P: источник файла (nullopt = локальный)
    std::optional<std::string> sourceDeviceId;
    bool isRemote = false;

    // Sync: для conflict resolution
    int64_t syncVersion = 0;
    std::optional<std::string> lastModifiedBy;  // deviceId

    // Дополнительные данные (загружаются отдельно)
    std::optional<ImageMetadata> imageMeta;
    std::vector<std::string> tags;

    // Helpers
    bool isLocal() const { return !isRemote; }
    bool isPrivate() const { return visibility == Visibility::Private; }
    
    // Получить полный путь
    std::string getFullPath() const {
        if (folderPath.empty()) return relativePath;
        if (folderPath.back() == '/' || folderPath.back() == '\\') {
            return folderPath + relativePath;
        }
        return folderPath + "/" + relativePath;
    }
};

// ═══════════════════════════════════════════════════════════
// Отслеживаемая папка
// ═══════════════════════════════════════════════════════════

struct WatchedFolder {
    int64_t id = 0;
    std::string path;
    std::string name;
    bool enabled = true;
    int64_t lastScanAt = 0;
    int64_t fileCount = 0;
    int64_t totalSize = 0;
    int64_t createdAt = 0;
    Visibility defaultVisibility = Visibility::Family;
};

// ═══════════════════════════════════════════════════════════
// Облачный аккаунт
// ═══════════════════════════════════════════════════════════

struct CloudAccount {
    int64_t id = 0;
    std::string type;
    std::string email;
    std::optional<std::string> displayName;
    std::optional<std::string> avatarUrl;
    std::optional<std::string> changeToken;
    std::optional<int64_t> lastSyncAt;
    int64_t fileCount = 0;
    bool enabled = true;
    int64_t createdAt = 0;
};

// ═══════════════════════════════════════════════════════════
// Облачная отслеживаемая папка
// ═══════════════════════════════════════════════════════════

struct CloudWatchedFolder {
    int64_t id = 0;
    int64_t accountId = 0;
    std::string cloudId;
    std::string name;
    std::optional<std::string> path;
    bool enabled = true;
    std::optional<int64_t> lastSyncAt;
};

// ═══════════════════════════════════════════════════════════
// Тег
// ═══════════════════════════════════════════════════════════

struct Tag {
    int64_t id = 0;
    std::string name;
    TagSource source = TagSource::User;
    int64_t fileCount = 0;
};

// ═══════════════════════════════════════════════════════════
// Поисковый запрос
// ═══════════════════════════════════════════════════════════

struct SearchQuery {
    std::string text;                           // Полнотекстовый поиск
    std::optional<ContentType> contentType;
    std::optional<std::string> extension;
    std::optional<int64_t> folderId;
    std::optional<int64_t> dateFrom;            // Unix timestamp
    std::optional<int64_t> dateTo;
    std::optional<int64_t> minSize;
    std::optional<int64_t> maxSize;
    std::vector<std::string> tags;              // Должен иметь ВСЕ теги
    std::vector<std::string> excludeTags;
    std::optional<Visibility> visibility;
    bool includeRemote = false;                 // По умолчанию: только локальные

    int32_t limit = 100;
    int32_t offset = 0;

    SortBy sortBy = SortBy::Relevance;
    bool sortAsc = false;
};

// ═══════════════════════════════════════════════════════════
// Результат поиска
// ═══════════════════════════════════════════════════════════

struct SearchResult {
    FileRecord file;
    double score = 0.0;
    std::string snippet;    // Фрагмент с подсветкой совпадения
};

/// Компактный результат поиска для списков
struct SearchResultCompact {
    FileRecordCompact file;
    double score = 0.0;
};

// ═══════════════════════════════════════════════════════════
// Информация об устройстве (P2P)
// ═══════════════════════════════════════════════════════════

struct DeviceInfo {
    std::string deviceId;           // UUID
    std::string deviceName;         // "John's PC"
    DeviceType deviceType = DeviceType::Desktop;
    std::string ipAddress;
    uint16_t servicePort = 0;
    int64_t lastSeenAt = 0;
    bool isOnline = false;
    bool isConnected = false;
    int64_t fileCount = 0;
    int64_t lastSyncAt = 0;
};

// ═══════════════════════════════════════════════════════════
// Группа дубликатов
// ═══════════════════════════════════════════════════════════

struct DuplicateGroup {
    std::string checksum;
    int64_t fileSize = 0;
    std::vector<FileRecord> localCopies;    // Дубликаты на ЭТОМ устройстве
    std::vector<FileRecord> remoteCopies;   // Копии на других устройствах

    int64_t potentialSavings() const {
        if (localCopies.size() <= 1) return 0;
        return fileSize * (static_cast<int64_t>(localCopies.size()) - 1);
    }

    bool hasRemoteBackup() const { return !remoteCopies.empty(); }
};

} // namespace FamilyVault

