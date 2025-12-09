// test_security.cpp — Тесты для SecureStorage и FamilyPairing

#include <gtest/gtest.h>
#include "familyvault/SecureStorage.h"
#include "familyvault/FamilyPairing.h"
#include <vector>
#include <string>

using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// SecureStorage Tests
// ═══════════════════════════════════════════════════════════

class SecureStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage = std::make_unique<SecureStorage>();
        // Очищаем тестовые ключи перед каждым тестом
        storage->remove("test.key1");
        storage->remove("test.key2");
        storage->remove("test.string");
    }

    void TearDown() override {
        // Очищаем после теста
        storage->remove("test.key1");
        storage->remove("test.key2");
        storage->remove("test.string");
    }

    std::unique_ptr<SecureStorage> storage;
};

TEST_F(SecureStorageTest, StoreAndRetrieve) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    ASSERT_TRUE(storage->store("test.key1", data));
    
    auto retrieved = storage->retrieve("test.key1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, data);
}

TEST_F(SecureStorageTest, RetrieveNonExistent) {
    auto result = storage->retrieve("non.existent.key");
    EXPECT_FALSE(result.has_value());
}

TEST_F(SecureStorageTest, Exists) {
    EXPECT_FALSE(storage->exists("test.key2"));
    
    std::vector<uint8_t> data = {0xAB, 0xCD};
    storage->store("test.key2", data);
    
    EXPECT_TRUE(storage->exists("test.key2"));
}

TEST_F(SecureStorageTest, Remove) {
    std::vector<uint8_t> data = {0x11, 0x22, 0x33};
    storage->store("test.key1", data);
    EXPECT_TRUE(storage->exists("test.key1"));
    
    ASSERT_TRUE(storage->remove("test.key1"));
    EXPECT_FALSE(storage->exists("test.key1"));
}

TEST_F(SecureStorageTest, RemoveNonExistent) {
    // Удаление несуществующего ключа должно быть OK
    EXPECT_TRUE(storage->remove("non.existent.key"));
}

TEST_F(SecureStorageTest, StoreAndRetrieveString) {
    std::string value = "Hello, SecureStorage! Привет!";
    
    ASSERT_TRUE(storage->storeString("test.string", value));
    
    auto retrieved = storage->retrieveString("test.string");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, value);
}

TEST_F(SecureStorageTest, StoreLargeData) {
    // 1KB of random data
    std::vector<uint8_t> data(1024);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>(i & 0xFF);
    }
    
    ASSERT_TRUE(storage->store("test.key1", data));
    
    auto retrieved = storage->retrieve("test.key1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), data.size());
    EXPECT_EQ(*retrieved, data);
}

TEST_F(SecureStorageTest, OverwriteExisting) {
    std::vector<uint8_t> data1 = {0x01, 0x02};
    std::vector<uint8_t> data2 = {0x03, 0x04, 0x05, 0x06};
    
    ASSERT_TRUE(storage->store("test.key1", data1));
    ASSERT_TRUE(storage->store("test.key1", data2)); // Overwrite
    
    auto retrieved = storage->retrieve("test.key1");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, data2);
}

// ═══════════════════════════════════════════════════════════
// FamilyPairing Tests
// ═══════════════════════════════════════════════════════════

class FamilyPairingTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage = std::make_shared<SecureStorage>();
        // Очищаем конфигурацию перед тестом
        storage->remove(SecureStorage::KEY_FAMILY_SECRET);
        storage->remove(SecureStorage::KEY_DEVICE_ID);
        storage->remove(SecureStorage::KEY_DEVICE_NAME);
        
        pairing = std::make_unique<FamilyPairing>(storage);
    }

    void TearDown() override {
        pairing.reset();
        // Очищаем после теста
        storage->remove(SecureStorage::KEY_FAMILY_SECRET);
        storage->remove(SecureStorage::KEY_DEVICE_ID);
        storage->remove(SecureStorage::KEY_DEVICE_NAME);
    }

    std::shared_ptr<SecureStorage> storage;
    std::unique_ptr<FamilyPairing> pairing;
};

TEST_F(FamilyPairingTest, NotConfiguredInitially) {
    EXPECT_FALSE(pairing->isConfigured());
}

TEST_F(FamilyPairingTest, HasDeviceId) {
    auto deviceId = pairing->getDeviceId();
    EXPECT_FALSE(deviceId.empty());
    // UUID format: 8-4-4-4-12
    EXPECT_EQ(deviceId.length(), 36);
    EXPECT_EQ(deviceId[8], '-');
    EXPECT_EQ(deviceId[13], '-');
}

TEST_F(FamilyPairingTest, DeviceIdPersists) {
    auto id1 = pairing->getDeviceId();
    
    // Создаём новый экземпляр с тем же storage
    auto pairing2 = std::make_unique<FamilyPairing>(storage);
    auto id2 = pairing2->getDeviceId();
    
    EXPECT_EQ(id1, id2);
}

TEST_F(FamilyPairingTest, SetDeviceName) {
    pairing->setDeviceName("Test Device");
    EXPECT_EQ(pairing->getDeviceName(), "Test Device");
    
    // Проверяем персистентность
    auto pairing2 = std::make_unique<FamilyPairing>(storage);
    EXPECT_EQ(pairing2->getDeviceName(), "Test Device");
}

TEST_F(FamilyPairingTest, CreateFamily) {
    auto info = pairing->createFamily();
    ASSERT_TRUE(info.has_value());
    
    // PIN должен быть 6 цифр
    EXPECT_EQ(info->pin.length(), 6);
    for (char c : info->pin) {
        EXPECT_TRUE(c >= '0' && c <= '9');
    }
    
    // QR data должен быть непустым base64
    EXPECT_FALSE(info->qrData.empty());
    
    // Время истечения должно быть в будущем
    auto now = std::chrono::system_clock::now();
    auto nowTs = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    EXPECT_GT(info->expiresAt, nowTs);
    
    // Теперь семья настроена
    EXPECT_TRUE(pairing->isConfigured());
}

TEST_F(FamilyPairingTest, RegeneratePin) {
    auto info1 = pairing->createFamily();
    ASSERT_TRUE(info1.has_value());
    
    // Небольшая пауза чтобы nonce был другим
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto info2 = pairing->regeneratePin();
    ASSERT_TRUE(info2.has_value());
    
    // PIN может измениться (зависит от nonce)
    EXPECT_EQ(info2->pin.length(), 6);
}

TEST_F(FamilyPairingTest, DerivePsk) {
    // Без семьи PSK не должен выводиться
    auto pskBefore = pairing->derivePsk();
    EXPECT_FALSE(pskBefore.has_value());
    
    // Создаём семью
    pairing->createFamily();
    
    auto psk = pairing->derivePsk();
    ASSERT_TRUE(psk.has_value());
    EXPECT_EQ(psk->size(), 32); // 256 бит
    
    // PSK должен быть детерминированным (одинаковый для того же secret)
    auto psk2 = pairing->derivePsk();
    ASSERT_TRUE(psk2.has_value());
    EXPECT_EQ(*psk, *psk2);
}

TEST_F(FamilyPairingTest, PskIdentityIsDeviceId) {
    auto identity = pairing->getPskIdentity();
    auto deviceId = pairing->getDeviceId();
    EXPECT_EQ(identity, deviceId);
}

TEST_F(FamilyPairingTest, Reset) {
    pairing->createFamily();
    EXPECT_TRUE(pairing->isConfigured());
    
    pairing->reset();
    EXPECT_FALSE(pairing->isConfigured());
    
    // PSK больше не доступен
    auto psk = pairing->derivePsk();
    EXPECT_FALSE(psk.has_value());
}

TEST_F(FamilyPairingTest, PendingPairing) {
    EXPECT_FALSE(pairing->hasPendingPairing());
    
    pairing->createFamily();
    EXPECT_TRUE(pairing->hasPendingPairing());
    
    pairing->cancelPairing();
    EXPECT_FALSE(pairing->hasPendingPairing());
}

// ═══════════════════════════════════════════════════════════
// Crypto Helper Tests
// ═══════════════════════════════════════════════════════════

TEST(CryptoTest, RandomBytes) {
    auto bytes1 = Crypto::randomBytes(32);
    auto bytes2 = Crypto::randomBytes(32);
    
    EXPECT_EQ(bytes1.size(), 32);
    EXPECT_EQ(bytes2.size(), 32);
    
    // Два вызова должны дать разные результаты
    EXPECT_NE(bytes1, bytes2);
}

TEST(CryptoTest, HkdfDeterministic) {
    std::vector<uint8_t> ikm = {0x01, 0x02, 0x03, 0x04};
    
    auto derived1 = Crypto::hkdf(ikm, "salt", "info", 32);
    auto derived2 = Crypto::hkdf(ikm, "salt", "info", 32);
    
    EXPECT_EQ(derived1, derived2);
}

TEST(CryptoTest, HkdfDifferentSaltsDifferentOutput) {
    std::vector<uint8_t> ikm = {0x01, 0x02, 0x03, 0x04};
    
    auto derived1 = Crypto::hkdf(ikm, "salt1", "info", 32);
    auto derived2 = Crypto::hkdf(ikm, "salt2", "info", 32);
    
    EXPECT_NE(derived1, derived2);
}

TEST(CryptoTest, GenerateUUID) {
    auto uuid1 = Crypto::generateUUID();
    auto uuid2 = Crypto::generateUUID();
    
    EXPECT_EQ(uuid1.length(), 36);
    EXPECT_EQ(uuid2.length(), 36);
    EXPECT_NE(uuid1, uuid2);
}

TEST(CryptoTest, GeneratePin) {
    std::vector<uint8_t> secret = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> nonce1 = {0xAA, 0xBB};
    std::vector<uint8_t> nonce2 = {0xCC, 0xDD};
    
    auto pin1 = Crypto::generatePin(secret, nonce1);
    auto pin1_again = Crypto::generatePin(secret, nonce1);
    auto pin2 = Crypto::generatePin(secret, nonce2);
    
    EXPECT_EQ(pin1.length(), 6);
    EXPECT_EQ(pin1, pin1_again); // Детерминированный
    // PIN с разными nonce могут совпадать, но обычно нет
}


