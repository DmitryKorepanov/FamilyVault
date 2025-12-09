// NetworkProtocol.h — P2P Protocol definitions
// См. SPECIFICATIONS.md, раздел 7.3

#pragma once

#include "../export.h"
#include "../Models.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <chrono>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Константы протокола
// ═══════════════════════════════════════════════════════════

constexpr uint32_t PROTOCOL_MAGIC = 0x46564C54;  // "FVLT" in big-endian
constexpr uint32_t MESSAGE_PROTOCOL_VERSION = 1;
constexpr size_t MAX_MESSAGE_SIZE = 16 * 1024 * 1024;  // 16 MB max
constexpr size_t CHUNK_SIZE = 64 * 1024;               // 64 KB chunks for files
constexpr int HEARTBEAT_INTERVAL_SEC = 30;
constexpr int CONNECTION_TIMEOUT_SEC = 90;
constexpr uint16_t PAIRING_PORT = 45680;               // Port for initial pairing (no TLS)

// ═══════════════════════════════════════════════════════════
// MessageType — типы сообщений
// ═══════════════════════════════════════════════════════════

enum class MessageType : uint8_t {
    // Control messages
    Heartbeat = 0x00,
    HeartbeatAck = 0x01,
    Disconnect = 0x02,
    Error = 0x0F,

    // Device info
    DeviceInfo = 0x10,
    DeviceInfoRequest = 0x11,

    // Index sync
    IndexSyncRequest = 0x20,
    IndexSyncResponse = 0x21,
    IndexDelta = 0x22,
    IndexDeltaAck = 0x23,

    // File operations
    FileRequest = 0x30,
    FileResponse = 0x31,
    FileChunk = 0x32,
    FileChunkAck = 0x33,
    FileNotFound = 0x34,

    // Search
    SearchRequest = 0x40,
    SearchResponse = 0x41,

    // Pairing (unencrypted, used before TLS PSK is established)
    PairingRequest = 0x50,
    PairingResponse = 0x51,
};

FV_API const char* messageTypeName(MessageType type);

// ═══════════════════════════════════════════════════════════
// Message — базовое сообщение протокола
// ═══════════════════════════════════════════════════════════

struct FV_API Message {
    MessageType type;
    std::string requestId;       // UUID для request/response matching
    std::vector<uint8_t> payload; // JSON or binary data

    Message() : type(MessageType::Heartbeat) {}
    Message(MessageType t) : type(t) {}
    Message(MessageType t, const std::string& reqId) : type(t), requestId(reqId) {}

    // Helpers for JSON payload
    void setJsonPayload(const std::string& json);
    std::string getJsonPayload() const;

    // Helpers for binary payload
    void setBinaryPayload(const uint8_t* data, size_t size);
    void setBinaryPayload(const std::vector<uint8_t>& data);
};

// ═══════════════════════════════════════════════════════════
// MessageSerializer — сериализация/десериализация
// ═══════════════════════════════════════════════════════════

class FV_API MessageSerializer {
public:
    /// Сериализовать сообщение в байты для отправки
    /// Формат: [Magic:4][Length:4][Type:1][ReqIdLen:1][ReqId:N][Payload:M]
    static std::vector<uint8_t> serialize(const Message& msg);

    /// Десериализовать сообщение из байтов
    /// @return Message или nullopt при ошибке
    static std::optional<Message> deserialize(const uint8_t* data, size_t size);
    static std::optional<Message> deserialize(const std::vector<uint8_t>& data);

    /// Проверить, есть ли полное сообщение в буфере
    /// @return размер сообщения или 0 если недостаточно данных
    static size_t getMessageSize(const uint8_t* data, size_t available);
};

// ═══════════════════════════════════════════════════════════
// Specific message structures (JSON payloads)
// ═══════════════════════════════════════════════════════════

/// DeviceInfo message payload
struct DeviceInfoPayload {
    std::string deviceId;
    std::string deviceName;
    DeviceType deviceType;
    int protocolVersion;
    int64_t fileCount;
    int64_t lastSyncTimestamp;

    std::string toJson() const;
    static std::optional<DeviceInfoPayload> fromJson(const std::string& json);
};

/// IndexSyncRequest payload
struct IndexSyncRequestPayload {
    int64_t sinceTimestamp;  // Request changes since this timestamp
    std::vector<std::string> folderIds;  // Empty = all folders

    std::string toJson() const;
    static std::optional<IndexSyncRequestPayload> fromJson(const std::string& json);
};

/// IndexDelta payload (one file record)
struct IndexDeltaPayload {
    int64_t fileId;
    std::string path;
    std::string name;
    std::string mimeType;
    int64_t size;
    int64_t modifiedAt;
    std::string checksum;
    std::string extractedText;  // May be empty
    bool isDeleted;
    std::string deviceId;       // Source device
    int64_t syncTimestamp;

    std::string toJson() const;
    static std::optional<IndexDeltaPayload> fromJson(const std::string& json);
};

/// FileRequest payload
struct FileRequestPayload {
    int64_t fileId;
    std::string checksum;       // For verification
    int64_t offset;             // For resumable transfer
    int64_t length;             // 0 = entire file

    std::string toJson() const;
    static std::optional<FileRequestPayload> fromJson(const std::string& json);
};

/// FileChunk payload (binary + metadata header)
struct FileChunkHeader {
    int64_t fileId;
    int64_t offset;
    int64_t totalSize;
    int32_t chunkSize;
    bool isLast;

    std::vector<uint8_t> serialize() const;
    static std::optional<FileChunkHeader> deserialize(const uint8_t* data, size_t size);
    static constexpr size_t HEADER_SIZE = 8 + 8 + 8 + 4 + 1;  // 29 bytes
};

/// SearchRequest payload
struct SearchRequestPayload {
    std::string query;
    int32_t limit;
    int32_t offset;

    std::string toJson() const;
    static std::optional<SearchRequestPayload> fromJson(const std::string& json);
};

/// PairingRequest payload
struct PairingRequestPayload {
    std::string pin;                    // 6-digit PIN
    std::string deviceId;               // Requester device ID
    std::string deviceName;             // Requester device name
    DeviceType deviceType;              // Requester device type

    std::string toJson() const;
    static std::optional<PairingRequestPayload> fromJson(const std::string& json);
};

/// PairingResponse payload
struct PairingResponsePayload {
    bool success;                       // Whether pairing succeeded
    std::vector<uint8_t> familySecret;  // 32-byte family secret (only if success)
    std::string errorCode;              // Error code if failed
    std::string errorMessage;           // Human-readable error message

    std::string toJson() const;
    static std::optional<PairingResponsePayload> fromJson(const std::string& json);
};

// ═══════════════════════════════════════════════════════════
// Helper: generate request ID
// ═══════════════════════════════════════════════════════════

FV_API std::string generateRequestId();

} // namespace FamilyVault

