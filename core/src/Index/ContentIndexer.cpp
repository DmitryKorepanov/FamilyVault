#include "familyvault/ContentIndexer.h"
#include "familyvault/Database.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════

ContentIndexer::ContentIndexer(std::shared_ptr<Database> db,
                               std::shared_ptr<TextExtractorRegistry> extractors)
    : m_db(std::move(db))
    , m_extractors(extractors ? std::move(extractors) : TextExtractorRegistry::createDefault())
{
    spdlog::info("ContentIndexer created");
}

ContentIndexer::~ContentIndexer() {
    stop(true);
    spdlog::info("ContentIndexer destroyed");
}

// ═══════════════════════════════════════════════════════════
// Control
// ═══════════════════════════════════════════════════════════

void ContentIndexer::start() {
    if (m_running.load()) {
        spdlog::debug("ContentIndexer: already running");
        return;
    }
    
    m_stopRequested = false;
    m_running = true;
    m_processed = 0;
    m_failed = 0;
    
    m_worker = std::thread(&ContentIndexer::workerThread, this);
    spdlog::info("ContentIndexer: started background processing");
}

void ContentIndexer::stop(bool wait) {
    if (!m_running.load()) {
        return;
    }
    
    m_stopRequested = true;
    m_queueCondition.notify_all();
    
    if (wait && m_worker.joinable()) {
        m_worker.join();
    }
    
    m_running = false;
    spdlog::info("ContentIndexer: stopped (processed: {}, failed: {})", 
                m_processed.load(), m_failed.load());
}

bool ContentIndexer::isRunning() const {
    return m_running.load();
}

// ═══════════════════════════════════════════════════════════
// Queue management
// ═══════════════════════════════════════════════════════════

void ContentIndexer::enqueueFile(int64_t fileId) {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push(fileId);
    }
    m_queueCondition.notify_one();
}

int ContentIndexer::enqueueUnprocessed() {
#if !ENABLE_TEXT_EXTRACTION
    // Text extraction disabled - nothing to enqueue
    return 0;
#else
    // Находим файлы с поддерживаемыми MIME типами:
    // 1. Без извлечённого контента
    // 2. ИЛИ изменённые после извлечения (rescan case)
    auto fileIds = m_db->query<int64_t>(R"SQL(
        SELECT f.id FROM files f
        LEFT JOIN file_content fc ON f.id = fc.file_id
        WHERE f.mime_type IS NOT NULL
          AND (
              f.mime_type LIKE 'text/%'
              OR f.mime_type = 'application/pdf'
              OR f.mime_type = 'application/json'
              OR f.mime_type LIKE 'application/vnd.openxmlformats%'
              OR f.mime_type LIKE 'application/vnd.oasis.opendocument%'
          )
          AND (
              fc.file_id IS NULL                    -- Never processed
              OR f.modified_at > fc.extracted_at    -- Modified after extraction
          )
        ORDER BY f.indexed_at DESC
        LIMIT ?
    )SQL",
    [](sqlite3_stmt* stmt) {
        return Database::getInt64(stmt, 0);
    },
    m_maxFilesPerSession);
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (int64_t id : fileIds) {
            m_queue.push(id);
        }
    }
    
    if (!fileIds.empty()) {
        m_queueCondition.notify_one();
    }
    
    spdlog::info("ContentIndexer: enqueued {} unprocessed files", fileIds.size());
    return static_cast<int>(fileIds.size());
#endif // ENABLE_TEXT_EXTRACTION
}

int ContentIndexer::getPendingCount() const {
#if !ENABLE_TEXT_EXTRACTION
    return 0;
#else
    auto count = m_db->queryScalar(R"SQL(
        SELECT COUNT(*) FROM files f
        LEFT JOIN file_content fc ON f.id = fc.file_id
        WHERE f.mime_type IS NOT NULL
          AND (
              f.mime_type LIKE 'text/%'
              OR f.mime_type = 'application/pdf'
              OR f.mime_type = 'application/json'
              OR f.mime_type LIKE 'application/vnd.openxmlformats%'
              OR f.mime_type LIKE 'application/vnd.oasis.opendocument%'
          )
          AND (
              fc.file_id IS NULL                    -- Never processed
              OR f.modified_at > fc.extracted_at    -- Modified after extraction
          )
    )SQL");
    
    return static_cast<int>(count);
#endif // ENABLE_TEXT_EXTRACTION
}

// ═══════════════════════════════════════════════════════════
// Processing
// ═══════════════════════════════════════════════════════════

bool ContentIndexer::processFile(int64_t fileId) {
    return processFileInternal(fileId);
}

void ContentIndexer::reindexAll(ProgressCallback onProgress) {
    spdlog::info("ContentIndexer: starting full reindex");
    
    // Получаем все файлы с поддерживаемыми MIME типами
    auto fileIds = m_db->query<int64_t>(R"SQL(
        SELECT id FROM files
        WHERE mime_type IS NOT NULL
          AND (
              mime_type LIKE 'text/%'
              OR mime_type = 'application/pdf'
              OR mime_type = 'application/json'
              OR mime_type LIKE 'application/vnd.openxmlformats%'
              OR mime_type LIKE 'application/vnd.oasis.opendocument%'
          )
        ORDER BY indexed_at DESC
    )SQL",
    [](sqlite3_stmt* stmt) {
        return Database::getInt64(stmt, 0);
    });
    
    int total = static_cast<int>(fileIds.size());
    int processed = 0;
    int failed = 0;
    
    for (int64_t fileId : fileIds) {
        if (m_stopRequested.load()) {
            spdlog::info("ContentIndexer: reindex cancelled");
            break;
        }
        
        bool success = processFileInternal(fileId);
        processed++;
        if (!success) failed++;
        
        if (onProgress) {
            onProgress(processed, total);
        }
        
        // Небольшая задержка чтобы не перегружать систему
        if (m_delayBetweenFiles > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_delayBetweenFiles));
        }
    }
    
    spdlog::info("ContentIndexer: reindex completed ({}/{} files, {} failed)", 
                processed, total, failed);
}

ContentIndexerStatus ContentIndexer::getStatus() const {
    ContentIndexerStatus status;
    
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        status.pending = static_cast<int>(m_queue.size());
    }
    
    status.processed = m_processed.load();
    status.failed = m_failed.load();
    status.isRunning = m_running.load();
    
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        status.currentFile = m_currentFile;
    }
    
    return status;
}

// ═══════════════════════════════════════════════════════════
// Callbacks
// ═══════════════════════════════════════════════════════════

void ContentIndexer::setProgressCallback(ProgressCallback cb) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_progressCallback = std::move(cb);
}

void ContentIndexer::setFileProcessedCallback(FileProcessedCallback cb) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_fileProcessedCallback = std::move(cb);
}

// ═══════════════════════════════════════════════════════════
// Worker thread
// ═══════════════════════════════════════════════════════════

void ContentIndexer::workerThread() {
    spdlog::debug("ContentIndexer: worker thread started");
    
    while (!m_stopRequested.load()) {
        int64_t fileId = 0;
        
        // Получаем файл из очереди
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            m_queueCondition.wait_for(lock, std::chrono::seconds(1), [this] {
                return m_stopRequested.load() || !m_queue.empty();
            });
            
            if (m_stopRequested.load()) {
                break;
            }
            
            if (m_queue.empty()) {
                // Очередь пуста — проверяем есть ли ещё необработанные файлы
                lock.unlock();
                int pending = getPendingCount();
                if (pending > 0) {
                    spdlog::debug("ContentIndexer: queue empty but {} files pending, re-enqueuing", pending);
                    enqueueUnprocessed();
                }
                continue;
            }
            
            fileId = m_queue.front();
            m_queue.pop();
        }
        
        // Обрабатываем файл
        bool success = processFileInternal(fileId);
        
        if (success) {
            m_processed++;
        } else {
            m_failed++;
        }
        
        // Вызываем callback
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_fileProcessedCallback) {
                m_fileProcessedCallback(fileId, success);
            }
            if (m_progressCallback) {
                std::lock_guard<std::mutex> qLock(m_queueMutex);
                int pending = static_cast<int>(m_queue.size());
                m_progressCallback(m_processed.load(), m_processed.load() + pending);
            }
        }
        
        // Небольшая задержка
        if (m_delayBetweenFiles > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_delayBetweenFiles));
        }
    }
    
    spdlog::debug("ContentIndexer: worker thread finished");
}

// ═══════════════════════════════════════════════════════════
// Internal processing
// ═══════════════════════════════════════════════════════════

bool ContentIndexer::processFileInternal(int64_t fileId) {
#if !ENABLE_TEXT_EXTRACTION
    // Text extraction disabled at compile time (Android builds)
    // Skip processing entirely to avoid filling DB with unsupported records
    (void)fileId;
    return false;
#else
    try {
        // Получаем информацию о файле
        auto fileInfo = getFileInfo(fileId);
        if (!fileInfo) {
            spdlog::warn("ContentIndexer: file {} not found", fileId);
            return false;
        }
        
        // Обновляем текущий файл
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_currentFile = fileInfo->fullPath;
        }
        
        // Проверяем, можем ли извлечь текст
        if (!m_extractors->canExtract(fileInfo->mimeType)) {
            spdlog::debug("ContentIndexer: no extractor for MIME type '{}' (file {})", 
                         fileInfo->mimeType, fileId);
            // Сохраняем пустую запись чтобы не пытаться снова
            saveContent(fileId, ExtractionResult{
                .text = "",
                .method = "unsupported",
                .language = "",
                .confidence = 0.0
            });
            return false;
        }
        
        // Извлекаем текст
        auto result = m_extractors->extract(fileInfo->fullPath, fileInfo->mimeType);
        
        if (!result || result->isEmpty()) {
            spdlog::debug("ContentIndexer: no text extracted from file {}", fileId);
            // Всё равно сохраняем пустую запись чтобы не обрабатывать повторно
            saveContent(fileId, ExtractionResult{
                .text = "",
                .method = "empty",
                .language = "",
                .confidence = 0.0
            });
            return false;
        }
        
        // Сохраняем результат
        saveContent(fileId, *result);
        
        spdlog::debug("ContentIndexer: extracted {} chars from file {} using {}", 
                     result->text.length(), fileId, result->method);
        
        // Очищаем текущий файл
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_currentFile.clear();
        }
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("ContentIndexer: exception processing file {}: {}", fileId, e.what());
        // Сохраняем пустую запись с пометкой об ошибке
        try {
            saveContent(fileId, ExtractionResult{
                .text = "",
                .method = "error",
                .language = "",
                .confidence = 0.0
            });
        } catch (...) {}
        return false;
    } catch (...) {
        spdlog::error("ContentIndexer: unknown exception processing file {}", fileId);
        try {
            saveContent(fileId, ExtractionResult{
                .text = "",
                .method = "error",
                .language = "",
                .confidence = 0.0
            });
        } catch (...) {}
        return false;
    }
#endif // ENABLE_TEXT_EXTRACTION
}

std::optional<ContentIndexer::FileInfo> ContentIndexer::getFileInfo(int64_t fileId) const {
    return m_db->queryOne<FileInfo>(R"SQL(
        SELECT f.id, wf.path || '/' || f.relative_path as full_path, f.mime_type, f.size
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.id = ?
    )SQL",
    [](sqlite3_stmt* stmt) {
        FileInfo info;
        info.id = Database::getInt64(stmt, 0);
        info.fullPath = Database::getString(stmt, 1);
        info.mimeType = Database::getString(stmt, 2);
        info.size = Database::getInt64(stmt, 3);
        return info;
    },
    fileId);
}

void ContentIndexer::saveContent(int64_t fileId, const ExtractionResult& result) {
    // Ограничиваем текст для FTS (настраиваемый лимит)
    std::string truncatedText = result.text;
    const size_t maxTextSize = static_cast<size_t>(m_maxTextSizeKB.load()) * 1024;
    if (truncatedText.size() > maxTextSize) {
        truncatedText = truncatedText.substr(0, maxTextSize);
    }
    
    // Сохраняем метаданные в file_content (текст идёт только в FTS)
    // Используем INSERT OR IGNORE + UPDATE вместо INSERT OR REPLACE (coding guidelines)
    m_db->execute(R"SQL(
        INSERT OR IGNORE INTO file_content (file_id, content, extraction_method, language, extracted_at)
        VALUES (?, '', ?, ?, strftime('%s', 'now'))
    )SQL",
    fileId, result.method, result.language);
    
    m_db->execute(R"SQL(
        UPDATE file_content 
        SET extraction_method = ?, language = ?, extracted_at = strftime('%s', 'now')
        WHERE file_id = ?
    )SQL",
    result.method, result.language, fileId);
    
    // Обновляем FTS вручную (без триггеров)
    updateFts(fileId, truncatedText);
}

void ContentIndexer::updateFts(int64_t fileId, const std::string& content) {
    try {
        // Простой UPDATE для обычной (не contentless) FTS5 таблицы
        m_db->execute(R"SQL(
            UPDATE files_fts SET content = ? WHERE rowid = ?
        )SQL", content, fileId);
    } catch (const std::exception& e) {
        spdlog::warn("FTS update failed for file {}: {}", fileId, e.what());
    }
}

} // namespace FamilyVault

