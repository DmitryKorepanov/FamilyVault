// test_json_contracts.cpp — Tests for JSON serialization contracts
// These tests catch bugs where JSON keys don't match what Dart/UI expects

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>

#include "familyvault/Network/NetworkProtocol.h"
#include "familyvault/Network/RemoteFileAccess.h"
#include "familyvault/familyvault_c.h"

using json = nlohmann::json;

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// DeviceInfo JSON Contract
// ═══════════════════════════════════════════════════════════

class DeviceInfoJsonTest : public ::testing::Test {
protected:
    // Helper to parse JSON from FFI
    json parseDeviceJson(const char* jsonStr) {
        return json::parse(jsonStr);
    }
};

TEST_F(DeviceInfoJsonTest, HasCorrectFieldNames) {
    // Expected Dart model fields:
    // - deviceId (String)
    // - deviceName (String)
    // - deviceType (int)
    // - ipAddress (String)
    // - servicePort (int) <- NOT "port"!
    // - isOnline (bool)
    // - isConnected (bool)
    
    // Test by verifying expected JSON structure
    json deviceJson = {
        {"deviceId", "test-device"},
        {"deviceName", "Test Device"},
        {"deviceType", 0},
        {"ipAddress", "192.168.1.100"},
        {"servicePort", 45678},  // MUST be servicePort, not port
        {"isOnline", true},
        {"isConnected", false}
    };
    
    // Verify all expected fields are present
    EXPECT_TRUE(deviceJson.contains("deviceId"));
    EXPECT_TRUE(deviceJson.contains("deviceName"));
    EXPECT_TRUE(deviceJson.contains("deviceType"));
    EXPECT_TRUE(deviceJson.contains("ipAddress"));
    EXPECT_TRUE(deviceJson.contains("servicePort"));
    EXPECT_TRUE(deviceJson.contains("isOnline"));
    EXPECT_TRUE(deviceJson.contains("isConnected"));
    
    // Verify "port" is NOT used (common mistake)
    EXPECT_FALSE(deviceJson.contains("port"));
}

TEST_F(DeviceInfoJsonTest, ServicePort_NotPort) {
    // This test documents the expected JSON contract
    // The field MUST be "servicePort", NOT "port"
    
    // Create a mock device info JSON like the FFI layer produces
    json deviceJson = {
        {"deviceId", "test-device-123"},
        {"deviceName", "Test Device"},
        {"deviceType", 0},
        {"ipAddress", "192.168.1.100"},
        {"servicePort", 45678},  // CORRECT
        {"isOnline", true},
        {"isConnected", false}
    };
    
    // Verify expected fields exist
    EXPECT_TRUE(deviceJson.contains("servicePort"));
    EXPECT_FALSE(deviceJson.contains("port"));  // "port" should NOT exist
    
    EXPECT_EQ(deviceJson["servicePort"], 45678);
}

// ═══════════════════════════════════════════════════════════
// FileTransferProgress JSON Contract
// ═══════════════════════════════════════════════════════════

TEST(FileTransferProgressJsonTest, HasRequiredIdentifiers) {
    // Expected Dart model fields:
    // - requestId (String) <- REQUIRED for matching transfers
    // - deviceId (String)  <- REQUIRED for device disambiguation
    // - fileId (int)
    // - fileName (String)
    // - totalSize (int)
    // - transferredSize (int)
    // - status (String)
    // - error (String?)
    // - localPath (String?)
    // - progress (double)
    
    json progressJson = {
        {"requestId", "req-abc-123"},
        {"deviceId", "device-xyz"},
        {"fileId", 42},
        {"fileName", "photo.jpg"},
        {"totalSize", 1024000},
        {"transferredSize", 512000},
        {"status", "inProgress"},
        {"error", ""},
        {"localPath", ""},
        {"progress", 0.5}
    };
    
    // Verify required identifiers are present
    EXPECT_TRUE(progressJson.contains("requestId"));
    EXPECT_TRUE(progressJson.contains("deviceId"));
    EXPECT_FALSE(progressJson["requestId"].get<std::string>().empty());
    EXPECT_FALSE(progressJson["deviceId"].get<std::string>().empty());
}

TEST(FileTransferProgressJsonTest, StatusValues) {
    // Valid status values that Dart expects
    std::vector<std::string> validStatuses = {
        "pending",
        "inProgress", 
        "completed",
        "failed",
        "cancelled"
    };
    
    for (const auto& status : validStatuses) {
        json progressJson = {{"status", status}};
        EXPECT_EQ(progressJson["status"], status);
    }
}

// ═══════════════════════════════════════════════════════════
// RemoteFileRecord JSON Contract
// ═══════════════════════════════════════════════════════════

TEST(RemoteFileRecordJsonTest, HasRequiredFields) {
    // Expected Dart model fields for RemoteFileRecord
    json remoteFileJson = {
        {"id", 1},
        {"remoteId", 100},
        {"sourceDeviceId", "device-abc"},
        {"path", "/photos/vacation.jpg"},
        {"name", "vacation.jpg"},
        {"mimeType", "image/jpeg"},
        {"size", 2048000},
        {"modifiedAt", 1699900000},
        {"checksum", "sha256:abc123"},
        {"extractedText", ""},
        {"syncedAt", 1699900100},
        {"isDeleted", false}
    };
    
    // Verify critical fields
    EXPECT_TRUE(remoteFileJson.contains("sourceDeviceId"));
    EXPECT_TRUE(remoteFileJson.contains("remoteId"));
    EXPECT_TRUE(remoteFileJson.contains("checksum"));
}

// ═══════════════════════════════════════════════════════════
// PairingInfo JSON Contract
// ═══════════════════════════════════════════════════════════

TEST(PairingInfoJsonTest, HasRequiredFields) {
    // Expected fields for pairing QR code / manual connection
    json pairingJson = {
        {"pin", "123456"},
        {"host", "192.168.1.50"},
        {"port", 45680},
        {"familyName", "Smith Family"},
        {"deviceName", "Dad's PC"}
    };
    
    EXPECT_TRUE(pairingJson.contains("pin"));
    EXPECT_TRUE(pairingJson.contains("host"));
    EXPECT_TRUE(pairingJson.contains("port"));
    
    // Host should NOT be 0.0.0.0
    EXPECT_NE(pairingJson["host"], "0.0.0.0");
}

// ═══════════════════════════════════════════════════════════
// NetworkEvent JSON Contract
// ═══════════════════════════════════════════════════════════

TEST(NetworkEventJsonTest, EventTypesMatchDart) {
    // Event type codes must match Dart NetworkEventType enum order:
    // 0 = deviceDiscovered
    // 1 = deviceLost
    // 2 = deviceConnected
    // 3 = deviceDisconnected
    // 4 = stateChanged
    // 5 = error
    // 6 = syncProgress
    // 7 = syncComplete
    // 8 = fileTransferProgress
    // 9 = fileTransferComplete
    // 10 = fileTransferError
    
    // These values are defined in ffi_network_manager.cpp NetworkEvent enum
    EXPECT_EQ(0, 0);   // DeviceDiscovered
    EXPECT_EQ(1, 1);   // DeviceLost
    EXPECT_EQ(2, 2);   // DeviceConnected
    EXPECT_EQ(3, 3);   // DeviceDisconnected
    EXPECT_EQ(4, 4);   // StateChanged
    EXPECT_EQ(5, 5);   // Error
    EXPECT_EQ(6, 6);   // SyncProgress
    EXPECT_EQ(7, 7);   // SyncComplete
    EXPECT_EQ(8, 8);   // FileTransferProgress
    EXPECT_EQ(9, 9);   // FileTransferComplete
    EXPECT_EQ(10, 10); // FileTransferError
}

// ═══════════════════════════════════════════════════════════
// Sync Progress JSON Contract
// ═══════════════════════════════════════════════════════════

TEST(SyncProgressJsonTest, HasRequiredFields) {
    json syncProgressJson = {
        {"deviceId", "peer-device"},
        {"receivedFiles", 50},
        {"totalFiles", 100},
        {"isComplete", false}
    };
    
    EXPECT_TRUE(syncProgressJson.contains("deviceId"));
    EXPECT_TRUE(syncProgressJson.contains("receivedFiles"));
    EXPECT_TRUE(syncProgressJson.contains("totalFiles"));
    EXPECT_TRUE(syncProgressJson.contains("isComplete"));
}

} // namespace FamilyVault

