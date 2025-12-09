// ffi_network_manager.cpp — C API for NetworkManager, IndexSyncManager, and RemoteFileAccess

#include "familyvault/familyvault_c.h"
#include "familyvault/Network/NetworkManager.h"
#include "familyvault/Network/IndexSyncManager.h"
#include "familyvault/Network/RemoteFileAccess.h"
#include "familyvault/FamilyPairing.h"
#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include "ffi_internal.h"

#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using namespace FamilyVault;
using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════
// NetworkManager Wrapper
// ═══════════════════════════════════════════════════════════

namespace {

enum class NetworkEvent : int32_t {
    DeviceDiscovered = 0,
    DeviceLost = 1,
    DeviceConnected = 2,
    DeviceDisconnected = 3,
    StateChanged = 4,
    Error = 5,
    SyncProgress = 6,
    SyncComplete = 7,
    FileTransferProgress = 8,
    FileTransferComplete = 9,
    FileTransferError = 10
};

struct NetworkManagerWrapper {
    std::unique_ptr<NetworkManager> manager;
    std::unique_ptr<IndexSyncManager> syncManager;
    std::unique_ptr<RemoteFileAccess> fileAccess;
    std::shared_ptr<Database> database;  // Keep database alive for syncManager
    std::shared_ptr<IndexManager> indexManager;  // For getting file paths
    std::string cacheDir;
    std::string deviceId;  // This device's ID
    FVNetworkCallback callback = nullptr;
    void* userData = nullptr;
    std::string lastError;
    bool messageHandlerInstalled = false;
    
    ~NetworkManagerWrapper() {
        // Clear message handler to avoid use-after-free
        // (callback captures 'this' which will be invalid after destruction)
        if (manager && messageHandlerInstalled) {
            manager->onMessage(nullptr);
        }
    }
    
    // Install unified message handler for sync and file transfer
    void installMessageHandler() {
        if (messageHandlerInstalled) return;
        messageHandlerInstalled = true;
        
        manager->onMessage([this](const std::string& fromDeviceId, const Message& msg) {
            handleMessage(fromDeviceId, msg);
        });
    }
    
    void handleMessage(const std::string& fromDeviceId, const Message& msg) {
        // Get peer connection for replies
        auto peer = manager->getPeerConnection(fromDeviceId);
        
        switch (msg.type) {
            // ═══════════════════════════════════════════════════════════
            // Index Sync Messages
            // ═══════════════════════════════════════════════════════════
            case MessageType::IndexSyncRequest: {
                if (!syncManager || !peer) break;
                spdlog::debug("Sync: Received IndexSyncRequest from {}", fromDeviceId);
                syncManager->handleSyncRequest(peer, msg);
                break;
            }
            case MessageType::IndexSyncResponse: {
                if (!syncManager || !peer) break;
                spdlog::debug("Sync: Received IndexSyncResponse from {}", fromDeviceId);
                syncManager->handleSyncResponse(peer, msg);
                break;
            }
            case MessageType::IndexDelta: {
                if (!syncManager || !peer) break;
                spdlog::debug("Sync: Received IndexDelta from {}", fromDeviceId);
                syncManager->handleIndexDelta(peer, msg);
                break;
            }
            case MessageType::IndexDeltaAck: {
                if (!syncManager) break;
                spdlog::debug("Sync: Received IndexDeltaAck from {}", fromDeviceId);
                // Ack received, can send next batch if needed
                break;
            }
            
            // ═══════════════════════════════════════════════════════════
            // File Transfer Messages
            // ═══════════════════════════════════════════════════════════
            case MessageType::FileRequest: {
                if (!fileAccess || !peer || !indexManager) break;
                spdlog::debug("FileTransfer: Received FileRequest from {}", fromDeviceId);
                fileAccess->handleFileRequest(peer, msg, [this, fromDeviceId](int64_t fileId) -> std::string {
                    auto fileOpt = indexManager->getFile(fileId);
                    if (!fileOpt) {
                        spdlog::warn("FileTransfer: File {} not found, rejecting request from {}", 
                                     fileId, fromDeviceId);
                        return "";
                    }
                    // Security check: only serve Family-visible files
                    if (fileOpt->visibility != Visibility::Family) {
                        spdlog::warn("FileTransfer: SECURITY - Peer {} attempted to access non-family file {} (visibility={})", 
                                     fromDeviceId, fileId, static_cast<int>(fileOpt->visibility));
                        return "";  // Refuse to serve private/hidden files
                    }
                    return fileOpt->getFullPath();
                });
                break;
            }
            case MessageType::FileResponse: {
                if (!fileAccess) break;
                spdlog::debug("FileTransfer: Received FileResponse from {}", fromDeviceId);
                fileAccess->handleFileResponse(msg);
                break;
            }
            case MessageType::FileChunk: {
                if (!fileAccess) break;
                // Don't log every chunk - too noisy
                fileAccess->handleFileChunk(msg);
                break;
            }
            case MessageType::FileNotFound: {
                if (!fileAccess) break;
                spdlog::debug("FileTransfer: Received FileNotFound from {}", fromDeviceId);
                fileAccess->handleFileNotFound(msg);
                break;
            }
            
            default:
                spdlog::debug("NetworkManager: Unhandled message type {} from {}", 
                              static_cast<int>(msg.type), fromDeviceId);
                break;
        }
    }
};

std::string deviceInfoToJson(const DeviceInfo& info) {
    json j = {
        {"deviceId", info.deviceId},
        {"deviceName", info.deviceName},
        {"deviceType", static_cast<int>(info.deviceType)},
        {"ipAddress", info.ipAddress},
        {"servicePort", info.servicePort},
        {"lastSeenAt", info.lastSeenAt},
        {"isOnline", info.isOnline},
        {"isConnected", info.isConnected},
        {"fileCount", info.fileCount},
        {"lastSyncAt", info.lastSyncAt}
    };
    return j.dump();
}

std::string devicesToJsonArray(const std::vector<DeviceInfo>& devices) {
    json arr = json::array();
    for (const auto& d : devices) {
        // Use same keys as deviceInfoToJson for consistency
        arr.push_back({
            {"deviceId", d.deviceId},
            {"deviceName", d.deviceName},
            {"deviceType", static_cast<int>(d.deviceType)},
            {"ipAddress", d.ipAddress},
            {"servicePort", d.servicePort},  // Fixed: was "port"
            {"lastSeenAt", d.lastSeenAt},
            {"isOnline", d.isOnline},
            {"isConnected", d.isConnected},
            {"fileCount", d.fileCount},
            {"lastSyncAt", d.lastSyncAt}
        });
    }
    return arr.dump();
}

// Helper to allocate heap string for async callbacks
// Caller (Dart) must call fv_free_string after reading
const char* allocEventJson(const std::string& json) {
    return strdup(json.c_str());
}

std::string remoteFilesToJson(const std::vector<RemoteFileRecord>& files) {
    json arr = json::array();
    for (const auto& f : files) {
        arr.push_back({
            {"localId", f.localId},
            {"remoteId", f.remoteId},
            {"sourceDeviceId", f.sourceDeviceId},
            {"path", f.path},
            {"name", f.name},
            {"mimeType", f.mimeType},
            {"size", f.size},
            {"modifiedAt", f.modifiedAt},
            {"checksum", f.checksum},
            {"syncedAt", f.syncedAt},
            {"isDeleted", f.isDeleted}
        });
    }
    return arr.dump();
}

std::string fileTransferStatusToString(FileTransferStatus status) {
    switch (status) {
        case FileTransferStatus::Pending: return "pending";
        case FileTransferStatus::InProgress: return "inProgress";
        case FileTransferStatus::Completed: return "completed";
        case FileTransferStatus::Failed: return "failed";
        case FileTransferStatus::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

std::string transferProgressToJson(const FileTransferProgress& progress) {
    json j = {
        {"requestId", progress.requestId},
        {"deviceId", progress.deviceId},
        {"fileId", progress.fileId},
        {"fileName", progress.fileName},
        {"totalSize", progress.totalSize},
        {"transferredSize", progress.transferredSize},
        {"status", fileTransferStatusToString(progress.status)},
        {"error", progress.error},
        {"localPath", progress.localPath},
        {"progress", progress.progress()}
    };
    return j.dump();
}

std::string transfersToJsonArray(const std::vector<FileTransferProgress>& transfers) {
    json arr = json::array();
    for (const auto& t : transfers) {
        arr.push_back({
            {"requestId", t.requestId},
            {"deviceId", t.deviceId},
            {"fileId", t.fileId},
            {"fileName", t.fileName},
            {"totalSize", t.totalSize},
            {"transferredSize", t.transferredSize},
            {"status", fileTransferStatusToString(t.status)},
            {"error", t.error},
            {"localPath", t.localPath},
            {"progress", t.progress()}
        });
    }
    return arr.dump();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// NetworkManager C API
// ═══════════════════════════════════════════════════════════

FV_API FVNetworkManager fv_network_create(FVFamilyPairing pairing) {
    clearLastError();
    
    if (!pairing) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "FamilyPairing is null");
        return nullptr;
    }

    try {
        auto* pairingPtr = reinterpret_cast<FamilyPairing*>(pairing);
        auto wrapper = new NetworkManagerWrapper();
        
        // Create shared_ptr with no-op deleter (pairing lifetime managed externally)
        auto pairingShared = std::shared_ptr<FamilyPairing>(pairingPtr, [](FamilyPairing*){});
        
        wrapper->manager = std::make_unique<NetworkManager>(pairingShared);
        
        spdlog::debug("NetworkManager created");
        return reinterpret_cast<FVNetworkManager>(wrapper);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

FV_API void fv_network_destroy(FVNetworkManager mgr) {
    if (!mgr) return;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (wrapper->manager) {
        wrapper->manager->stop();
    }
    delete wrapper;
    spdlog::debug("NetworkManager destroyed");
}

FV_API FVError fv_network_start(FVNetworkManager mgr, uint16_t port,
                                 FVNetworkCallback callback, void* user_data) {
    clearLastError();
    
    if (!mgr) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "NetworkManager is null");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        wrapper->callback = callback;
        wrapper->userData = user_data;

        // Set up callbacks
        // NOTE: All callbacks use allocEventJson() to heap-allocate JSON strings
        // because NativeCallable.listener is async and local strings would be freed
        // before Dart reads them. Dart MUST call fv_free_string() after reading.
        
        wrapper->manager->onDeviceDiscovered([wrapper](const DeviceInfo& info) {
            if (wrapper->callback) {
                const char* json = allocEventJson(deviceInfoToJson(info));
                wrapper->callback(static_cast<int32_t>(NetworkEvent::DeviceDiscovered),
                                json, wrapper->userData);
            }
        });

        wrapper->manager->onDeviceLost([wrapper](const DeviceInfo& info) {
            if (wrapper->callback) {
                const char* json = allocEventJson(deviceInfoToJson(info));
                wrapper->callback(static_cast<int32_t>(NetworkEvent::DeviceLost),
                                json, wrapper->userData);
            }
        });

        wrapper->manager->onDeviceConnected([wrapper](const DeviceInfo& info) {
            if (wrapper->callback) {
                const char* json = allocEventJson(deviceInfoToJson(info));
                wrapper->callback(static_cast<int32_t>(NetworkEvent::DeviceConnected),
                                json, wrapper->userData);
            }
        });

        wrapper->manager->onDeviceDisconnected([wrapper](const DeviceInfo& info) {
            // Cancel any active file transfers for this device
            // Each cancelled transfer will emit its own FileTransferError via onError callback
            if (wrapper->fileAccess) {
                wrapper->fileAccess->cancelAllRequests(info.deviceId);
                spdlog::debug("Cancelled file transfers for disconnected device: {}", info.deviceId);
            }
            
            if (wrapper->callback) {
                const char* json = allocEventJson(deviceInfoToJson(info));
                wrapper->callback(static_cast<int32_t>(NetworkEvent::DeviceDisconnected),
                                json, wrapper->userData);
            }
        });

        wrapper->manager->onStateChanged([wrapper](NetworkManager::NetworkState state) {
            if (wrapper->callback) {
                json j = {{"state", static_cast<int>(state)}};
                const char* jsonStr = allocEventJson(j.dump());
                wrapper->callback(static_cast<int32_t>(NetworkEvent::StateChanged),
                                jsonStr, wrapper->userData);
            }
        });

        wrapper->manager->onError([wrapper](const std::string& error) {
            wrapper->lastError = error;
            if (wrapper->callback) {
                json j = {{"error", error}};
                const char* jsonStr = allocEventJson(j.dump());
                wrapper->callback(static_cast<int32_t>(NetworkEvent::Error),
                                jsonStr, wrapper->userData);
            }
        });

        bool started = wrapper->manager->start(port);
        if (!started) {
            std::string error = wrapper->manager->getLastError();
            if (error.empty()) error = "Failed to start NetworkManager";
            setLastError(FV_ERROR_NETWORK, error);
            spdlog::error("NetworkManager failed to start: {}", error);
            return FV_ERROR_NETWORK;
        }
        
        spdlog::info("NetworkManager started on port {}", wrapper->manager->getServerPort());
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_NETWORK, e.what());
        return FV_ERROR_NETWORK;
    }
}

FV_API void fv_network_stop(FVNetworkManager mgr) {
    if (!mgr) return;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (wrapper->manager) {
        wrapper->manager->stop();
        spdlog::info("NetworkManager stopped");
    }
}

FV_API int32_t fv_network_get_state(FVNetworkManager mgr) {
    if (!mgr) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    return static_cast<int32_t>(wrapper->manager->getState());
}

FV_API int32_t fv_network_is_running(FVNetworkManager mgr) {
    if (!mgr) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    return wrapper->manager->isRunning() ? 1 : 0;
}

FV_API uint16_t fv_network_get_port(FVNetworkManager mgr) {
    if (!mgr) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    return wrapper->manager->getServerPort();
}

FV_API char* fv_network_get_discovered_devices(FVNetworkManager mgr) {
    if (!mgr) return nullptr;
    
    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        auto devices = wrapper->manager->getDiscoveredDevices();
        return strdup(devicesToJsonArray(devices).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API char* fv_network_get_connected_devices(FVNetworkManager mgr) {
    if (!mgr) return nullptr;
    
    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        auto devices = wrapper->manager->getConnectedDevices();
        return strdup(devicesToJsonArray(devices).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API FVError fv_network_connect_to_device(FVNetworkManager mgr, const char* device_id) {
    clearLastError();
    
    if (!mgr || !device_id) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        if (wrapper->manager->connectToDevice(device_id)) {
            return FV_OK;
        }
        setLastError(FV_ERROR_NETWORK, "Failed to connect to device");
        return FV_ERROR_NETWORK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_NETWORK, e.what());
        return FV_ERROR_NETWORK;
    }
}

FV_API FVError fv_network_connect_to_address(FVNetworkManager mgr, 
                                              const char* host, uint16_t port) {
    clearLastError();
    
    if (!mgr || !host) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        if (wrapper->manager->connectToAddress(host, port)) {
            return FV_OK;
        }
        setLastError(FV_ERROR_NETWORK, "Failed to connect to address");
        return FV_ERROR_NETWORK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_NETWORK, e.what());
        return FV_ERROR_NETWORK;
    }
}

FV_API void fv_network_disconnect_device(FVNetworkManager mgr, const char* device_id) {
    if (!mgr || !device_id) return;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    wrapper->manager->disconnectFromDevice(device_id);
}

FV_API void fv_network_disconnect_all(FVNetworkManager mgr) {
    if (!mgr) return;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    wrapper->manager->disconnectAll();
}

FV_API int32_t fv_network_is_connected_to(FVNetworkManager mgr, const char* device_id) {
    if (!mgr || !device_id) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    return wrapper->manager->isConnectedTo(device_id) ? 1 : 0;
}

FV_API char* fv_network_get_last_error(FVNetworkManager mgr) {
    if (!mgr) return nullptr;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (wrapper->lastError.empty()) return nullptr;
    return strdup(wrapper->lastError.c_str());
}

FV_API FVError fv_network_set_database(FVNetworkManager mgr, FVDatabase db, const char* device_id) {
    clearLastError();
    
    if (!mgr || !db || !device_id) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        auto* dbHolder = reinterpret_cast<DatabaseHolder*>(db);
        
        // Get shared_ptr from DatabaseHolder (properly managed lifetime)
        wrapper->database = dbHolder->getDatabase();
        wrapper->deviceId = device_id;
        
        // Create IndexSyncManager for sync
        wrapper->syncManager = std::make_unique<IndexSyncManager>(wrapper->database, device_id);
        
        // Create IndexManager for file path lookups (needed for FileRequest handling)
        wrapper->indexManager = std::make_shared<IndexManager>(wrapper->database);
        
        // Set up sync callbacks (heap-allocated JSON for async Dart callbacks)
        wrapper->syncManager->onProgress([wrapper](const SyncProgress& progress) {
            if (wrapper->callback) {
                json j = {
                    {"deviceId", progress.deviceId},
                    {"totalFiles", progress.totalFiles},
                    {"receivedFiles", progress.receivedFiles},
                    {"progress", progress.progress()}
                };
                const char* jsonStr = allocEventJson(j.dump());
                wrapper->callback(static_cast<int32_t>(NetworkEvent::SyncProgress),
                                jsonStr, wrapper->userData);
            }
        });
        
        wrapper->syncManager->onComplete([wrapper](const std::string& deviceId, int64_t filesReceived) {
            if (wrapper->callback) {
                json j = {
                    {"deviceId", deviceId},
                    {"filesReceived", filesReceived}
                };
                const char* jsonStr = allocEventJson(j.dump());
                wrapper->callback(static_cast<int32_t>(NetworkEvent::SyncComplete),
                                jsonStr, wrapper->userData);
            }
        });
        
        // Install unified message handler
        wrapper->installMessageHandler();
        
        spdlog::info("NetworkManager: Database and IndexSyncManager configured");
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

FV_API FVError fv_network_request_sync(FVNetworkManager mgr, const char* device_id, int32_t full_sync) {
    clearLastError();
    
    if (!mgr || !device_id) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        
        if (!wrapper->syncManager) {
            setLastError(FV_ERROR_INVALID_ARGUMENT, "Database not configured - call fv_network_set_database first");
            return FV_ERROR_INVALID_ARGUMENT;
        }
        
        if (!wrapper->manager->isConnectedTo(device_id)) {
            setLastError(FV_ERROR_NETWORK, "Not connected to device");
            return FV_ERROR_NETWORK;
        }
        
        // Create sync request message
        Message msg(MessageType::IndexSyncRequest, generateRequestId());
        IndexSyncRequestPayload payload;
        payload.sinceTimestamp = full_sync ? 0 : 
            wrapper->syncManager->getLastSyncTimestamp(device_id);
        msg.setJsonPayload(payload.toJson());
        
        // Send via NetworkManager
        if (!wrapper->manager->sendTo(device_id, msg)) {
            setLastError(FV_ERROR_NETWORK, "Failed to send sync request");
            return FV_ERROR_NETWORK;
        }
        
        spdlog::info("NetworkManager: Requested sync from {} (full={})", device_id, full_sync);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

FV_API char* fv_network_get_remote_files(FVNetworkManager mgr) {
    if (!mgr) return nullptr;
    
    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        if (!wrapper->syncManager) return nullptr;
        
        auto files = wrapper->syncManager->getAllRemoteFiles();
        return strdup(remoteFilesToJson(files).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API char* fv_network_search_remote_files(FVNetworkManager mgr, const char* query, int32_t limit) {
    if (!mgr || !query) return nullptr;
    
    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        if (!wrapper->syncManager) return nullptr;
        
        auto files = wrapper->syncManager->searchRemoteFiles(query, limit);
        return strdup(remoteFilesToJson(files).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API int64_t fv_network_get_remote_file_count(FVNetworkManager mgr) {
    if (!mgr) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (!wrapper->syncManager) return 0;
    
    return wrapper->syncManager->getRemoteFileCount();
}

// ═══════════════════════════════════════════════════════════
// IndexSyncManager C API (standalone)
// ═══════════════════════════════════════════════════════════

FV_API FVIndexSyncManager fv_sync_create(FVDatabase db, const char* device_id) {
    clearLastError();
    
    if (!db || !device_id) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return nullptr;
    }

    try {
        auto* dbPtr = reinterpret_cast<Database*>(db);
        auto dbShared = std::shared_ptr<Database>(dbPtr, [](Database*){});
        
        auto* mgr = new IndexSyncManager(dbShared, device_id);
        spdlog::debug("IndexSyncManager created");
        return reinterpret_cast<FVIndexSyncManager>(mgr);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

FV_API void fv_sync_destroy(FVIndexSyncManager mgr) {
    if (!mgr) return;
    delete reinterpret_cast<IndexSyncManager*>(mgr);
    spdlog::debug("IndexSyncManager destroyed");
}

FV_API char* fv_sync_get_remote_files(FVIndexSyncManager mgr) {
    if (!mgr) return nullptr;
    
    try {
        auto* syncMgr = reinterpret_cast<IndexSyncManager*>(mgr);
        auto files = syncMgr->getAllRemoteFiles();
        return strdup(remoteFilesToJson(files).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API char* fv_sync_get_remote_files_from(FVIndexSyncManager mgr, const char* device_id) {
    if (!mgr || !device_id) return nullptr;
    
    try {
        auto* syncMgr = reinterpret_cast<IndexSyncManager*>(mgr);
        auto files = syncMgr->getRemoteFiles(device_id);
        return strdup(remoteFilesToJson(files).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API char* fv_sync_search_remote(FVIndexSyncManager mgr, const char* query, int32_t limit) {
    if (!mgr || !query) return nullptr;
    
    try {
        auto* syncMgr = reinterpret_cast<IndexSyncManager*>(mgr);
        auto files = syncMgr->searchRemoteFiles(query, limit);
        return strdup(remoteFilesToJson(files).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API int64_t fv_sync_get_remote_count(FVIndexSyncManager mgr) {
    if (!mgr) return 0;
    
    auto* syncMgr = reinterpret_cast<IndexSyncManager*>(mgr);
    return syncMgr->getRemoteFileCount();
}

FV_API int32_t fv_sync_is_syncing(FVIndexSyncManager mgr) {
    if (!mgr) return 0;
    
    auto* syncMgr = reinterpret_cast<IndexSyncManager*>(mgr);
    return syncMgr->isSyncing() ? 1 : 0;
}

// ═══════════════════════════════════════════════════════════
// File Transfer C API
// ═══════════════════════════════════════════════════════════

FV_API FVError fv_network_set_cache_dir(FVNetworkManager mgr, const char* cache_dir) {
    clearLastError();
    
    if (!mgr || !cache_dir) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        wrapper->cacheDir = cache_dir;
        
        // Create RemoteFileAccess with the cache directory
        wrapper->fileAccess = std::make_unique<RemoteFileAccess>(cache_dir);
        
        // Set up callbacks (heap-allocated JSON for async Dart callbacks)
        wrapper->fileAccess->onProgress([wrapper](const FileTransferProgress& progress) {
            if (wrapper->callback) {
                const char* json = allocEventJson(transferProgressToJson(progress));
                wrapper->callback(static_cast<int32_t>(NetworkEvent::FileTransferProgress),
                                json, wrapper->userData);
            }
        });
        
        wrapper->fileAccess->onComplete([wrapper](const FileTransferProgress& progress) {
            if (wrapper->callback) {
                const char* json = allocEventJson(transferProgressToJson(progress));
                wrapper->callback(static_cast<int32_t>(NetworkEvent::FileTransferComplete),
                                json, wrapper->userData);
            }
        });
        
        wrapper->fileAccess->onError([wrapper](const FileTransferProgress& progress) {
            if (wrapper->callback) {
                const char* json = allocEventJson(transferProgressToJson(progress));
                wrapper->callback(static_cast<int32_t>(NetworkEvent::FileTransferError),
                                json, wrapper->userData);
            }
        });
        
        // Install unified message handler (handles both sync and file transfer)
        wrapper->installMessageHandler();
        
        spdlog::info("NetworkManager: File access configured with cache dir: {}", cache_dir);
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

FV_API char* fv_network_request_file(FVNetworkManager mgr, const char* device_id,
                                      int64_t file_id, const char* file_name,
                                      int64_t expected_size, const char* checksum) {
    clearLastError();
    
    if (!mgr || !device_id || !file_name) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return nullptr;
    }

    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        
        if (!wrapper->fileAccess) {
            setLastError(FV_ERROR_INVALID_ARGUMENT, "Cache not configured - call fv_network_set_cache_dir first");
            return nullptr;
        }
        
        std::string checksumStr = checksum ? checksum : "";
        
        // Check cache first - if hit, return special marker
        if (wrapper->fileAccess->isCached(device_id, file_id, checksumStr)) {
            std::string cachedPath = wrapper->fileAccess->getCachedPath(device_id, file_id);
            spdlog::info("NetworkManager: File {}:{} already cached at {}", 
                         device_id, file_id, cachedPath);
            // Return "CACHED:path" so Dart knows it's a cache hit, not an error
            return strdup(("CACHED:" + cachedPath).c_str());
        }
        
        // Get peer connection
        auto peer = wrapper->manager->getPeerConnection(device_id);
        if (!peer) {
            setLastError(FV_ERROR_NETWORK, "Not connected to device");
            return nullptr;
        }
        
        // Use RemoteFileAccess to properly track the request
        std::string requestId = wrapper->fileAccess->requestFile(
            peer,
            device_id,
            file_id,
            file_name,
            expected_size,
            checksumStr
        );
        
        if (requestId.empty()) {
            // This shouldn't happen now since we checked cache first
            setLastError(FV_ERROR_NETWORK, "Failed to create file request");
            return nullptr;
        }
        
        spdlog::info("NetworkManager: Requested file {} ({}) from {}, request={}", 
                     file_id, file_name, device_id, requestId);
        return strdup(requestId.c_str());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

FV_API void fv_network_cancel_file_request(FVNetworkManager mgr, const char* request_id) {
    if (!mgr || !request_id) return;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (wrapper->fileAccess) {
        wrapper->fileAccess->cancelRequest(request_id);
    }
}

FV_API void fv_network_cancel_all_file_requests(FVNetworkManager mgr, const char* device_id) {
    if (!mgr || !device_id) return;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (wrapper->fileAccess) {
        wrapper->fileAccess->cancelAllRequests(device_id);
    }
}

FV_API char* fv_network_get_active_transfers(FVNetworkManager mgr) {
    if (!mgr) return nullptr;
    
    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        if (!wrapper->fileAccess) {
            return strdup("[]");
        }
        
        auto transfers = wrapper->fileAccess->getActiveTransfers();
        return strdup(transfersToJsonArray(transfers).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API char* fv_network_get_transfer_progress(FVNetworkManager mgr, const char* request_id) {
    if (!mgr || !request_id) return nullptr;
    
    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        if (!wrapper->fileAccess) {
            return nullptr;
        }
        
        auto progress = wrapper->fileAccess->getProgress(request_id);
        return strdup(transferProgressToJson(progress).c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API int32_t fv_network_is_file_cached(FVNetworkManager mgr, const char* device_id, int64_t file_id, const char* checksum) {
    if (!mgr || !device_id) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (!wrapper->fileAccess) return 0;
    
    return wrapper->fileAccess->isCached(device_id, file_id, checksum ? checksum : "") ? 1 : 0;
}

FV_API char* fv_network_get_cached_path(FVNetworkManager mgr, const char* device_id, int64_t file_id) {
    if (!mgr || !device_id) return nullptr;
    
    try {
        auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
        if (!wrapper->fileAccess) return nullptr;
        
        // Only return path if file is actually cached
        if (!wrapper->fileAccess->isCached(device_id, file_id, "")) {
            return nullptr;
        }
        
        std::string path = wrapper->fileAccess->getCachedPath(device_id, file_id);
        if (path.empty()) return nullptr;
        return strdup(path.c_str());
    } catch (...) {
        return nullptr;
    }
}

FV_API void fv_network_clear_cache(FVNetworkManager mgr) {
    if (!mgr) return;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (wrapper->fileAccess) {
        wrapper->fileAccess->clearCache();
        spdlog::info("NetworkManager: Cache cleared");
    }
}

FV_API int64_t fv_network_get_cache_size(FVNetworkManager mgr) {
    if (!mgr) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (!wrapper->fileAccess) return 0;
    
    return wrapper->fileAccess->getCacheSize();
}

FV_API int32_t fv_network_has_active_transfers(FVNetworkManager mgr) {
    if (!mgr) return 0;
    
    auto* wrapper = reinterpret_cast<NetworkManagerWrapper*>(mgr);
    if (!wrapper->fileAccess) return 0;
    
    return wrapper->fileAccess->hasActiveTransfers() ? 1 : 0;
}

