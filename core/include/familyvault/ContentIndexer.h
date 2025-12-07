// ContentIndexer.h — Фоновая индексация текста из документов
// Интегрирует TextExtractor с базой данных

#pragma once

#include "export.h"
#include "TextExtractor.h"
#include <memory>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace FamilyVault {

// Forward declarations
class Database;

// ═══════════════════════════════════════════════════════════
// Статус индексации контента
// ═══════════════════════════════════════════════════════════

struct ContentIndexerStatus {
    int pending = 0;        // Файлов в очереди
    int processed = 0;      // Обработано в текущей сессии
    int failed = 0;         // Не удалось обработать
    bool isRunning = false; // Идёт ли обработка
    std::string currentFile; // Текущий обрабатываемый файл
};

// ═══════════════════════════════════════════════════════════
// ContentIndexer — фоновое извлечение текста
// ═══════════════════════════════════════════════════════════

class FV_API ContentIndexer {
public:
    /// Callback для прогресса
    using ProgressCallback = std::function<void(int processed, int total)>;
    
    /// Callback при обработке файла
    using FileProcessedCallback = std::function<void(int64_t fileId, bool success)>;
    
    ContentIndexer(std::shared_ptr<Database> db,
                   std::shared_ptr<TextExtractorRegistry> extractors = nullptr);
    ~ContentIndexer();
    
    // Prevent copying
    ContentIndexer(const ContentIndexer&) = delete;
    ContentIndexer& operator=(const ContentIndexer&) = delete;
    
    // ═══════════════════════════════════════════════════════════
    // Основные операции
    // ═══════════════════════════════════════════════════════════
    
    /// Запустить фоновую обработку
    /// @note Создаёт рабочий поток если его ещё нет
    void start();
    
    /// Остановить фоновую обработку
    /// @param wait Ожидать завершения текущего файла
    void stop(bool wait = true);
    
    /// Проверить, запущена ли обработка
    bool isRunning() const;
    
    /// Обработать конкретный файл (синхронно)
    /// @param fileId ID файла в БД
    /// @return true если текст успешно извлечён
    bool processFile(int64_t fileId);
    
    /// Добавить файл в очередь обработки
    void enqueueFile(int64_t fileId);
    
    /// Добавить все файлы без извлечённого текста в очередь
    /// @return Количество добавленных файлов
    int enqueueUnprocessed();
    
    /// Переиндексировать все файлы
    /// @param onProgress Callback для прогресса
    /// @note Блокирует до завершения
    void reindexAll(ProgressCallback onProgress = nullptr);
    
    /// Получить статус
    ContentIndexerStatus getStatus() const;
    
    /// Получить количество файлов без извлечённого текста
    int getPendingCount() const;
    
    // ═══════════════════════════════════════════════════════════
    // Callbacks
    // ═══════════════════════════════════════════════════════════
    
    void setProgressCallback(ProgressCallback cb);
    void setFileProcessedCallback(FileProcessedCallback cb);
    
    // ═══════════════════════════════════════════════════════════
    // Настройки
    // ═══════════════════════════════════════════════════════════
    
    /// Установить максимальное количество файлов для обработки за сессию
    void setMaxFilesPerSession(int max) { m_maxFilesPerSession = max; }
    
    /// Установить задержку между файлами (мс)
    void setDelayBetweenFiles(int ms) { m_delayBetweenFiles = ms; }
    
    /// Установить максимальный размер текста для индексации (KB)
    void setMaxTextSizeKB(int sizeKB) { m_maxTextSizeKB.store(sizeKB); }
    
    /// Получить максимальный размер текста (KB)
    int getMaxTextSizeKB() const { return m_maxTextSizeKB.load(); }
    
private:
    /// Рабочая функция потока
    void workerThread();
    
    /// Обработать файл и сохранить результат в БД
    bool processFileInternal(int64_t fileId);
    
    /// Сохранить извлечённый контент в БД
    void saveContent(int64_t fileId, const ExtractionResult& result);
    
    /// Обновить FTS индекс
    void updateFts(int64_t fileId, const std::string& content);
    
    /// Получить информацию о файле для извлечения
    struct FileInfo {
        int64_t id = 0;
        std::string fullPath;
        std::string mimeType;
        int64_t size = 0;
    };
    std::optional<FileInfo> getFileInfo(int64_t fileId) const;
    
    // Database и extractors
    std::shared_ptr<Database> m_db;
    std::shared_ptr<TextExtractorRegistry> m_extractors;
    
    // Worker thread
    std::thread m_worker;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    
    // Queue
    std::queue<int64_t> m_queue;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    
    // Statistics
    std::atomic<int> m_processed{0};
    std::atomic<int> m_failed{0};
    std::string m_currentFile;
    mutable std::mutex m_statusMutex;
    
    // Callbacks
    ProgressCallback m_progressCallback;
    FileProcessedCallback m_fileProcessedCallback;
    mutable std::mutex m_callbackMutex;
    
    // Settings
    int m_maxFilesPerSession = 1000;
    int m_delayBetweenFiles = 10;          // ms
    std::atomic<int> m_maxTextSizeKB{100}; // KB, atomic for thread safety
};

} // namespace FamilyVault

