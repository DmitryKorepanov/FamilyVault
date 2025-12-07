#include "familyvault/FileScanner.h"
#include "familyvault/MimeTypeDetector.h"
#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace FamilyVault {

// Helper to safely convert path to UTF-8 string
static std::string pathToUtf8(const fs::path& p) {
#ifdef _WIN32
    try {
        // On Windows, use u8string() for proper UTF-8 conversion
        auto u8str = p.u8string();
        return std::string(u8str.begin(), u8str.end());
    } catch (...) {
        // Fallback: try native string
        try {
            return p.string();
        } catch (...) {
            return ""; // Skip unparseable paths
        }
    }
#else
    return p.string();
#endif
}

// Helper to convert UTF-8 string to fs::path (handles Unicode on Windows)
static fs::path utf8ToPath(const std::string& s) {
#ifdef _WIN32
    try {
        // Use Windows API for UTF-8 to wide string conversion
        if (s.empty()) return fs::path();
        
        int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return fs::path(s);
        
        std::wstring wstr(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &wstr[0], wlen);
        return fs::path(wstr);
    } catch (...) {
        return fs::path(s);
    }
#else
    return fs::path(s);
#endif
}

// Системные директории для пропуска
static const std::vector<std::string> SYSTEM_DIRS = {
    "$RECYCLE.BIN",
    "System Volume Information",
    ".Trash",
    ".Trash-1000",
    "lost+found",
    ".git",
    ".svn",
    ".hg",
    "node_modules",
    "__pycache__",
    ".cache",
    ".vs",
    ".idea",
    ".vscode",
};

// Расширения файлов для пропуска (конфиги, системные, временные)
static const std::vector<std::string> SKIP_EXTENSIONS = {
    "ini", "cfg", "conf", "config",    // Конфиги
    "log", "bak", "tmp", "temp",       // Временные
    "lock", "pid",                      // Lock файлы
    "lnk", "url",                       // Ярлыки Windows
    "db", "db-journal", "db-shm", "db-wal",  // SQLite
    "thumbs", "ds_store",               // Системные
    "sys", "drv",                       // Драйверы
    "pyc", "pyo", "class",              // Скомпилированные
    "o", "obj", "pdb", "ilk",           // Object файлы
    "suo", "user", "sln", "vcxproj",    // Проекты VS
    "gitignore", "gitattributes",       // Git
    "npmrc", "yarnrc",                  // npm/yarn
    "eslintrc", "prettierrc",           // Линтеры
    "editorconfig",                     // Редактор
};

FileScanner::FileScanner() = default;
FileScanner::~FileScanner() {
    cancelAll();
}

bool FileScanner::isRunning() const {
    return m_running;
}

void FileScanner::cancelAll() {
    m_cancelRequested = true;
}

bool FileScanner::shouldSkipDirectory(const std::string& name, const ScanOptions& options) const {
    // Скрытые директории (начинаются с точки)
    if (!options.includeHidden && !name.empty() && name[0] == '.') {
        return true;
    }

    // Системные директории
    for (const auto& sysDir : SYSTEM_DIRS) {
        if (name == sysDir) {
            return true;
        }
    }

    // Exclude patterns
    for (const auto& pattern : options.excludePatterns) {
        if (matchesPattern(name, pattern)) {
            return true;
        }
    }

    return false;
}

bool FileScanner::shouldSkipFile(const std::string& name, const std::string& ext,
                                  const ScanOptions& options) const {
    // Скрытые файлы
    if (!options.includeHidden && !name.empty() && name[0] == '.') {
        return true;
    }

    // Exclude patterns
    for (const auto& pattern : options.excludePatterns) {
        if (matchesPattern(name, pattern)) {
            return true;
        }
    }

    // Пропускаем мусорные расширения
    if (!ext.empty()) {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        for (const auto& skipExt : SKIP_EXTENSIONS) {
            if (lowerExt == skipExt) {
                return true;
            }
        }
    }

    // Include extensions filter
    if (!options.includeExtensions.empty()) {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        bool found = false;
        for (const auto& allowedExt : options.includeExtensions) {
            std::string lowerAllowed = allowedExt;
            std::transform(lowerAllowed.begin(), lowerAllowed.end(), lowerAllowed.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lowerExt == lowerAllowed) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }
    }

    return false;
}

bool FileScanner::matchesPattern(const std::string& name, const std::string& pattern) const {
    // Простое сопоставление с wildcard (*)
    if (pattern.empty()) return false;

    // Точное совпадение
    if (pattern.find('*') == std::string::npos) {
        return name == pattern;
    }

    // Паттерн *.ext
    if (pattern[0] == '*' && pattern.find('*', 1) == std::string::npos) {
        std::string suffix = pattern.substr(1);
        if (name.length() >= suffix.length()) {
            return name.compare(name.length() - suffix.length(), suffix.length(), suffix) == 0;
        }
        return false;
    }

    // Паттерн prefix*
    if (pattern.back() == '*' && pattern.find('*') == pattern.length() - 1) {
        std::string prefix = pattern.substr(0, pattern.length() - 1);
        return name.compare(0, prefix.length(), prefix) == 0;
    }

    // Сложные паттерны — просто проверяем содержит ли имя часть без *
    std::string part = pattern;
    part.erase(std::remove(part.begin(), part.end(), '*'), part.end());
    return name.find(part) != std::string::npos;
}

int64_t FileScanner::countFiles(const std::string& rootPath, const ScanOptions& options,
                                 CancellationToken& token) {
    int64_t count = 0;
    int entriesProcessed = 0;

    spdlog::info("countFiles: Creating iterator for {}", rootPath);
    try {
        fs::recursive_directory_iterator it(utf8ToPath(rootPath), fs::directory_options::skip_permission_denied);
        fs::recursive_directory_iterator end;
        spdlog::info("countFiles: Iterator created, starting count");

        while (it != end) {
            if (token.isCancelled() || m_cancelRequested) {
                spdlog::info("countFiles: Cancelled");
                break;
            }

            entriesProcessed++;
            if (entriesProcessed % 500 == 0) {
                spdlog::info("countFiles: Processed {} entries, found {} files so far", entriesProcessed, count);
            }

            try {
                const auto& entry = *it;

                // Get filename safely
                std::string name;
                try {
                    name = pathToUtf8(entry.path().filename());
                } catch (...) {
                    ++it;
                    continue;
                }

                if (entry.is_directory()) {
                    if (shouldSkipDirectory(name, options)) {
                        it.disable_recursion_pending();
                    }
                } else if (entry.is_regular_file()) {
                    std::string ext = MimeTypeDetector::extractExtension(name);

                    if (!shouldSkipFile(name, ext, options)) {
                        count++;
                    }
                }
                ++it;
            } catch (const fs::filesystem_error& e) {
                spdlog::debug("countFiles: fs error at entry {}: {}", entriesProcessed, e.what());
                try { ++it; } catch (...) { break; }
            } catch (const std::exception& e) {
                spdlog::debug("countFiles: exception at entry {}: {}", entriesProcessed, e.what());
                try { ++it; } catch (...) { break; }
            } catch (...) {
                spdlog::debug("countFiles: unknown exception at entry {}", entriesProcessed);
                try { ++it; } catch (...) { break; }
            }
        }
        spdlog::info("countFiles: Finished, {} entries processed, {} files found", entriesProcessed, count);
    } catch (const fs::filesystem_error& e) {
        spdlog::warn("Error counting files in {}: {}", rootPath, e.what());
    } catch (const std::exception& e) {
        spdlog::warn("Unexpected error counting files in {}: {}", rootPath, e.what());
    }

    return count;
}

void FileScanner::scan(const std::string& rootPath,
                       FileFoundCallback onFile,
                       ScanProgressCallback onProgress,
                       const ScanOptions& options,
                       CancellationToken cancelToken) {
    if (m_running) {
        spdlog::warn("Scanner is already running");
        return;
    }

    m_running = true;
    m_cancelRequested = false;

    ScanProgress progress;

    // Count files first if a progress callback is provided (so we can report percentage)
    if (onProgress) {
        progress.isCountingPhase = true;
        progress.currentFile = "Counting files...";
        onProgress(progress);
        
        progress.totalFiles = countFiles(rootPath, options, cancelToken);
        spdlog::info("Counted {} files to process", progress.totalFiles);
    } else {
        progress.totalFiles = 0;
    }
    
    if (cancelToken.isCancelled() || m_cancelRequested) {
        m_running = false;
        return;
    }

    progress.isCountingPhase = false;
    fs::path root = utf8ToPath(rootPath);

    try {
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied);
        fs::recursive_directory_iterator end;

        while (it != end) {
            if (cancelToken.isCancelled() || m_cancelRequested) {
                break;
            }

            try {
                const auto& entry = *it;

                // Skip symlinks/junctions to avoid broken links
                try {
                    if (entry.is_symlink()) {
                        ++it;
                        continue;
                    }
                } catch (...) {
                    ++it;
                    continue;
                }

                // Get filename safely
                std::string name;
                try {
                    name = pathToUtf8(entry.path().filename());
                } catch (...) {
                    ++it;
                    continue;
                }

                if (entry.is_directory()) {
                    if (shouldSkipDirectory(name, options)) {
                        it.disable_recursion_pending();
                    }
                } else if (entry.is_regular_file()) {
                    std::string ext = MimeTypeDetector::extractExtension(name);

                    if (!shouldSkipFile(name, ext, options)) {
                        // Создаём ScannedFile
                        ScannedFile file;
                        file.name = name;
                        file.extension = ext;

                        // Относительный путь (UTF-8)
                        try {
                            auto relPath = fs::relative(entry.path(), root);
                            file.relativePath = pathToUtf8(relPath);
                            // Use forward slashes for consistency
                            std::replace(file.relativePath.begin(), file.relativePath.end(), '\\', '/');
                        } catch (...) {
                            file.relativePath = name;
                        }

                        // Размер и даты
                        try {
                            file.size = static_cast<int64_t>(entry.file_size());

                            auto lastWrite = fs::last_write_time(entry);
                            auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
                                std::chrono::clock_cast<std::chrono::system_clock>(lastWrite)
                            );
                            file.modifiedAt = sctp.time_since_epoch().count();

                            // created_at не всегда доступен, используем modified
                            file.createdAt = file.modifiedAt;
                        } catch (...) {
                            file.size = 0;
                            file.modifiedAt = 0;
                            file.createdAt = 0;
                        }

                        // MIME тип
                        try {
                            file.mimeType = MimeTypeDetector::detect(pathToUtf8(entry.path()), ext);
                        } catch (...) {
                            file.mimeType = "application/octet-stream";
                        }
                        file.contentType = MimeTypeDetector::mimeToContentType(file.mimeType);

                        // Callback
                        if (onFile) {
                            onFile(file);
                        }

                        progress.processedFiles++;
                        progress.totalSize += file.size;
                    }
                }

                // Progress callback - report every 10 files for smoother UI updates
                if (onProgress && progress.processedFiles % 10 == 0) {
                    progress.currentFile = name;
                    onProgress(progress);
                }

                try {
                    ++it;
                } catch (...) {
                    try { it.disable_recursion_pending(); ++it; } catch (...) {}
                }
            } catch (const fs::filesystem_error&) {
                try { it.disable_recursion_pending(); ++it; } catch (...) {}
            } catch (const std::exception&) {
                try { it.disable_recursion_pending(); ++it; } catch (...) {}
            } catch (...) {
                try { it.disable_recursion_pending(); ++it; } catch (...) {}
            }
        }
    } catch (const fs::filesystem_error& e) {
        spdlog::error("Error scanning {}: {}", rootPath, e.what());
    } catch (const std::exception& e) {
        spdlog::error("Unexpected error scanning {}: {}", rootPath, e.what());
    } catch (...) {
        spdlog::error("Unknown error scanning {}", rootPath);
    }

    // Финальный progress
    if (onProgress) {
        progress.currentFile = "";
        onProgress(progress);
    }

    m_running = false;
    spdlog::info("Scan completed: {} files, {} bytes", progress.processedFiles, progress.totalSize);
}

} // namespace FamilyVault

