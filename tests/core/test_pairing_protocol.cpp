// test_pairing_protocol.cpp — Тесты для P2P pairing протокола
// Проверяет обмен family_secret между двумя устройствами

#include <gtest/gtest.h>
#include "familyvault/SecureStorage.h"
#include "familyvault/FamilyPairing.h"
#include "familyvault/Network/PairingServer.h"
#include "familyvault/Network/NetworkProtocol.h"
#include <thread>
#include <chrono>
#include <memory>

using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// PairingServer Tests
// ═══════════════════════════════════════════════════════════

class PairingServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        server = std::make_unique<PairingServer>();
    }

    void TearDown() override {
        server->stop();
    }

    std::unique_ptr<PairingServer> server;
};

TEST_F(PairingServerTest, StartStop) {
    EXPECT_FALSE(server->isRunning());
    
    ASSERT_TRUE(server->start(0)); // Random port
    EXPECT_TRUE(server->isRunning());
    EXPECT_GT(server->getPort(), 0);
    
    server->stop();
    EXPECT_FALSE(server->isRunning());
}

TEST_F(PairingServerTest, StartOnSpecificPort) {
    // Use a high port that's likely available
    uint16_t testPort = 45681;
    
    ASSERT_TRUE(server->start(testPort));
    EXPECT_TRUE(server->isRunning());
    EXPECT_EQ(server->getPort(), testPort);
}

// ═══════════════════════════════════════════════════════════
// PairingClient Tests
// ═══════════════════════════════════════════════════════════

TEST(PairingClientTest, ConnectToNonExistentServer) {
    PairingClient client;
    
    PairingRequestPayload request;
    request.pin = "123456";
    request.deviceId = "test-device";
    request.deviceName = "Test Device";
    request.deviceType = DeviceType::Desktop;
    
    // Should fail to connect
    auto result = client.pair("127.0.0.1", 45699, request, 1000);
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════
// Pairing Protocol Tests (Server + Client)
// ═══════════════════════════════════════════════════════════

class PairingProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // On a single machine, both SecureStorage instances use the same
        // underlying storage (registry/file). We need to simulate two
        // separate devices by carefully managing storage state.
        
        storage = std::make_shared<SecureStorage>();
        
        // Clear all keys first
        clearAllKeys();
    }

    void TearDown() override {
        if (pairingA) pairingA->stopPairingServer();
        clearAllKeys();
    }
    
    void clearAllKeys() {
        storage->remove(SecureStorage::KEY_FAMILY_SECRET);
        storage->remove(SecureStorage::KEY_DEVICE_ID);
        storage->remove(SecureStorage::KEY_DEVICE_NAME);
    }
    
    // Helper: create Device A (server) with a fresh family
    void createDeviceA() {
        clearAllKeys();
        pairingA = std::make_unique<FamilyPairing>(storage);
        pairingA->setDeviceName("Device A (Server)");
    }
    
    // Helper: create Device B (client) that should not have family_secret
    // This simulates a fresh device on another machine
    void createDeviceB() {
        // Clear family_secret so B appears unconfigured
        storage->remove(SecureStorage::KEY_FAMILY_SECRET);
        // Clear device ID so B gets a new identity
        storage->remove(SecureStorage::KEY_DEVICE_ID);
        storage->remove(SecureStorage::KEY_DEVICE_NAME);
        
        pairingB = std::make_unique<FamilyPairing>(storage);
        pairingB->setDeviceName("Device B (Client)");
    }

    std::shared_ptr<SecureStorage> storage;
    std::unique_ptr<FamilyPairing> pairingA;
    std::unique_ptr<FamilyPairing> pairingB;
};

TEST_F(PairingProtocolTest, FullPairingFlow) {
    // 1. Device A creates a family
    createDeviceA();
    auto pairingInfo = pairingA->createFamily();
    ASSERT_TRUE(pairingInfo.has_value());
    EXPECT_TRUE(pairingA->isConfigured());
    
    std::string pin = pairingInfo->pin;
    EXPECT_EQ(pin.length(), 6);
    
    // Save the PSK before we mess with storage
    auto pskA = pairingA->derivePsk();
    ASSERT_TRUE(pskA.has_value());
    
    // 2. Device A starts pairing server
    ASSERT_TRUE(pairingA->startPairingServer(0)); // Random port
    EXPECT_TRUE(pairingA->isPairingServerRunning());
    
    uint16_t serverPort = pairingA->getPairingServerPort();
    EXPECT_GT(serverPort, 0);
    
    // Small delay to ensure server is ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 3. Create Device B (simulates fresh device on another machine)
    createDeviceB();
    EXPECT_FALSE(pairingB->isConfigured());
    
    // 4. Device B joins by PIN
    JoinResult result = pairingB->joinByPin(pin, "127.0.0.1", serverPort);
    EXPECT_EQ(result, JoinResult::Success);
    
    // 5. Device B is now configured
    EXPECT_TRUE(pairingB->isConfigured());
    
    // 6. Both devices should derive the same PSK
    auto pskB = pairingB->derivePsk();
    
    ASSERT_TRUE(pskB.has_value());
    EXPECT_EQ(*pskA, *pskB);
    
    // 7. Stop server
    pairingA->stopPairingServer();
    EXPECT_FALSE(pairingA->isPairingServerRunning());
}

TEST_F(PairingProtocolTest, InvalidPinRejected) {
    // 1. Device A creates a family and starts server
    createDeviceA();
    auto pairingInfo = pairingA->createFamily();
    ASSERT_TRUE(pairingInfo.has_value());
    ASSERT_TRUE(pairingA->startPairingServer(0));
    
    uint16_t serverPort = pairingA->getPairingServerPort();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 2. Create Device B
    createDeviceB();
    EXPECT_FALSE(pairingB->isConfigured());
    
    // 3. Device B tries with wrong PIN
    JoinResult result = pairingB->joinByPin("000000", "127.0.0.1", serverPort);
    EXPECT_EQ(result, JoinResult::InvalidPin);
    
    // 4. Device B should NOT be configured
    EXPECT_FALSE(pairingB->isConfigured());
}

TEST_F(PairingProtocolTest, AlreadyConfiguredRejected) {
    // 1. Device A creates a family and starts server
    createDeviceA();
    auto pairingInfo = pairingA->createFamily();
    ASSERT_TRUE(pairingInfo.has_value());
    ASSERT_TRUE(pairingA->startPairingServer(0));
    
    uint16_t serverPort = pairingA->getPairingServerPort();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 2. Create Device B, but then configure it with its own family
    createDeviceB();
    auto infoB = pairingB->createFamily();
    ASSERT_TRUE(infoB.has_value());
    EXPECT_TRUE(pairingB->isConfigured());
    
    // 3. Device B tries to join but is already configured
    JoinResult result = pairingB->joinByPin(pairingInfo->pin, "127.0.0.1", serverPort);
    EXPECT_EQ(result, JoinResult::AlreadyConfigured);
}

TEST_F(PairingProtocolTest, ExpiredPairingRejected) {
    // 1. Device A creates a family
    createDeviceA();
    auto pairingInfo = pairingA->createFamily();
    ASSERT_TRUE(pairingInfo.has_value());
    std::string pin = pairingInfo->pin;
    
    // 2. Cancel the pairing session
    pairingA->cancelPairing();
    EXPECT_FALSE(pairingA->hasPendingPairing());
    
    // 3. Start server
    ASSERT_TRUE(pairingA->startPairingServer(0));
    uint16_t serverPort = pairingA->getPairingServerPort();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 4. Create Device B
    createDeviceB();
    EXPECT_FALSE(pairingB->isConfigured());
    
    // 5. Device B tries to join but pairing session was cancelled
    JoinResult result = pairingB->joinByPin(pin, "127.0.0.1", serverPort);
    EXPECT_EQ(result, JoinResult::Expired);
    EXPECT_FALSE(pairingB->isConfigured());
}

TEST_F(PairingProtocolTest, PinSessionRemainsActiveAfterJoin) {
    // Test that the pairing session remains active after one device joins
    // (allowing multiple devices to join with the same PIN)
    // Note: On a single machine with shared storage, we can't truly test multiple
    // devices, but we can verify the session stays active.
    
    // 1. Device A creates a family and starts server
    createDeviceA();
    auto pairingInfo = pairingA->createFamily();
    ASSERT_TRUE(pairingInfo.has_value());
    std::string pin = pairingInfo->pin;
    auto pskA = pairingA->derivePsk();
    ASSERT_TRUE(pskA.has_value());
    
    ASSERT_TRUE(pairingA->startPairingServer(0));
    uint16_t serverPort = pairingA->getPairingServerPort();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 2. Device B joins
    createDeviceB();
    JoinResult resultB = pairingB->joinByPin(pin, "127.0.0.1", serverPort);
    EXPECT_EQ(resultB, JoinResult::Success);
    EXPECT_TRUE(pairingB->isConfigured());
    
    auto pskB = pairingB->derivePsk();
    ASSERT_TRUE(pskB.has_value());
    EXPECT_EQ(*pskA, *pskB);
    
    // 3. Verify pairing session is still active on A
    EXPECT_TRUE(pairingA->hasPendingPairing());
}

// ═══════════════════════════════════════════════════════════
// Message Serialization Tests
// ═══════════════════════════════════════════════════════════

TEST(PairingMessageTest, PairingRequestSerialization) {
    PairingRequestPayload request;
    request.pin = "123456";
    request.deviceId = "test-uuid-1234";
    request.deviceName = "Test Device";
    request.deviceType = DeviceType::Mobile;
    
    std::string json = request.toJson();
    EXPECT_FALSE(json.empty());
    
    auto parsed = PairingRequestPayload::fromJson(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->pin, "123456");
    EXPECT_EQ(parsed->deviceId, "test-uuid-1234");
    EXPECT_EQ(parsed->deviceName, "Test Device");
    EXPECT_EQ(parsed->deviceType, DeviceType::Mobile);
}

TEST(PairingMessageTest, PairingResponseSuccess) {
    PairingResponsePayload response;
    response.success = true;
    response.familySecret = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                             0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
                             0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
    
    std::string json = response.toJson();
    EXPECT_FALSE(json.empty());
    
    auto parsed = PairingResponsePayload::fromJson(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->success);
    EXPECT_EQ(parsed->familySecret.size(), 32);
    EXPECT_EQ(parsed->familySecret, response.familySecret);
}

TEST(PairingMessageTest, PairingResponseError) {
    PairingResponsePayload response;
    response.success = false;
    response.errorCode = "INVALID_PIN";
    response.errorMessage = "The PIN is incorrect";
    
    std::string json = response.toJson();
    
    auto parsed = PairingResponsePayload::fromJson(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_FALSE(parsed->success);
    EXPECT_EQ(parsed->errorCode, "INVALID_PIN");
    EXPECT_EQ(parsed->errorMessage, "The PIN is incorrect");
    EXPECT_TRUE(parsed->familySecret.empty());
}

TEST(PairingMessageTest, MessageTypeNames) {
    EXPECT_STREQ(messageTypeName(MessageType::PairingRequest), "PairingRequest");
    EXPECT_STREQ(messageTypeName(MessageType::PairingResponse), "PairingResponse");
}

