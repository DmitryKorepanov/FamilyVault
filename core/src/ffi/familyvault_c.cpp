// familyvault_c.cpp — реализация C API для FFI

#include "ffi_internal.h"
#include "familyvault/familyvault_c.h"
#include "familyvault/core.h"
#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include "familyvault/SearchEngine.h"
#include "familyvault/TagManager.h"
#include "familyvault/DuplicateFinder.h"
#include "familyvault/ContentIndexer.h"
#include "familyvault/Models.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <atomic>

using json = nlohmann::json;
using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// Thread-local error state for proper C API error handling
// ═══════════════════════════════════════════════════════════

// Thread-local error state (shared across ffi_*.cpp files)
thread_local FVError g_lastError = FV_OK;
thread_local std::string g_lastErrorMessage;

// Non-static so other ffi_*.cpp files can call these
void setLastError(FVError error, const std::string& message) {
    g_lastError = error;
    g_lastErrorMessage = message;
    if (error != FV_OK) {
        spdlog::error("FFI error (code {}): {}", static_cast<int>(error), message);
    }
}

// DatabaseHolder is defined in ffi_internal.h

// ═══════════════════════════════════════════════════════════
// Manager Wrappers - store DatabaseHolder reference for auto-release
// ═══════════════════════════════════════════════════════════

/// Base wrapper that holds manager + DatabaseHolder reference
template<typename T>
struct ManagerWrapper {
    std::unique_ptr<T> manager;
    DatabaseHolder* dbHolder;  // Not owned, but ref-counted
    
    ManagerWrapper(T* mgr, DatabaseHolder* holder)
        : manager(mgr), dbHolder(holder) {
        if (dbHolder) dbHolder->addRef();
    }
    
    ~ManagerWrapper() {
        manager.reset();  // Delete manager first
        if (dbHolder) dbHolder->release();
    }
    
    T* get() const { return manager.get(); }
};

using IndexManagerWrapper = ManagerWrapper<IndexManager>;
using SearchEngineWrapper = ManagerWrapper<SearchEngine>;
using TagManagerWrapper = ManagerWrapper<TagManager>;
using ContentIndexerWrapper = ManagerWrapper<ContentIndexer>;

/// DuplicateFinder wrapper also stores optional IndexManager reference
struct DuplicateFinderWrapper {
    std::unique_ptr<DuplicateFinder> finder;
    DatabaseHolder* dbHolder;
    IndexManager* indexMgr;  // Optional, not owned
    
    DuplicateFinderWrapper(DuplicateFinder* f, DatabaseHolder* holder, IndexManager* idx = nullptr)
        : finder(f), dbHolder(holder), indexMgr(idx) {
        if (dbHolder) dbHolder->addRef();
    }
    
    ~DuplicateFinderWrapper() {
        finder.reset();
        if (dbHolder) dbHolder->release();
    }
    
    DuplicateFinder* get() const { return finder.get(); }
};

// ═══════════════════════════════════════════════════════════
// Хелперы
// ═══════════════════════════════════════════════════════════

char* alloc_string(const std::string& str) {
    return fv_strdup(str.c_str());
}

static json fileRecordToJson(const FileRecord& f) {
    json j;
    j["id"] = f.id;
    j["folderId"] = f.folderId;
    j["relativePath"] = f.relativePath;
    j["name"] = f.name;
    j["extension"] = f.extension;
    j["size"] = f.size;
    j["mimeType"] = f.mimeType;
    j["contentType"] = static_cast<int>(f.contentType);
    // Emit null for unset optionals, not empty strings
    j["checksum"] = f.checksum.has_value() ? json(f.checksum.value()) : json(nullptr);
    j["createdAt"] = f.createdAt;
    j["modifiedAt"] = f.modifiedAt;
    j["indexedAt"] = f.indexedAt;
    j["visibility"] = static_cast<int>(f.visibility);
    j["sourceDeviceId"] = f.sourceDeviceId.has_value() ? json(f.sourceDeviceId.value()) : json(nullptr);
    j["isRemote"] = f.isRemote;
    j["syncVersion"] = f.syncVersion;
    j["lastModifiedBy"] = f.lastModifiedBy.has_value() ? json(f.lastModifiedBy.value()) : json(nullptr);
    j["tags"] = f.tags;
    return j;
}

static json fileRecordCompactToJson(const FileRecordCompact& f) {
    return {
        {"id", f.id},
        {"folderId", f.folderId},
        {"relativePath", f.relativePath},
        {"folderPath", f.folderPath},
        {"name", f.name},
        {"extension", f.extension},
        {"size", f.size},
        {"contentType", static_cast<int>(f.contentType)},
        {"modifiedAt", f.modifiedAt},
        {"isRemote", f.isRemote},
        {"hasThumbnail", f.hasThumbnail}
    };
}

static json searchResultCompactToJson(const SearchResultCompact& r) {
    json j = fileRecordCompactToJson(r.file);
    j["score"] = r.score;
    return j;
}

static json watchedFolderToJson(const WatchedFolder& f) {
    return {
        {"id", f.id},
        {"path", f.path},
        {"name", f.name},
        {"enabled", f.enabled},
        {"lastScanAt", f.lastScanAt},
        {"fileCount", f.fileCount},
        {"totalSize", f.totalSize},
        {"createdAt", f.createdAt},
        {"visibility", static_cast<int>(f.defaultVisibility)}
    };
}

static json tagToJson(const Tag& t) {
    return {
        {"id", t.id},
        {"name", t.name},
        {"source", static_cast<int>(t.source)},
        {"fileCount", t.fileCount}
    };
}

static SearchQuery parseSearchQuery(const std::string& jsonStr) {
    SearchQuery q;
    try {
        auto j = json::parse(jsonStr);
        q.text = j.value("text", "");
        if (j.contains("contentType") && !j["contentType"].is_null()) {
            q.contentType = static_cast<ContentType>(j["contentType"].get<int>());
        }
        if (j.contains("extension") && !j["extension"].is_null()) {
            q.extension = j["extension"].get<std::string>();
        }
        if (j.contains("folderId") && !j["folderId"].is_null()) {
            q.folderId = j["folderId"].get<int64_t>();
        }
        if (j.contains("dateFrom") && !j["dateFrom"].is_null()) {
            q.dateFrom = j["dateFrom"].get<int64_t>();
        }
        if (j.contains("dateTo") && !j["dateTo"].is_null()) {
            q.dateTo = j["dateTo"].get<int64_t>();
        }
        if (j.contains("minSize") && !j["minSize"].is_null()) {
            q.minSize = j["minSize"].get<int64_t>();
        }
        if (j.contains("maxSize") && !j["maxSize"].is_null()) {
            q.maxSize = j["maxSize"].get<int64_t>();
        }
        if (j.contains("tags")) {
            q.tags = j["tags"].get<std::vector<std::string>>();
        }
        if (j.contains("excludeTags")) {
            q.excludeTags = j["excludeTags"].get<std::vector<std::string>>();
        }
        if (j.contains("visibility") && !j["visibility"].is_null()) {
            q.visibility = static_cast<Visibility>(j["visibility"].get<int>());
        }
        q.includeRemote = j.value("includeRemote", false);
        q.limit = j.value("limit", 100);
        q.offset = j.value("offset", 0);
        q.sortBy = static_cast<SortBy>(j.value("sortBy", 0));
        q.sortAsc = j.value("sortAsc", false);
    } catch (...) {
        // Возвращаем дефолтный query
    }
    return q;
}

// ═══════════════════════════════════════════════════════════
// Общие функции
// ═══════════════════════════════════════════════════════════

extern "C" {

const char* fv_version(void) {
    return FamilyVault::VERSION;
}

const char* fv_error_message(FVError error) {
    switch (error) {
        case FV_OK: return "Success";
        case FV_ERROR_INVALID_ARGUMENT: return "Invalid argument";
        case FV_ERROR_DATABASE: return "Database error";
        case FV_ERROR_IO: return "I/O error";
        case FV_ERROR_NOT_FOUND: return "Not found";
        case FV_ERROR_ALREADY_EXISTS: return "Already exists";
        case FV_ERROR_AUTH_FAILED: return "Authentication failed";
        case FV_ERROR_NETWORK: return "Network error";
        case FV_ERROR_BUSY: return "Resource busy";
        case FV_ERROR_INTERNAL:
        default: return "Internal error";
    }
}

FVError fv_last_error(void) {
    return g_lastError;
}

const char* fv_last_error_message(void) {
    return g_lastErrorMessage.c_str();
}

void fv_clear_error(void) {
    g_lastError = FV_OK;
    g_lastErrorMessage.clear();
}

void fv_free_string(char* str) {
    std::free(str);
}

// ═══════════════════════════════════════════════════════════
// Database
// ═══════════════════════════════════════════════════════════

FVDatabase fv_database_open(const char* path, FVError* out_error) {
    try {
        auto* holder = new DatabaseHolder(path);
        setLastError(FV_OK);
        if (out_error) *out_error = FV_OK;
        return reinterpret_cast<FVDatabase>(holder);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        if (out_error) *out_error = FV_ERROR_DATABASE;
        return nullptr;
    }
}

FVError fv_database_close(FVDatabase db) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    auto* holder = reinterpret_cast<DatabaseHolder*>(db);
    
    // Check if there are still active managers
    if (holder->refCount() > 1) {
        setLastError(FV_ERROR_BUSY, 
            "Cannot close database: " + std::to_string(holder->refCount() - 1) + " manager(s) still active");
        return FV_ERROR_BUSY;
    }
    
    delete holder;
    setLastError(FV_OK);
    return FV_OK;
}

FVError fv_database_initialize(FVDatabase db) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        holder->initialize();
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

int32_t fv_database_is_initialized(FVDatabase db) {
    if (!db) return 0;
    return reinterpret_cast<DatabaseHolder*>(db)->isInitialized() ? 1 : 0;
}

// ═══════════════════════════════════════════════════════════
// Index Manager
// ═══════════════════════════════════════════════════════════

FVIndexManager fv_index_create(FVDatabase db) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return nullptr;
    }
    
    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        auto* mgr = new IndexManager(holder->getDatabase());
        auto* wrapper = new IndexManagerWrapper(mgr, holder);
        setLastError(FV_OK);
        return reinterpret_cast<FVIndexManager>(wrapper);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_index_destroy(FVIndexManager mgr) {
    if (mgr) {
        delete reinterpret_cast<IndexManagerWrapper*>(mgr);
    }
}

int64_t fv_index_add_folder(FVIndexManager mgr, const char* path,
                             const char* name, int32_t visibility) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return -1;
    }
    
    try {
        auto* indexMgr = reinterpret_cast<IndexManagerWrapper*>(mgr)->get();
        auto result = indexMgr->addFolder(path, name ? name : "",
                                   static_cast<Visibility>(visibility));
        setLastError(FV_OK);
        return result;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return -1;
    }
}

FVError fv_index_remove_folder(FVIndexManager mgr, int64_t folder_id) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->removeFolder(folder_id);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

FVError fv_index_optimize_database(FVIndexManager mgr) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->optimizeDatabase();
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

int fv_index_get_max_text_size_kb(FVIndexManager mgr) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return 100; // default
    }
    
    try {
        return reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->getMaxTextSizeKB();
    } catch (...) {
        return 100;
    }
}

FVError fv_index_set_max_text_size_kb(FVIndexManager mgr, int size_kb) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->setMaxTextSizeKB(size_kb);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

char* fv_index_get_folders(FVIndexManager mgr) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return nullptr;
    }
    
    try {
        auto folders = reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->getFolders();
        json arr = json::array();
        for (const auto& f : folders) {
            arr.push_back(watchedFolderToJson(f));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

FVError fv_index_scan_folder(FVIndexManager mgr, int64_t folder_id,
                              FVScanCallback cb, void* user_data) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        auto* indexMgr = reinterpret_cast<IndexManagerWrapper*>(mgr)->get();
        indexMgr->scanFolder(folder_id, [cb, user_data](const ScanProgress& p) {
            if (cb) {
                cb(p.processedFiles, p.totalFiles, p.currentFile.c_str(), user_data);
            }
        });
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_IO, e.what());
        return FV_ERROR_IO;
    }
}

FVError fv_index_scan_all(FVIndexManager mgr, FVScanCallback cb, void* user_data) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        auto* indexMgr = reinterpret_cast<IndexManagerWrapper*>(mgr)->get();
        indexMgr->scanAllFolders([cb, user_data](const ScanProgress& p) {
            if (cb) {
                cb(p.processedFiles, p.totalFiles, p.currentFile.c_str(), user_data);
            }
        });
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_IO, e.what());
        return FV_ERROR_IO;
    }
}

void fv_index_stop_scan(FVIndexManager mgr) {
    if (mgr) {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->stopScan();
    }
}

int32_t fv_index_is_scanning(FVIndexManager mgr) {
    if (!mgr) return 0;
    return reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->isScanning() ? 1 : 0;
}

char* fv_index_get_file(FVIndexManager mgr, int64_t file_id) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return nullptr;
    }
    
    try {
        auto file = reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->getFile(file_id);
        if (file) {
            setLastError(FV_OK);
            return alloc_string(fileRecordToJson(*file).dump());
        }
        setLastError(FV_ERROR_NOT_FOUND, "File not found");
        return alloc_string("");  // Empty string indicates "not found" (not an error)
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

FVError fv_index_delete_file(FVIndexManager mgr, int64_t file_id, int32_t delete_from_disk) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->deleteFile(file_id, delete_from_disk != 0);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_IO, e.what());
        return FV_ERROR_IO;
    }
}

char* fv_index_get_recent(FVIndexManager mgr, int32_t limit) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return nullptr;
    }
    
    try {
        auto files = reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->getRecentFiles(limit);
        json arr = json::array();
        for (const auto& f : files) {
            arr.push_back(fileRecordToJson(f));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_index_get_recent_compact(FVIndexManager mgr, int32_t limit) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return nullptr;
    }
    
    try {
        auto files = reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->getRecentFilesCompact(limit);
        json arr = json::array();
        for (const auto& f : files) {
            arr.push_back(fileRecordCompactToJson(f));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_index_get_by_folder_compact(FVIndexManager mgr, int64_t folder_id,
                                      int32_t limit, int32_t offset) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return nullptr;
    }
    
    try {
        auto files = reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->getFilesByFolderCompact(folder_id, limit, offset);
        json arr = json::array();
        for (const auto& f : files) {
            arr.push_back(fileRecordCompactToJson(f));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_index_get_stats(FVIndexManager mgr) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return nullptr;
    }
    
    try {
        auto stats = reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->getStats();
        json j = {
            {"totalFolders", stats.totalFolders},
            {"totalFiles", stats.totalFiles},
            {"totalSize", stats.totalSize},
            {"imageCount", stats.imageCount},
            {"videoCount", stats.videoCount},
            {"audioCount", stats.audioCount},
            {"documentCount", stats.documentCount},
            {"archiveCount", stats.archiveCount},
            {"otherCount", stats.otherCount}
        };
        setLastError(FV_OK);
        return alloc_string(j.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

FVError fv_index_set_folder_visibility(FVIndexManager mgr, int64_t folder_id, int32_t visibility) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->setFolderVisibility(folder_id,
            static_cast<Visibility>(visibility));
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

FVError fv_index_set_folder_enabled(FVIndexManager mgr, int64_t folder_id, int32_t enabled) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->setFolderEnabled(folder_id, enabled != 0);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

FVError fv_index_set_file_visibility(FVIndexManager mgr, int64_t file_id, int32_t visibility) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null index manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<IndexManagerWrapper*>(mgr)->get()->setFileVisibility(file_id,
            visibility >= 0 ? std::optional(static_cast<Visibility>(visibility)) : std::nullopt);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

// ═══════════════════════════════════════════════════════════
// Search Engine
// ═══════════════════════════════════════════════════════════

FVSearchEngine fv_search_create(FVDatabase db) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return nullptr;
    }
    
    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        auto* engine = new SearchEngine(holder->getDatabase());
        auto* wrapper = new SearchEngineWrapper(engine, holder);
        setLastError(FV_OK);
        return reinterpret_cast<FVSearchEngine>(wrapper);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_search_destroy(FVSearchEngine engine) {
    if (engine) {
        delete reinterpret_cast<SearchEngineWrapper*>(engine);
    }
}

char* fv_search_query(FVSearchEngine engine, const char* query_json) {
    if (!engine) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null search engine");
        return nullptr;
    }
    
    try {
        auto query = parseSearchQuery(query_json);
        auto results = reinterpret_cast<SearchEngineWrapper*>(engine)->get()->search(query);

        json arr = json::array();
        for (const auto& r : results) {
            json j = fileRecordToJson(r.file);
            j["score"] = r.score;
            j["snippet"] = r.snippet;
            arr.push_back(j);
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_search_query_compact(FVSearchEngine engine, const char* query_json) {
    if (!engine) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null search engine");
        return nullptr;
    }
    
    try {
        auto query = parseSearchQuery(query_json);
        auto results = reinterpret_cast<SearchEngineWrapper*>(engine)->get()->searchCompact(query);

        json arr = json::array();
        for (const auto& r : results) {
            arr.push_back(searchResultCompactToJson(r));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

int64_t fv_search_count(FVSearchEngine engine, const char* query_json) {
    if (!engine) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null search engine");
        return -1;
    }
    
    try {
        auto query = parseSearchQuery(query_json);
        auto result = reinterpret_cast<SearchEngineWrapper*>(engine)->get()->countResults(query);
        setLastError(FV_OK);
        return result;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return -1;
    }
}

char* fv_search_suggest(FVSearchEngine engine, const char* prefix, int32_t limit) {
    if (!engine) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null search engine");
        return nullptr;
    }
    
    try {
        auto suggestions = reinterpret_cast<SearchEngineWrapper*>(engine)->get()->suggest(prefix, limit);
        json arr = json::array();
        for (const auto& s : suggestions) {
            arr.push_back(s);
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

// ═══════════════════════════════════════════════════════════
// Tag Manager
// ═══════════════════════════════════════════════════════════

FVTagManager fv_tags_create(FVDatabase db) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return nullptr;
    }
    
    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        auto* mgr = new TagManager(holder->getDatabase());
        auto* wrapper = new TagManagerWrapper(mgr, holder);
        setLastError(FV_OK);
        return reinterpret_cast<FVTagManager>(wrapper);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_tags_destroy(FVTagManager mgr) {
    if (mgr) {
        delete reinterpret_cast<TagManagerWrapper*>(mgr);
    }
}

FVError fv_tags_add(FVTagManager mgr, int64_t file_id, const char* tag) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null tag manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<TagManagerWrapper*>(mgr)->get()->addTag(file_id, tag);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

FVError fv_tags_remove(FVTagManager mgr, int64_t file_id, const char* tag) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null tag manager");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<TagManagerWrapper*>(mgr)->get()->removeTag(file_id, tag);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return FV_ERROR_DATABASE;
    }
}

char* fv_tags_get_for_file(FVTagManager mgr, int64_t file_id) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null tag manager");
        return nullptr;
    }
    
    try {
        auto tags = reinterpret_cast<TagManagerWrapper*>(mgr)->get()->getFileTags(file_id);
        json arr = json::array();
        for (const auto& t : tags) {
            arr.push_back(t);
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_tags_get_all(FVTagManager mgr) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null tag manager");
        return nullptr;
    }
    
    try {
        auto tags = reinterpret_cast<TagManagerWrapper*>(mgr)->get()->getAllTags();
        json arr = json::array();
        for (const auto& t : tags) {
            arr.push_back(tagToJson(t));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_tags_get_popular(FVTagManager mgr, int32_t limit) {
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null tag manager");
        return nullptr;
    }
    
    try {
        auto tags = reinterpret_cast<TagManagerWrapper*>(mgr)->get()->getPopularTags(limit);
        json arr = json::array();
        for (const auto& t : tags) {
            arr.push_back(tagToJson(t));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

// ═══════════════════════════════════════════════════════════
// Duplicate Finder
// ═══════════════════════════════════════════════════════════

FVDuplicateFinder fv_duplicates_create(FVDatabase db, FVIndexManager indexMgr) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return nullptr;
    }
    
    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        IndexManager* idx = indexMgr ? reinterpret_cast<IndexManagerWrapper*>(indexMgr)->get() : nullptr;
        auto* finder = new DuplicateFinder(holder->getDatabase(), idx);
        auto* wrapper = new DuplicateFinderWrapper(finder, holder, idx);
        setLastError(FV_OK);
        return reinterpret_cast<FVDuplicateFinder>(wrapper);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_duplicates_destroy(FVDuplicateFinder finder) {
    if (finder) {
        delete reinterpret_cast<DuplicateFinderWrapper*>(finder);
    }
}

char* fv_duplicates_find(FVDuplicateFinder finder) {
    if (!finder) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null duplicate finder");
        return nullptr;
    }
    
    try {
        auto groups = reinterpret_cast<DuplicateFinderWrapper*>(finder)->get()->findLocalDuplicates();
        json arr = json::array();
        for (const auto& g : groups) {
            json groupJson = {
                {"checksum", g.checksum},
                {"fileSize", g.fileSize},
                {"potentialSavings", g.potentialSavings()},
                {"hasRemoteBackup", g.hasRemoteBackup()}
            };

            json localCopies = json::array();
            for (const auto& f : g.localCopies) {
                localCopies.push_back(fileRecordToJson(f));
            }
            groupJson["localCopies"] = localCopies;

            json remoteCopies = json::array();
            for (const auto& f : g.remoteCopies) {
                remoteCopies.push_back(fileRecordToJson(f));
            }
            groupJson["remoteCopies"] = remoteCopies;

            arr.push_back(groupJson);
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_duplicates_stats(FVDuplicateFinder finder) {
    if (!finder) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null duplicate finder");
        return nullptr;
    }
    
    try {
        auto stats = reinterpret_cast<DuplicateFinderWrapper*>(finder)->get()->getDuplicateStats();
        json j = {
            {"totalGroups", stats.totalGroups},
            {"totalDuplicates", stats.totalDuplicates},
            {"potentialSavings", stats.potentialSavings}
        };
        setLastError(FV_OK);
        return alloc_string(j.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

char* fv_duplicates_without_backup(FVDuplicateFinder finder) {
    if (!finder) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null duplicate finder");
        return nullptr;
    }
    
    try {
        auto files = reinterpret_cast<DuplicateFinderWrapper*>(finder)->get()->findFilesWithoutBackup();
        json arr = json::array();
        for (const auto& f : files) {
            arr.push_back(fileRecordToJson(f));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return nullptr;
    }
}

FVError fv_duplicates_delete_file(FVDuplicateFinder finder, int64_t file_id) {
    if (!finder) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null duplicate finder");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<DuplicateFinderWrapper*>(finder)->get()->deleteFile(file_id);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_IO, e.what());
        return FV_ERROR_IO;
    }
}

FVError fv_duplicates_compute_checksums(FVDuplicateFinder finder,
                                         FVChecksumProgressCallback cb, void* user_data) {
    if (!finder) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null duplicate finder");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<DuplicateFinderWrapper*>(finder)->get()->computeMissingChecksums(
            [cb, user_data](int processed, int total) {
                if (cb) {
                    cb(processed, total, user_data);
                }
            }
        );
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_IO, e.what());
        return FV_ERROR_IO;
    }
}

// ═══════════════════════════════════════════════════════════
// Content Indexer
// ═══════════════════════════════════════════════════════════

FVContentIndexer fv_content_indexer_create(FVDatabase db) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return nullptr;
    }
    
    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        auto* indexer = new ContentIndexer(holder->getDatabase());
        auto* wrapper = new ContentIndexerWrapper(indexer, holder);
        setLastError(FV_OK);
        return reinterpret_cast<FVContentIndexer>(wrapper);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_content_indexer_destroy(FVContentIndexer indexer) {
    if (indexer) {
        delete reinterpret_cast<ContentIndexerWrapper*>(indexer);
    }
}

FVError fv_content_indexer_start(FVContentIndexer indexer) {
    if (!indexer) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null content indexer");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->start();
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

FVError fv_content_indexer_stop(FVContentIndexer indexer, int32_t wait) {
    if (!indexer) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null content indexer");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->stop(wait != 0);
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

int32_t fv_content_indexer_is_running(FVContentIndexer indexer) {
    if (!indexer) return 0;
    return reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->isRunning() ? 1 : 0;
}

FVError fv_content_indexer_process_file(FVContentIndexer indexer, int64_t file_id) {
    if (!indexer) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null content indexer");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        bool success = reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->processFile(file_id);
        setLastError(FV_OK);
        return success ? FV_OK : FV_ERROR_NOT_FOUND;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_IO, e.what());
        return FV_ERROR_IO;
    }
}

int32_t fv_content_indexer_enqueue_unprocessed(FVContentIndexer indexer) {
    if (!indexer) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null content indexer");
        return -1;
    }
    
    try {
        int count = reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->enqueueUnprocessed();
        setLastError(FV_OK);
        return count;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return -1;
    }
}

char* fv_content_indexer_get_status(FVContentIndexer indexer) {
    if (!indexer) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null content indexer");
        return nullptr;
    }
    
    try {
        auto status = reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->getStatus();
        json j = {
            {"pending", status.pending},
            {"processed", status.processed},
            {"failed", status.failed},
            {"isRunning", status.isRunning},
            {"currentFile", status.currentFile}
        };
        setLastError(FV_OK);
        return alloc_string(j.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_content_indexer_set_max_text_size_kb(FVContentIndexer indexer, int32_t size_kb) {
    if (!indexer) return;
    reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->setMaxTextSizeKB(size_kb);
}

int32_t fv_content_indexer_get_max_text_size_kb(FVContentIndexer indexer) {
    if (!indexer) return 100;
    return reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->getMaxTextSizeKB();
}

int32_t fv_content_indexer_get_pending_count(FVContentIndexer indexer) {
    if (!indexer) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null content indexer");
        return -1;
    }
    
    try {
        int count = reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->getPendingCount();
        setLastError(FV_OK);
        return count;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_DATABASE, e.what());
        return -1;
    }
}

FVError fv_content_indexer_reindex_all(FVContentIndexer indexer,
                                        FVContentProgressCallback cb, void* user_data) {
    if (!indexer) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null content indexer");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    
    try {
        reinterpret_cast<ContentIndexerWrapper*>(indexer)->get()->reindexAll(
            [cb, user_data](int processed, int total) {
                if (cb) {
                    cb(processed, total, user_data);
                }
            }
        );
        setLastError(FV_OK);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_IO, e.what());
        return FV_ERROR_IO;
    }
}

int32_t fv_content_indexer_can_extract(FVContentIndexer indexer, const char* mime_type) {
    if (!indexer || !mime_type) {
        return 0;
    }
    
    try {
        // ContentIndexer использует внутренний TextExtractorRegistry
        // Для этой проверки нужен доступ к registry
        // Пока просто проверяем известные типы
        std::string mt(mime_type);
        if (mt.starts_with("text/") ||
            mt == "application/pdf" ||
            mt == "application/json" ||
            mt.find("openxmlformats") != std::string::npos ||
            mt.find("opendocument") != std::string::npos) {
            return 1;
        }
        return 0;
    } catch (...) {
        return 0;
    }
}

} // extern "C"
