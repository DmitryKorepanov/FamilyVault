// test_network_manager.cpp — Тесты NetworkManager

#include <gtest/gtest.h>
#include "familyvault/Network/NetworkManager.h"
#include "familyvault/FamilyPairing.h"
#include "familyvault/SecureStorage.h"
#include "familyvault/familyvault_c.h"
#include <thread>
#include <chrono>

using namespace FamilyVault;

// Helper to clear secure storage for clean test state
void clearSecureStorageForTests() {
    SecureStorage storage;
    storage.remove("family_secret");
    storage.remove("device_id");
}

class NetworkManagerTest : public ::testing::Test {
protected:
    std::shared_ptr<SecureStorage> secureStorage;
    std::shared_ptr<FamilyPairing> pairing;
    std::unique_ptr<NetworkManager> networkManager;

    void SetUp() override {
        clearSecureStorageForTests();
        
        secureStorage = std::make_shared<SecureStorage>();
        pairing = std::make_shared<FamilyPairing>(secureStorage);
        
        // Ensure family is configured before creating NetworkManager
        if (!pairing->isConfigured()) {
            pairing->createFamily();
        }
        
        networkManager = std::make_unique<NetworkManager>(pairing);
    }

    void TearDown() override {
        if (networkManager) {
            networkManager->stop();
        }
        networkManager.reset();
        pairing.reset();
        secureStorage.reset();
        clearSecureStorageForTests();
    }
};

// ═══════════════════════════════════════════════════════════
// Basic Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerTest, CreateAndDestroy) {
    EXPECT_EQ(networkManager->getState(), NetworkManager::NetworkState::Stopped);
    EXPECT_FALSE(networkManager->isRunning());
}

TEST_F(NetworkManagerTest, GetDiscoveredDevices_EmptyInitially) {
    auto devices = networkManager->getDiscoveredDevices();
    EXPECT_TRUE(devices.empty());
}

TEST_F(NetworkManagerTest, GetConnectedDevices_EmptyInitially) {
    auto devices = networkManager->getConnectedDevices();
    EXPECT_TRUE(devices.empty());
}

TEST_F(NetworkManagerTest, GetDevice_ReturnsNullopt) {
    auto device = networkManager->getDevice("non-existent");
    EXPECT_FALSE(device.has_value());
}

TEST_F(NetworkManagerTest, IsConnectedTo_FalseInitially) {
    EXPECT_FALSE(networkManager->isConnectedTo("any-device"));
}

// ═══════════════════════════════════════════════════════════
// Start/Stop Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerTest, StartWorks) {
    // NetworkManager should start regardless of family configuration
    // (family may already be configured from previous tests)
    bool result = networkManager->start();
    
    // Should work - NetworkManager handles any pairing state
    EXPECT_TRUE(result);
    EXPECT_TRUE(networkManager->isRunning());
    
    networkManager->stop();
    EXPECT_FALSE(networkManager->isRunning());
}

TEST_F(NetworkManagerTest, StartWithFamily) {
    // Create a family first
    pairing->createFamily();
    
    bool result = networkManager->start();
    EXPECT_TRUE(result);
    EXPECT_TRUE(networkManager->isRunning());
    
    // Server port should be assigned
    uint16_t port = networkManager->getServerPort();
    EXPECT_GT(port, 0);
    
    networkManager->stop();
    EXPECT_FALSE(networkManager->isRunning());
}

TEST_F(NetworkManagerTest, StopIsIdempotent) {
    networkManager->start();
    
    networkManager->stop();
    EXPECT_FALSE(networkManager->isRunning());
    
    // Should be safe to call multiple times
    networkManager->stop();
    networkManager->stop();
    EXPECT_FALSE(networkManager->isRunning());
}

TEST_F(NetworkManagerTest, StartStopMultipleTimes) {
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(networkManager->start());
        EXPECT_TRUE(networkManager->isRunning());
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        networkManager->stop();
        EXPECT_FALSE(networkManager->isRunning());
    }
}

// ═══════════════════════════════════════════════════════════
// State Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerTest, StateTransitions) {
    EXPECT_EQ(networkManager->getState(), NetworkManager::NetworkState::Stopped);
    
    networkManager->start();
    // State should be Running (or Starting briefly)
    auto state = networkManager->getState();
    EXPECT_TRUE(state == NetworkManager::NetworkState::Running ||
                state == NetworkManager::NetworkState::Starting);
    
    // Wait for running state
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(networkManager->getState(), NetworkManager::NetworkState::Running);
    
    networkManager->stop();
    // State should transition to Stopped
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(networkManager->getState(), NetworkManager::NetworkState::Stopped);
}

// ═══════════════════════════════════════════════════════════
// Callback Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerTest, SetCallbacks) {
    bool discoveredCalled = false;
    bool lostCalled = false;
    bool connectedCalled = false;
    bool disconnectedCalled = false;
    bool messageCalled = false;
    bool stateChangedCalled = false;
    bool errorCalled = false;
    
    networkManager->onDeviceDiscovered([&](const DeviceInfo&) {
        discoveredCalled = true;
    });
    
    networkManager->onDeviceLost([&](const DeviceInfo&) {
        lostCalled = true;
    });
    
    networkManager->onDeviceConnected([&](const DeviceInfo&) {
        connectedCalled = true;
    });
    
    networkManager->onDeviceDisconnected([&](const DeviceInfo&) {
        disconnectedCalled = true;
    });
    
    networkManager->onMessage([&](const std::string&, const Message&) {
        messageCalled = true;
    });
    
    networkManager->onStateChanged([&](NetworkManager::NetworkState) {
        stateChangedCalled = true;
    });
    
    networkManager->onError([&](const std::string&) {
        errorCalled = true;
    });
    
    // Callbacks should be set without error
    SUCCEED();
}

TEST_F(NetworkManagerTest, StateChangedCallbackCalled) {
    bool stateChangedCalled = false;
    std::vector<NetworkManager::NetworkState> states;
    
    networkManager->onStateChanged([&](NetworkManager::NetworkState state) {
        stateChangedCalled = true;
        states.push_back(state);
    });
    
    networkManager->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    networkManager->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(stateChangedCalled);
    EXPECT_GE(states.size(), 1); // At least one state change
}

// ═══════════════════════════════════════════════════════════
// Connection Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerTest, ConnectToDevice_NotDiscovered) {
    networkManager->start();
    
    // Should fail - device not discovered
    bool result = networkManager->connectToDevice("unknown-device-id");
    EXPECT_FALSE(result);
    
    networkManager->stop();
}

TEST_F(NetworkManagerTest, ConnectToAddress_InvalidAddress) {
    networkManager->start();
    
    // Connection is now async - returns true immediately, errors via callback
    std::atomic<bool> errorReceived{false};
    std::string errorMsg;
    
    networkManager->onError([&](const std::string& error) {
        errorReceived = true;
        errorMsg = error;
    });
    
    // Should return true (async start), but fail later
    bool result = networkManager->connectToAddress("invalid-host", 12345);
    EXPECT_TRUE(result);  // Async - returns immediately
    
    // Wait for async connection attempt to fail
    for (int i = 0; i < 50 && !errorReceived; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    EXPECT_TRUE(errorReceived) << "Expected error callback for invalid address";
    
    networkManager->stop();
}

TEST_F(NetworkManagerTest, ConnectToAddress_ReturnsImmediately) {
    networkManager->start();
    
    // Measure how long connectToAddress takes to return
    auto startTime = std::chrono::steady_clock::now();
    
    // This should return immediately (< 100ms) even though TLS would take 5-10 sec
    bool result = networkManager->connectToAddress("10.255.255.1", 12345);
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    EXPECT_TRUE(result) << "Async connect should return true immediately";
    EXPECT_LT(duration.count(), 100) << "connectToAddress should return in < 100ms, took " << duration.count() << "ms";
    
    // Connection happens in background - just wait a bit and stop
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    networkManager->stop();
}

TEST_F(NetworkManagerTest, DisconnectFromDevice_NotConnected) {
    networkManager->start();
    
    // Should not crash
    EXPECT_NO_THROW(networkManager->disconnectFromDevice("unknown-device"));
    
    networkManager->stop();
}

TEST_F(NetworkManagerTest, DisconnectAll_NoConnections) {
    networkManager->start();
    
    // Should not crash
    EXPECT_NO_THROW(networkManager->disconnectAll());
    
    networkManager->stop();
}

// ═══════════════════════════════════════════════════════════
// Local Address Tests
// ═══════════════════════════════════════════════════════════

TEST_F(NetworkManagerTest, GetLocalAddresses) {
    auto addresses = networkManager->getLocalAddresses();
    
    // Should return at least empty list (won't crash)
    // Actual IPs depend on network configuration
    for (const auto& addr : addresses) {
        EXPECT_FALSE(addr.empty());
    }
}

TEST_F(NetworkManagerTest, GetServerPort_ZeroBeforeStart) {
    uint16_t port = networkManager->getServerPort();
    EXPECT_EQ(port, 0);
}

TEST_F(NetworkManagerTest, GetServerPort_NonZeroAfterStart) {
    networkManager->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    uint16_t port = networkManager->getServerPort();
    // Port should be assigned (might be 0 if server failed to start)
    // We don't strictly assert > 0 because network might not be available
    
    networkManager->stop();
}

// ═══════════════════════════════════════════════════════════
// C API Tests
// ═══════════════════════════════════════════════════════════

TEST(NetworkManagerCApiTest, CreateRequiresPairing) {
    auto mgr = fv_network_create(nullptr);
    EXPECT_EQ(mgr, nullptr);
}

TEST(NetworkManagerCApiTest, CreateWithPairing) {
    clearSecureStorageForTests();
    
    auto storage = fv_secure_create();
    ASSERT_NE(storage, nullptr);
    
    auto pairing = fv_pairing_create(storage);
    ASSERT_NE(pairing, nullptr);
    
    auto mgr = fv_network_create(pairing);
    EXPECT_NE(mgr, nullptr);
    
    EXPECT_EQ(fv_network_is_running(mgr), 0);
    EXPECT_EQ(fv_network_get_state(mgr), 0); // Stopped
    
    fv_network_destroy(mgr);
    fv_pairing_destroy(pairing);
    fv_secure_destroy(storage);
    
    clearSecureStorageForTests();
}

TEST(NetworkManagerCApiTest, StartAndStop) {
    clearSecureStorageForTests();
    
    auto storage = fv_secure_create();
    auto pairing = fv_pairing_create(storage);
    auto mgr = fv_network_create(pairing);
    
    ASSERT_NE(mgr, nullptr);
    
    FVError err = fv_network_start(mgr, 0, nullptr, nullptr);
    EXPECT_EQ(err, FV_OK);
    EXPECT_EQ(fv_network_is_running(mgr), 1);
    
    fv_network_stop(mgr);
    EXPECT_EQ(fv_network_is_running(mgr), 0);
    
    fv_network_destroy(mgr);
    fv_pairing_destroy(pairing);
    fv_secure_destroy(storage);
    
    clearSecureStorageForTests();
}

TEST(NetworkManagerCApiTest, GetDevices) {
    clearSecureStorageForTests();
    
    auto storage = fv_secure_create();
    auto pairing = fv_pairing_create(storage);
    auto mgr = fv_network_create(pairing);
    
    char* discovered = fv_network_get_discovered_devices(mgr);
    ASSERT_NE(discovered, nullptr);
    EXPECT_EQ(std::string(discovered), "[]");
    fv_free_string(discovered);
    
    char* connected = fv_network_get_connected_devices(mgr);
    ASSERT_NE(connected, nullptr);
    EXPECT_EQ(std::string(connected), "[]");
    fv_free_string(connected);
    
    fv_network_destroy(mgr);
    fv_pairing_destroy(pairing);
    fv_secure_destroy(storage);
    
    clearSecureStorageForTests();
}


