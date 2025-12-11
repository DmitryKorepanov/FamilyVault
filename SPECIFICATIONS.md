# FamilyVault — Технические спецификации

**Этот документ — единственный источник истины для всех общих концепций.**

---

## 1. Структура заголовочных файлов

```
core/include/familyvault/
├── core.h              # Версия, константы
├── export.h            # FV_API макрос для DLL export
│
├── Types.h             # ВСЕ enum'ы и базовые типы (единственное место!)
├── Models.h            # Структуры данных (использует Types.h)
├── Database.h          # Работа с SQLite
│
├── FileScanner.h       # Сканирование файлов
├── IndexManager.h      # Индексация
├── SearchEngine.h      # Поиск
├── TagManager.h        # Теги
├── DuplicateFinder.h   # Дубликаты
│
├── TextExtractor.h     # Интерфейсы экстракторов (Этап 3)
├── ContentIndexer.h    # Фоновая индексация контента
├── MimeTypeDetector.h  # Определение MIME типов
│
├── SecureStorage.h     # Хранение секретов (ЕДИНСТВЕННОЕ место!)
├── FamilyPairing.h     # Семейный pairing (PIN/QR)
│
├── Network/            # P2P (Этап 5)
│   ├── Discovery.h         # UDP broadcast обнаружение
│   ├── NetworkProtocol.h   # Типы сообщений, сериализация
│   ├── TlsPsk.h            # TLS 1.3 PSK обёртка над OpenSSL
│   ├── PeerConnection.h    # TCP соединение с peer
│   ├── NetworkManager.h    # Координатор P2P сети
│   ├── IndexSyncManager.h  # Синхронизация индекса
│   ├── RemoteFileAccess.h  # Запросы и передача файлов
│   └── PairingServer.h     # TCP сервер для pairing
│
└── familyvault_c.h     # C API для FFI (единственная версия!)
```

---

## 2. Типы и перечисления (Types.h) — ЕДИНСТВЕННОЕ МЕСТО

```cpp
// include/familyvault/Types.h
#pragma once

#include <cstdint>
#include <string>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Типы контента
// ═══════════════════════════════════════════════════════════

enum class ContentType : int32_t {
    Unknown = 0,
    Image = 1,
    Video = 2,
    Audio = 3,
    Document = 4,
    Archive = 5,
    Other = 99
};

const char* contentTypeToString(ContentType type);
ContentType contentTypeFromString(const std::string& str);
ContentType contentTypeFromMime(const std::string& mimeType);

// ═══════════════════════════════════════════════════════════
// Видимость файлов (Private = только локально, Family = синхронизируется)
// ═══════════════════════════════════════════════════════════

enum class Visibility : int32_t {
    Private = 0,    // Только на этом устройстве
    Family = 1      // Виден семье, синхронизируется через P2P
};

const char* visibilityToString(Visibility v);
Visibility visibilityFromString(const std::string& str);

// ═══════════════════════════════════════════════════════════
// Источник тегов
// ═══════════════════════════════════════════════════════════

enum class TagSource : int32_t {
    User = 0,   // Добавлен пользователем
    Auto = 1,   // Автоматический (по типу/дате/размеру)
    AI = 2      // От AI (в будущем)
};

// ═══════════════════════════════════════════════════════════
// Состояние P2P соединения
// ═══════════════════════════════════════════════════════════

enum class ConnectionState : int32_t {
    Disconnected = 0,
    Connecting = 1,
    Authenticating = 2,
    Connected = 3,
    AuthFailed = 4,
    Error = 99
};

// ═══════════════════════════════════════════════════════════
// Типы устройств
// ═══════════════════════════════════════════════════════════

enum class DeviceType : int32_t {
    Desktop = 0,
    Mobile = 1,
    Tablet = 2
};

// ═══════════════════════════════════════════════════════════
// Порядок сортировки
// ═══════════════════════════════════════════════════════════

enum class SortBy : int32_t {
    Relevance = 0,
    Name = 1,
    Date = 2,
    Size = 3
};

} // namespace FamilyVault
```

---

## 3. Модели данных (Models.h)

```cpp
// include/familyvault/Models.h
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
// Запись о файле (две версии для оптимизации)
// ═══════════════════════════════════════════════════════════

// Компактная версия для списков (меньше данных через FFI)
struct FileRecordCompact {
    int64_t id = 0;
    std::string name;
    std::string extension;
    int64_t size = 0;
    ContentType contentType = ContentType::Unknown;
    int64_t modifiedAt = 0;
    bool isRemote = false;
    bool hasThumbnail = false;  // для UI: показывать placeholder или нет
};

// Полная версия для детального view
struct FileRecord {
    int64_t id = 0;
    int64_t folderId = 0;
    std::string relativePath;
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
    
    // P2P: источник файла (NULL = локальный)
    std::optional<std::string> sourceDeviceId;
    bool isRemote = false;
    
    // Sync: для conflict resolution (см. раздел 13)
    int64_t syncVersion = 0;
    std::optional<std::string> lastModifiedBy;  // deviceId
    
    // Дополнительные данные (загружаются отдельно)
    std::optional<ImageMetadata> imageMeta;
    std::vector<std::string> tags;
    
    // Helpers
    bool isLocal() const { return !isRemote; }
    bool isPrivate() const { return visibility == Visibility::Private; }
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
    bool includeRemote = false;                 // По умолчанию: только локальные файлы
    
    int32_t limit = 100;
    int32_t offset = 0;
    
    enum class SortBy : int32_t { 
        Relevance = 0, 
        Name = 1, 
        Date = 2, 
        Size = 3 
    };
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
    int64_t fileCount = 0;          // Кол-во файлов на устройстве
    int64_t lastSyncAt = 0;
};

// ═══════════════════════════════════════════════════════════
// Группа дубликатов
// ═══════════════════════════════════════════════════════════

struct DuplicateGroup {
    std::string checksum;
    int64_t fileSize = 0;
    std::vector<FileRecord> localCopies;    // Дубликаты на ЭТОМ устройстве
    std::vector<FileRecord> remoteCopies;   // Копии на других устройствах (бэкапы)
    
    int64_t potentialSavings() const {
        if (localCopies.size() <= 1) return 0;
        return fileSize * (static_cast<int64_t>(localCopies.size()) - 1);
    }
    
    bool hasRemoteBackup() const { return !remoteCopies.empty(); }
};

} // namespace FamilyVault
```

---

## 4. JSON схемы для FFI

Все данные между C++ и Dart передаются как JSON строки.

### 4.1 FileRecordCompact JSON (для списков)

```json
{
  "id": 123,
  "name": "IMG_001.jpg",
  "extension": "jpg",
  "size": 2456789,
  "contentType": 1,
  "modifiedAt": 1704153600,
  "isRemote": false,
  "hasThumbnail": true
}
```

### 4.2 FileRecord JSON (полная версия)

```json
{
  "id": 123,
  "folderId": 1,
  "relativePath": "photos/2024/IMG_001.jpg",
  "name": "IMG_001.jpg",
  "extension": "jpg",
  "size": 2456789,
  "mimeType": "image/jpeg",
  "contentType": 1,
  "checksum": "sha256:abc123...",
  "createdAt": 1704067200,
  "modifiedAt": 1704153600,
  "indexedAt": 1704240000,
  "visibility": 1,
  "sourceDeviceId": null,
  "isRemote": false,
  "syncVersion": 42,
  "lastModifiedBy": "device-uuid-123",
  "tags": ["photo", "2024", "vacation"],
  "imageMeta": {
    "width": 4032,
    "height": 3024,
    "takenAt": 1704067200,
    "cameraMake": "Apple",
    "cameraModel": "iPhone 15",
    "latitude": 55.7558,
    "longitude": 37.6173,
    "orientation": 1
  }
}
```

### 4.3 SearchQuery JSON

```json
{
  "text": "vacation photos",
  "contentType": 1,
  "extension": null,
  "folderId": null,
  "dateFrom": 1704067200,
  "dateTo": null,
  "minSize": null,
  "maxSize": null,
  "tags": ["vacation"],
  "excludeTags": [],
  "visibility": null,
  "includeRemote": false,
  "limit": 50,
  "offset": 0,
  "sortBy": 2,
  "sortAsc": false
}
```

### 4.4 DeviceInfo JSON

```json
{
  "deviceId": "550e8400-e29b-41d4-a716-446655440000",
  "deviceName": "John's PC",
  "deviceType": 0,
  "ipAddress": "192.168.1.100",
  "servicePort": 45678,
  "lastSeenAt": 1704240000,
  "isOnline": true,
  "isConnected": true,
  "fileCount": 15234,
  "lastSyncAt": 1704239000
}
```

### 4.5 Enum кодирование в JSON

| Enum | JSON type | Values |
|------|-----------|--------|
| ContentType | int32 | 0=Unknown, 1=Image, 2=Video, 3=Audio, 4=Document, 5=Archive, 99=Other |
| Visibility | int32 | 0=Private, 1=Family |
| TagSource | int32 | 0=User, 1=Auto, 2=AI |
| DeviceType | int32 | 0=Desktop, 1=Mobile, 2=Tablet |
| SortBy | int32 | 0=Relevance, 1=Name, 2=Date, 3=Size |

---

## 5. C API (familyvault_c.h) — ПОЛНАЯ ВЕРСИЯ

```c
// include/familyvault/familyvault_c.h
// ЕДИНСТВЕННЫЙ ИСТОЧНИК ИСТИНЫ для C API
#ifndef FAMILYVAULT_C_H
#define FAMILYVAULT_C_H

#include "export.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════
// Opaque handles (struct-based для типобезопасности)
// ═══════════════════════════════════════════════════════════

typedef struct FVDatabase_* FVDatabase;
typedef struct FVIndexManager_* FVIndexManager;
typedef struct FVSearchEngine_* FVSearchEngine;
typedef struct FVTagManager_* FVTagManager;
typedef struct FVDuplicateFinder_* FVDuplicateFinder;
typedef struct FVContentIndexer_* FVContentIndexer;
typedef struct FVSecureStorage_* FVSecureStorage;
typedef struct FVFamilyPairing_* FVFamilyPairing;
typedef struct FVNetworkDiscovery_* FVNetworkDiscovery;
typedef struct FVNetworkManager_* FVNetworkManager;

// ═══════════════════════════════════════════════════════════
// Коды ошибок
// ═══════════════════════════════════════════════════════════

typedef enum {
    FV_OK = 0,
    FV_ERROR_INVALID_ARGUMENT = 1,
    FV_ERROR_DATABASE = 2,
    FV_ERROR_IO = 3,
    FV_ERROR_NOT_FOUND = 4,
    FV_ERROR_ALREADY_EXISTS = 5,
    FV_ERROR_AUTH_FAILED = 6,
    FV_ERROR_NETWORK = 7,
    FV_ERROR_BUSY = 8,              // Resource busy (e.g., DB has active managers)
    FV_ERROR_INTERNAL = 99
} FVError;

// ═══════════════════════════════════════════════════════════
// Общие функции
// ═══════════════════════════════════════════════════════════

FV_API const char* fv_version(void);
FV_API const char* fv_error_message(FVError error);

/// Получить последнюю ошибку (thread-local)
FV_API FVError fv_last_error(void);

/// Получить сообщение последней ошибки (thread-local)
FV_API const char* fv_last_error_message(void);

/// Очистить состояние ошибки
FV_API void fv_clear_error(void);

/// Освободить строку, выделенную библиотекой
FV_API void fv_free_string(char* str);

// ═══════════════════════════════════════════════════════════
// Database
// ═══════════════════════════════════════════════════════════

/// Открыть базу данных
/// @note База использует reference counting. Закрытие возможно только
///       когда все менеджеры уничтожены.
FV_API FVDatabase fv_database_open(const char* path, FVError* out_error);

/// Закрыть базу данных
/// @return FV_OK при успехе, FV_ERROR_BUSY если есть живые менеджеры
FV_API FVError fv_database_close(FVDatabase db);

/// Инициализировать БД (применить миграции)
FV_API FVError fv_database_initialize(FVDatabase db);

/// Проверить, инициализирована ли БД
FV_API int32_t fv_database_is_initialized(FVDatabase db);

// ═══════════════════════════════════════════════════════════
// Cloud Accounts & Watched Folders
// ═══════════════════════════════════════════════════════════

/// Добавить облачный аккаунт. Возвращает ID аккаунта или -1 при ошибке.
FV_API int64_t fv_cloud_account_add(FVDatabase db,
                                    const char* type,
                                    const char* email,
                                    const char* display_name,
                                    const char* avatar_url);

/// Получить список аккаунтов (JSON array CloudAccount)
FV_API char* fv_cloud_account_list(FVDatabase db);

/// Удалить облачный аккаунт (каскадно удаляет папки и файлы)
FV_API bool fv_cloud_account_remove(FVDatabase db, int64_t account_id);

/// Включить/отключить облачный аккаунт
FV_API bool fv_cloud_account_set_enabled(FVDatabase db, int64_t account_id, bool enabled);

/// Обновить состояние синхронизации аккаунта (file_count, change_token)
FV_API bool fv_cloud_account_update_sync(FVDatabase db,
                                         int64_t account_id,
                                         int64_t file_count,
                                         const char* change_token);

/// Добавить облачную папку для синхронизации. Возвращает ID папки или -1 при ошибке.
FV_API int64_t fv_cloud_folder_add(FVDatabase db,
                                   int64_t account_id,
                                   const char* cloud_id,
                                   const char* name,
                                   const char* path);

/// Удалить облачную папку
FV_API bool fv_cloud_folder_remove(FVDatabase db, int64_t folder_id);

/// Получить папки аккаунта (JSON array CloudWatchedFolder)
FV_API char* fv_cloud_folder_list(FVDatabase db, int64_t account_id);

/// Включить/отключить облачную папку
FV_API bool fv_cloud_folder_set_enabled(FVDatabase db, int64_t folder_id, bool enabled);

/// Добавить/обновить файл из облака
/// @param file_json JSON объект CloudFile
FV_API bool fv_cloud_file_upsert(FVDatabase db, int64_t account_id, const char* file_json);

/// Удалить файл из облака
FV_API bool fv_cloud_file_remove(FVDatabase db, int64_t account_id, const char* cloud_id);

/// Удалить все файлы аккаунта
FV_API bool fv_cloud_file_remove_all(FVDatabase db, int64_t account_id);

Примеры JSON:

```json
{
  "id": 1,
  "type": "google_drive",
  "email": "user@example.com",
  "displayName": "User",
  "avatarUrl": "https://avatar",
  "changeToken": "startToken",
  "lastSyncAt": 1704240000,
  "fileCount": 123,
  "enabled": true,
  "createdAt": 1704239000
}

{
  "id": 10,
  "accountId": 1,
  "cloudId": "abcdef",
  "name": "My Photos",
  "path": "My Drive/Photos",
  "enabled": true,
  "lastSyncAt": null
}
```

// ═══════════════════════════════════════════════════════════
// Index Manager
// ═══════════════════════════════════════════════════════════

/// Создать IndexManager
/// @note Увеличивает reference count базы данных
FV_API FVIndexManager fv_index_create(FVDatabase db);

/// Уничтожить IndexManager
/// @note Автоматически уменьшает reference count базы данных
FV_API void fv_index_destroy(FVIndexManager mgr);

// Folders
FV_API int64_t fv_index_add_folder(FVIndexManager mgr, const char* path, 
                                    const char* name, int32_t visibility);
FV_API FVError fv_index_remove_folder(FVIndexManager mgr, int64_t folder_id);
FV_API char* fv_index_get_folders(FVIndexManager mgr);  // JSON array

// Scanning
typedef void (*FVScanCallback)(int64_t processed, int64_t total, 
                               const char* current_file, void* user_data);
FV_API FVError fv_index_scan_folder(FVIndexManager mgr, int64_t folder_id,
                                     FVScanCallback cb, void* user_data);
FV_API FVError fv_index_scan_all(FVIndexManager mgr, FVScanCallback cb, void* user_data);
FV_API void fv_index_stop_scan(FVIndexManager mgr);
FV_API int32_t fv_index_is_scanning(FVIndexManager mgr);

// Files — полная версия (FileRecord)
FV_API char* fv_index_get_file(FVIndexManager mgr, int64_t file_id);  // JSON FileRecord
FV_API char* fv_index_get_recent(FVIndexManager mgr, int32_t limit);  // JSON array FileRecord

// Files — компактная версия для списков (FileRecordCompact, меньше overhead)
FV_API char* fv_index_get_recent_compact(FVIndexManager mgr, int32_t limit);  // JSON array
FV_API char* fv_index_get_by_folder_compact(FVIndexManager mgr, int64_t folder_id, 
                                             int32_t limit, int32_t offset);  // JSON array

FV_API char* fv_index_get_stats(FVIndexManager mgr);  // JSON

/// Удалить файл из индекса (и опционально с диска)
FV_API FVError fv_index_delete_file(FVIndexManager mgr, int64_t file_id, int32_t delete_from_disk);

/// Оптимизация БД (rebuild FTS + VACUUM)
FV_API FVError fv_index_optimize_database(FVIndexManager mgr);

// Visibility
FV_API FVError fv_index_set_folder_visibility(FVIndexManager mgr, int64_t folder_id, int32_t visibility);
FV_API FVError fv_index_set_folder_enabled(FVIndexManager mgr, int64_t folder_id, int32_t enabled);
FV_API FVError fv_index_set_file_visibility(FVIndexManager mgr, int64_t file_id, int32_t visibility);

// ═══════════════════════════════════════════════════════════
// Search Engine
// ═══════════════════════════════════════════════════════════

FV_API FVSearchEngine fv_search_create(FVDatabase db);
FV_API void fv_search_destroy(FVSearchEngine engine);

// Полная версия (SearchResult с FileRecord)
FV_API char* fv_search_query(FVSearchEngine engine, const char* query_json);  // JSON array

// Компактная версия для UI списков (SearchResult с FileRecordCompact)
FV_API char* fv_search_query_compact(FVSearchEngine engine, const char* query_json);  // JSON array

FV_API int64_t fv_search_count(FVSearchEngine engine, const char* query_json);
FV_API char* fv_search_suggest(FVSearchEngine engine, const char* prefix, int32_t limit);

// ═══════════════════════════════════════════════════════════
// Tag Manager
// ═══════════════════════════════════════════════════════════

/// @note Увеличивает reference count базы данных
FV_API FVTagManager fv_tags_create(FVDatabase db);
FV_API void fv_tags_destroy(FVTagManager mgr);

FV_API FVError fv_tags_add(FVTagManager mgr, int64_t file_id, const char* tag);
FV_API FVError fv_tags_remove(FVTagManager mgr, int64_t file_id, const char* tag);
FV_API char* fv_tags_get_for_file(FVTagManager mgr, int64_t file_id);  // JSON array
FV_API char* fv_tags_get_all(FVTagManager mgr);  // JSON array with counts
FV_API char* fv_tags_get_popular(FVTagManager mgr, int32_t limit);  // JSON array

// ═══════════════════════════════════════════════════════════
// Duplicate Finder
// ═══════════════════════════════════════════════════════════

/// Создать DuplicateFinder
/// @param db База данных
/// @param indexMgr Опционально: IndexManager для централизованного удаления
FV_API FVDuplicateFinder fv_duplicates_create(FVDatabase db, FVIndexManager indexMgr);
FV_API void fv_duplicates_destroy(FVDuplicateFinder finder);

FV_API char* fv_duplicates_find(FVDuplicateFinder finder);  // JSON array
FV_API char* fv_duplicates_stats(FVDuplicateFinder finder);  // JSON
FV_API char* fv_duplicates_without_backup(FVDuplicateFinder finder);  // JSON array
FV_API FVError fv_duplicates_delete_file(FVDuplicateFinder finder, int64_t file_id);

/// Вычислить недостающие checksums
typedef void (*FVChecksumProgressCallback)(int32_t processed, int32_t total, void* user_data);
FV_API FVError fv_duplicates_compute_checksums(FVDuplicateFinder finder,
                                                FVChecksumProgressCallback cb, void* user_data);

// ═══════════════════════════════════════════════════════════
// Content Indexer (Text Extraction)
// ═══════════════════════════════════════════════════════════

FV_API FVContentIndexer fv_content_indexer_create(FVDatabase db);
FV_API void fv_content_indexer_destroy(FVContentIndexer indexer);

/// Запустить фоновую обработку текста
FV_API FVError fv_content_indexer_start(FVContentIndexer indexer);

/// Остановить фоновую обработку
FV_API FVError fv_content_indexer_stop(FVContentIndexer indexer, int32_t wait);

/// Проверить, запущена ли обработка
FV_API int32_t fv_content_indexer_is_running(FVContentIndexer indexer);

/// Обработать конкретный файл (синхронно)
FV_API FVError fv_content_indexer_process_file(FVContentIndexer indexer, int64_t file_id);

/// Добавить все необработанные файлы в очередь
FV_API int32_t fv_content_indexer_enqueue_unprocessed(FVContentIndexer indexer);

/// Получить статус (JSON: pending, processed, failed, isRunning, currentFile)
FV_API char* fv_content_indexer_get_status(FVContentIndexer indexer);

/// Callback для прогресса
typedef void (*FVContentProgressCallback)(int32_t processed, int32_t total, void* user_data);

/// Переиндексировать все файлы
FV_API FVError fv_content_indexer_reindex_all(FVContentIndexer indexer,
                                               FVContentProgressCallback cb, void* user_data);

/// Проверить, поддерживается ли MIME тип
FV_API int32_t fv_content_indexer_can_extract(FVContentIndexer indexer, const char* mime_type);

// ═══════════════════════════════════════════════════════════
// Secure Storage
// ═══════════════════════════════════════════════════════════

FV_API FVSecureStorage fv_secure_create(void);
FV_API void fv_secure_destroy(FVSecureStorage storage);

/// Сохранить бинарные данные
FV_API FVError fv_secure_store(FVSecureStorage storage, const char* key, 
                                const uint8_t* data, int32_t size);

/// Получить бинарные данные
/// @return Размер данных или -1 при ошибке. Если out_data=NULL, возвращает только размер.
FV_API int32_t fv_secure_retrieve(FVSecureStorage storage, const char* key,
                                   uint8_t* out_data, int32_t max_size);

FV_API FVError fv_secure_remove(FVSecureStorage storage, const char* key);

/// Проверить существование ключа
FV_API int32_t fv_secure_exists(FVSecureStorage storage, const char* key);

// Convenience для строк
FV_API FVError fv_secure_store_string(FVSecureStorage storage, const char* key, const char* value);
FV_API char* fv_secure_retrieve_string(FVSecureStorage storage, const char* key);

// ═══════════════════════════════════════════════════════════
// Family Pairing
// ═══════════════════════════════════════════════════════════

FV_API FVFamilyPairing fv_pairing_create(FVSecureStorage storage);
FV_API void fv_pairing_destroy(FVFamilyPairing pairing);

FV_API int32_t fv_pairing_is_configured(FVFamilyPairing pairing);
FV_API char* fv_pairing_get_device_id(FVFamilyPairing pairing);
FV_API char* fv_pairing_get_device_name(FVFamilyPairing pairing);
FV_API void fv_pairing_set_device_name(FVFamilyPairing pairing, const char* name);
FV_API int32_t fv_pairing_get_device_type(FVFamilyPairing pairing);

// Создание семьи (первое устройство)
FV_API char* fv_pairing_create_family(FVFamilyPairing pairing);  // JSON {pin, qrData, expiresAt}
FV_API char* fv_pairing_regenerate_pin(FVFamilyPairing pairing);
FV_API void fv_pairing_cancel(FVFamilyPairing pairing);
FV_API int32_t fv_pairing_has_pending(FVFamilyPairing pairing);

// Присоединение к семье
/// @return 0=Success, 1=InvalidPin, 2=Expired, 3=RateLimited, 4=NetworkError, 5=AlreadyConfigured
FV_API int32_t fv_pairing_join_pin(FVFamilyPairing pairing, const char* pin,
                                    const char* initiator_host, uint16_t initiator_port);
FV_API int32_t fv_pairing_join_qr(FVFamilyPairing pairing, const char* qr_data);

FV_API void fv_pairing_reset(FVFamilyPairing pairing);

// PSK для TLS
FV_API int32_t fv_pairing_derive_psk(FVFamilyPairing pairing, uint8_t* out_psk);
FV_API char* fv_pairing_get_psk_identity(FVFamilyPairing pairing);

// Pairing сервер
FV_API FVError fv_pairing_start_server(FVFamilyPairing pairing, uint16_t port);
FV_API void fv_pairing_stop_server(FVFamilyPairing pairing);
FV_API int32_t fv_pairing_is_server_running(FVFamilyPairing pairing);
FV_API uint16_t fv_pairing_get_server_port(FVFamilyPairing pairing);

// ═══════════════════════════════════════════════════════════
// Network Discovery
// ═══════════════════════════════════════════════════════════

/// Callback при обнаружении/потере устройства
/// @param event 0=found, 1=lost, 2=updated
typedef void (*FVDeviceCallback)(int32_t event, const char* device_json, void* user_data);

FV_API FVNetworkDiscovery fv_discovery_create(void);
FV_API void fv_discovery_destroy(FVNetworkDiscovery discovery);
FV_API FVError fv_discovery_start(FVNetworkDiscovery discovery, FVFamilyPairing pairing,
                                   FVDeviceCallback callback, void* user_data);
FV_API void fv_discovery_stop(FVNetworkDiscovery discovery);
FV_API char* fv_discovery_get_devices(FVNetworkDiscovery discovery);
FV_API char* fv_discovery_get_local_ips(void);

// ═══════════════════════════════════════════════════════════
// Network Manager (P2P координатор)
// ═══════════════════════════════════════════════════════════

/// Callback события сети
/// @param event 0=device_discovered, 1=device_lost, 2=device_connected, 
///              3=device_disconnected, 4=state_changed, 5=error
typedef void (*FVNetworkCallback)(int32_t event, const char* data_json, void* user_data);

FV_API FVNetworkManager fv_network_create(FVFamilyPairing pairing);
FV_API void fv_network_destroy(FVNetworkManager mgr);

FV_API FVError fv_network_start(FVNetworkManager mgr, uint16_t port,
                                 FVNetworkCallback callback, void* user_data);
FV_API void fv_network_stop(FVNetworkManager mgr);

/// Состояние: 0=stopped, 1=starting, 2=running, 3=stopping, 4=error
FV_API int32_t fv_network_get_state(FVNetworkManager mgr);
FV_API int32_t fv_network_is_running(FVNetworkManager mgr);
FV_API uint16_t fv_network_get_port(FVNetworkManager mgr);

FV_API char* fv_network_get_discovered_devices(FVNetworkManager mgr);
FV_API char* fv_network_get_connected_devices(FVNetworkManager mgr);

FV_API FVError fv_network_connect_to_device(FVNetworkManager mgr, const char* device_id);
FV_API FVError fv_network_connect_to_address(FVNetworkManager mgr, const char* host, uint16_t port);
FV_API void fv_network_disconnect_device(FVNetworkManager mgr, const char* device_id);
FV_API void fv_network_disconnect_all(FVNetworkManager mgr);
FV_API int32_t fv_network_is_connected_to(FVNetworkManager mgr, const char* device_id);

/// Установить базу данных для синхронизации индекса
FV_API FVError fv_network_set_database(FVNetworkManager mgr, FVDatabase db, const char* device_id);

/// Установить кэш-директорию для загруженных файлов
FV_API FVError fv_network_set_file_cache_dir(FVNetworkManager mgr, const char* cache_dir);

/// Синхронизация (только для подключённых devices с настроенной БД)
FV_API FVError fv_network_setup_index_sync(FVNetworkManager mgr);

/// File transfer
typedef void (*FVFileProgressCallback)(const char* progress_json, void* user_data);
typedef void (*FVFileCompleteCallback)(const char* result_json, void* user_data);
typedef void (*FVFileErrorCallback)(const char* error_json, void* user_data);

FV_API char* fv_network_request_file(FVNetworkManager mgr, const char* device_id, int64_t file_id,
                                      FVFileProgressCallback on_progress,
                                      FVFileCompleteCallback on_complete,
                                      FVFileErrorCallback on_error, void* user_data);

FV_API FVError fv_network_cancel_download(FVNetworkManager mgr, const char* request_id);

/// Получить удалённые файлы (из sync index)
FV_API char* fv_network_get_remote_files(FVNetworkManager mgr, const char* device_id,
                                          int32_t limit, int32_t offset);

#ifdef __cplusplus
}
#endif

#endif // FAMILYVAULT_C_H
```

---

## 6. SecureStorage — ЕДИНСТВЕННОЕ ОПРЕДЕЛЕНИЕ

```cpp
// include/familyvault/SecureStorage.h
// ЕДИНСТВЕННЫЙ ИСТОЧНИК ИСТИНЫ для SecureStorage
#pragma once

#include <string>
#include <vector>
#include <optional>

namespace FamilyVault {

class SecureStorage {
public:
    SecureStorage();
    ~SecureStorage();
    
    // Основной интерфейс — байтовые данные
    void store(const std::string& key, const std::vector<uint8_t>& value);
    std::optional<std::vector<uint8_t>> retrieve(const std::string& key);
    void remove(const std::string& key);
    
    // Convenience методы для строк (JSON, OAuth tokens)
    void storeString(const std::string& key, const std::string& value);
    std::optional<std::string> retrieveString(const std::string& key);
    
private:
    // Platform-specific implementation
    void storeImpl(const std::string& key, const std::vector<uint8_t>& value);
    std::optional<std::vector<uint8_t>> retrieveImpl(const std::string& key);
    void removeImpl(const std::string& key);
    
#ifdef __ANDROID__
    void* m_jniStorageObj = nullptr;  // JNI reference
#endif
};

} // namespace FamilyVault
```

**Используется в:**
- CloudAccountManager (OAuth tokens) — Этап 4
- FamilyPairing (family_secret) — Этап 5

---

## 7. P2P протокол — ЕДИНСТВЕННОЕ ОПРЕДЕЛЕНИЕ

### 7.1 Discovery (UDP broadcast)

```
Порт: 45679 (UDP)
Интервал: 5 секунд
TTL: 15 секунд (3 пропуска = offline)

Формат JSON:
{
  "app": "FamilyVault",
  "protocolVersion": 1,
  "minProtocolVersion": 1,
  "deviceId": "uuid-...",
  "deviceName": "John's PC",
  "deviceType": 0,
  "servicePort": 45678
}

⚠️ В discovery НЕ передаётся:
- family_secret
- индекс файлов  
- любые приватные данные
```

### 7.1b Family Pairing Protocol

```
Первое устройство создаёт семью:
┌────────────────────────────────────────────────────────────┐
│ 1. Генерирует family_secret (32 bytes, cryptographically   │
│    secure random)                                          │
│ 2. Генерирует временный PIN: 6 цифр (TTL = 5 минут)        │
│ 3. Сохраняет: SecureStorage["family_secret"] = secret      │
│ 4. Показывает PIN на экране / генерирует QR                │
└────────────────────────────────────────────────────────────┘

QR содержит (base64):
{
  "host": "192.168.1.100",       // IP первого устройства
  "port": 45680,                 // временный pairing port
  "nonce": "random-16-bytes",    // для этой сессии
  "expires": 1704067500          // Unix timestamp
}

Второе устройство присоединяется:
┌────────────────────────────────────────────────────────────┐
│ 1. Сканирует QR или вводит PIN                             │
│ 2. Подключается к host:port (TCP, без TLS на этом этапе!)  │
│ 3. Выполняет SPAKE2 (RFC 9382) с PIN как паролем:          │
│    - A → B: SPAKE2 message A                               │
│    - B → A: SPAKE2 message B                               │
│    - Обе стороны получают shared_key                       │
│ 4. Первое устройство шифрует family_secret:                │
│    encrypted = AES-GCM(shared_key, family_secret, nonce)   │
│ 5. Второе устройство расшифровывает и сохраняет            │
│ 6. Соединение закрывается, pairing port отключается        │
└────────────────────────────────────────────────────────────┘

Защита от brute-force PIN:
- Max 3 попытки, потом 30 сек cooldown
- После 10 неудач — PIN сбрасывается
- Rate limit на pairing port: 1 req/sec per IP

Защита от перехвата (shoulder surfing):
- PIN показывается 5 минут, потом сбрасывается
- QR предпочтительнее (не виден на расстоянии)
- SPAKE2 защищён от offline dictionary attack
```

### 7.2 Соединение (TLS 1.3 PSK)

```
Порт: 45678 (TCP)
Протокол: TLS 1.3 PSK mode (RFC 8446)

PSK derivation:
  PSK = HKDF-SHA256(
    ikm = family_secret,
    salt = "familyvault-psk-v1",
    info = "tls13-psk",
    length = 32
  )

PSK Identity = deviceId (UUID строка)

Ciphersuites:
  - TLS_AES_256_GCM_SHA384
  - TLS_CHACHA20_POLY1305_SHA256
```

### 7.3 Протокол сообщений (внутри TLS)

```
┌──────────────┬──────────────┬──────────────────────┐
│ Magic (4B)   │ Length (4B)  │ Payload (JSON)       │
│ "FVLT"       │ uint32 BE    │ variable             │
└──────────────┴──────────────┴──────────────────────┘

Payload JSON:
{
  "type": <int>,
  "requestId": "uuid",
  "data": { ... }
}

Message Types:
  1 = DeviceInfo       — обмен информацией об устройстве
  2 = DeviceInfoAck
  10 = Ping
  11 = Pong
  20 = IndexRequest    — запрос индекса (delta sync)
  21 = IndexResponse
  30 = SearchRequest   — удалённый поиск
  31 = SearchResponse
  40 = FileRequest     — запрос файла
  41 = FileMetadata
  42 = FileChunk       — часть файла (binary payload)
  43 = FileComplete
  44 = FileError
  50 = ThumbnailRequest
  51 = ThumbnailResponse

FileRequest data:
{
  "fileId": 12345,
  "startOffset": 0       // optional, для resume (0 = с начала)
}

FileChunk — BINARY формат (НЕ JSON для эффективности):
┌──────────────┬──────────────┬──────────────┬──────────────┬────────────┐
│ Magic (4B)   │ Length (4B)  │ Header JSON  │ Padding      │ Raw bytes  │
│ "FVCH"       │ total len    │ (variable)   │ to 8-align   │ (chunk)    │
└──────────────┴──────────────┴──────────────┴──────────────┴────────────┘

Header JSON:
{
  "fileId": 12345,
  "offset": 1048576,     // текущий offset в файле
  "chunkSize": 65536,    // размер данных после header
  "isLast": false,
  "seq": 42              // sequence number для ACK
}

Flow Control (ACK-based):
- Отправитель шлёт до WINDOW_SIZE (10) чанков без ACK
- Получатель отправляет FileChunkAck каждые N чанков:
  { "type": 45, "fileId": 12345, "ackedSeq": 42 }
- Отправитель ждёт ACK если window заполнен
- Timeout 5 сек → уменьшение window до 5
- 3 timeout подряд → abort transfer

Преимущества:
- Нет base64 overhead (экономия 33%)
- Zero-copy возможен
- Max chunk size: 64KB
- Backpressure при медленном получателе
```

---

## 8. Dart модели (контракт с C++)

```dart
// lib/core/models/file_record.dart

// Компактная версия для списков (меньше overhead через FFI)
class FileRecordCompact {
  final int id;
  final String name;
  final String extension;
  final int size;
  final ContentType contentType;
  final DateTime modifiedAt;
  final bool isRemote;
  final bool hasThumbnail;
  
  factory FileRecordCompact.fromJson(Map<String, dynamic> json) {
    return FileRecordCompact(
      id: json['id'] as int,
      name: json['name'] as String,
      extension: json['extension'] as String,
      size: json['size'] as int,
      contentType: ContentType.fromValue(json['contentType'] as int),
      modifiedAt: DateTime.fromMillisecondsSinceEpoch((json['modifiedAt'] as int) * 1000),
      isRemote: json['isRemote'] as bool,
      hasThumbnail: json['hasThumbnail'] as bool? ?? false,
    );
  }
}

// Полная версия для детального view
class FileRecord {
  final int id;
  final int folderId;
  final String relativePath;
  final String name;
  final String extension;
  final int size;
  final String mimeType;
  final ContentType contentType;
  final String? checksum;
  final DateTime createdAt;
  final DateTime modifiedAt;
  final DateTime indexedAt;
  final Visibility visibility;
  final String? sourceDeviceId;
  final bool isRemote;
  final int syncVersion;
  final String? lastModifiedBy;
  final List<String> tags;
  final ImageMetadata? imageMeta;
  
  bool get isLocal => !isRemote;
  bool get isPrivate => visibility == Visibility.private;
  
  factory FileRecord.fromJson(Map<String, dynamic> json) {
    return FileRecord(
      id: json['id'] as int,
      folderId: json['folderId'] as int,
      relativePath: json['relativePath'] as String,
      name: json['name'] as String,
      extension: json['extension'] as String,
      size: json['size'] as int,
      mimeType: json['mimeType'] as String,
      contentType: ContentType.fromValue(json['contentType'] as int),
      checksum: json['checksum'] as String?,
      createdAt: DateTime.fromMillisecondsSinceEpoch((json['createdAt'] as int) * 1000),
      modifiedAt: DateTime.fromMillisecondsSinceEpoch((json['modifiedAt'] as int) * 1000),
      indexedAt: DateTime.fromMillisecondsSinceEpoch((json['indexedAt'] as int) * 1000),
      visibility: Visibility.fromValue(json['visibility'] as int),
      sourceDeviceId: json['sourceDeviceId'] as String?,
      isRemote: json['isRemote'] as bool,
      syncVersion: json['syncVersion'] as int? ?? 0,
      lastModifiedBy: json['lastModifiedBy'] as String?,
      tags: (json['tags'] as List<dynamic>).cast<String>(),
      imageMeta: json['imageMeta'] != null 
        ? ImageMetadata.fromJson(json['imageMeta']) 
        : null,
    );
  }
}

// lib/core/models/types.dart

enum ContentType {
  unknown(0),
  image(1),
  video(2),
  audio(3),
  document(4),
  archive(5),
  other(99);
  
  final int value;
  const ContentType(this.value);
  
  static ContentType fromValue(int v) => 
    ContentType.values.firstWhere((e) => e.value == v, orElse: () => unknown);
}

enum Visibility {
  private(0),
  family(1);
  
  final int value;
  const Visibility(this.value);
  
  static Visibility fromValue(int v) =>
    Visibility.values.firstWhere((e) => e.value == v, orElse: () => family);
}
```

---

## 9. Threading Model

### 9.1 Гарантии thread-safety

```
┌─────────────────────────────────────────────────────────────┐
│ Thread Safety по компонентам                                │
├─────────────────────────────────────────────────────────────┤
│ FVDatabase        │ Thread-safe (SQLITE_OPEN_FULLMUTEX)     │
│ FVIndexManager    │ Thread-safe для разных folder_id        │
│ FVSearchEngine    │ Thread-safe (read-only операции)        │
│ FVTagManager      │ Thread-safe                             │
│ FVNetworkManager  │ Thread-safe                             │
│ FVSecureStorage   │ Thread-safe                             │
└─────────────────────────────────────────────────────────────┘
```

### 9.2 Callbacks

```
Гарантии:
1. Callbacks вызываются из рабочего потока C++ (не UI thread)
2. Callbacks для одного handle НИКОГДА не вызываются параллельно
3. const char* в callback валиден ТОЛЬКО во время вызова — копировать если нужно сохранить
4. После fv_*_destroy() callbacks больше не вызываются

Flutter: использовать NativeCallable.listener для async callbacks,
         затем переключиться на UI thread через setState/StreamController.
```

### 9.3 SQLite Configuration

```cpp
// Database открывается с флагами:
SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX

// WAL mode для concurrent reads:
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 5000;
```

---

## 10. Memory Ownership

### 10.1 Правила владения

```
┌─────────────────────────────────────────────────────────────┐
│ Функция возвращает          │ Кто освобождает              │
├─────────────────────────────────────────────────────────────┤
│ char*                       │ Caller через fv_free_string() │
│ uint8_t* (thumbnails)       │ Caller через fv_free_bytes()  │
│ FV* handles                 │ Caller через fv_*_destroy()   │
│ const char* в callback      │ НЕ КОПИРОВАТЬ, валиден        │
│                             │ только во время callback      │
└─────────────────────────────────────────────────────────────┘
```

### 10.2 Пример правильного использования

```c
// ✅ Правильно
char* json = fv_index_get_folders(mgr);
// использовать json...
fv_free_string(json);  // обязательно!

// ❌ Неправильно — утечка памяти
void bad() {
    char* json = fv_index_get_folders(mgr);
    // забыли fv_free_string
}

// ❌ Неправильно — use-after-free в callback
void bad_callback(const char* file, void* data) {
    saved_ptr = file;  // НЕЛЬЗЯ! Указатель станет невалидным
}

// ✅ Правильно — копирование в callback
void good_callback(const char* file, void* data) {
    std::string copy(file);  // OK
}
```

---

## 11. Database Migrations

### 11.1 Версионирование схемы

```sql
-- Первая таблица в БД
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY,
    applied_at INTEGER DEFAULT (strftime('%s', 'now')),
    description TEXT
);

-- Проверка при старте
SELECT MAX(version) FROM schema_version;
-- Если NULL → применить все миграции
-- Если < CURRENT_VERSION → применить недостающие
```

### 11.2 Формат миграций

```cpp
// Каждая миграция — отдельная функция
struct Migration {
    int version;
    const char* description;
    const char* sql;
};

const Migration MIGRATIONS[] = {
    {1, "Initial schema", R"(
        CREATE TABLE settings (...);
        CREATE TABLE watched_folders (...);
        CREATE TABLE files (...);
        CREATE TABLE tags (...);
        CREATE TABLE file_tags (...);
        CREATE TABLE file_content (...);
        CREATE TABLE image_metadata (...);
        CREATE TABLE deleted_files (...);
        CREATE TABLE sync_state (...);
        CREATE VIRTUAL TABLE files_fts USING fts5(...);
        -- P2P поля включены: source_device_id, is_remote, sync_version, last_modified_by
    )"},
    {2, "Reset extracted_at to trigger re-indexing", R"(
        UPDATE file_content SET extracted_at = 0;
        DELETE FROM files_fts;
        INSERT INTO files_fts(rowid, name, relative_path, content) SELECT ...;
    )"},
    {3, "Cloud accounts and files", R"(
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
            UNIQUE(account_id, cloud_id),
            FOREIGN KEY (account_id) REFERENCES cloud_accounts(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_cloud_files_account ON cloud_files(account_id);
        CREATE INDEX IF NOT EXISTS idx_cloud_files_name ON cloud_files(name);
        CREATE INDEX IF NOT EXISTS idx_cloud_files_modified ON cloud_files(modified_at DESC);

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
    )"},
};
```

### 11.3 Применение миграций

```cpp
void Database::runMigrations() {
    int currentVersion = getCurrentSchemaVersion();
    int targetVersion = MIGRATIONS[std::size(MIGRATIONS) - 1].version;
    
    if (currentVersion < targetVersion) {
        // ⚠️ ОБЯЗАТЕЛЬНО: backup перед миграцией
        backupDatabase(fmt::format("{}.backup.v{}", m_path, currentVersion));
    }
    
    for (const auto& m : MIGRATIONS) {
        if (m.version > currentVersion) {
            beginTransaction();
            try {
                execute(m.sql);
                execute("INSERT INTO schema_version (version, description) VALUES (?, ?)",
                        m.version, m.description);
                commit();
                spdlog::info("Migration {} applied: {}", m.version, m.description);
            } catch (const std::exception& e) {
                rollback();
                spdlog::error("Migration {} failed: {}", m.version, e.what());
                // Восстановить из backup если нужно
                throw;
            }
        }
    }
}

void Database::backupDatabase(const std::string& backupPath) {
    // SQLite Online Backup API
    sqlite3* pBackup;
    sqlite3_open(backupPath.c_str(), &pBackup);
    auto* backup = sqlite3_backup_init(pBackup, "main", m_db, "main");
    sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    sqlite3_close(pBackup);
}
```

---

## 12. P2P Protocol Versioning

### 12.1 Версия протокола в Discovery

```json
{
  "app": "FamilyVault",
  "protocolVersion": 1,
  "minProtocolVersion": 1,
  "deviceId": "...",
  ...
}
```

### 12.2 Совместимость

```
При подключении:
1. Сравнить protocolVersion обоих устройств
2. Использовать MIN(v1, v2) если оба поддерживают
3. Отклонить если MIN < minProtocolVersion любого устройства

Пример:
  Device A: protocolVersion=2, minProtocolVersion=1
  Device B: protocolVersion=1, minProtocolVersion=1
  → Используют протокол v1 (оба поддерживают)

  Device A: protocolVersion=3, minProtocolVersion=2
  Device B: protocolVersion=1, minProtocolVersion=1
  → Отклонить (A не поддерживает v1)
```

### 12.3 Graceful Degradation

```
Новые message types игнорируются старыми версиями.
Обязательные поля помечены в схеме, optional — добавляются в новых версиях.
```

---

## 13. Sync Conflict Resolution

### 13.1 Стратегия: Last-Write-Wins + Vector Clocks

```
Метаданные файла:
- modifiedAt: timestamp последнего изменения
- deviceId: устройство, сделавшее изменение
- syncVersion: инкрементный счётчик на устройстве

При конфликте:
1. Сравнить syncVersion — выше побеждает
2. При равенстве — сравнить modifiedAt
3. При равенстве — lexicographic по deviceId (детерминированно)
```

### 13.2 Типы данных и их разрешение

```
┌─────────────────────────────────────────────────────────────┐
│ Данные              │ Стратегия                             │
├─────────────────────────────────────────────────────────────┤
│ File content        │ НЕ синхронизируется (только метаданные)│
│ File metadata       │ Last-Write-Wins                       │
│ Tags (user)         │ Union (объединение множеств)          │
│ Tags (auto)         │ Regenerate locally                    │
│ Visibility          │ Last-Write-Wins                       │
│ Deleted files       │ Tombstone с TTL 30 дней               │
└─────────────────────────────────────────────────────────────┘
```

### 13.3 Sync Metadata

```sql
-- Добавить в таблицу files:
ALTER TABLE files ADD COLUMN sync_version INTEGER DEFAULT 0;
ALTER TABLE files ADD COLUMN last_modified_by TEXT;  -- deviceId

-- Tombstones для удалённых файлов
CREATE TABLE deleted_files (
    checksum TEXT PRIMARY KEY,
    deleted_at INTEGER,
    deleted_by TEXT  -- deviceId
);

-- Метаданные синхронизации
CREATE TABLE sync_state (
    device_id TEXT PRIMARY KEY,
    last_sync_version INTEGER DEFAULT 0,
    last_sync_at INTEGER,
    needs_full_resync INTEGER DEFAULT 0
);
```

### 13.4 Full Resync при большом gap

```
Проблема: Tombstone TTL = 30 дней. Если устройство offline > 30 дней,
удалённые файлы "воскреснут" при reconnect.

Решение:
┌────────────────────────────────────────────────────────────┐
│ При IndexSync:                                             │
│ 1. Получить last_sync_at для peer                          │
│ 2. Если (now - last_sync_at) > TOMBSTONE_TTL:              │
│    → Установить needs_full_resync = 1                      │
│    → Уведомить пользователя                                │
│ 3. Full resync: сравнить полные списки checksums           │
│    → Файлы только на одном устройстве = новые ИЛИ удалённые│
│    → UI спрашивает: "Восстановить или удалить?"            │
└────────────────────────────────────────────────────────────┘

IndexRequest включает:
{
  "sinceVersion": 12345,      // delta sync
  "fullSync": false,          // true = запрос полного списка
  "lastSyncAt": 1704067200    // для детекции gap
}
```

---

## 14. Known Limitations

### 14.1 Network Discovery

```
⚠️ UDP Broadcast работает ТОЛЬКО в пределах одной подсети.
   Устройства в разных VLAN/подсетях не найдут друг друга.
   
Решение в MVP:
- fv_network_connect_ip() — ручной ввод IP (см. C API)

Workaround для будущих версий:
- mDNS/DNS-SD (Bonjour) — кросс-подсетевое обнаружение
- Cloud relay — relay сервер для NAT traversal
```

### 14.2 Mobile Background Sync

```
⚠️ iOS/Android ограничивают фоновую работу.
   Sync происходит только когда приложение активно.
   
Workaround:
- Android: WorkManager для периодических задач (минимум 15 мин)
- iOS: Background App Refresh (не гарантирован)
- Push notifications для "wake up" (требует backend)
```

### 14.3 Large Files

```
⚠️ SHA-256 хеширование больших файлов (>1GB) занимает время.

Оптимизация (отложенная):
- Partial hash: first 1MB + last 1MB + size для quick-check
- Full hash — в фоне после индексации
- Resumable transfer — startOffset уже в протоколе (§7.3), реализация — на этапе P2P
```

### 14.4 Scalability Targets

```
Рекомендуемые лимиты:
- До 100,000 файлов на устройство
- До 10 устройств в семье
- FTS индекс до 500 MB
- Sync batch size: 1000 файлов за раз
- Thumbnail cache: 500 MB на устройство
```

### 14.5 Sync & Transfer

```
⚠️ Last-Write-Wins может терять изменения при одновременном редактировании.
   Для MVP это приемлемо. Улучшение: UI-уведомление о конфликте.

⚠️ Нет flow control при file transfer.
   Если получатель медленный — отправитель может переполнить буфер.
   Улучшение: ACK каждые N чанков или sliding window.

⚠️ Нет PSK rotation механизма.
   Если family_secret скомпрометирован — только ручной reset на всех устройствах.
   Улучшение: протокол FamilySecretRotation с версионированием.
```

### 14.6 Performance

```
⚠️ JSON serialization для массовых операций.
   100 результатов поиска → serialize + copy + parse.
   Улучшение: FlatBuffers или streaming batches.

⚠️ SQLite FULLMUTEX на мобильных.
   Serializes все операции. Может быть bottleneck при частых UI-запросах.
   Улучшение: WAL + NORMAL mutex + connection pool.

⚠️ Нет offline queue для pending changes.
   При reconnect — полная синхронизация.
   Улучшение: локальная очередь с retry и exponential backoff.
```


