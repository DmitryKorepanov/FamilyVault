// IndexSyncManager.cpp — Index synchronization implementation

#include "familyvault/Network/IndexSyncManager.h"
#include "familyvault/IndexManager.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>

namespace FamilyVault {

using json = nlohmann::json;
using Clock = std::chrono::system_clock;

// ═══════════════════════════════════════════════════════════
// Helper: map FileRecord to JSON for sync
// ═══════════════════════════════════════════════════════════

namespace {

std::string fileRecordToSyncJson(const FileRecord& file, const std::string& deviceId) {
    // Send relative path only - absolute paths leak host filesystem structure
    // and are meaningless on other devices. Recipient uses deviceId + relativePath
    // as unique identifier for the remote file.
    json j = {
        {"id", file.id},
        {"path", file.relativePath},  // Relative path within family library, not absolute!
        {"folderId", file.folderId},  // Logical folder ID for grouping
        {"name", file.name},
        {"mimeType", file.mimeType},
        {"size", file.size},
        {"modifiedAt", file.modifiedAt},
        {"checksum", file.checksum.value_or("")},
        {"visibility", static_cast<int>(file.visibility)},
        {"deviceId", deviceId},
        {"syncVersion", file.syncVersion},
        {"isDeleted", false}
    };
    return j.dump();
}

std::optional<RemoteFileRecord> parseRemoteFile(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        RemoteFileRecord r;
        r.remoteId = j.value("id", 0);
        r.path = j.value("path", "");
        r.name = j.value("name", "");
        r.mimeType = j.value("mimeType", "");
        r.size = j.value("size", 0);
        r.modifiedAt = j.value("modifiedAt", 0);
        r.checksum = j.value("checksum", "");
        r.sourceDeviceId = j.value("deviceId", "");
        r.extractedText = j.value("extractedText", "");
        r.isDeleted = j.value("isDeleted", false);
        r.syncedAt = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now().time_since_epoch()).count();
        return r;
    } catch (...) {
        return std::nullopt;
    }
}

RemoteFileRecord mapRemoteFileRecord(sqlite3_stmt* stmt) {
    RemoteFileRecord r;
    r.localId = Database::getInt64(stmt, 0);
    r.remoteId = Database::getInt64(stmt, 1);
    r.sourceDeviceId = Database::getString(stmt, 2);
    r.path = Database::getString(stmt, 3);
    r.name = Database::getString(stmt, 4);
    r.mimeType = Database::getString(stmt, 5);
    r.size = Database::getInt64(stmt, 6);
    r.modifiedAt = Database::getInt64(stmt, 7);
    r.checksum = Database::getString(stmt, 8);
    r.extractedText = Database::getString(stmt, 9);
    r.syncedAt = Database::getInt64(stmt, 10);
    r.isDeleted = Database::getInt(stmt, 11) != 0;
    return r;
}

FileRecord mapFileRecord(sqlite3_stmt* stmt) {
    FileRecord f;
    f.id = Database::getInt64(stmt, 0);
    f.folderId = Database::getInt64(stmt, 1);
    f.relativePath = Database::getString(stmt, 2);
    f.name = Database::getString(stmt, 3);
    f.extension = Database::getString(stmt, 4);
    f.size = Database::getInt64(stmt, 5);
    f.mimeType = Database::getString(stmt, 6);
    f.contentType = static_cast<ContentType>(Database::getInt(stmt, 7));
    f.checksum = Database::getStringOpt(stmt, 8);
    f.createdAt = Database::getInt64(stmt, 9);
    f.modifiedAt = Database::getInt64(stmt, 10);
    f.indexedAt = Database::getInt64(stmt, 11);
    f.visibility = static_cast<Visibility>(Database::getInt(stmt, 12));
    f.sourceDeviceId = Database::getStringOpt(stmt, 13);
    f.isRemote = Database::getInt(stmt, 14) != 0;
    f.syncVersion = Database::getInt64(stmt, 15);
    return f;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// IndexSyncManager::Impl
// ═══════════════════════════════════════════════════════════

class IndexSyncManager::Impl {
public:
    Impl(std::shared_ptr<Database> db, const std::string& deviceId)
        : m_db(std::move(db)), m_deviceId(deviceId) {
        ensureRemoteFilesTable();
    }

    void requestSync(std::shared_ptr<PeerConnection> peer, bool fullSync) {
        if (!peer || !peer->isConnected()) return;

        std::string peerId = peer->getPeerId();
        int64_t sinceTimestamp = fullSync ? FULL_SYNC_TIMESTAMP : getLastSyncTimestamp(peerId);

        // Create sync request message
        Message msg(MessageType::IndexSyncRequest, generateRequestId());
        IndexSyncRequestPayload payload;
        payload.sinceTimestamp = sinceTimestamp;
        msg.setJsonPayload(payload.toJson());

        // Track progress
        {
            std::lock_guard<std::mutex> lock(m_progressMutex);
            m_syncProgress[peerId] = SyncProgress{peerId, 0, 0, 0, false, ""};
        }

        peer->sendMessage(msg);
        spdlog::info("IndexSync: Requested sync from {} (since={})", peerId, sinceTimestamp);
    }

    void handleSyncRequest(std::shared_ptr<PeerConnection> peer, const Message& request) {
        if (!peer || !peer->isConnected()) return;

        auto payload = IndexSyncRequestPayload::fromJson(request.getJsonPayload());
        if (!payload) {
            spdlog::warn("IndexSync: Invalid sync request from {}", peer->getPeerId());
            return;
        }

        std::string peerId = peer->getPeerId();
        spdlog::info("IndexSync: Received sync request from {} (since={})", 
                     peerId, payload->sinceTimestamp);

        // Get total count first
        int64_t totalFiles = countLocalChangesSince(payload->sinceTimestamp);
        
        // Send response with count
        Message response(MessageType::IndexSyncResponse, request.requestId);
        json responseJson = {{"totalFiles", totalFiles}};
        response.setJsonPayload(responseJson.dump());
        peer->sendMessage(response);

        // Stream all files in batches (no artificial limit)
        int64_t sentCount = 0;
        int offset = 0;
        const int batchSize = SYNC_BATCH_SIZE;
        
        while (sentCount < totalFiles && peer->isConnected()) {
            auto batch = getLocalChangesSince(payload->sinceTimestamp, batchSize, offset);
            if (batch.empty()) break;
            
            for (const auto& file : batch) {
                Message delta(MessageType::IndexDelta, request.requestId);
                delta.setJsonPayload(fileRecordToSyncJson(file, m_deviceId));
                peer->sendMessage(delta);
                sentCount++;
            }
            
            offset += batch.size();
            
            // Small delay to avoid overwhelming the receiver
            if (sentCount < totalFiles) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        spdlog::info("IndexSync: Sent {} files to {}", sentCount, peerId);
    }

    void handleSyncResponse(std::shared_ptr<PeerConnection> peer, const Message& response) {
        if (!peer) return;

        std::string peerId = peer->getPeerId();
        
        try {
            auto j = json::parse(response.getJsonPayload());
            int64_t totalFiles = j.value("totalFiles", 0);

            std::lock_guard<std::mutex> lock(m_progressMutex);
            auto& progress = m_syncProgress[peerId];
            progress.totalFiles = totalFiles;
            
            if (totalFiles == 0) {
                progress.isComplete = true;
                notifyComplete(peerId, 0);
            }

            spdlog::info("IndexSync: Expecting {} files from {}", totalFiles, peerId);
        } catch (...) {
            spdlog::warn("IndexSync: Invalid sync response from {}", peerId);
        }
    }

    void handleIndexDelta(std::shared_ptr<PeerConnection> peer, const Message& delta) {
        if (!peer) return;

        std::string peerId = peer->getPeerId();
        auto remoteFile = parseRemoteFile(delta.getJsonPayload());
        
        if (!remoteFile) {
            spdlog::warn("IndexSync: Invalid delta from {}", peerId);
            return;
        }

        // Store remote file
        storeRemoteFile(*remoteFile);

        // Update progress
        {
            std::lock_guard<std::mutex> lock(m_progressMutex);
            auto& progress = m_syncProgress[peerId];
            progress.receivedFiles++;
            notifyProgress(progress);

            if (progress.receivedFiles >= progress.totalFiles) {
                progress.isComplete = true;
                setLastSyncTimestamp(peerId, std::chrono::duration_cast<std::chrono::seconds>(
                    Clock::now().time_since_epoch()).count());
                notifyComplete(peerId, progress.receivedFiles);
            }
        }
    }

    std::vector<FileRecord> getLocalChangesSince(int64_t sinceTimestamp, int limit = 0, int offset = 0) const {
        // Only return Family visibility files (Private files never leave device!)
        // Use COALESCE to inherit visibility from folder when file.visibility is NULL
        std::string sql;
        if (limit > 0) {
            sql = R"(
                SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size, f.mime_type,
                       f.content_type, f.checksum, f.created_at, f.modified_at, f.indexed_at,
                       COALESCE(f.visibility, wf.visibility) as visibility,
                       f.source_device_id, f.is_remote, f.sync_version
                FROM files f
                JOIN watched_folders wf ON f.folder_id = wf.id
                WHERE COALESCE(f.visibility, wf.visibility) = 1
                  AND f.is_remote = 0
                  AND f.indexed_at > ?
                ORDER BY f.indexed_at ASC
                LIMIT ? OFFSET ?
            )";
            return m_db->query<FileRecord>(sql, mapFileRecord, sinceTimestamp, limit, offset);
        } else {
            // No limit - return all (for internal use)
            sql = R"(
                SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size, f.mime_type,
                       f.content_type, f.checksum, f.created_at, f.modified_at, f.indexed_at,
                       COALESCE(f.visibility, wf.visibility) as visibility,
                       f.source_device_id, f.is_remote, f.sync_version
                FROM files f
                JOIN watched_folders wf ON f.folder_id = wf.id
                WHERE COALESCE(f.visibility, wf.visibility) = 1
                  AND f.is_remote = 0
                  AND f.indexed_at > ?
                ORDER BY f.indexed_at ASC
            )";
            return m_db->query<FileRecord>(sql, mapFileRecord, sinceTimestamp);
        }
    }
    
    int64_t countLocalChangesSince(int64_t sinceTimestamp) const {
        return m_db->queryScalar(R"(
            SELECT COUNT(*)
            FROM files f
            JOIN watched_folders wf ON f.folder_id = wf.id
            WHERE COALESCE(f.visibility, wf.visibility) = 1
              AND f.is_remote = 0
              AND f.indexed_at > ?
        )", sinceTimestamp);
    }

    int64_t getLastSyncTimestamp(const std::string& deviceId) const {
        return m_db->queryScalar(
            "SELECT COALESCE(MAX(last_sync_at), 0) FROM sync_state WHERE device_id = ?",
            deviceId);
    }

    void setLastSyncTimestamp(const std::string& deviceId, int64_t timestamp) {
        m_db->execute(R"(
            INSERT OR REPLACE INTO sync_state (device_id, last_sync_at)
            VALUES (?, ?)
        )", deviceId, timestamp);
    }

    std::vector<RemoteFileRecord> getRemoteFiles(const std::string& deviceId) const {
        return m_db->query<RemoteFileRecord>(R"(
            SELECT id, remote_id, source_device_id, path, name, mime_type, size,
                   modified_at, checksum, extracted_text, synced_at, is_deleted
            FROM remote_files
            WHERE source_device_id = ? AND is_deleted = 0
            ORDER BY name ASC
        )", mapRemoteFileRecord, deviceId);
    }

    std::vector<RemoteFileRecord> getAllRemoteFiles() const {
        return m_db->query<RemoteFileRecord>(R"(
            SELECT id, remote_id, source_device_id, path, name, mime_type, size,
                   modified_at, checksum, extracted_text, synced_at, is_deleted
            FROM remote_files
            WHERE is_deleted = 0
            ORDER BY source_device_id, name ASC
        )", mapRemoteFileRecord);
    }

    std::optional<RemoteFileRecord> getRemoteFile(int64_t localId) const {
        return m_db->queryOne<RemoteFileRecord>(R"(
            SELECT id, remote_id, source_device_id, path, name, mime_type, size,
                   modified_at, checksum, extracted_text, synced_at, is_deleted
            FROM remote_files
            WHERE id = ?
        )", mapRemoteFileRecord, localId);
    }

    std::vector<RemoteFileRecord> searchRemoteFiles(const std::string& query, int limit) const {
        std::string searchPattern = "%" + query + "%";
        return m_db->query<RemoteFileRecord>(R"(
            SELECT id, remote_id, source_device_id, path, name, mime_type, size,
                   modified_at, checksum, extracted_text, synced_at, is_deleted
            FROM remote_files
            WHERE is_deleted = 0 AND (name LIKE ? OR extracted_text LIKE ?)
            ORDER BY name ASC
            LIMIT ?
        )", mapRemoteFileRecord, searchPattern, searchPattern, limit);
    }

    int64_t getRemoteFileCount() const {
        return m_db->queryScalar("SELECT COUNT(*) FROM remote_files WHERE is_deleted = 0");
    }

    int64_t getRemoteFileCount(const std::string& deviceId) const {
        return m_db->queryScalar(
            "SELECT COUNT(*) FROM remote_files WHERE source_device_id = ? AND is_deleted = 0",
            deviceId);
    }

    SyncProgress getSyncProgress(const std::string& deviceId) const {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        auto it = m_syncProgress.find(deviceId);
        if (it != m_syncProgress.end()) {
            return it->second;
        }
        return SyncProgress{deviceId, 0, 0, 0, true, ""};
    }

    bool isSyncing() const {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        for (const auto& [id, progress] : m_syncProgress) {
            if (!progress.isComplete) return true;
        }
        return false;
    }

    bool isSyncingWith(const std::string& deviceId) const {
        std::lock_guard<std::mutex> lock(m_progressMutex);
        auto it = m_syncProgress.find(deviceId);
        return it != m_syncProgress.end() && !it->second.isComplete;
    }

    void onProgress(ProgressCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onProgress = std::move(callback);
    }

    void onComplete(CompleteCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onComplete = std::move(callback);
    }

    void onError(ErrorCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onError = std::move(callback);
    }

private:
    std::shared_ptr<Database> m_db;
    std::string m_deviceId;

    mutable std::mutex m_progressMutex;
    std::map<std::string, SyncProgress> m_syncProgress;

    std::mutex m_callbackMutex;
    ProgressCallback m_onProgress;
    CompleteCallback m_onComplete;
    ErrorCallback m_onError;

    void ensureRemoteFilesTable() {
        // Create remote_files table if not exists
        m_db->execute(R"(
            CREATE TABLE IF NOT EXISTS remote_files (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                remote_id INTEGER NOT NULL,
                source_device_id TEXT NOT NULL,
                path TEXT NOT NULL,
                name TEXT NOT NULL,
                mime_type TEXT,
                size INTEGER DEFAULT 0,
                modified_at INTEGER DEFAULT 0,
                checksum TEXT,
                extracted_text TEXT,
                synced_at INTEGER DEFAULT 0,
                is_deleted INTEGER DEFAULT 0,
                UNIQUE(source_device_id, remote_id)
            )
        )");

        m_db->execute(R"(
            CREATE INDEX IF NOT EXISTS idx_remote_files_device 
            ON remote_files(source_device_id)
        )");

        m_db->execute(R"(
            CREATE INDEX IF NOT EXISTS idx_remote_files_name 
            ON remote_files(name)
        )");

        // Create sync_state table
        m_db->execute(R"(
            CREATE TABLE IF NOT EXISTS sync_state (
                device_id TEXT PRIMARY KEY,
                last_sync_at INTEGER DEFAULT 0
            )
        )");
    }

    void storeRemoteFile(const RemoteFileRecord& file) {
        if (file.isDeleted) {
            // Mark as deleted
            m_db->execute(R"(
                UPDATE remote_files SET is_deleted = 1, synced_at = ?
                WHERE source_device_id = ? AND remote_id = ?
            )", file.syncedAt, file.sourceDeviceId, file.remoteId);
        } else {
            // Insert or update
            m_db->execute(R"(
                INSERT INTO remote_files 
                    (remote_id, source_device_id, path, name, mime_type, size, 
                     modified_at, checksum, extracted_text, synced_at, is_deleted)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)
                ON CONFLICT(source_device_id, remote_id) DO UPDATE SET
                    path = excluded.path,
                    name = excluded.name,
                    mime_type = excluded.mime_type,
                    size = excluded.size,
                    modified_at = excluded.modified_at,
                    checksum = excluded.checksum,
                    extracted_text = excluded.extracted_text,
                    synced_at = excluded.synced_at,
                    is_deleted = 0
            )", file.remoteId, file.sourceDeviceId, file.path, file.name,
                file.mimeType, file.size, file.modifiedAt, file.checksum,
                file.extractedText, file.syncedAt);
        }
    }

    void notifyProgress(const SyncProgress& progress) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onProgress) {
            m_onProgress(progress);
        }
    }

    void notifyComplete(const std::string& deviceId, int64_t filesReceived) {
        spdlog::info("IndexSync: Completed sync with {}, received {} files", deviceId, filesReceived);
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onComplete) {
            m_onComplete(deviceId, filesReceived);
        }
    }

    void notifyError(const std::string& deviceId, const std::string& error) {
        spdlog::error("IndexSync: Error syncing with {}: {}", deviceId, error);
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onError) {
            m_onError(deviceId, error);
        }
    }
};

// ═══════════════════════════════════════════════════════════
// IndexSyncManager Public Interface
// ═══════════════════════════════════════════════════════════

IndexSyncManager::IndexSyncManager(std::shared_ptr<Database> db, const std::string& deviceId)
    : m_impl(std::make_unique<Impl>(std::move(db), deviceId)) {}

IndexSyncManager::~IndexSyncManager() = default;

void IndexSyncManager::requestSync(std::shared_ptr<PeerConnection> peer, bool fullSync) {
    m_impl->requestSync(std::move(peer), fullSync);
}

void IndexSyncManager::handleSyncRequest(std::shared_ptr<PeerConnection> peer, const Message& request) {
    m_impl->handleSyncRequest(std::move(peer), request);
}

void IndexSyncManager::handleSyncResponse(std::shared_ptr<PeerConnection> peer, const Message& response) {
    m_impl->handleSyncResponse(std::move(peer), response);
}

void IndexSyncManager::handleIndexDelta(std::shared_ptr<PeerConnection> peer, const Message& delta) {
    m_impl->handleIndexDelta(std::move(peer), delta);
}

std::vector<FileRecord> IndexSyncManager::getLocalChangesSince(int64_t sinceTimestamp) const {
    return m_impl->getLocalChangesSince(sinceTimestamp);
}

int64_t IndexSyncManager::getLastSyncTimestamp(const std::string& deviceId) const {
    return m_impl->getLastSyncTimestamp(deviceId);
}

void IndexSyncManager::setLastSyncTimestamp(const std::string& deviceId, int64_t timestamp) {
    m_impl->setLastSyncTimestamp(deviceId, timestamp);
}

std::vector<RemoteFileRecord> IndexSyncManager::getRemoteFiles(const std::string& deviceId) const {
    return m_impl->getRemoteFiles(deviceId);
}

std::vector<RemoteFileRecord> IndexSyncManager::getAllRemoteFiles() const {
    return m_impl->getAllRemoteFiles();
}

std::optional<RemoteFileRecord> IndexSyncManager::getRemoteFile(int64_t localId) const {
    return m_impl->getRemoteFile(localId);
}

std::vector<RemoteFileRecord> IndexSyncManager::searchRemoteFiles(const std::string& query, int limit) const {
    return m_impl->searchRemoteFiles(query, limit);
}

int64_t IndexSyncManager::getRemoteFileCount() const {
    return m_impl->getRemoteFileCount();
}

int64_t IndexSyncManager::getRemoteFileCount(const std::string& deviceId) const {
    return m_impl->getRemoteFileCount(deviceId);
}

SyncProgress IndexSyncManager::getSyncProgress(const std::string& deviceId) const {
    return m_impl->getSyncProgress(deviceId);
}

bool IndexSyncManager::isSyncing() const {
    return m_impl->isSyncing();
}

bool IndexSyncManager::isSyncingWith(const std::string& deviceId) const {
    return m_impl->isSyncingWith(deviceId);
}

void IndexSyncManager::onProgress(ProgressCallback callback) {
    m_impl->onProgress(std::move(callback));
}

void IndexSyncManager::onComplete(CompleteCallback callback) {
    m_impl->onComplete(std::move(callback));
}

void IndexSyncManager::onError(ErrorCallback callback) {
    m_impl->onError(std::move(callback));
}

} // namespace FamilyVault

