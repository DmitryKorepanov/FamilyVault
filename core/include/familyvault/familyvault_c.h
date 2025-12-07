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

#ifdef __cplusplus
}
#endif

#endif // FAMILYVAULT_C_H
