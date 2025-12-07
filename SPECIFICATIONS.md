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
├── TextExtractor.h     # Извлечение текста (Этап 3)
├── ThumbnailGenerator.h # Генерация превью (новый!)
│
├── SecureStorage.h     # Хранение секретов (ЕДИНСТВЕННОЕ место!)
├── CloudAccount.h      # OAuth, Google Drive (Этап 4)
│
├── Network/            # P2P (Этап 5)
│   ├── Discovery.h
│   ├── Protocol.h
│   ├── TlsPsk.h
│   ├── PeerConnection.h
│   └── FamilyPairing.h
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
typedef struct FVThumbnailGenerator_* FVThumbnailGenerator;
typedef struct FVSecureStorage_* FVSecureStorage;
typedef struct FVNetworkManager_* FVNetworkManager;
typedef struct FVFamilyPairing_* FVFamilyPairing;

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
    FV_ERROR_INTERNAL = 99
} FVError;

// ═══════════════════════════════════════════════════════════
// Общие функции
// ═══════════════════════════════════════════════════════════

FV_API const char* fv_version(void);
FV_API const char* fv_error_message(FVError error);
FV_API void fv_free_string(char* str);

// ═══════════════════════════════════════════════════════════
// Database
// ═══════════════════════════════════════════════════════════

FV_API FVDatabase fv_database_open(const char* path, FVError* out_error);
FV_API void fv_database_close(FVDatabase db);
FV_API FVError fv_database_initialize(FVDatabase db);

// ═══════════════════════════════════════════════════════════
// Index Manager
// ═══════════════════════════════════════════════════════════

FV_API FVIndexManager fv_index_create(FVDatabase db);
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

// Files — полная версия (FileRecord)
FV_API char* fv_index_get_file(FVIndexManager mgr, int64_t file_id);  // JSON FileRecord
FV_API char* fv_index_get_recent(FVIndexManager mgr, int32_t limit);  // JSON array FileRecord

// Files — компактная версия для списков (FileRecordCompact, меньше overhead)
FV_API char* fv_index_get_recent_compact(FVIndexManager mgr, int32_t limit);  // JSON array
FV_API char* fv_index_get_by_folder_compact(FVIndexManager mgr, int64_t folder_id, 
                                             int32_t limit, int32_t offset);  // JSON array

FV_API char* fv_index_get_stats(FVIndexManager mgr);  // JSON

// Visibility
FV_API FVError fv_index_set_folder_visibility(FVIndexManager mgr, int64_t folder_id, int32_t visibility);
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

FV_API FVTagManager fv_tags_create(FVDatabase db);
FV_API void fv_tags_destroy(FVTagManager mgr);

FV_API FVError fv_tags_add(FVTagManager mgr, int64_t file_id, const char* tag);
FV_API FVError fv_tags_remove(FVTagManager mgr, int64_t file_id, const char* tag);
FV_API char* fv_tags_get_for_file(FVTagManager mgr, int64_t file_id);  // JSON array
FV_API char* fv_tags_get_all(FVTagManager mgr);  // JSON array with counts

// ═══════════════════════════════════════════════════════════
// Duplicate Finder
// ═══════════════════════════════════════════════════════════

FV_API FVDuplicateFinder fv_duplicates_create(FVDatabase db);
FV_API void fv_duplicates_destroy(FVDuplicateFinder finder);

FV_API char* fv_duplicates_find(FVDuplicateFinder finder);  // JSON array
FV_API char* fv_duplicates_stats(FVDuplicateFinder finder);  // JSON
FV_API char* fv_duplicates_without_backup(FVDuplicateFinder finder);  // JSON array
FV_API FVError fv_duplicates_delete_file(FVDuplicateFinder finder, int64_t file_id);

// ═══════════════════════════════════════════════════════════
// Thumbnail Generator (Этап 2+)
// ═══════════════════════════════════════════════════════════

FV_API FVThumbnailGenerator fv_thumbs_create(FVDatabase db);
FV_API void fv_thumbs_destroy(FVThumbnailGenerator gen);

// Возвращает JPEG данные. out_size заполняется размером.
// Вызывающий должен освободить память через fv_free_bytes()
//
// Поддерживаемые форматы:
//   Images: JPEG, PNG, GIF, WebP (через stb_image)
//   Video:  первый keyframe (через FFmpeg, optional dependency)
//   PDF:    первая страница (через Poppler, optional dependency)
// Fallback: иконка по типу файла
//

// Sync версия (для кэшированных/быстрых форматов)
FV_API uint8_t* fv_thumbs_generate(FVThumbnailGenerator gen, int64_t file_id, 
                                    int32_t max_size, int32_t* out_size);

// Async версия (для video/PDF — не блокирует UI thread)
typedef void (*FVThumbnailCallback)(int64_t file_id, uint8_t* data, int32_t size, void* user_data);
FV_API void fv_thumbs_generate_async(FVThumbnailGenerator gen, int64_t file_id,
                                      int32_t max_size, FVThumbnailCallback cb, void* user_data);

FV_API void fv_free_bytes(uint8_t* data);

// Проверка наличия в кэше (disk cache, max 500MB, LRU eviction)
FV_API bool fv_thumbs_is_cached(FVThumbnailGenerator gen, int64_t file_id);

// Capability detection: какие форматы поддерживаются на текущем устройстве
// Возвращает JSON: ["jpeg","png","gif","webp","pdf","mp4"]
FV_API char* fv_thumbs_supported_formats(FVThumbnailGenerator gen);

// ═══════════════════════════════════════════════════════════
// Secure Storage
// ═══════════════════════════════════════════════════════════

FV_API FVSecureStorage fv_secure_create(void);
FV_API void fv_secure_destroy(FVSecureStorage storage);

FV_API FVError fv_secure_store(FVSecureStorage storage, const char* key, 
                                const uint8_t* data, int32_t size);
FV_API uint8_t* fv_secure_retrieve(FVSecureStorage storage, const char* key, 
                                    int32_t* out_size);  // Caller frees with fv_free_bytes
FV_API FVError fv_secure_remove(FVSecureStorage storage, const char* key);

// Convenience для строк
FV_API FVError fv_secure_store_string(FVSecureStorage storage, const char* key, const char* value);
FV_API char* fv_secure_retrieve_string(FVSecureStorage storage, const char* key);

// ═══════════════════════════════════════════════════════════
// Family Pairing (Этап 5)
// ═══════════════════════════════════════════════════════════

FV_API FVFamilyPairing fv_pairing_create(FVSecureStorage storage);
FV_API void fv_pairing_destroy(FVFamilyPairing pairing);

FV_API bool fv_pairing_is_configured(FVFamilyPairing pairing);
FV_API char* fv_pairing_get_device_id(FVFamilyPairing pairing);

// Создание семьи (первое устройство)
FV_API char* fv_pairing_create_family(FVFamilyPairing pairing);  // JSON {pin, qrData, expiresAt}

// Присоединение к семье
FV_API FVError fv_pairing_join_pin(FVFamilyPairing pairing, const char* pin);
FV_API FVError fv_pairing_join_qr(FVFamilyPairing pairing, const char* qr_data);

FV_API void fv_pairing_reset(FVFamilyPairing pairing);

// ═══════════════════════════════════════════════════════════
// Network Manager (Этап 5)
// ═══════════════════════════════════════════════════════════

FV_API FVNetworkManager fv_network_create(FVDatabase db, FVIndexManager idx, 
                                           FVFamilyPairing pairing);
FV_API void fv_network_destroy(FVNetworkManager mgr);

FV_API FVError fv_network_start(FVNetworkManager mgr, const char* device_name);
FV_API void fv_network_stop(FVNetworkManager mgr);

FV_API char* fv_network_get_devices(FVNetworkManager mgr);  // JSON array
FV_API char* fv_network_get_status(FVNetworkManager mgr);   // JSON

FV_API FVError fv_network_connect(FVNetworkManager mgr, const char* device_id);
FV_API void fv_network_disconnect(FVNetworkManager mgr, const char* device_id);
FV_API void fv_network_sync_now(FVNetworkManager mgr);

// Manual IP connect (для разных подсетей, когда UDP discovery не работает)
FV_API FVError fv_network_connect_ip(FVNetworkManager mgr, const char* ip_address, uint16_t port);

// File download — возвращает download_id для отмены
typedef void (*FVDownloadProgressCallback)(int64_t received, int64_t total, void* user_data);
typedef void (*FVDownloadCompleteCallback)(const char* local_path, void* user_data);
typedef void (*FVDownloadErrorCallback)(const char* error, void* user_data);

FV_API int64_t fv_network_request_file(FVNetworkManager mgr, const char* device_id, int64_t file_id,
                                        FVDownloadProgressCallback on_progress,
                                        FVDownloadCompleteCallback on_complete,
                                        FVDownloadErrorCallback on_error,
                                        void* user_data);  // returns download_id, 0 on error

FV_API void fv_network_cancel_download(FVNetworkManager mgr, int64_t download_id);

// Event callbacks
typedef void (*FVDeviceEventCallback)(const char* device_json, void* user_data);
FV_API void fv_network_on_device_found(FVNetworkManager mgr, FVDeviceEventCallback cb, void* user_data);
FV_API void fv_network_on_device_connected(FVNetworkManager mgr, FVDeviceEventCallback cb, void* user_data);
FV_API void fv_network_on_device_disconnected(FVNetworkManager mgr, FVDeviceEventCallback cb, void* user_data);

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
        -- и т.д.
    )"},
    {2, "Add tags support", R"(
        CREATE TABLE tags (...);
        CREATE TABLE file_tags (...);
    )"},
    {3, "Add P2P fields", R"(
        ALTER TABLE files ADD COLUMN source_device_id TEXT;
        ALTER TABLE files ADD COLUMN is_remote INTEGER DEFAULT 0;
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


