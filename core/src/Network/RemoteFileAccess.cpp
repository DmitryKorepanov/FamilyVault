// RemoteFileAccess.cpp — Remote file access implementation

#include "familyvault/Network/RemoteFileAccess.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <queue>
#include <thread>
#include <condition_variable>
#include <iomanip>
#include <sstream>
#include <openssl/evp.h>
#include <openssl/sha.h>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Checksum utilities
// ═══════════════════════════════════════════════════════════

static std::string computeFileChecksum(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(ctx);
            return "";
        }
    }
    if (file.gcount() > 0) {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(ctx);
            return "";
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &length) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << "sha256:";
    for (unsigned int i = 0; i < length; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return oss.str();
}

static bool verifyChecksum(const std::string& filePath, const std::string& expectedChecksum) {
    if (expectedChecksum.empty()) {
        return true;  // No checksum to verify
    }
    std::string actualChecksum = computeFileChecksum(filePath);
    return !actualChecksum.empty() && actualChecksum == expectedChecksum;
}

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
// Transfer state
// ═══════════════════════════════════════════════════════════

struct FileTransfer {
    std::string requestId;
    std::weak_ptr<PeerConnection> peer;
    std::string deviceId;  // Source device ID for unique cache keys
    int64_t fileId;
    std::string fileName;
    int64_t expectedSize;
    std::string expectedChecksum;
    std::string localPath;
    std::ofstream outputFile;
    int64_t receivedSize = 0;
    int64_t lastNotifiedSize = 0;  // For progress throttling
    FileTransferStatus status = FileTransferStatus::Pending;
    std::string error;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastProgressNotify;  // For throttling
};

// Upload request for async processing
struct UploadRequest {
    std::shared_ptr<PeerConnection> peer;
    std::string requestId;
    std::string filePath;
    int64_t fileId;
    int64_t offset;
    int64_t length;
};

// ═══════════════════════════════════════════════════════════
// RemoteFileAccess::Impl
// ═══════════════════════════════════════════════════════════

class RemoteFileAccess::Impl {
public:
    explicit Impl(const std::string& cacheDir) : m_cacheDir(cacheDir) {
        // Ensure cache directory exists
        fs::create_directories(cacheDir);
        
        // Start upload worker thread
        m_uploadWorkerRunning = true;
        m_uploadWorker = std::thread([this]() { uploadWorkerLoop(); });
    }

    ~Impl() {
        // Stop upload worker
        {
            std::lock_guard<std::mutex> lock(m_uploadMutex);
            m_uploadWorkerRunning = false;
        }
        m_uploadCv.notify_all();
        if (m_uploadWorker.joinable()) {
            m_uploadWorker.join();
        }
        
        // Close any open transfers
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, transfer] : m_transfers) {
            if (transfer.outputFile.is_open()) {
                transfer.outputFile.close();
            }
        }
    }

    std::string requestFile(
        std::shared_ptr<PeerConnection> peer,
        const std::string& deviceId,
        int64_t fileId,
        const std::string& fileName,
        int64_t expectedSize,
        const std::string& checksum) {
        
        if (!peer || !peer->isConnected()) {
            spdlog::error("RemoteFileAccess: Cannot request file - peer not connected");
            return "";
        }

        // Note: Cache check is done by FFI layer before calling this method
        // If we get here, the file is not cached and needs to be requested

        // Create request
        std::string requestId = generateRequestId();
        
        FileTransfer transfer;
        transfer.requestId = requestId;
        transfer.peer = peer;
        transfer.deviceId = deviceId;
        transfer.fileId = fileId;
        transfer.fileName = fileName;
        transfer.expectedSize = expectedSize;
        transfer.expectedChecksum = checksum;
        transfer.localPath = getCachePathForWrite(deviceId, fileId, fileName);
        transfer.startTime = std::chrono::steady_clock::now();

        // Open output file
        transfer.outputFile.open(transfer.localPath, std::ios::binary | std::ios::trunc);
        if (!transfer.outputFile.is_open()) {
            spdlog::error("RemoteFileAccess: Failed to open output file: {}", transfer.localPath);
            return "";
        }

        // Store transfer
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_transfers[requestId] = std::move(transfer);
        }

        // Send request
        Message msg(MessageType::FileRequest, requestId);
        FileRequestPayload payload;
        payload.fileId = fileId;
        payload.checksum = checksum;
        payload.offset = 0;
        payload.length = 0;  // Entire file
        msg.setJsonPayload(payload.toJson());

        if (!peer->sendMessage(msg)) {
            // Send failed - clean up the transfer
            spdlog::error("RemoteFileAccess: Failed to send file request for {}:{}", deviceId, fileId);
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_transfers.find(requestId);
            if (it != m_transfers.end()) {
                it->second.outputFile.close();
                fs::remove(it->second.localPath);
                m_transfers.erase(it);
            }
            return "";  // Return empty to signal failure
        }
        
        spdlog::info("RemoteFileAccess: Requested file {}:{} from {}", deviceId, fileId, peer->getPeerId());
        return requestId;
    }

    void cancelRequest(const std::string& requestId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_transfers.find(requestId);
        if (it != m_transfers.end()) {
            it->second.status = FileTransferStatus::Cancelled;
            if (it->second.outputFile.is_open()) {
                it->second.outputFile.close();
            }
            // Delete incomplete file
            fs::remove(it->second.localPath);
            m_transfers.erase(it);
        }
    }

    void cancelAllRequests(const std::string& deviceId) {
        std::vector<FileTransferProgress> cancelled;
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_transfers.begin(); it != m_transfers.end(); ) {
                // Use cached deviceId field - works even when peer is dead
                if (it->second.deviceId == deviceId) {
                    spdlog::info("RemoteFileAccess: Cancelling transfer {} for disconnected device {}", 
                                 it->second.requestId, deviceId);
                    it->second.status = FileTransferStatus::Cancelled;
                    it->second.error = "Device disconnected";
                    if (it->second.outputFile.is_open()) {
                        it->second.outputFile.close();
                    }
                    fs::remove(it->second.localPath);
                    
                    // Save progress for notification after releasing lock
                    cancelled.push_back(toProgress(it->second));
                    
                    it = m_transfers.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Notify errors outside of lock (one per cancelled transfer)
        {
            std::lock_guard<std::mutex> cbLock(m_callbackMutex);
            if (m_onError) {
                for (const auto& progress : cancelled) {
                    m_onError(progress);
                }
            }
        }
    }

    void handleFileRequest(
        std::shared_ptr<PeerConnection> peer,
        const Message& request,
        std::function<std::string(int64_t fileId)> getFilePath) {
        
        if (!peer) return;

        auto payload = FileRequestPayload::fromJson(request.getJsonPayload());
        if (!payload) {
            spdlog::warn("RemoteFileAccess: Invalid file request");
            return;
        }

        std::string filePath = getFilePath(payload->fileId);
        if (filePath.empty() || !fs::exists(filePath)) {
            // Send "not found" immediately (lightweight operation)
            Message notFound(MessageType::FileNotFound, request.requestId);
            peer->sendMessage(notFound);
            spdlog::debug("RemoteFileAccess: File {} not found", payload->fileId);
            return;
        }

        // Enqueue upload request to worker thread (don't block receive thread)
        UploadRequest req;
        req.peer = peer;
        req.requestId = request.requestId;
        req.filePath = filePath;
        req.fileId = payload->fileId;
        req.offset = payload->offset;
        req.length = payload->length;
        
        {
            std::lock_guard<std::mutex> lock(m_uploadMutex);
            m_uploadQueue.push(std::move(req));
        }
        m_uploadCv.notify_one();
        
        spdlog::debug("RemoteFileAccess: Queued file {} for upload", payload->fileId);
    }

    void handleFileResponse(const Message& response) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_transfers.find(response.requestId);
        if (it == m_transfers.end()) return;

        it->second.status = FileTransferStatus::InProgress;
        
        // Parse header
        auto header = FileChunkHeader::deserialize(response.payload.data(), response.payload.size());
        if (header) {
            it->second.expectedSize = header->totalSize;
        }

        notifyProgress(it->second);
    }

    void handleFileChunk(const Message& chunk) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_transfers.find(chunk.requestId);
        if (it == m_transfers.end()) return;

        auto& transfer = it->second;
        if (transfer.status != FileTransferStatus::InProgress) {
            transfer.status = FileTransferStatus::InProgress;
        }

        // Parse header
        if (chunk.payload.size() < FileChunkHeader::HEADER_SIZE) {
            spdlog::warn("RemoteFileAccess: Invalid chunk - too small");
            return;
        }

        auto header = FileChunkHeader::deserialize(chunk.payload.data(), chunk.payload.size());
        if (!header) {
            spdlog::warn("RemoteFileAccess: Invalid chunk header");
            return;
        }

        // Write data
        const uint8_t* data = chunk.payload.data() + FileChunkHeader::HEADER_SIZE;
        size_t dataSize = chunk.payload.size() - FileChunkHeader::HEADER_SIZE;

        if (transfer.outputFile.is_open() && dataSize > 0) {
            transfer.outputFile.write(reinterpret_cast<const char*>(data), dataSize);
            transfer.receivedSize += dataSize;
        }

        // Throttled progress notification to avoid flooding UI (~160 events/sec at 10MB/s)
        notifyProgressThrottled(transfer);

        // Check if complete
        if (header->isLast || transfer.receivedSize >= transfer.expectedSize) {
            transfer.outputFile.close();
            
            // Verify checksum if expected
            if (!transfer.expectedChecksum.empty()) {
                if (!verifyChecksum(transfer.localPath, transfer.expectedChecksum)) {
                    spdlog::error("RemoteFileAccess: File {} checksum mismatch!", transfer.fileId);
                    transfer.status = FileTransferStatus::Failed;
                    transfer.error = "Checksum verification failed";
                    
                    // Delete corrupt file
                    fs::remove(transfer.localPath);
                    
                    // Capture progress before erasing
                    auto failedProgress = toProgress(transfer);
                    
                    // Remove failed transfer to prevent memory leak
                    m_transfers.erase(it);
                    
                    // Notify error with full progress info
                    {
                        std::lock_guard<std::mutex> cbLock(m_callbackMutex);
                        if (m_onError) {
                            m_onError(failedProgress);
                        }
                    }
                    return;
                }
            }
            
            transfer.status = FileTransferStatus::Completed;
            std::string localPath = transfer.localPath;

            spdlog::info("RemoteFileAccess: File {} download complete ({} bytes, checksum {})", 
                         transfer.fileId, transfer.receivedSize,
                         transfer.expectedChecksum.empty() ? "not verified" : "OK");

            // Create progress snapshot before removing
            auto completedProgress = toProgress(transfer);
            completedProgress.localPath = localPath;
            completedProgress.status = FileTransferStatus::Completed;
            
            // Remove completed transfer to prevent memory leak
            m_transfers.erase(it);

            // Notify complete with full progress info
            {
                std::lock_guard<std::mutex> cbLock(m_callbackMutex);
                if (m_onComplete) {
                    m_onComplete(completedProgress);
                }
            }
        }
    }

    void handleFileNotFound(const Message& msg) {
        FileTransferProgress failedProgress;
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_transfers.find(msg.requestId);
            if (it == m_transfers.end()) return;

            it->second.status = FileTransferStatus::Failed;
            it->second.error = "File not found on remote device";
            
            // Capture progress before erasing
            failedProgress = toProgress(it->second);
            
            if (it->second.outputFile.is_open()) {
                it->second.outputFile.close();
            }
            fs::remove(it->second.localPath);

            // Remove failed transfer to prevent memory leak
            m_transfers.erase(it);
        }

        spdlog::warn("RemoteFileAccess: File {} not found", failedProgress.fileId);

        {
            std::lock_guard<std::mutex> cbLock(m_callbackMutex);
            if (m_onError) {
                m_onError(failedProgress);
            }
        }
    }

    FileTransferProgress getProgress(const std::string& requestId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_transfers.find(requestId);
        if (it != m_transfers.end()) {
            return toProgress(it->second);
        }
        return FileTransferProgress{"", "", 0, "", 0, 0, FileTransferStatus::Failed, "Not found", ""};
    }

    std::vector<FileTransferProgress> getActiveTransfers() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<FileTransferProgress> result;
        for (const auto& [id, transfer] : m_transfers) {
            if (transfer.status == FileTransferStatus::Pending ||
                transfer.status == FileTransferStatus::InProgress) {
                result.push_back(toProgress(transfer));
            }
        }
        return result;
    }

    bool hasActiveTransfers() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, transfer] : m_transfers) {
            if (transfer.status == FileTransferStatus::Pending ||
                transfer.status == FileTransferStatus::InProgress) {
                return true;
            }
        }
        return false;
    }

    bool isCached(const std::string& deviceId, int64_t fileId, const std::string& checksum) const {
        std::string path = findCachedFile(deviceId, fileId);
        if (path.empty()) return false;
        
        // Verify checksum if provided
        if (!checksum.empty()) {
            if (!verifyChecksum(path, checksum)) {
                spdlog::debug("RemoteFileAccess: Cached file {}:{} has wrong checksum", deviceId, fileId);
                return false;  // Stale/corrupt file
            }
        }
        return true;
    }

    std::string getCachedPath(const std::string& deviceId, int64_t fileId) const {
        // First try to find existing cached file (may have extension)
        std::string existing = findCachedFile(deviceId, fileId);
        if (!existing.empty()) {
            return existing;
        }
        // Return expected path without extension (for planning purposes)
        return m_cacheDir + "/" + deviceId + "/" + std::to_string(fileId);
    }
    
    // Find cached file by deviceId and fileId (handles extensions)
    std::string findCachedFile(const std::string& deviceId, int64_t fileId) const {
        std::string deviceDir = m_cacheDir + "/" + deviceId;
        if (!fs::exists(deviceDir)) return "";
        
        std::string prefix = std::to_string(fileId);
        
        // Look for files starting with fileId (e.g., "12345" or "12345.jpg")
        for (const auto& entry : fs::directory_iterator(deviceDir)) {
            if (!entry.is_regular_file()) continue;
            
            std::string filename = entry.path().filename().string();
            // Match exact fileId or fileId.extension
            if (filename == prefix || 
                (filename.size() > prefix.size() && 
                 filename.substr(0, prefix.size()) == prefix &&
                 filename[prefix.size()] == '.')) {
                return entry.path().string();
            }
        }
        return "";
    }

    void clearCache() {
        // Remove all files and subdirectories in cache
        for (const auto& entry : fs::directory_iterator(m_cacheDir)) {
            fs::remove_all(entry.path());  // Use remove_all for directories
        }
    }

    int64_t getCacheSize() const {
        int64_t size = 0;
        // Use recursive_directory_iterator to count files in subdirectories
        for (const auto& entry : fs::recursive_directory_iterator(m_cacheDir)) {
            if (entry.is_regular_file()) {
                size += entry.file_size();
            }
        }
        return size;
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
    std::string m_cacheDir;
    mutable std::mutex m_mutex;
    std::map<std::string, FileTransfer> m_transfers;

    std::mutex m_callbackMutex;
    ProgressCallback m_onProgress;
    CompleteCallback m_onComplete;
    ErrorCallback m_onError;
    
    // Upload worker thread (to avoid blocking receive thread)
    std::thread m_uploadWorker;
    std::mutex m_uploadMutex;
    std::condition_variable m_uploadCv;
    std::queue<UploadRequest> m_uploadQueue;
    bool m_uploadWorkerRunning = false;
    
    void uploadWorkerLoop() {
        while (true) {
            UploadRequest req;
            {
                std::unique_lock<std::mutex> lock(m_uploadMutex);
                m_uploadCv.wait(lock, [this]() { 
                    return !m_uploadQueue.empty() || !m_uploadWorkerRunning; 
                });
                
                if (!m_uploadWorkerRunning && m_uploadQueue.empty()) {
                    break;
                }
                
                if (m_uploadQueue.empty()) continue;
                
                req = std::move(m_uploadQueue.front());
                m_uploadQueue.pop();
            }
            
            // Process upload request
            processUpload(req);
        }
    }
    
    void processUpload(const UploadRequest& req) {
        if (!req.peer || !req.peer->isConnected()) return;
        
        std::ifstream file(req.filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            Message notFound(MessageType::FileNotFound, req.requestId);
            req.peer->sendMessage(notFound);
            return;
        }
        
        int64_t fileSize = file.tellg();
        file.seekg(req.offset);
        
        int64_t bytesToSend = (req.length > 0) 
            ? std::min(req.length, fileSize - req.offset)
            : (fileSize - req.offset);
        
        // Send response header
        Message response(MessageType::FileResponse, req.requestId);
        FileChunkHeader header;
        header.fileId = req.fileId;
        header.offset = req.offset;
        header.totalSize = fileSize;
        header.chunkSize = 0;
        header.isLast = false;
        response.setBinaryPayload(header.serialize());
        req.peer->sendMessage(response);
        
        // Send file in chunks
        std::vector<uint8_t> buffer(FILE_CHUNK_SIZE);
        int64_t sentBytes = 0;
        
        while (sentBytes < bytesToSend && req.peer->isConnected()) {
            size_t toRead = std::min(static_cast<size_t>(bytesToSend - sentBytes), FILE_CHUNK_SIZE);
            file.read(reinterpret_cast<char*>(buffer.data()), toRead);
            size_t actualRead = file.gcount();
            
            if (actualRead == 0) break;
            
            Message chunk(MessageType::FileChunk, req.requestId);
            
            FileChunkHeader chunkHeader;
            chunkHeader.fileId = req.fileId;
            chunkHeader.offset = req.offset + sentBytes;
            chunkHeader.totalSize = fileSize;
            chunkHeader.chunkSize = static_cast<int32_t>(actualRead);
            chunkHeader.isLast = (sentBytes + actualRead >= bytesToSend);
            
            auto headerBytes = chunkHeader.serialize();
            std::vector<uint8_t> chunkPayload;
            chunkPayload.reserve(headerBytes.size() + actualRead);
            chunkPayload.insert(chunkPayload.end(), headerBytes.begin(), headerBytes.end());
            chunkPayload.insert(chunkPayload.end(), buffer.begin(), buffer.begin() + actualRead);
            
            chunk.setBinaryPayload(chunkPayload);
            req.peer->sendMessage(chunk);
            
            sentBytes += actualRead;
        }
        
        spdlog::info("RemoteFileAccess: Sent file {} ({} bytes)", req.fileId, sentBytes);
    }

    std::string getCachePathForWrite(const std::string& deviceId, int64_t fileId, const std::string& fileName) const {
        // Cache structure: cacheDir/deviceId/fileId.ext
        // Extension is preserved for proper file opening
        std::string deviceDir = m_cacheDir + "/" + deviceId;
        fs::create_directories(deviceDir);
        
        std::string ext;
        auto dotPos = fileName.rfind('.');
        if (dotPos != std::string::npos) {
            ext = fileName.substr(dotPos);
        }
        return deviceDir + "/" + std::to_string(fileId) + ext;
    }

    FileTransferProgress toProgress(const FileTransfer& transfer) const {
        return FileTransferProgress{
            transfer.requestId,
            transfer.deviceId,
            transfer.fileId,
            transfer.fileName,
            transfer.expectedSize,
            transfer.receivedSize,
            transfer.status,
            transfer.error,
            transfer.localPath
        };
    }

    void notifyProgress(const FileTransfer& transfer) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onProgress) {
            m_onProgress(toProgress(transfer));
        }
    }
    
    // Throttled progress notification to avoid flooding UI (max ~10 updates/sec)
    // Returns true if notification was sent
    bool notifyProgressThrottled(FileTransfer& transfer) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - transfer.lastProgressNotify).count();
        
        // Calculate progress percentage
        int64_t progressChange = transfer.receivedSize - transfer.lastNotifiedSize;
        double progressPercent = transfer.expectedSize > 0 
            ? (progressChange * 100.0 / transfer.expectedSize) 
            : 0;
        
        // Throttle: notify if 100ms elapsed OR 1% progress OR status change
        constexpr int64_t MIN_NOTIFY_INTERVAL_MS = 100;
        constexpr double MIN_PROGRESS_PERCENT = 1.0;
        
        bool shouldNotify = (elapsed >= MIN_NOTIFY_INTERVAL_MS) || 
                            (progressPercent >= MIN_PROGRESS_PERCENT) ||
                            (transfer.status != FileTransferStatus::InProgress);
        
        if (shouldNotify) {
            transfer.lastProgressNotify = now;
            transfer.lastNotifiedSize = transfer.receivedSize;
            notifyProgress(transfer);
            return true;
        }
        return false;
    }
};

// ═══════════════════════════════════════════════════════════
// RemoteFileAccess Public Interface
// ═══════════════════════════════════════════════════════════

RemoteFileAccess::RemoteFileAccess(const std::string& cacheDir)
    : m_impl(std::make_unique<Impl>(cacheDir)) {}

RemoteFileAccess::~RemoteFileAccess() = default;

std::string RemoteFileAccess::requestFile(
    std::shared_ptr<PeerConnection> peer,
    const std::string& deviceId,
    int64_t fileId,
    const std::string& fileName,
    int64_t expectedSize,
    const std::string& checksum) {
    return m_impl->requestFile(std::move(peer), deviceId, fileId, fileName, expectedSize, checksum);
}

void RemoteFileAccess::cancelRequest(const std::string& requestId) {
    m_impl->cancelRequest(requestId);
}

void RemoteFileAccess::cancelAllRequests(const std::string& deviceId) {
    m_impl->cancelAllRequests(deviceId);
}

void RemoteFileAccess::handleFileRequest(
    std::shared_ptr<PeerConnection> peer,
    const Message& request,
    std::function<std::string(int64_t fileId)> getFilePath) {
    m_impl->handleFileRequest(std::move(peer), request, std::move(getFilePath));
}

void RemoteFileAccess::handleFileResponse(const Message& response) {
    m_impl->handleFileResponse(response);
}

void RemoteFileAccess::handleFileChunk(const Message& chunk) {
    m_impl->handleFileChunk(chunk);
}

void RemoteFileAccess::handleFileNotFound(const Message& msg) {
    m_impl->handleFileNotFound(msg);
}

FileTransferProgress RemoteFileAccess::getProgress(const std::string& requestId) const {
    return m_impl->getProgress(requestId);
}

std::vector<FileTransferProgress> RemoteFileAccess::getActiveTransfers() const {
    return m_impl->getActiveTransfers();
}

bool RemoteFileAccess::hasActiveTransfers() const {
    return m_impl->hasActiveTransfers();
}

bool RemoteFileAccess::isCached(const std::string& deviceId, int64_t fileId, const std::string& checksum) const {
    return m_impl->isCached(deviceId, fileId, checksum);
}

std::string RemoteFileAccess::getCachedPath(const std::string& deviceId, int64_t fileId) const {
    return m_impl->getCachedPath(deviceId, fileId);
}

void RemoteFileAccess::clearCache() {
    m_impl->clearCache();
}

int64_t RemoteFileAccess::getCacheSize() const {
    return m_impl->getCacheSize();
}

void RemoteFileAccess::onProgress(ProgressCallback callback) {
    m_impl->onProgress(std::move(callback));
}

void RemoteFileAccess::onComplete(CompleteCallback callback) {
    m_impl->onComplete(std::move(callback));
}

void RemoteFileAccess::onError(ErrorCallback callback) {
    m_impl->onError(std::move(callback));
}

} // namespace FamilyVault

