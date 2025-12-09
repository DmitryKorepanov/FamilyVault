// familyvault_c.h — C API для FFI
// ЕДИНСТВЕННЫЙ ИСТОЧНИК ИСТИНЫ для C API
// См. SPECIFICATIONS.md, раздел 5

#ifndef FAMILYVAULT_C_H
#define FAMILYVAULT_C_H

#include "export.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ═══════════════════════════════════════════════════════════
// Opaque handles
// ═══════════════════════════════════════════════════════════

typedef struct FVDatabase_* FVDatabase;
typedef struct FVIndexManager_* FVIndexManager;
typedef struct FVSearchEngine_* FVSearchEngine;
typedef struct FVTagManager_* FVTagManager;
typedef struct FVDuplicateFinder_* FVDuplicateFinder;
typedef struct FVContentIndexer_* FVContentIndexer;

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

/// Возвращает версию библиотеки
FV_API const char* fv_version(void);

/// Возвращает текстовое описание ошибки
FV_API const char* fv_error_message(FVError error);

/// Получить последнюю ошибку (thread-local)
FV_API FVError fv_last_error(void);

/// Получить сообщение последней ошибки (thread-local)
FV_API const char* fv_last_error_message(void);

/// Очистить состояние ошибки
FV_API void fv_clear_error(void);

/// Освобождает строку, выделенную библиотекой
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
/// @note Миграции должны запускаться только один раз, в главном потоке
FV_API FVError fv_database_initialize(FVDatabase db);

/// Проверить, инициализирована ли БД
FV_API int32_t fv_database_is_initialized(FVDatabase db);

// ═══════════════════════════════════════════════════════════
// Index Manager
// ═══════════════════════════════════════════════════════════

/// Создать IndexManager
/// @note Увеличивает reference count базы данных
FV_API FVIndexManager fv_index_create(FVDatabase db);

/// Уничтожить IndexManager
/// @note Автоматически уменьшает reference count базы данных
FV_API void fv_index_destroy(FVIndexManager mgr);

/// Добавить папку для отслеживания
/// @param visibility 0=Private, 1=Family
/// @return ID папки или -1 при ошибке
FV_API int64_t fv_index_add_folder(FVIndexManager mgr, const char* path,
                                    const char* name, int32_t visibility);

/// Удалить папку из отслеживания
FV_API FVError fv_index_remove_folder(FVIndexManager mgr, int64_t folder_id);

/// Оптимизация БД (rebuild FTS + VACUUM)
FV_API FVError fv_index_optimize_database(FVIndexManager mgr);

/// Получить максимальный размер текста для индексации (KB)
FV_API int fv_index_get_max_text_size_kb(FVIndexManager mgr);

/// Установить максимальный размер текста для индексации (KB)
FV_API FVError fv_index_set_max_text_size_kb(FVIndexManager mgr, int size_kb);

/// Получить список папок (JSON array)
/// @return JSON строка или nullptr при ошибке (см. fv_last_error)
FV_API char* fv_index_get_folders(FVIndexManager mgr);

/// Callback для прогресса сканирования
typedef void (*FVScanCallback)(int64_t processed, int64_t total,
                               const char* current_file, void* user_data);

/// Сканировать папку
FV_API FVError fv_index_scan_folder(FVIndexManager mgr, int64_t folder_id,
                                     FVScanCallback cb, void* user_data);

/// Сканировать все папки
FV_API FVError fv_index_scan_all(FVIndexManager mgr, FVScanCallback cb, void* user_data);

/// Остановить сканирование
/// @note Cooperative cancellation - сканирование остановится при следующей проверке
FV_API void fv_index_stop_scan(FVIndexManager mgr);

/// Проверить, идёт ли сканирование
FV_API int32_t fv_index_is_scanning(FVIndexManager mgr);

/// Получить файл по ID (JSON FileRecord)
/// @return JSON строка, пустая строка если не найден, nullptr при ошибке
FV_API char* fv_index_get_file(FVIndexManager mgr, int64_t file_id);

/// Удалить файл из индекса (и опционально с диска)
/// @param delete_from_disk 1 - удалить также с диска, 0 - только из индекса
FV_API FVError fv_index_delete_file(FVIndexManager mgr, int64_t file_id, int32_t delete_from_disk);

/// Получить недавние файлы (JSON array FileRecord)
FV_API char* fv_index_get_recent(FVIndexManager mgr, int32_t limit);

/// Получить недавние файлы (компактная версия, JSON array)
FV_API char* fv_index_get_recent_compact(FVIndexManager mgr, int32_t limit);

/// Получить файлы папки (JSON array FileRecordCompact)
FV_API char* fv_index_get_by_folder_compact(FVIndexManager mgr, int64_t folder_id,
                                             int32_t limit, int32_t offset);

/// Получить статистику индекса (JSON)
FV_API char* fv_index_get_stats(FVIndexManager mgr);

/// Установить видимость папки
FV_API FVError fv_index_set_folder_visibility(FVIndexManager mgr, int64_t folder_id, int32_t visibility);

/// Включить/отключить папку
FV_API FVError fv_index_set_folder_enabled(FVIndexManager mgr, int64_t folder_id, int32_t enabled);

/// Установить видимость файла
FV_API FVError fv_index_set_file_visibility(FVIndexManager mgr, int64_t file_id, int32_t visibility);

// ═══════════════════════════════════════════════════════════
// Search Engine
// ═══════════════════════════════════════════════════════════

/// Создать SearchEngine
/// @note Увеличивает reference count базы данных
FV_API FVSearchEngine fv_search_create(FVDatabase db);

/// Уничтожить SearchEngine
/// @note Автоматически уменьшает reference count базы данных
FV_API void fv_search_destroy(FVSearchEngine engine);

/// Поиск (JSON query -> JSON array SearchResult)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_search_query(FVSearchEngine engine, const char* query_json);

/// Поиск (компактная версия)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_search_query_compact(FVSearchEngine engine, const char* query_json);

/// Подсчёт результатов
/// @return Количество результатов или -1 при ошибке
FV_API int64_t fv_search_count(FVSearchEngine engine, const char* query_json);

/// Автодополнение
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_search_suggest(FVSearchEngine engine, const char* prefix, int32_t limit);

// ═══════════════════════════════════════════════════════════
// Tag Manager
// ═══════════════════════════════════════════════════════════

/// Создать TagManager
/// @note Увеличивает reference count базы данных
FV_API FVTagManager fv_tags_create(FVDatabase db);

/// Уничтожить TagManager
/// @note Автоматически уменьшает reference count базы данных
FV_API void fv_tags_destroy(FVTagManager mgr);

/// Добавить тег к файлу
FV_API FVError fv_tags_add(FVTagManager mgr, int64_t file_id, const char* tag);

/// Удалить тег с файла
FV_API FVError fv_tags_remove(FVTagManager mgr, int64_t file_id, const char* tag);

/// Получить теги файла (JSON array)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_tags_get_for_file(FVTagManager mgr, int64_t file_id);

/// Получить все теги с количеством файлов (JSON array)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_tags_get_all(FVTagManager mgr);

/// Получить популярные теги (JSON array)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_tags_get_popular(FVTagManager mgr, int32_t limit);

// ═══════════════════════════════════════════════════════════
// Duplicate Finder
// ═══════════════════════════════════════════════════════════

/// Создать DuplicateFinder
/// @param db База данных
/// @param indexMgr Опционально: IndexManager для централизованного удаления файлов
/// @note Увеличивает reference count базы данных
FV_API FVDuplicateFinder fv_duplicates_create(FVDatabase db, FVIndexManager indexMgr);

/// Уничтожить DuplicateFinder
/// @note Автоматически уменьшает reference count базы данных
FV_API void fv_duplicates_destroy(FVDuplicateFinder finder);

/// Найти дубликаты (JSON array DuplicateGroup)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_duplicates_find(FVDuplicateFinder finder);

/// Статистика дубликатов (JSON)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_duplicates_stats(FVDuplicateFinder finder);

/// Файлы без бэкапа (JSON array FileRecord)
/// @return JSON строка или nullptr при ошибке
FV_API char* fv_duplicates_without_backup(FVDuplicateFinder finder);

/// Удалить файл
/// @note Если создан с IndexManager, удаление идёт через него
FV_API FVError fv_duplicates_delete_file(FVDuplicateFinder finder, int64_t file_id);

/// Вычислить недостающие checksums
typedef void (*FVChecksumProgressCallback)(int32_t processed, int32_t total, void* user_data);
FV_API FVError fv_duplicates_compute_checksums(FVDuplicateFinder finder,
                                                FVChecksumProgressCallback cb, void* user_data);

// ═══════════════════════════════════════════════════════════
// Content Indexer (Text Extraction)
// ═══════════════════════════════════════════════════════════

/// Создать ContentIndexer
/// @note Увеличивает reference count базы данных
FV_API FVContentIndexer fv_content_indexer_create(FVDatabase db);

/// Уничтожить ContentIndexer
/// @note Автоматически останавливает фоновую обработку
FV_API void fv_content_indexer_destroy(FVContentIndexer indexer);

/// Запустить фоновую обработку текста
FV_API FVError fv_content_indexer_start(FVContentIndexer indexer);

/// Остановить фоновую обработку
/// @param wait 1 - ожидать завершения текущего файла
FV_API FVError fv_content_indexer_stop(FVContentIndexer indexer, int32_t wait);

/// Проверить, запущена ли обработка
FV_API int32_t fv_content_indexer_is_running(FVContentIndexer indexer);

/// Обработать конкретный файл (синхронно)
/// @return FV_OK если текст успешно извлечён
FV_API FVError fv_content_indexer_process_file(FVContentIndexer indexer, int64_t file_id);

/// Добавить все необработанные файлы в очередь
/// @return Количество добавленных файлов или -1 при ошибке
FV_API int32_t fv_content_indexer_enqueue_unprocessed(FVContentIndexer indexer);

/// Получить статус (JSON)
/// @return JSON с полями: pending, processed, failed, isRunning, currentFile
FV_API char* fv_content_indexer_get_status(FVContentIndexer indexer);

/// Установить максимальный размер текста для индексации (KB)
FV_API void fv_content_indexer_set_max_text_size_kb(FVContentIndexer indexer, int32_t size_kb);

/// Получить максимальный размер текста для индексации (KB)
FV_API int32_t fv_content_indexer_get_max_text_size_kb(FVContentIndexer indexer);

/// Получить количество файлов без извлечённого текста
FV_API int32_t fv_content_indexer_get_pending_count(FVContentIndexer indexer);

/// Callback для прогресса индексации контента
typedef void (*FVContentProgressCallback)(int32_t processed, int32_t total, void* user_data);

/// Переиндексировать все файлы (синхронно, блокирует)
FV_API FVError fv_content_indexer_reindex_all(FVContentIndexer indexer,
                                               FVContentProgressCallback cb, void* user_data);

/// Проверить, поддерживается ли MIME тип для извлечения текста
FV_API int32_t fv_content_indexer_can_extract(FVContentIndexer indexer, const char* mime_type);

// ═══════════════════════════════════════════════════════════
// Secure Storage
// ═══════════════════════════════════════════════════════════

typedef struct FVSecureStorage_* FVSecureStorage;

/// Создать SecureStorage
FV_API FVSecureStorage fv_secure_create(void);

/// Уничтожить SecureStorage
FV_API void fv_secure_destroy(FVSecureStorage storage);

/// Сохранить бинарные данные
FV_API FVError fv_secure_store(FVSecureStorage storage, const char* key,
                                const uint8_t* data, int32_t size);

/// Получить бинарные данные
/// @return Размер данных или -1 при ошибке. Если out_data=NULL, возвращает только размер.
FV_API int32_t fv_secure_retrieve(FVSecureStorage storage, const char* key,
                                   uint8_t* out_data, int32_t max_size);

/// Удалить данные
FV_API FVError fv_secure_remove(FVSecureStorage storage, const char* key);

/// Проверить существование ключа
FV_API int32_t fv_secure_exists(FVSecureStorage storage, const char* key);

/// Сохранить строку (convenience)
FV_API FVError fv_secure_store_string(FVSecureStorage storage, const char* key, const char* value);

/// Получить строку (convenience)
/// @return JSON строка или nullptr при ошибке. Caller должен освободить через fv_free_string.
FV_API char* fv_secure_retrieve_string(FVSecureStorage storage, const char* key);

// ═══════════════════════════════════════════════════════════
// Family Pairing
// ═══════════════════════════════════════════════════════════

typedef struct FVFamilyPairing_* FVFamilyPairing;

/// Создать FamilyPairing
/// @param storage SecureStorage для хранения секретов
FV_API FVFamilyPairing fv_pairing_create(FVSecureStorage storage);

/// Уничтожить FamilyPairing
FV_API void fv_pairing_destroy(FVFamilyPairing pairing);

/// Проверить, настроена ли семья
FV_API int32_t fv_pairing_is_configured(FVFamilyPairing pairing);

/// Получить ID устройства
/// @return UUID строка. Caller должен освободить через fv_free_string.
FV_API char* fv_pairing_get_device_id(FVFamilyPairing pairing);

/// Получить имя устройства
/// @return Имя устройства. Caller должен освободить через fv_free_string.
FV_API char* fv_pairing_get_device_name(FVFamilyPairing pairing);

/// Установить имя устройства
FV_API void fv_pairing_set_device_name(FVFamilyPairing pairing, const char* name);

/// Получить тип устройства (0=Desktop, 1=Mobile, 2=Tablet)
FV_API int32_t fv_pairing_get_device_type(FVFamilyPairing pairing);

/// Создать новую семью
/// @return JSON строка {pin, qrData, expiresAt, createdAt} или nullptr при ошибке.
///         Caller должен освободить через fv_free_string.
FV_API char* fv_pairing_create_family(FVFamilyPairing pairing);

/// Сгенерировать новый PIN
/// @return JSON строка {pin, qrData, expiresAt, createdAt} или nullptr при ошибке.
FV_API char* fv_pairing_regenerate_pin(FVFamilyPairing pairing);

/// Отменить активную pairing сессию
FV_API void fv_pairing_cancel(FVFamilyPairing pairing);

/// Есть ли активная pairing сессия
FV_API int32_t fv_pairing_has_pending(FVFamilyPairing pairing);

/// Присоединиться к семье по PIN
/// @param pin 6-значный PIN
/// @param initiator_host IP инициатора (может быть NULL)
/// @param initiator_port Порт инициатора (0 если не указан)
/// @return 0=Success, 1=InvalidPin, 2=Expired, 3=RateLimited, 4=NetworkError, 5=AlreadyConfigured
FV_API int32_t fv_pairing_join_pin(FVFamilyPairing pairing, const char* pin,
                                    const char* initiator_host, uint16_t initiator_port);

/// Присоединиться к семье по QR
/// @param qr_data Base64 данные из QR-кода
/// @return Код результата (см. fv_pairing_join_pin)
FV_API int32_t fv_pairing_join_qr(FVFamilyPairing pairing, const char* qr_data);

/// Сбросить семейную конфигурацию
FV_API void fv_pairing_reset(FVFamilyPairing pairing);

/// Получить PSK для TLS (32 байта)
/// @param out_psk Буфер для PSK (минимум 32 байта)
/// @return 32 при успехе, 0 если семья не настроена
FV_API int32_t fv_pairing_derive_psk(FVFamilyPairing pairing, uint8_t* out_psk);

/// Получить PSK Identity (deviceId для TLS)
/// @return UUID строка. Caller должен освободить через fv_free_string.
FV_API char* fv_pairing_get_psk_identity(FVFamilyPairing pairing);

/// Запустить pairing сервер
/// @param port Порт (0 = PAIRING_PORT = 45680)
/// @return FV_OK при успехе
FV_API FVError fv_pairing_start_server(FVFamilyPairing pairing, uint16_t port);

/// Остановить pairing сервер
FV_API void fv_pairing_stop_server(FVFamilyPairing pairing);

/// Проверить, запущен ли pairing сервер
FV_API int32_t fv_pairing_is_server_running(FVFamilyPairing pairing);

/// Получить порт pairing сервера
FV_API uint16_t fv_pairing_get_server_port(FVFamilyPairing pairing);

// ═══════════════════════════════════════════════════════════
// Network Discovery
// ═══════════════════════════════════════════════════════════

typedef struct FVNetworkDiscovery_* FVNetworkDiscovery;

/// Callback при обнаружении/потере устройства
/// @param event 0=found, 1=lost, 2=updated
/// @param device_json JSON DeviceInfo
typedef void (*FVDeviceCallback)(int32_t event, const char* device_json, void* user_data);

/// Создать NetworkDiscovery
FV_API FVNetworkDiscovery fv_discovery_create(void);

/// Уничтожить NetworkDiscovery
FV_API void fv_discovery_destroy(FVNetworkDiscovery discovery);

/// Запустить discovery
/// @param pairing FamilyPairing для получения deviceId/name/type
/// @param callback Callback для событий устройств (может быть NULL)
/// @param user_data User data для callback
FV_API FVError fv_discovery_start(FVNetworkDiscovery discovery,
                                   FVFamilyPairing pairing,
                                   FVDeviceCallback callback, void* user_data);

/// Остановить discovery
FV_API void fv_discovery_stop(FVNetworkDiscovery discovery);

/// Проверить, запущен ли discovery
FV_API int32_t fv_discovery_is_running(FVNetworkDiscovery discovery);

/// Получить список найденных устройств
/// @return JSON array DeviceInfo или nullptr при ошибке
FV_API char* fv_discovery_get_devices(FVNetworkDiscovery discovery);

/// Получить количество найденных устройств
FV_API int32_t fv_discovery_get_device_count(FVNetworkDiscovery discovery);

/// Получить устройство по ID
/// @return JSON DeviceInfo или nullptr если не найдено
FV_API char* fv_discovery_get_device(FVNetworkDiscovery discovery, const char* device_id);

/// Получить локальные IP адреса этого устройства
/// @return JSON array строк IP или nullptr при ошибке
FV_API char* fv_discovery_get_local_ips(void);

// ═══════════════════════════════════════════════════════════
// Network Manager (P2P координатор)
// ═══════════════════════════════════════════════════════════

typedef struct FVNetworkManager_* FVNetworkManager;

/// Callback события сети
/// @param event 0=device_discovered, 1=device_lost, 2=device_connected, 
///              3=device_disconnected, 4=state_changed, 5=error
/// @param data_json JSON с данными события
typedef void (*FVNetworkCallback)(int32_t event, const char* data_json, void* user_data);

/// Создать NetworkManager
/// @param pairing FamilyPairing для PSK и device info
FV_API FVNetworkManager fv_network_create(FVFamilyPairing pairing);

/// Уничтожить NetworkManager
FV_API void fv_network_destroy(FVNetworkManager mgr);

/// Запустить сеть (discovery + server)
/// @param port TCP порт (0 = авто)
/// @param callback Callback для событий (может быть NULL)
/// @param user_data User data для callback
FV_API FVError fv_network_start(FVNetworkManager mgr, uint16_t port,
                                 FVNetworkCallback callback, void* user_data);

/// Остановить сеть
FV_API void fv_network_stop(FVNetworkManager mgr);

/// Проверить состояние (0=stopped, 1=starting, 2=running, 3=stopping, 4=error)
FV_API int32_t fv_network_get_state(FVNetworkManager mgr);

/// Проверить, запущен ли
FV_API int32_t fv_network_is_running(FVNetworkManager mgr);

/// Получить порт сервера
FV_API uint16_t fv_network_get_port(FVNetworkManager mgr);

/// Получить обнаруженные устройства (JSON array)
FV_API char* fv_network_get_discovered_devices(FVNetworkManager mgr);

/// Получить подключённые устройства (JSON array)
FV_API char* fv_network_get_connected_devices(FVNetworkManager mgr);

/// Подключиться к устройству по ID
FV_API FVError fv_network_connect_to_device(FVNetworkManager mgr, const char* device_id);

/// Подключиться к устройству по адресу
FV_API FVError fv_network_connect_to_address(FVNetworkManager mgr, 
                                              const char* host, uint16_t port);

/// Отключиться от устройства
FV_API void fv_network_disconnect_device(FVNetworkManager mgr, const char* device_id);

/// Отключиться от всех
FV_API void fv_network_disconnect_all(FVNetworkManager mgr);

/// Проверить подключение к устройству
FV_API int32_t fv_network_is_connected_to(FVNetworkManager mgr, const char* device_id);

/// Получить последнюю ошибку
FV_API char* fv_network_get_last_error(FVNetworkManager mgr);

/// Установить базу данных для синхронизации индекса
/// @param db База данных (должна быть инициализирована)
/// @param device_id ID этого устройства
FV_API FVError fv_network_set_database(FVNetworkManager mgr, FVDatabase db, const char* device_id);

/// Запросить синхронизацию индекса с устройством
/// @param device_id ID устройства (должно быть подключено)
/// @param full_sync true для полной синхронизации
FV_API FVError fv_network_request_sync(FVNetworkManager mgr, const char* device_id, int32_t full_sync);

/// Получить удалённые файлы (JSON array)
/// Требует предварительного вызова fv_network_set_database
FV_API char* fv_network_get_remote_files(FVNetworkManager mgr);

/// Поиск по удалённым файлам (JSON array)
FV_API char* fv_network_search_remote_files(FVNetworkManager mgr, const char* query, int32_t limit);

/// Получить количество удалённых файлов
FV_API int64_t fv_network_get_remote_file_count(FVNetworkManager mgr);

// ═══════════════════════════════════════════════════════════
// File Transfer
// ═══════════════════════════════════════════════════════════

/// Установить директорию кэша для файлов
FV_API FVError fv_network_set_cache_dir(FVNetworkManager mgr, const char* cache_dir);

/// Запросить файл с удалённого устройства
/// @param device_id ID устройства (должно быть подключено)
/// @param file_id ID файла на удалённом устройстве
/// @param file_name Имя файла (для сохранения)
/// @param expected_size Ожидаемый размер файла
/// @param checksum Контрольная сумма (может быть NULL)
/// @return request_id для отслеживания, или NULL при ошибке
FV_API char* fv_network_request_file(FVNetworkManager mgr, const char* device_id,
                                      int64_t file_id, const char* file_name,
                                      int64_t expected_size, const char* checksum);

/// Отменить запрос файла
FV_API void fv_network_cancel_file_request(FVNetworkManager mgr, const char* request_id);

/// Отменить все запросы файлов к устройству
FV_API void fv_network_cancel_all_file_requests(FVNetworkManager mgr, const char* device_id);

/// Получить активные передачи (JSON array)
FV_API char* fv_network_get_active_transfers(FVNetworkManager mgr);

/// Получить прогресс передачи (JSON)
FV_API char* fv_network_get_transfer_progress(FVNetworkManager mgr, const char* request_id);

/// Проверить, есть ли файл в кэше
/// @param device_id ID устройства-источника
/// @param file_id ID файла
/// @param checksum Контрольная сумма (опционально)
FV_API int32_t fv_network_is_file_cached(FVNetworkManager mgr, const char* device_id, int64_t file_id, const char* checksum);

/// Получить путь к кэшированному файлу
/// @param device_id ID устройства-источника
/// @param file_id ID файла
FV_API char* fv_network_get_cached_path(FVNetworkManager mgr, const char* device_id, int64_t file_id);

/// Очистить кэш файлов
FV_API void fv_network_clear_cache(FVNetworkManager mgr);

/// Получить размер кэша (в байтах)
FV_API int64_t fv_network_get_cache_size(FVNetworkManager mgr);

/// Проверить, есть ли активные передачи
FV_API int32_t fv_network_has_active_transfers(FVNetworkManager mgr);

// ═══════════════════════════════════════════════════════════
// Index Sync (standalone)
// ═══════════════════════════════════════════════════════════

typedef struct FVIndexSyncManager_* FVIndexSyncManager;

/// Создать IndexSyncManager
/// @param db База данных
/// @param device_id ID этого устройства
FV_API FVIndexSyncManager fv_sync_create(FVDatabase db, const char* device_id);

/// Уничтожить IndexSyncManager
FV_API void fv_sync_destroy(FVIndexSyncManager mgr);

/// Получить удалённые файлы (JSON array)
FV_API char* fv_sync_get_remote_files(FVIndexSyncManager mgr);

/// Получить удалённые файлы с устройства (JSON array)
FV_API char* fv_sync_get_remote_files_from(FVIndexSyncManager mgr, const char* device_id);

/// Поиск по удалённым файлам (JSON array)
FV_API char* fv_sync_search_remote(FVIndexSyncManager mgr, const char* query, int32_t limit);

/// Количество удалённых файлов
FV_API int64_t fv_sync_get_remote_count(FVIndexSyncManager mgr);

/// Проверить, идёт ли синхронизация
FV_API int32_t fv_sync_is_syncing(FVIndexSyncManager mgr);

#ifdef __cplusplus
}
#endif

#endif // FAMILYVAULT_C_H
