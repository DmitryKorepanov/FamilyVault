#pragma once

#include "Models.h"
#include <functional>
#include <atomic>
#include <string>
#include <vector>
#include <memory>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Структуры для сканирования
// ═══════════════════════════════════════════════════════════

struct ScanProgress {
    int64_t totalFiles = 0;
    int64_t processedFiles = 0;
    int64_t totalSize = 0;
    std::string currentFile;
    bool isCountingPhase = true;
};

struct ScanOptions {
    bool includeHidden = false;
    bool followSymlinks = false;
    std::vector<std::string> excludePatterns;   // glob patterns
    std::vector<std::string> includeExtensions; // пустой = все
};

struct ScannedFile {
    std::string relativePath;
    std::string name;
    std::string extension;
    int64_t size = 0;
    int64_t createdAt = 0;
    int64_t modifiedAt = 0;
    std::string mimeType;
    ContentType contentType = ContentType::Unknown;
};

// ═══════════════════════════════════════════════════════════
// Callbacks
// ═══════════════════════════════════════════════════════════

using ScanProgressCallback = std::function<void(const ScanProgress&)>;
using FileFoundCallback = std::function<void(const ScannedFile&)>;

// ═══════════════════════════════════════════════════════════
// Токен отмены
// ═══════════════════════════════════════════════════════════

class CancellationToken {
public:
    CancellationToken() : m_cancelled(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() { *m_cancelled = true; }
    bool isCancelled() const { return *m_cancelled; }
    void reset() { *m_cancelled = false; }

private:
    std::shared_ptr<std::atomic<bool>> m_cancelled;
};

// ═══════════════════════════════════════════════════════════
// FileScanner
// ═══════════════════════════════════════════════════════════

class FileScanner {
public:
    FileScanner();
    ~FileScanner();

    /// Синхронное сканирование
    void scan(
        const std::string& rootPath,
        FileFoundCallback onFile,
        ScanProgressCallback onProgress = nullptr,
        const ScanOptions& options = {},
        CancellationToken cancelToken = CancellationToken()
    );

    /// Отмена всех сканирований
    void cancelAll();

    /// Проверка статуса
    bool isRunning() const;

private:
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_running{false};

    bool shouldSkipDirectory(const std::string& name, const ScanOptions& options) const;
    bool shouldSkipFile(const std::string& name, const std::string& ext, const ScanOptions& options) const;
    bool matchesPattern(const std::string& name, const std::string& pattern) const;
    int64_t countFiles(const std::string& rootPath, const ScanOptions& options, CancellationToken& token);
};

} // namespace FamilyVault

