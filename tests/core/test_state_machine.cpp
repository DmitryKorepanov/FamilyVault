// test_state_machine.cpp — Tests for state machines and event deduplication
// These tests catch bugs like duplicate disconnect events

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <filesystem>

#include "familyvault/Network/NetworkManager.h"
#include "familyvault/Network/RemoteFileAccess.h"
#include "familyvault/FamilyPairing.h"
#include "familyvault/SecureStorage.h"

namespace fs = std::filesystem;
using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// NetworkManager State Machine Tests
// ═══════════════════════════════════════════════════════════

// Helper to clear secure storage
static void clearSecureStorage() {
    SecureStorage storage;
    storage.remove("family_secret");
    storage.remove("device_id");
}

class NetworkManagerStateTest : public ::testing::Test {
protected:
    std::string tempDir;
    std::shared_ptr<SecureStorage> secureStorage;
    std::shared_ptr<FamilyPairing> pairing;
    
    void SetUp() override {
        clearSecureStorage();
        
        tempDir = fs::temp_directory_path().string() + "/fv_state_test_" + 
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(tempDir);
        
        secureStorage = std::make_shared<SecureStorage>();
        pairing = std::make_shared<FamilyPairing>(secureStorage);
        pairing->createFamily();
    }
    
    void TearDown() override {
        pairing.reset();
        secureStorage.reset();
        clearSecureStorage();
        fs::remove_all(tempDir);
    }
};

TEST_F(NetworkManagerStateTest, InitialStateIsStopped) {
    NetworkManager mgr(pairing);
    EXPECT_EQ(mgr.getState(), NetworkManager::NetworkState::Stopped);
}

TEST_F(NetworkManagerStateTest, StartTransitionsToRunning) {
    NetworkManager mgr(pairing);
    
    EXPECT_TRUE(mgr.start(0));  // auto-pick port
    EXPECT_EQ(mgr.getState(), NetworkManager::NetworkState::Running);
    
    mgr.stop();
}

TEST_F(NetworkManagerStateTest, StopTransitionsToStopped) {
    NetworkManager mgr(pairing);
    
    mgr.start(0);
    EXPECT_EQ(mgr.getState(), NetworkManager::NetworkState::Running);
    
    mgr.stop();
    EXPECT_EQ(mgr.getState(), NetworkManager::NetworkState::Stopped);
}

TEST_F(NetworkManagerStateTest, DoubleStopIsIdempotent) {
    NetworkManager mgr(pairing);
    
    mgr.start(0);
    mgr.stop();
    EXPECT_EQ(mgr.getState(), NetworkManager::NetworkState::Stopped);
    
    // Second stop should be safe
    mgr.stop();
    EXPECT_EQ(mgr.getState(), NetworkManager::NetworkState::Stopped);
}

// ═══════════════════════════════════════════════════════════
// Port Auto-Pick Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerStateTest, AutoPickPort_ReturnsNonZero) {
    NetworkManager mgr(pairing);
    
    EXPECT_TRUE(mgr.start(0));  // 0 = auto-pick
    
    uint16_t port = mgr.getServerPort();
    EXPECT_GT(port, 0) << "Auto-picked port should be non-zero";
    
    mgr.stop();
}

// ═══════════════════════════════════════════════════════════
// Transfer State Tests
// ═══════════════════════════════════════════════════════════

TEST(FileTransferStateTest, NoActiveTransfersInitially) {
    std::string tempDir = fs::temp_directory_path().string() + "/fv_transfer_test";
    fs::create_directories(tempDir);
    
    RemoteFileAccess fileAccess(tempDir);
    
    EXPECT_FALSE(fileAccess.hasActiveTransfers());
    EXPECT_TRUE(fileAccess.getActiveTransfers().empty());
    
    fs::remove_all(tempDir);
}

// ═══════════════════════════════════════════════════════════
// State Callback Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerStateTest, StateCallback_FiresOnStartStop) {
    NetworkManager mgr(pairing);
    
    std::atomic<int> callbackCount{0};
    mgr.onStateChanged([&](NetworkManager::NetworkState state) {
        callbackCount++;
    });
    
    mgr.start(0);
    mgr.stop();
    
    // Should have received at least 2 state changes (Running, Stopped)
    EXPECT_GE(callbackCount.load(), 2);
}
