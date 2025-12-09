// test_file_transfer.cpp — Тесты интеграции RemoteFileAccess с NetworkManager

#include <gtest/gtest.h>
#include "familyvault/familyvault_c.h"
#include "familyvault/SecureStorage.h"
#include "familyvault/FamilyPairing.h"
#include "familyvault/Network/RemoteFileAccess.h"
#include "familyvault/Network/NetworkProtocol.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace FamilyVault;
namespace fs = std::filesystem;

class FileTransferFFITest : public ::testing::Test {
protected:
    FVSecureStorage storage = nullptr;
    FVFamilyPairing pairing = nullptr;
    FVNetworkManager networkMgr = nullptr;
    std::string cacheDir;
    std::string testFileDir;

    void SetUp() override {
        // Создаём временные директории
        cacheDir = (fs::temp_directory_path() / "test_file_transfer_cache").string();
        testFileDir = (fs::temp_directory_path() / "test_file_transfer_files").string();
        fs::create_directories(cacheDir);
        fs::create_directories(testFileDir);
        
        // Инициализируем SecureStorage
        storage = fv_secure_create();
        ASSERT_NE(storage, nullptr);
        
        // Сбрасываем состояние
        fv_secure_remove(storage, "family_secret");
        fv_secure_remove(storage, "device_id");
        fv_secure_remove(storage, "device_name");
        
        // Создаём FamilyPairing
        pairing = fv_pairing_create(storage);
        ASSERT_NE(pairing, nullptr);
        
        // Создаём семью
        char* pairingJson = fv_pairing_create_family(pairing);
        ASSERT_NE(pairingJson, nullptr);
        fv_free_string(pairingJson);
        ASSERT_TRUE(fv_pairing_is_configured(pairing));
        
        // Создаём NetworkManager
        networkMgr = fv_network_create(pairing);
        ASSERT_NE(networkMgr, nullptr);
    }

    void TearDown() override {
        if (networkMgr) {
            fv_network_destroy(networkMgr);
            networkMgr = nullptr;
        }
        if (pairing) {
            fv_pairing_destroy(pairing);
            pairing = nullptr;
        }
        if (storage) {
            fv_secure_destroy(storage);
            storage = nullptr;
        }
        
        // Удаляем временные директории
        fs::remove_all(cacheDir);
        fs::remove_all(testFileDir);
    }

    // Создать тестовый файл
    std::string createTestFile(const std::string& name, const std::string& content) {
        std::string path = testFileDir + "/" + name;
        std::ofstream file(path);
        file << content;
        return path;
    }
};

// ═══════════════════════════════════════════════════════════
// Basic FFI Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileTransferFFITest, SetCacheDir) {
    FVError result = fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    EXPECT_EQ(result, FV_OK);
}

TEST_F(FileTransferFFITest, SetCacheDir_NullManager) {
    FVError result = fv_network_set_cache_dir(nullptr, cacheDir.c_str());
    EXPECT_EQ(result, FV_ERROR_INVALID_ARGUMENT);
}

TEST_F(FileTransferFFITest, SetCacheDir_NullPath) {
    FVError result = fv_network_set_cache_dir(networkMgr, nullptr);
    EXPECT_EQ(result, FV_ERROR_INVALID_ARGUMENT);
}

TEST_F(FileTransferFFITest, GetCacheSize_BeforeSetup) {
    int64_t size = fv_network_get_cache_size(networkMgr);
    EXPECT_EQ(size, 0);
}

TEST_F(FileTransferFFITest, GetCacheSize_AfterSetup) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    int64_t size = fv_network_get_cache_size(networkMgr);
    EXPECT_GE(size, 0);
}

TEST_F(FileTransferFFITest, HasActiveTransfers_Initially) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    int32_t result = fv_network_has_active_transfers(networkMgr);
    EXPECT_EQ(result, 0);
}

TEST_F(FileTransferFFITest, GetActiveTransfers_EmptyInitially) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    char* json = fv_network_get_active_transfers(networkMgr);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(json, "[]");
    fv_free_string(json);
}

TEST_F(FileTransferFFITest, IsFileCached_NotCached) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    int32_t result = fv_network_is_file_cached(networkMgr, "device-123", 12345, nullptr);
    EXPECT_EQ(result, 0);
}

TEST_F(FileTransferFFITest, GetCachedPath_NotCached) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    // For non-cached file, should return nullptr
    char* path = fv_network_get_cached_path(networkMgr, "device-123", 12345);
    EXPECT_EQ(path, nullptr);
}

TEST_F(FileTransferFFITest, GetCachedPath_Cached) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    
    // Create a cached file
    std::string deviceDir = cacheDir + "/device-123";
    fs::create_directories(deviceDir);
    std::ofstream file(deviceDir + "/12345.txt");
    file << "test content";
    file.close();
    
    // Now should return path
    char* path = fv_network_get_cached_path(networkMgr, "device-123", 12345);
    ASSERT_NE(path, nullptr);
    EXPECT_FALSE(std::string(path).empty());
    // Path should include device ID
    EXPECT_NE(std::string(path).find("device-123"), std::string::npos);
    EXPECT_NE(std::string(path).find("12345"), std::string::npos);
    fv_free_string(path);
}

TEST_F(FileTransferFFITest, ClearCache) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    // Должно работать без ошибок
    fv_network_clear_cache(networkMgr);
    EXPECT_EQ(fv_network_get_cache_size(networkMgr), 0);
}

// ═══════════════════════════════════════════════════════════
// Request File Tests (без подключения)
// ═══════════════════════════════════════════════════════════

TEST_F(FileTransferFFITest, RequestFile_NotConnected) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    
    char* requestId = fv_network_request_file(
        networkMgr,
        "non-existent-device",
        12345,
        "test.txt",
        1024,
        nullptr
    );
    
    // Должно вернуть nullptr так как устройство не подключено
    EXPECT_EQ(requestId, nullptr);
    EXPECT_EQ(fv_last_error(), FV_ERROR_NETWORK);
}

TEST_F(FileTransferFFITest, RequestFile_NoCacheDir) {
    // Без установки кэша
    char* requestId = fv_network_request_file(
        networkMgr,
        "device-id",
        12345,
        "test.txt",
        1024,
        nullptr
    );
    
    EXPECT_EQ(requestId, nullptr);
    EXPECT_EQ(fv_last_error(), FV_ERROR_INVALID_ARGUMENT);
}

// ═══════════════════════════════════════════════════════════
// Cancel Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileTransferFFITest, CancelRequest_NonExistent) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    // Не должно крашиться
    fv_network_cancel_file_request(networkMgr, "non-existent-request");
    SUCCEED();
}

TEST_F(FileTransferFFITest, CancelAllRequests_NonExistentDevice) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    // Не должно крашиться
    fv_network_cancel_all_file_requests(networkMgr, "non-existent-device");
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════
// Progress Tests
// ═══════════════════════════════════════════════════════════

TEST_F(FileTransferFFITest, GetTransferProgress_NonExistent) {
    fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    char* json = fv_network_get_transfer_progress(networkMgr, "non-existent-request");
    // Может вернуть nullptr или JSON с ошибкой
    if (json) {
        fv_free_string(json);
    }
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════
// Cache with Files Tests
// ═══════════════════════════════════════════════════════════

class FileTransferCacheTest : public FileTransferFFITest {
protected:
    void SetUp() override {
        FileTransferFFITest::SetUp();
        fv_network_set_cache_dir(networkMgr, cacheDir.c_str());
    }

    void createCachedFile(const std::string& deviceId, int64_t fileId, const std::string& content) {
        // Cache structure: cacheDir/deviceId/fileId
        std::string deviceDir = cacheDir + "/" + deviceId;
        fs::create_directories(deviceDir);
        std::string path = deviceDir + "/" + std::to_string(fileId);
        std::ofstream file(path);
        file << content;
    }
};

TEST_F(FileTransferCacheTest, IsFileCached_AfterCreation) {
    createCachedFile("device-123", 12345, "Hello World");
    
    // Проверяем через getCachedPath
    char* path = fv_network_get_cached_path(networkMgr, "device-123", 12345);
    ASSERT_NE(path, nullptr);
    
    // Проверяем что файл существует
    EXPECT_TRUE(fs::exists(path));
    fv_free_string(path);
    
    // Проверяем через isCached
    EXPECT_TRUE(fv_network_is_file_cached(networkMgr, "device-123", 12345, nullptr));
}

TEST_F(FileTransferCacheTest, ClearCache_RemovesFiles) {
    createCachedFile("device-a", 1, "Hello");
    createCachedFile("device-b", 2, "World");
    
    EXPECT_GT(fv_network_get_cache_size(networkMgr), 0);
    
    fv_network_clear_cache(networkMgr);
    
    EXPECT_EQ(fv_network_get_cache_size(networkMgr), 0);
}

TEST_F(FileTransferCacheTest, CacheKeysAreDeviceSpecific) {
    // Создаём файлы с одинаковым fileId но разными устройствами
    createCachedFile("device-a", 123, "Content from A");
    createCachedFile("device-b", 123, "Content from B");
    
    // Оба файла должны существовать независимо
    EXPECT_TRUE(fv_network_is_file_cached(networkMgr, "device-a", 123, nullptr));
    EXPECT_TRUE(fv_network_is_file_cached(networkMgr, "device-b", 123, nullptr));
    
    // И их пути должны быть разными
    char* pathA = fv_network_get_cached_path(networkMgr, "device-a", 123);
    char* pathB = fv_network_get_cached_path(networkMgr, "device-b", 123);
    
    ASSERT_NE(pathA, nullptr);
    ASSERT_NE(pathB, nullptr);
    EXPECT_NE(std::string(pathA), std::string(pathB));
    
    fv_free_string(pathA);
    fv_free_string(pathB);
}

// ═══════════════════════════════════════════════════════════
// Protocol Message Tests
// ═══════════════════════════════════════════════════════════

TEST(FileTransferProtocolTest, FileRequestPayload_Serialization) {
    FileRequestPayload payload;
    payload.fileId = 12345;
    payload.checksum = "abc123";
    payload.offset = 0;
    payload.length = 1024;
    
    std::string json = payload.toJson();
    EXPECT_FALSE(json.empty());
    
    auto parsed = FileRequestPayload::fromJson(json);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->fileId, 12345);
    EXPECT_EQ(parsed->checksum, "abc123");
    EXPECT_EQ(parsed->offset, 0);
    EXPECT_EQ(parsed->length, 1024);
}

TEST(FileTransferProtocolTest, FileChunkHeader_Serialization) {
    FileChunkHeader header;
    header.fileId = 12345;
    header.offset = 1000;
    header.totalSize = 10000;
    header.chunkSize = 64 * 1024;
    header.isLast = false;
    
    auto bytes = header.serialize();
    EXPECT_EQ(bytes.size(), FileChunkHeader::HEADER_SIZE);
    
    auto parsed = FileChunkHeader::deserialize(bytes.data(), bytes.size());
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->fileId, 12345);
    EXPECT_EQ(parsed->offset, 1000);
    EXPECT_EQ(parsed->totalSize, 10000);
    EXPECT_EQ(parsed->chunkSize, 64 * 1024);
    EXPECT_FALSE(parsed->isLast);
}

TEST(FileTransferProtocolTest, FileChunkHeader_LastChunk) {
    FileChunkHeader header;
    header.fileId = 1;
    header.offset = 5000;
    header.totalSize = 5064;
    header.chunkSize = 64;
    header.isLast = true;
    
    auto bytes = header.serialize();
    auto parsed = FileChunkHeader::deserialize(bytes.data(), bytes.size());
    
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->isLast);
}

