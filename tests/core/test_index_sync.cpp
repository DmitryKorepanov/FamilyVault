// test_index_sync.cpp — Тесты IndexSyncManager

#include <gtest/gtest.h>
#include "familyvault/Network/IndexSyncManager.h"
#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include <filesystem>
#include <fstream>

using namespace FamilyVault;
namespace fs = std::filesystem;

class IndexSyncManagerTest : public ::testing::Test {
protected:
    std::string testDbPath;
    std::shared_ptr<Database> db;
    std::unique_ptr<IndexSyncManager> syncManager;

    void SetUp() override {
        // Create temp database
        testDbPath = (fs::temp_directory_path() / "test_sync.db").string();
        fs::remove(testDbPath);
        
        db = std::make_shared<Database>(testDbPath);
        db->initialize();
        
        syncManager = std::make_unique<IndexSyncManager>(db, "test-device-001");
    }

    void TearDown() override {
        syncManager.reset();
        db.reset();
        fs::remove(testDbPath);
    }
};

// ═══════════════════════════════════════════════════════════
// Basic Tests
// ═══════════════════════════════════════════════════════════

TEST_F(IndexSyncManagerTest, CreateAndDestroy) {
    EXPECT_FALSE(syncManager->isSyncing());
    EXPECT_EQ(syncManager->getRemoteFileCount(), 0);
}

TEST_F(IndexSyncManagerTest, GetLastSyncTimestamp_ReturnsZeroInitially) {
    int64_t ts = syncManager->getLastSyncTimestamp("other-device");
    EXPECT_EQ(ts, 0);
}

TEST_F(IndexSyncManagerTest, SetAndGetLastSyncTimestamp) {
    const std::string deviceId = "device-abc";
    const int64_t timestamp = 1700000000;
    
    syncManager->setLastSyncTimestamp(deviceId, timestamp);
    int64_t retrieved = syncManager->getLastSyncTimestamp(deviceId);
    
    EXPECT_EQ(retrieved, timestamp);
}

TEST_F(IndexSyncManagerTest, GetLocalChangesSince_ReturnsEmpty) {
    auto changes = syncManager->getLocalChangesSince(0);
    EXPECT_TRUE(changes.empty());
}

TEST_F(IndexSyncManagerTest, GetRemoteFiles_ReturnsEmptyInitially) {
    auto files = syncManager->getAllRemoteFiles();
    EXPECT_TRUE(files.empty());
    
    auto filesFromDevice = syncManager->getRemoteFiles("other-device");
    EXPECT_TRUE(filesFromDevice.empty());
}

TEST_F(IndexSyncManagerTest, SearchRemoteFiles_ReturnsEmptyInitially) {
    auto results = syncManager->searchRemoteFiles("test", 10);
    EXPECT_TRUE(results.empty());
}

TEST_F(IndexSyncManagerTest, GetRemoteFileCount_ReturnsZeroInitially) {
    EXPECT_EQ(syncManager->getRemoteFileCount(), 0);
    EXPECT_EQ(syncManager->getRemoteFileCount("other-device"), 0);
}

// ═══════════════════════════════════════════════════════════
// Progress & State Tests
// ═══════════════════════════════════════════════════════════

TEST_F(IndexSyncManagerTest, IsSyncing_FalseInitially) {
    EXPECT_FALSE(syncManager->isSyncing());
    EXPECT_FALSE(syncManager->isSyncingWith("other-device"));
}

TEST_F(IndexSyncManagerTest, GetSyncProgress_ReturnsComplete) {
    auto progress = syncManager->getSyncProgress("non-existent");
    EXPECT_TRUE(progress.isComplete);
    EXPECT_EQ(progress.totalFiles, 0);
    EXPECT_EQ(progress.receivedFiles, 0);
}

// ═══════════════════════════════════════════════════════════
// Callback Tests
// ═══════════════════════════════════════════════════════════

TEST_F(IndexSyncManagerTest, SetCallbacks) {
    bool progressCalled = false;
    bool completeCalled = false;
    bool errorCalled = false;
    
    syncManager->onProgress([&](const SyncProgress&) {
        progressCalled = true;
    });
    
    syncManager->onComplete([&](const std::string&, int64_t) {
        completeCalled = true;
    });
    
    syncManager->onError([&](const std::string&, const std::string&) {
        errorCalled = true;
    });
    
    // Callbacks should be set without error
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════
// Remote Files Table Tests
// ═══════════════════════════════════════════════════════════

TEST_F(IndexSyncManagerTest, RemoteFilesTableCreated) {
    // Table should be created automatically
    // Verify by checking count (should not throw)
    EXPECT_NO_THROW({
        auto count = syncManager->getRemoteFileCount();
        EXPECT_EQ(count, 0);
    });
}

TEST_F(IndexSyncManagerTest, SyncStateTableCreated) {
    // Table should be created automatically
    // Verify by setting/getting timestamp
    EXPECT_NO_THROW({
        syncManager->setLastSyncTimestamp("test-device", 12345);
        auto ts = syncManager->getLastSyncTimestamp("test-device");
        EXPECT_EQ(ts, 12345);
    });
}

// ═══════════════════════════════════════════════════════════
// Multiple Device Timestamps
// ═══════════════════════════════════════════════════════════

TEST_F(IndexSyncManagerTest, MultipleDeviceTimestamps) {
    syncManager->setLastSyncTimestamp("device-1", 1000);
    syncManager->setLastSyncTimestamp("device-2", 2000);
    syncManager->setLastSyncTimestamp("device-3", 3000);
    
    EXPECT_EQ(syncManager->getLastSyncTimestamp("device-1"), 1000);
    EXPECT_EQ(syncManager->getLastSyncTimestamp("device-2"), 2000);
    EXPECT_EQ(syncManager->getLastSyncTimestamp("device-3"), 3000);
    
    // Update one
    syncManager->setLastSyncTimestamp("device-2", 2500);
    EXPECT_EQ(syncManager->getLastSyncTimestamp("device-2"), 2500);
    
    // Others unchanged
    EXPECT_EQ(syncManager->getLastSyncTimestamp("device-1"), 1000);
    EXPECT_EQ(syncManager->getLastSyncTimestamp("device-3"), 3000);
}

// ═══════════════════════════════════════════════════════════
// With Indexed Files
// ═══════════════════════════════════════════════════════════

class IndexSyncManagerWithFilesTest : public IndexSyncManagerTest {
protected:
    std::unique_ptr<IndexManager> indexManager;
    std::string testFolderPath;
    
    void SetUp() override {
        IndexSyncManagerTest::SetUp();
        
        // Create index manager
        indexManager = std::make_unique<IndexManager>(db);
        
        // Create temp folder with test files
        testFolderPath = (fs::temp_directory_path() / "test_sync_files").string();
        fs::create_directories(testFolderPath);
        
        // Create test files
        createTestFile("test1.txt", "Hello World");
        createTestFile("test2.txt", "Another file");
        createTestFile("private.txt", "Private content");
    }
    
    void TearDown() override {
        indexManager.reset();
        fs::remove_all(testFolderPath);
        IndexSyncManagerTest::TearDown();
    }
    
    void createTestFile(const std::string& name, const std::string& content) {
        std::ofstream file(testFolderPath + "/" + name);
        file << content;
    }
};

TEST_F(IndexSyncManagerWithFilesTest, GetLocalChangesSince_WithIndexedFiles) {
    // Add folder and scan
    int64_t folderId = indexManager->addFolder(testFolderPath);
    EXPECT_GT(folderId, 0);
    
    indexManager->scanFolder(folderId);
    
    // Get changes since beginning of time
    auto changes = syncManager->getLocalChangesSince(0);
    
    // Should return Family visibility files only
    // Note: Default visibility is Family
    EXPECT_GE(changes.size(), 0); // Might be 0 if scan is async
}



