// test_remote_file_access.cpp — Тесты RemoteFileAccess

#include <gtest/gtest.h>
#include "familyvault/Network/RemoteFileAccess.h"
#include <filesystem>
#include <fstream>

using namespace FamilyVault;
namespace fs = std::filesystem;

class RemoteFileAccessTest : public ::testing::Test {
protected:
    std::string cacheDir;
    std::unique_ptr<RemoteFileAccess> fileAccess;

    void SetUp() override {
        cacheDir = (fs::temp_directory_path() / "test_file_cache").string();
        fs::create_directories(cacheDir);
        
        fileAccess = std::make_unique<RemoteFileAccess>(cacheDir);
    }

    void TearDown() override {
        fileAccess.reset();
        fs::remove_all(cacheDir);
    }
};

// ═══════════════════════════════════════════════════════════
// Basic Tests
// ═══════════════════════════════════════════════════════════

TEST_F(RemoteFileAccessTest, CreateAndDestroy) {
    EXPECT_FALSE(fileAccess->hasActiveTransfers());
    EXPECT_EQ(fileAccess->getCacheSize(), 0);
}

TEST_F(RemoteFileAccessTest, GetActiveTransfers_EmptyInitially) {
    auto transfers = fileAccess->getActiveTransfers();
    EXPECT_TRUE(transfers.empty());
}

TEST_F(RemoteFileAccessTest, GetProgress_InvalidRequest) {
    auto progress = fileAccess->getProgress("non-existent-request");
    EXPECT_EQ(progress.status, FileTransferStatus::Failed);
}

// ═══════════════════════════════════════════════════════════
// Cache Tests
// ═══════════════════════════════════════════════════════════

TEST_F(RemoteFileAccessTest, IsCached_FalseInitially) {
    EXPECT_FALSE(fileAccess->isCached("device-123", 12345));
    EXPECT_FALSE(fileAccess->isCached("device-123", 12345, "abc123"));
}

TEST_F(RemoteFileAccessTest, GetCachedPath) {
    std::string path = fileAccess->getCachedPath("device-123", 12345);
    EXPECT_FALSE(path.empty());
    // Path should include both device ID and file ID
    EXPECT_TRUE(path.find("device-123") != std::string::npos);
    EXPECT_TRUE(path.find("12345") != std::string::npos);
}

TEST_F(RemoteFileAccessTest, ClearCache_EmptyCache) {
    EXPECT_NO_THROW(fileAccess->clearCache());
    EXPECT_EQ(fileAccess->getCacheSize(), 0);
}

TEST_F(RemoteFileAccessTest, GetCacheSize_EmptyInitially) {
    EXPECT_EQ(fileAccess->getCacheSize(), 0);
}

// ═══════════════════════════════════════════════════════════
// Callback Tests
// ═══════════════════════════════════════════════════════════

TEST_F(RemoteFileAccessTest, SetCallbacks) {
    bool progressCalled = false;
    bool completeCalled = false;
    bool errorCalled = false;
    
    fileAccess->onProgress([&](const FileTransferProgress&) {
        progressCalled = true;
    });
    
    fileAccess->onComplete([&](const FileTransferProgress&) {
        completeCalled = true;
    });
    
    fileAccess->onError([&](const FileTransferProgress&) {
        errorCalled = true;
    });
    
    // Callbacks should be set without error
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════
// Cancel Tests
// ═══════════════════════════════════════════════════════════

TEST_F(RemoteFileAccessTest, CancelRequest_NonExistent) {
    // Should not crash when canceling non-existent request
    EXPECT_NO_THROW(fileAccess->cancelRequest("non-existent"));
}

TEST_F(RemoteFileAccessTest, CancelAllRequests_NonExistentDevice) {
    // Should not crash when canceling requests for non-existent device
    EXPECT_NO_THROW(fileAccess->cancelAllRequests("non-existent-device"));
}

// ═══════════════════════════════════════════════════════════
// Request Without Peer Tests
// ═══════════════════════════════════════════════════════════

TEST_F(RemoteFileAccessTest, RequestFile_NullPeer) {
    std::string requestId = fileAccess->requestFile(
        nullptr,  // null peer
        "device-123",
        12345,
        "test.txt",
        1024,
        "checksum123"
    );
    
    // Should return empty string (failed)
    EXPECT_TRUE(requestId.empty());
}

// ═══════════════════════════════════════════════════════════
// FileTransferProgress Tests
// ═══════════════════════════════════════════════════════════

TEST(FileTransferProgressTest, ProgressCalculation) {
    FileTransferProgress progress;
    progress.fileId = 1;
    progress.fileName = "test.txt";
    progress.totalSize = 1000;
    progress.transferredSize = 500;
    progress.status = FileTransferStatus::InProgress;
    
    EXPECT_DOUBLE_EQ(progress.progress(), 0.5);
}

TEST(FileTransferProgressTest, ProgressZeroWhenNoTotal) {
    FileTransferProgress progress;
    progress.totalSize = 0;
    progress.transferredSize = 0;
    
    EXPECT_DOUBLE_EQ(progress.progress(), 0.0);
}

TEST(FileTransferProgressTest, ProgressComplete) {
    FileTransferProgress progress;
    progress.totalSize = 1000;
    progress.transferredSize = 1000;
    progress.status = FileTransferStatus::Completed;
    
    EXPECT_DOUBLE_EQ(progress.progress(), 1.0);
}

// ═══════════════════════════════════════════════════════════
// Cache with Files Tests
// ═══════════════════════════════════════════════════════════

class RemoteFileAccessCacheTest : public RemoteFileAccessTest {
protected:
    void createCachedFile(const std::string& deviceId, int64_t fileId, const std::string& content) {
        // Cache structure: cacheDir/deviceId/fileId
        std::string deviceDir = cacheDir + "/" + deviceId;
        fs::create_directories(deviceDir);
        std::string path = deviceDir + "/" + std::to_string(fileId);
        std::ofstream file(path);
        file << content;
    }
};

TEST_F(RemoteFileAccessCacheTest, IsCached_AfterFileCreated) {
    createCachedFile("device-123", 12345, "Hello World");
    
    EXPECT_TRUE(fileAccess->isCached("device-123", 12345));
    // Different device should not see the file
    EXPECT_FALSE(fileAccess->isCached("device-other", 12345));
}

TEST_F(RemoteFileAccessCacheTest, GetCacheSize_WithFiles) {
    createCachedFile("device-a", 1, "Hello");
    createCachedFile("device-b", 2, "World!");
    
    int64_t size = fileAccess->getCacheSize();
    EXPECT_GT(size, 0);
    EXPECT_GE(size, 11); // "Hello" + "World!" = 11 bytes
}

TEST_F(RemoteFileAccessCacheTest, ClearCache_RemovesFiles) {
    createCachedFile("device-a", 1, "Hello");
    createCachedFile("device-b", 2, "World!");
    
    EXPECT_GT(fileAccess->getCacheSize(), 0);
    
    fileAccess->clearCache();
    
    EXPECT_EQ(fileAccess->getCacheSize(), 0);
    EXPECT_FALSE(fileAccess->isCached("device-a", 1));
    EXPECT_FALSE(fileAccess->isCached("device-b", 2));
}


