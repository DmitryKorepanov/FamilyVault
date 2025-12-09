// test_ffi_initialization.cpp — Tests for FFI initialization order and wiring
// These tests catch bugs where components exist but are never properly initialized

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

#include "familyvault/familyvault_c.h"

namespace fs = std::filesystem;

class FFIInitializationTest : public ::testing::Test {
protected:
    std::string tempDir;
    FVDatabase db = nullptr;
    FVSecureStorage secure = nullptr;
    FVFamilyPairing pairing = nullptr;
    FVNetworkManager network = nullptr;
    
    void SetUp() override {
        tempDir = fs::temp_directory_path().string() + "/fv_init_test_" + 
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(tempDir);
        
        // Setup pairing (required for network)
        FVError err;
        std::string dbPath = tempDir + "/test.db";
        db = fv_database_open(dbPath.c_str(), &err);
        ASSERT_NE(db, nullptr);
        fv_database_initialize(db);
        
        secure = fv_secure_create();
        ASSERT_NE(secure, nullptr);
        
        pairing = fv_pairing_create(secure);
        ASSERT_NE(pairing, nullptr);
        
        fv_pairing_create_family(pairing);
        
        network = fv_network_create(pairing);
        ASSERT_NE(network, nullptr);
    }
    
    void TearDown() override {
        if (network) fv_network_destroy(network);
        if (pairing) fv_pairing_destroy(pairing);
        if (secure) fv_secure_destroy(secure);
        if (db) fv_database_close(db);
        fs::remove_all(tempDir);
    }
};

// ═══════════════════════════════════════════════════════════
// Sync requires setDatabase() first
// ═══════════════════════════════════════════════════════════

TEST_F(FFIInitializationTest, SyncFailsBeforeSetDatabase) {
    // Sync should fail because setDatabase was never called
    auto result = fv_network_request_sync(network, "some-device", false);
    EXPECT_EQ(result, FV_ERROR_INVALID_ARGUMENT);
}

// Fixed: Now properly uses DatabaseHolder to get shared Database pointer
TEST_F(FFIInitializationTest, SetDatabase_MakesSyncPossible) {
    // Setup database for sync
    auto result = fv_network_set_database(network, db, "device-test");
    EXPECT_EQ(result, 0);  // FV_SUCCESS = 0
    
    // Now sync should NOT fail with INVALID_ARGUMENT
    // (will fail with NETWORK because not connected, which is expected)
    result = fv_network_request_sync(network, "device-2", false);
    EXPECT_NE(result, FV_ERROR_INVALID_ARGUMENT);
}

// ═══════════════════════════════════════════════════════════
// File transfer requires setCacheDir() first
// ═══════════════════════════════════════════════════════════

TEST_F(FFIInitializationTest, FileRequestFailsBeforeSetCacheDir) {
    auto result = fv_network_request_file(network, "device", 1, "file.txt", 100, "");
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(fv_last_error(), FV_ERROR_INVALID_ARGUMENT);
}

TEST_F(FFIInitializationTest, SetCacheDir_MakesFileTransferPossible) {
    std::string cacheDir = tempDir + "/cache";
    auto result = fv_network_set_cache_dir(network, cacheDir.c_str());
    EXPECT_EQ(result, 0);  // FV_SUCCESS = 0
    
    // Now request should fail with NETWORK error (not connected)
    // instead of INVALID_ARGUMENT (not configured)
    auto requestId = fv_network_request_file(network, "device", 1, "file.txt", 100, "");
    EXPECT_EQ(requestId, nullptr);
    EXPECT_EQ(fv_last_error(), FV_ERROR_NETWORK);
}

TEST_F(FFIInitializationTest, SetCacheDir_CreatesDirectory) {
    std::string cacheDir = tempDir + "/new_cache";
    EXPECT_FALSE(fs::exists(cacheDir));
    
    fv_network_set_cache_dir(network, cacheDir.c_str());
    
    EXPECT_TRUE(fs::exists(cacheDir));
}

// ═══════════════════════════════════════════════════════════
// Connected devices empty initially
// ═══════════════════════════════════════════════════════════

TEST_F(FFIInitializationTest, GetConnectedDevices_EmptyInitially) {
    char* json = fv_network_get_connected_devices(network);
    ASSERT_NE(json, nullptr);
    
    std::string jsonStr(json);
    EXPECT_EQ(jsonStr, "[]");  // Empty array
    
    fv_free_string(json);
}
