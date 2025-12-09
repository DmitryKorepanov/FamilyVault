// test_network.cpp — Тесты Network Discovery

#include <gtest/gtest.h>
#include "familyvault/Network/Discovery.h"
#include "familyvault/familyvault_c.h"
#include <thread>
#include <chrono>

using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// NetworkDiscovery Tests
// ═══════════════════════════════════════════════════════════

TEST(NetworkDiscoveryTest, GetLocalIpAddresses) {
    auto ips = NetworkDiscovery::getLocalIpAddresses();
    
    // Should return at least one IP (unless no network)
    // We don't assert here because CI might not have network
    for (const auto& ip : ips) {
        EXPECT_FALSE(ip.empty());
        EXPECT_NE(ip, "127.0.0.1"); // Should not include loopback
    }
}

TEST(NetworkDiscoveryTest, GetBroadcastAddresses) {
    auto broadcasts = NetworkDiscovery::getBroadcastAddresses();
    
    // Should always return at least 255.255.255.255 as fallback
    EXPECT_GE(broadcasts.size(), 1);
    
    for (const auto& addr : broadcasts) {
        EXPECT_FALSE(addr.empty());
    }
}

TEST(NetworkDiscoveryTest, CreateAndDestroy) {
    NetworkDiscovery discovery;
    EXPECT_FALSE(discovery.isRunning());
    EXPECT_EQ(discovery.getDeviceCount(), 0);
}

TEST(NetworkDiscoveryTest, StartAndStop) {
    NetworkDiscovery discovery;
    
    DeviceInfo thisDevice;
    thisDevice.deviceId = "test-device-123";
    thisDevice.deviceName = "Test Device";
    thisDevice.deviceType = DeviceType::Desktop;
    thisDevice.servicePort = 45678;
    
    EXPECT_TRUE(discovery.start(thisDevice));
    EXPECT_TRUE(discovery.isRunning());
    
    // Give it a moment to start threads
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    discovery.stop();
    EXPECT_FALSE(discovery.isRunning());
}

TEST(NetworkDiscoveryTest, DoubleStartIsIdempotent) {
    NetworkDiscovery discovery;
    
    DeviceInfo thisDevice;
    thisDevice.deviceId = "test-device-456";
    thisDevice.deviceName = "Test Device 2";
    thisDevice.deviceType = DeviceType::Mobile;
    thisDevice.servicePort = 45678;
    
    EXPECT_TRUE(discovery.start(thisDevice));
    EXPECT_TRUE(discovery.start(thisDevice)); // Should return true (already running)
    EXPECT_TRUE(discovery.isRunning());
    
    discovery.stop();
    discovery.stop(); // Should be safe to call multiple times
    EXPECT_FALSE(discovery.isRunning());
}

TEST(NetworkDiscoveryTest, GetDevicesReturnsEmpty) {
    NetworkDiscovery discovery;
    
    DeviceInfo thisDevice;
    thisDevice.deviceId = "test-device-789";
    thisDevice.deviceName = "Test Device 3";
    thisDevice.deviceType = DeviceType::Desktop;
    thisDevice.servicePort = 45678;
    
    discovery.start(thisDevice);
    
    // Initially should have no devices
    auto devices = discovery.getDevices();
    EXPECT_TRUE(devices.empty());
    
    discovery.stop();
}

TEST(NetworkDiscoveryTest, GetDeviceByIdReturnsNullopt) {
    NetworkDiscovery discovery;
    
    auto device = discovery.getDevice("non-existent-id");
    EXPECT_FALSE(device.has_value());
}

// ═══════════════════════════════════════════════════════════
// C API Tests
// ═══════════════════════════════════════════════════════════

TEST(NetworkDiscoveryCApiTest, CreateAndDestroy) {
    auto discovery = fv_discovery_create();
    EXPECT_NE(discovery, nullptr);
    
    EXPECT_EQ(fv_discovery_is_running(discovery), 0);
    EXPECT_EQ(fv_discovery_get_device_count(discovery), 0);
    
    fv_discovery_destroy(discovery);
}

TEST(NetworkDiscoveryCApiTest, GetLocalIps) {
    char* ips_json = fv_discovery_get_local_ips();
    
    if (ips_json) {
        // Should be valid JSON array
        std::string json_str(ips_json);
        EXPECT_EQ(json_str[0], '[');
        EXPECT_EQ(json_str.back(), ']');
        fv_free_string(ips_json);
    }
}

TEST(NetworkDiscoveryCApiTest, GetDevicesReturnsEmptyArray) {
    auto discovery = fv_discovery_create();
    ASSERT_NE(discovery, nullptr);
    
    char* devices_json = fv_discovery_get_devices(discovery);
    ASSERT_NE(devices_json, nullptr);
    
    std::string json_str(devices_json);
    EXPECT_EQ(json_str, "[]");
    
    fv_free_string(devices_json);
    fv_discovery_destroy(discovery);
}

TEST(NetworkDiscoveryCApiTest, StartRequiresPairing) {
    auto discovery = fv_discovery_create();
    ASSERT_NE(discovery, nullptr);
    
    // Start without pairing should fail
    FVError err = fv_discovery_start(discovery, nullptr, nullptr, nullptr);
    EXPECT_EQ(err, FV_ERROR_INVALID_ARGUMENT);
    
    fv_discovery_destroy(discovery);
}

TEST(NetworkDiscoveryCApiTest, StartWithPairing) {
    // Create secure storage and pairing
    auto storage = fv_secure_create();
    ASSERT_NE(storage, nullptr);
    
    auto pairing = fv_pairing_create(storage);
    ASSERT_NE(pairing, nullptr);
    
    auto discovery = fv_discovery_create();
    ASSERT_NE(discovery, nullptr);
    
    // Start should succeed with valid pairing
    FVError err = fv_discovery_start(discovery, pairing, nullptr, nullptr);
    EXPECT_EQ(err, FV_OK);
    EXPECT_EQ(fv_discovery_is_running(discovery), 1);
    
    fv_discovery_stop(discovery);
    EXPECT_EQ(fv_discovery_is_running(discovery), 0);
    
    fv_discovery_destroy(discovery);
    fv_pairing_destroy(pairing);
    fv_secure_destroy(storage);
}

TEST(NetworkDiscoveryCApiTest, GetDeviceNotFound) {
    auto discovery = fv_discovery_create();
    ASSERT_NE(discovery, nullptr);
    
    char* device_json = fv_discovery_get_device(discovery, "non-existent-id");
    EXPECT_EQ(device_json, nullptr);
    
    // Should not set error for "not found"
    FVError err = fv_last_error();
    EXPECT_EQ(err, FV_OK);
    
    fv_discovery_destroy(discovery);
}


