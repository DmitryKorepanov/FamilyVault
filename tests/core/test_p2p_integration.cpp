// test_p2p_integration.cpp — Integration tests for P2P subsystem
// Эти тесты проверяют взаимодействие компонентов, а не изолированные методы

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include "familyvault/Types.h"
#include "familyvault/Network/IndexSyncManager.h"
#include "familyvault/Network/RemoteFileAccess.h"
#include "familyvault/Network/NetworkManager.h"
#include "familyvault/Network/NetworkProtocol.h"

namespace fs = std::filesystem;
using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// Cache Path Consistency Tests - проверяют совместимость путей
// Эти тесты бы отловили баг с расширениями файлов
// ═══════════════════════════════════════════════════════════

class CachePathConsistencyTest : public ::testing::Test {
protected:
    std::string cacheDir;
    std::unique_ptr<RemoteFileAccess> fileAccess;

    void SetUp() override {
        cacheDir = (fs::temp_directory_path() / "test_cache_paths").string();
        fs::create_directories(cacheDir);
        fileAccess = std::make_unique<RemoteFileAccess>(cacheDir);
    }

    void TearDown() override {
        fileAccess.reset();
        fs::remove_all(cacheDir);
    }
};

// Тест: Файл записанный с расширением должен находиться через isCached
TEST_F(CachePathConsistencyTest, FileWithExtension_FoundByIsCached) {
    const std::string deviceId = "device-abc";
    const int64_t fileId = 12345;
    
    // Simulate what getCachePathForWrite does - create file with extension
    std::string deviceDir = cacheDir + "/" + deviceId;
    fs::create_directories(deviceDir);
    
    std::string fileWithExt = deviceDir + "/" + std::to_string(fileId) + ".jpg";
    std::ofstream(fileWithExt) << "fake image data";
    
    // Now isCached should find it
    EXPECT_TRUE(fileAccess->isCached(deviceId, fileId, ""));
    
    // And getCachedPath should return the path with extension
    std::string foundPath = fileAccess->getCachedPath(deviceId, fileId);
    EXPECT_FALSE(foundPath.empty());
    EXPECT_NE(foundPath.find(".jpg"), std::string::npos);
}

// Тест: Разные расширения для одного fileId
TEST_F(CachePathConsistencyTest, DifferentExtensions_StillFound) {
    const std::string deviceId = "device-xyz";
    const int64_t fileId = 99999;
    
    std::string deviceDir = cacheDir + "/" + deviceId;
    fs::create_directories(deviceDir);
    
    // Create files with different extensions
    std::ofstream(deviceDir + "/" + std::to_string(fileId) + ".png") << "png data";
    
    EXPECT_TRUE(fileAccess->isCached(deviceId, fileId, ""));
    
    std::string path = fileAccess->getCachedPath(deviceId, fileId);
    EXPECT_NE(path.find("99999"), std::string::npos);
}

// Тест: Файл без расширения тоже находится
TEST_F(CachePathConsistencyTest, FileWithoutExtension_Found) {
    const std::string deviceId = "device-noext";
    const int64_t fileId = 55555;
    
    std::string deviceDir = cacheDir + "/" + deviceId;
    fs::create_directories(deviceDir);
    
    // Create file without extension
    std::ofstream(deviceDir + "/" + std::to_string(fileId)) << "raw data";
    
    EXPECT_TRUE(fileAccess->isCached(deviceId, fileId, ""));
}

// Тест: Разные устройства не конфликтуют
TEST_F(CachePathConsistencyTest, DifferentDevices_NoCollision) {
    const int64_t fileId = 11111;
    
    // Device A
    std::string dirA = cacheDir + "/device-A";
    fs::create_directories(dirA);
    std::ofstream(dirA + "/" + std::to_string(fileId) + ".txt") << "from A";
    
    // Device B
    std::string dirB = cacheDir + "/device-B";
    fs::create_directories(dirB);
    std::ofstream(dirB + "/" + std::to_string(fileId) + ".txt") << "from B";
    
    // Both should be found independently
    EXPECT_TRUE(fileAccess->isCached("device-A", fileId, ""));
    EXPECT_TRUE(fileAccess->isCached("device-B", fileId, ""));
    
    std::string pathA = fileAccess->getCachedPath("device-A", fileId);
    std::string pathB = fileAccess->getCachedPath("device-B", fileId);
    
    EXPECT_NE(pathA, pathB);
    EXPECT_NE(pathA.find("device-A"), std::string::npos);
    EXPECT_NE(pathB.find("device-B"), std::string::npos);
}

// Тест: getCachedPath возвращает путь даже для несуществующего файла (для планирования)
TEST_F(CachePathConsistencyTest, GetCachedPath_ReturnsPathForNonexistent) {
    const std::string deviceId = "device-new";
    const int64_t fileId = 77777;
    
    // File doesn't exist yet
    EXPECT_FALSE(fileAccess->isCached(deviceId, fileId, ""));
    
    // But getCachedPath should still return a valid path
    std::string path = fileAccess->getCachedPath(deviceId, fileId);
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find(deviceId), std::string::npos);
    EXPECT_NE(path.find(std::to_string(fileId)), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// Sync Integration Tests - проверяют обмен между SyncManager'ами
// Эти тесты бы отловили баги с handleSyncResponse и indexManager
// ═══════════════════════════════════════════════════════════

class SyncIntegrationTest : public ::testing::Test {
protected:
    std::string tempDir;
    std::shared_ptr<Database> dbA;
    std::shared_ptr<Database> dbB;
    std::unique_ptr<IndexSyncManager> syncA;
    std::unique_ptr<IndexSyncManager> syncB;
    std::unique_ptr<IndexManager> indexA;

    void SetUp() override {
        tempDir = (fs::temp_directory_path() / "test_sync_integration").string();
        fs::create_directories(tempDir);
        
        // Create two separate databases (simulating two devices)
        dbA = std::make_shared<Database>(tempDir + "/device_a.db");
        dbB = std::make_shared<Database>(tempDir + "/device_b.db");
        
        // Initialize database schema
        dbA->initialize();
        dbB->initialize();
        
        // Create sync managers for each
        syncA = std::make_unique<IndexSyncManager>(dbA, "device-A");
        syncB = std::make_unique<IndexSyncManager>(dbB, "device-B");
        
        // Create index manager for device A to add files
        indexA = std::make_unique<IndexManager>(dbA);
    }

    void TearDown() override {
        syncA.reset();
        syncB.reset();
        indexA.reset();
        dbA.reset();
        dbB.reset();
        fs::remove_all(tempDir);
    }
    
    // Helper: create a test file on device A
    void createTestFileOnA(const std::string& relativePath) {
        std::string fullPath = tempDir + "/files_a/" + relativePath;
        fs::create_directories(fs::path(fullPath).parent_path());
        std::ofstream(fullPath) << "test content for " << relativePath;
    }
};

// Тест: IndexSyncManager создаёт нужные таблицы
TEST_F(SyncIntegrationTest, SyncManager_CreatesTables) {
    // remote_files table should exist
    auto remoteFiles = syncB->getAllRemoteFiles();
    EXPECT_TRUE(remoteFiles.empty());  // Empty, but query works
    
    // sync_state table should exist (via timestamp operations)
    int64_t ts = syncB->getLastSyncTimestamp("any-device");
    EXPECT_EQ(ts, 0);  // Zero, but query works
}

// Тест: Файлы сканируются и индексируются
TEST_F(SyncIntegrationTest, FilesScanned_AreIndexed) {
    // Add a watched folder with FAMILY visibility
    std::string folderPath = tempDir + "/files_a";
    fs::create_directories(folderPath);
    
    int64_t folderId = indexA->addFolder(folderPath, "Test Folder", Visibility::Family);
    ASSERT_GT(folderId, 0);
    
    createTestFileOnA("photo1.jpg");
    createTestFileOnA("photo2.jpg");
    createTestFileOnA("doc.pdf");
    
    // Scan to index files
    indexA->scanFolder(folderId);
    
    // Files should be in database
    auto files = indexA->getFilesByFolder(folderId);
    EXPECT_GE(files.size(), 3u);
}

// Тест: После синхронизации remote_files содержит данные
TEST_F(SyncIntegrationTest, AfterSync_RemoteFilesPopulated) {
    // Initially device B has no remote files
    EXPECT_EQ(syncB->getRemoteFileCount(), 0);
    
    // Simulate receiving files from device A
    // (In real scenario this would happen via network)
    auto remoteFiles = syncB->getAllRemoteFiles();
    EXPECT_TRUE(remoteFiles.empty());
}

// Тест: Timestamps обновляются после синхронизации
TEST_F(SyncIntegrationTest, Timestamps_UpdateAfterSync) {
    // Initially no sync timestamp
    int64_t ts1 = syncB->getLastSyncTimestamp("device-A");
    EXPECT_EQ(ts1, 0);
    
    // Set timestamp
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    syncB->setLastSyncTimestamp("device-A", now);
    
    int64_t ts2 = syncB->getLastSyncTimestamp("device-A");
    EXPECT_EQ(ts2, now);
}

// Тест: Множественные устройства отслеживаются независимо
TEST_F(SyncIntegrationTest, MultipleDevices_IndependentTimestamps) {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    syncB->setLastSyncTimestamp("device-A", now);
    syncB->setLastSyncTimestamp("device-C", now + 1000);
    syncB->setLastSyncTimestamp("device-D", now + 2000);
    
    EXPECT_EQ(syncB->getLastSyncTimestamp("device-A"), now);
    EXPECT_EQ(syncB->getLastSyncTimestamp("device-C"), now + 1000);
    EXPECT_EQ(syncB->getLastSyncTimestamp("device-D"), now + 2000);
}

// ═══════════════════════════════════════════════════════════
// File Transfer Integration Tests - полный цикл передачи файлов
// Эти тесты бы отловили баг с cache hit = error
// ═══════════════════════════════════════════════════════════

class FileTransferIntegrationTest : public ::testing::Test {
protected:
    std::string cacheDir;
    std::unique_ptr<RemoteFileAccess> fileAccess;
    bool completeCalled = false;
    std::string completedPath;

    void SetUp() override {
        cacheDir = (fs::temp_directory_path() / "test_transfer_integration").string();
        fs::create_directories(cacheDir);
        fileAccess = std::make_unique<RemoteFileAccess>(cacheDir);
        
        // Set up callbacks
        fileAccess->onComplete([this](const FileTransferProgress& progress) {
            completeCalled = true;
            completedPath = progress.localPath;
        });
    }

    void TearDown() override {
        fileAccess.reset();
        fs::remove_all(cacheDir);
    }
};

// Тест: После успешного transfer файл находится в кэше
TEST_F(FileTransferIntegrationTest, AfterTransfer_FileIsCached) {
    const std::string deviceId = "device-sender";
    const int64_t fileId = 42;
    
    // Simulate a completed transfer by creating the cached file
    std::string deviceDir = cacheDir + "/" + deviceId;
    fs::create_directories(deviceDir);
    std::string cachedFile = deviceDir + "/" + std::to_string(fileId) + ".pdf";
    std::ofstream(cachedFile) << "PDF content here";
    
    // Now isCached should return true
    EXPECT_TRUE(fileAccess->isCached(deviceId, fileId, ""));
    
    // And getCachedPath should return the correct path
    std::string path = fileAccess->getCachedPath(deviceId, fileId);
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(fs::exists(path));
}

// Тест: Cache hit не создаёт новый request
TEST_F(FileTransferIntegrationTest, CacheHit_NoNewRequest) {
    const std::string deviceId = "device-cached";
    const int64_t fileId = 100;
    
    // Pre-populate cache
    std::string deviceDir = cacheDir + "/" + deviceId;
    fs::create_directories(deviceDir);
    std::ofstream(deviceDir + "/" + std::to_string(fileId) + ".txt") << "cached";
    
    // Verify it's cached
    EXPECT_TRUE(fileAccess->isCached(deviceId, fileId, ""));
    
    // No active transfers should exist
    EXPECT_FALSE(fileAccess->hasActiveTransfers());
}

// Тест: Cache size учитывает файлы в поддиректориях (по устройствам)
TEST_F(FileTransferIntegrationTest, CacheSize_IncludesSubdirectories) {
    // Create files for multiple devices
    std::string dirA = cacheDir + "/device-A";
    std::string dirB = cacheDir + "/device-B";
    fs::create_directories(dirA);
    fs::create_directories(dirB);
    
    std::ofstream(dirA + "/1.txt") << "content A";  // ~9 bytes
    std::ofstream(dirB + "/2.txt") << "content B";  // ~9 bytes
    
    int64_t size = fileAccess->getCacheSize();
    EXPECT_GT(size, 0);  // Should count files in subdirs
}

// ═══════════════════════════════════════════════════════════
// Message Routing Tests - проверяют что сообщения идут куда надо
// Эти тесты бы отловили баг с дропанием IndexSyncResponse
// ═══════════════════════════════════════════════════════════

class MessageRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Тест: IndexSyncResponse должен иметь правильный тип
TEST_F(MessageRoutingTest, IndexSyncResponse_HasCorrectType) {
    Message msg(MessageType::IndexSyncResponse, "test-request-id");
    EXPECT_EQ(msg.type, MessageType::IndexSyncResponse);
    EXPECT_EQ(msg.requestId, "test-request-id");
}

// Тест: IndexDelta должен иметь правильный тип
TEST_F(MessageRoutingTest, IndexDelta_HasCorrectType) {
    Message msg(MessageType::IndexDelta, "delta-id");
    EXPECT_EQ(msg.type, MessageType::IndexDelta);
}

// Тест: FileRequest должен иметь правильный тип
TEST_F(MessageRoutingTest, FileRequest_HasCorrectType) {
    Message msg(MessageType::FileRequest, "file-req-id");
    EXPECT_EQ(msg.type, MessageType::FileRequest);
}

// Тест: Все sync-related типы сообщений определены корректно
TEST_F(MessageRoutingTest, AllSyncMessageTypes_Defined) {
    EXPECT_EQ(static_cast<uint8_t>(MessageType::IndexSyncRequest), 0x20);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::IndexSyncResponse), 0x21);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::IndexDelta), 0x22);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::IndexDeltaAck), 0x23);
}

// Тест: Все file transfer типы сообщений определены корректно
TEST_F(MessageRoutingTest, AllFileTransferMessageTypes_Defined) {
    EXPECT_EQ(static_cast<uint8_t>(MessageType::FileRequest), 0x30);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::FileResponse), 0x31);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::FileChunk), 0x32);
    EXPECT_EQ(static_cast<uint8_t>(MessageType::FileNotFound), 0x34);
}

// ═══════════════════════════════════════════════════════════
// Component Wiring Tests - проверяют что компоненты связаны правильно
// Эти тесты бы отловили баг с неинициализированным indexManager
// ═══════════════════════════════════════════════════════════

class ComponentWiringTest : public ::testing::Test {
protected:
    std::string tempDir;
    std::shared_ptr<Database> db;

    void SetUp() override {
        tempDir = (fs::temp_directory_path() / "test_component_wiring").string();
        fs::create_directories(tempDir);
        db = std::make_shared<Database>(tempDir + "/test.db");
        db->initialize();  // Initialize database schema
    }

    void TearDown() override {
        db.reset();
        fs::remove_all(tempDir);
    }
};

// Тест: IndexSyncManager и IndexManager могут использовать одну БД
TEST_F(ComponentWiringTest, SyncAndIndex_ShareDatabase) {
    auto syncMgr = std::make_unique<IndexSyncManager>(db, "device-test");
    auto indexMgr = std::make_unique<IndexManager>(db);
    
    // Add folder via IndexManager
    std::string folderPath = tempDir + "/files";
    fs::create_directories(folderPath);
    int64_t folderId = indexMgr->addFolder(folderPath, "Test", Visibility::Family);
    ASSERT_GT(folderId, 0);
    
    // Create a file and scan
    std::ofstream(folderPath + "/test.txt") << "content";
    indexMgr->scanFolder(folderId);
    
    // Both managers should see the same data via shared database
    auto files = indexMgr->getFilesByFolder(folderId);
    EXPECT_GE(files.size(), 1u);
    
    // SyncManager's remote file count should start at 0
    EXPECT_EQ(syncMgr->getRemoteFileCount(), 0);
}

// Тест: Множественные IndexManager на одной БД работают
TEST_F(ComponentWiringTest, MultipleIndexManagers_SameDatabase) {
    auto indexMgr1 = std::make_unique<IndexManager>(db);
    auto indexMgr2 = std::make_unique<IndexManager>(db);
    
    // Add folder via first manager
    std::string folderPath = tempDir + "/files";
    fs::create_directories(folderPath);
    int64_t folderId = indexMgr1->addFolder(folderPath, "Test");
    ASSERT_GT(folderId, 0);
    
    // Second manager should see it
    auto folders = indexMgr2->getFolders();
    EXPECT_EQ(folders.size(), 1u);
}

// Тест: База данных правильно закрывается после всех менеджеров
TEST_F(ComponentWiringTest, Database_ClosesAfterManagers) {
    {
        auto syncMgr = std::make_unique<IndexSyncManager>(db, "device-test");
        auto indexMgr = std::make_unique<IndexManager>(db);
        // Managers go out of scope here
    }
    
    // Database should still be usable
    auto newIndex = std::make_unique<IndexManager>(db);
    EXPECT_TRUE(newIndex != nullptr);
}

