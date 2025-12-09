// test_loopback_integration.cpp — Full P2P integration tests using loopback
// Tests complete flows on single machine

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>

#include "familyvault/Network/NetworkManager.h"
#include "familyvault/Network/IndexSyncManager.h"
#include "familyvault/Network/RemoteFileAccess.h"
#include "familyvault/FamilyPairing.h"
#include "familyvault/SecureStorage.h"
#include "familyvault/IndexManager.h"
#include "familyvault/Database.h"

namespace fs = std::filesystem;
using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// Test Fixture
// ═══════════════════════════════════════════════════════════

// Helper to clear secure storage
static void clearLoopbackSecureStorage() {
    SecureStorage storage;
    storage.remove("family_secret");
    storage.remove("device_id");
}

class LoopbackIntegrationTest : public ::testing::Test {
protected:
    std::string tempDir;
    std::shared_ptr<Database> db;
    std::shared_ptr<SecureStorage> secureStorage;
    std::shared_ptr<FamilyPairing> pairing;
    
    void SetUp() override {
        clearLoopbackSecureStorage();
        
        tempDir = fs::temp_directory_path().string() + "/fv_loopback_" + 
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(tempDir);
        fs::create_directories(tempDir + "/files");
        
        db = std::make_shared<Database>(tempDir + "/test.db");
        db->initialize();
        
        secureStorage = std::make_shared<SecureStorage>();
        pairing = std::make_shared<FamilyPairing>(secureStorage);
        pairing->createFamily();
    }
    
    void TearDown() override {
        pairing.reset();
        secureStorage.reset();
        db.reset();
        clearLoopbackSecureStorage();
        fs::remove_all(tempDir);
    }
    
    void createTestFiles(int count) {
        for (int i = 0; i < count; i++) {
            std::string path = tempDir + "/files/file" + std::to_string(i) + ".txt";
            std::ofstream f(path);
            f << "Content of file " << i;
        }
    }
};

// ═══════════════════════════════════════════════════════════
// Network Startup Tests
// ═══════════════════════════════════════════════════════════

TEST_F(LoopbackIntegrationTest, NetworkCanStart) {
    NetworkManager network(pairing);
    
    EXPECT_TRUE(network.start(0));
    EXPECT_TRUE(network.isRunning());
    EXPECT_GT(network.getServerPort(), 0);
    
    network.stop();
}

// DISABLED: Two FamilyPairing instances share secure storage and conflict
TEST_F(LoopbackIntegrationTest, DISABLED_TwoNetworksCanStart) {
    // Create second device with separate storage
    auto secureStorage2 = std::make_shared<SecureStorage>();
    secureStorage2->store("device_id", std::vector<uint8_t>{'d','e','v','2'});
    auto pairing2 = std::make_shared<FamilyPairing>(secureStorage2);
    pairing2->createFamily();
    
    NetworkManager network1(pairing);
    NetworkManager network2(pairing2);
    
    EXPECT_TRUE(network1.start(0));
    EXPECT_TRUE(network2.start(0));
    
    EXPECT_NE(network1.getServerPort(), network2.getServerPort());
    
    network1.stop();
    network2.stop();
}

// ═══════════════════════════════════════════════════════════
// Index Sync Integration Tests
// ═══════════════════════════════════════════════════════════

// Fixed: getLocalChangesSince now uses COALESCE to inherit folder visibility
TEST_F(LoopbackIntegrationTest, IndexedFiles_AvailableForSync) {
    IndexManager indexMgr(db);
    IndexSyncManager syncMgr(db, pairing->getDeviceId());
    
    int64_t folderId = indexMgr.addFolder(tempDir + "/files", "TestFolder");
    ASSERT_GT(folderId, 0);
    
    createTestFiles(5);
    indexMgr.scanFolder(folderId);
    
    // Get changes since timestamp 0 (all files)
    auto changes = syncMgr.getLocalChangesSince(0);
    EXPECT_GE(changes.size(), 5u);
}

// Fixed: getLocalChangesSince now uses COALESCE to inherit folder visibility
TEST_F(LoopbackIntegrationTest, SyncPagination_HandlesLargeIndex) {
    IndexManager indexMgr(db);
    IndexSyncManager syncMgr(db, pairing->getDeviceId());
    
    int64_t folderId = indexMgr.addFolder(tempDir + "/files", "TestFolder");
    ASSERT_GT(folderId, 0);
    
    // Create more files than batch size (100)
    createTestFiles(150);
    indexMgr.scanFolder(folderId);
    
    // Should return all files (pagination should work)
    auto changes = syncMgr.getLocalChangesSince(0);
    EXPECT_GE(changes.size(), 150u);
}

// ═══════════════════════════════════════════════════════════
// File Transfer Cache Tests
// ═══════════════════════════════════════════════════════════

TEST_F(LoopbackIntegrationTest, FileCache_DeviceSpecificPaths) {
    std::string cacheDir = tempDir + "/cache";
    fs::create_directories(cacheDir);
    fs::create_directories(cacheDir + "/device-A");
    fs::create_directories(cacheDir + "/device-B");
    
    RemoteFileAccess fileAccess(cacheDir);
    
    // Create cache files manually with device-specific paths
    std::string pathA = cacheDir + "/device-A/100.jpg";
    std::string pathB = cacheDir + "/device-B/100.jpg";
    
    std::ofstream(pathA) << "content A";
    std::ofstream(pathB) << "content B";
    
    // isCached should find correct file per device
    EXPECT_TRUE(fileAccess.isCached("device-A", 100, ""));
    EXPECT_TRUE(fileAccess.isCached("device-B", 100, ""));
    
    // Different content, should both exist
    std::ifstream fA(pathA);
    std::ifstream fB(pathB);
    std::string contentA, contentB;
    std::getline(fA, contentA);
    std::getline(fB, contentB);
    EXPECT_EQ(contentA, "content A");
    EXPECT_EQ(contentB, "content B");
}

TEST_F(LoopbackIntegrationTest, FileCache_ChecksumValidation) {
    std::string cacheDir = tempDir + "/cache";
    fs::create_directories(cacheDir);
    fs::create_directories(cacheDir + "/device-A");
    
    RemoteFileAccess fileAccess(cacheDir);
    
    // Create a cached file
    std::string path = cacheDir + "/device-A/200.bin";
    std::ofstream(path) << "test content";
    
    // With no checksum, should return cached
    EXPECT_TRUE(fileAccess.isCached("device-A", 200, ""));
    
    // With wrong checksum, should return not cached (stale)
    EXPECT_FALSE(fileAccess.isCached("device-A", 200, "sha256:wrongchecksum"));
}

// ═══════════════════════════════════════════════════════════
// Cleanup Tests
// ═══════════════════════════════════════════════════════════

TEST_F(LoopbackIntegrationTest, NetworkShutdown_CleanupsProperly) {
    NetworkManager network(pairing);
    
    network.start(0);
    EXPECT_TRUE(network.isRunning());
    
    network.stop();
    EXPECT_FALSE(network.isRunning());
    
    // Should be able to restart
    network.start(0);
    EXPECT_TRUE(network.isRunning());
    
    network.stop();
}
