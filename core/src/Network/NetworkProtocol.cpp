// NetworkProtocol.cpp — P2P Protocol implementation

#include "familyvault/Network/NetworkProtocol.h"
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>
#include <cstring>

namespace FamilyVault {

using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════
// MessageType names
// ═══════════════════════════════════════════════════════════

const char* messageTypeName(MessageType type) {
    switch (type) {
        case MessageType::Heartbeat: return "Heartbeat";
        case MessageType::HeartbeatAck: return "HeartbeatAck";
        case MessageType::Disconnect: return "Disconnect";
        case MessageType::Error: return "Error";
        case MessageType::DeviceInfo: return "DeviceInfo";
        case MessageType::DeviceInfoRequest: return "DeviceInfoRequest";
        case MessageType::IndexSyncRequest: return "IndexSyncRequest";
        case MessageType::IndexSyncResponse: return "IndexSyncResponse";
        case MessageType::IndexDelta: return "IndexDelta";
        case MessageType::IndexDeltaAck: return "IndexDeltaAck";
        case MessageType::FileRequest: return "FileRequest";
        case MessageType::FileResponse: return "FileResponse";
        case MessageType::FileChunk: return "FileChunk";
        case MessageType::FileChunkAck: return "FileChunkAck";
        case MessageType::FileNotFound: return "FileNotFound";
        case MessageType::SearchRequest: return "SearchRequest";
        case MessageType::SearchResponse: return "SearchResponse";
        case MessageType::PairingRequest: return "PairingRequest";
        case MessageType::PairingResponse: return "PairingResponse";
        default: return "Unknown";
    }
}

// ═══════════════════════════════════════════════════════════
// Message
// ═══════════════════════════════════════════════════════════

void Message::setJsonPayload(const std::string& json) {
    payload.assign(json.begin(), json.end());
}

std::string Message::getJsonPayload() const {
    return std::string(payload.begin(), payload.end());
}

void Message::setBinaryPayload(const uint8_t* data, size_t size) {
    payload.assign(data, data + size);
}

void Message::setBinaryPayload(const std::vector<uint8_t>& data) {
    payload = data;
}

// ═══════════════════════════════════════════════════════════
// MessageSerializer
// ═══════════════════════════════════════════════════════════

std::vector<uint8_t> MessageSerializer::serialize(const Message& msg) {
    // Calculate total size
    // Header: Magic(4) + Length(4) + Type(1) + ReqIdLen(1) + ReqId(N) + Payload(M)
    size_t reqIdLen = std::min(msg.requestId.size(), size_t(255));
    size_t totalSize = 4 + 4 + 1 + 1 + reqIdLen + msg.payload.size();

    std::vector<uint8_t> result(totalSize);
    uint8_t* ptr = result.data();

    // Magic (big-endian)
    ptr[0] = (PROTOCOL_MAGIC >> 24) & 0xFF;
    ptr[1] = (PROTOCOL_MAGIC >> 16) & 0xFF;
    ptr[2] = (PROTOCOL_MAGIC >> 8) & 0xFF;
    ptr[3] = PROTOCOL_MAGIC & 0xFF;
    ptr += 4;

    // Length (big-endian) - total message size
    uint32_t len = static_cast<uint32_t>(totalSize);
    ptr[0] = (len >> 24) & 0xFF;
    ptr[1] = (len >> 16) & 0xFF;
    ptr[2] = (len >> 8) & 0xFF;
    ptr[3] = len & 0xFF;
    ptr += 4;

    // Type
    *ptr++ = static_cast<uint8_t>(msg.type);

    // RequestId length + data
    *ptr++ = static_cast<uint8_t>(reqIdLen);
    if (reqIdLen > 0) {
        memcpy(ptr, msg.requestId.data(), reqIdLen);
        ptr += reqIdLen;
    }

    // Payload
    if (!msg.payload.empty()) {
        memcpy(ptr, msg.payload.data(), msg.payload.size());
    }

    return result;
}

std::optional<Message> MessageSerializer::deserialize(const uint8_t* data, size_t size) {
    if (size < 10) {  // Minimum: Magic(4) + Length(4) + Type(1) + ReqIdLen(1)
        return std::nullopt;
    }

    // Check magic
    uint32_t magic = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (magic != PROTOCOL_MAGIC) {
        spdlog::debug("Protocol: Invalid magic 0x{:08X}", magic);
        return std::nullopt;
    }

    // Get length
    uint32_t len = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    if (len > MAX_MESSAGE_SIZE || len > size) {
        spdlog::debug("Protocol: Invalid length {}", len);
        return std::nullopt;
    }

    Message msg;
    const uint8_t* ptr = data + 8;

    // Type
    msg.type = static_cast<MessageType>(*ptr++);

    // RequestId
    uint8_t reqIdLen = *ptr++;
    if (reqIdLen > 0) {
        if (ptr + reqIdLen > data + len) {
            return std::nullopt;
        }
        msg.requestId.assign(reinterpret_cast<const char*>(ptr), reqIdLen);
        ptr += reqIdLen;
    }

    // Payload
    size_t payloadSize = (data + len) - ptr;
    if (payloadSize > 0) {
        msg.payload.assign(ptr, ptr + payloadSize);
    }

    return msg;
}

std::optional<Message> MessageSerializer::deserialize(const std::vector<uint8_t>& data) {
    return deserialize(data.data(), data.size());
}

size_t MessageSerializer::getMessageSize(const uint8_t* data, size_t available) {
    if (available < 8) {
        return 0;  // Not enough data for header
    }

    // Check magic
    uint32_t magic = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (magic != PROTOCOL_MAGIC) {
        return 0;
    }

    // Get length
    uint32_t len = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    if (len > MAX_MESSAGE_SIZE) {
        return 0;
    }

    return len;
}

// ═══════════════════════════════════════════════════════════
// DeviceInfoPayload
// ═══════════════════════════════════════════════════════════

std::string DeviceInfoPayload::toJson() const {
    json j = {
        {"deviceId", deviceId},
        {"deviceName", deviceName},
        {"deviceType", static_cast<int>(deviceType)},
        {"protocolVersion", protocolVersion},
        {"fileCount", fileCount},
        {"lastSyncTimestamp", lastSyncTimestamp}
    };
    return j.dump();
}

std::optional<DeviceInfoPayload> DeviceInfoPayload::fromJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        DeviceInfoPayload p;
        p.deviceId = j.value("deviceId", "");
        p.deviceName = j.value("deviceName", "");
        p.deviceType = static_cast<DeviceType>(j.value("deviceType", 0));
        p.protocolVersion = j.value("protocolVersion", 1);
        p.fileCount = j.value("fileCount", 0);
        p.lastSyncTimestamp = j.value("lastSyncTimestamp", 0);
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════
// IndexSyncRequestPayload
// ═══════════════════════════════════════════════════════════

std::string IndexSyncRequestPayload::toJson() const {
    json j = {
        {"sinceTimestamp", sinceTimestamp},
        {"folderIds", folderIds}
    };
    return j.dump();
}

std::optional<IndexSyncRequestPayload> IndexSyncRequestPayload::fromJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        IndexSyncRequestPayload p;
        p.sinceTimestamp = j.value("sinceTimestamp", 0);
        if (j.contains("folderIds") && j["folderIds"].is_array()) {
            for (const auto& id : j["folderIds"]) {
                p.folderIds.push_back(id.get<std::string>());
            }
        }
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════
// IndexDeltaPayload
// ═══════════════════════════════════════════════════════════

std::string IndexDeltaPayload::toJson() const {
    json j = {
        {"fileId", fileId},
        {"path", path},
        {"name", name},
        {"mimeType", mimeType},
        {"size", size},
        {"modifiedAt", modifiedAt},
        {"checksum", checksum},
        {"extractedText", extractedText},
        {"isDeleted", isDeleted},
        {"deviceId", deviceId},
        {"syncTimestamp", syncTimestamp}
    };
    return j.dump();
}

std::optional<IndexDeltaPayload> IndexDeltaPayload::fromJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        IndexDeltaPayload p;
        p.fileId = j.value("fileId", 0);
        p.path = j.value("path", "");
        p.name = j.value("name", "");
        p.mimeType = j.value("mimeType", "");
        p.size = j.value("size", 0);
        p.modifiedAt = j.value("modifiedAt", 0);
        p.checksum = j.value("checksum", "");
        p.extractedText = j.value("extractedText", "");
        p.isDeleted = j.value("isDeleted", false);
        p.deviceId = j.value("deviceId", "");
        p.syncTimestamp = j.value("syncTimestamp", 0);
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════
// FileRequestPayload
// ═══════════════════════════════════════════════════════════

std::string FileRequestPayload::toJson() const {
    json j = {
        {"fileId", fileId},
        {"checksum", checksum},
        {"offset", offset},
        {"length", length}
    };
    return j.dump();
}

std::optional<FileRequestPayload> FileRequestPayload::fromJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        FileRequestPayload p;
        p.fileId = j.value("fileId", 0);
        p.checksum = j.value("checksum", "");
        p.offset = j.value("offset", 0);
        p.length = j.value("length", 0);
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════
// FileChunkHeader
// ═══════════════════════════════════════════════════════════

std::vector<uint8_t> FileChunkHeader::serialize() const {
    std::vector<uint8_t> result(HEADER_SIZE);
    uint8_t* ptr = result.data();

    // fileId (8 bytes, big-endian)
    for (int i = 7; i >= 0; --i) {
        *ptr++ = (fileId >> (i * 8)) & 0xFF;
    }

    // offset (8 bytes)
    for (int i = 7; i >= 0; --i) {
        *ptr++ = (offset >> (i * 8)) & 0xFF;
    }

    // totalSize (8 bytes)
    for (int i = 7; i >= 0; --i) {
        *ptr++ = (totalSize >> (i * 8)) & 0xFF;
    }

    // chunkSize (4 bytes)
    ptr[0] = (chunkSize >> 24) & 0xFF;
    ptr[1] = (chunkSize >> 16) & 0xFF;
    ptr[2] = (chunkSize >> 8) & 0xFF;
    ptr[3] = chunkSize & 0xFF;
    ptr += 4;

    // isLast (1 byte)
    *ptr = isLast ? 1 : 0;

    return result;
}

std::optional<FileChunkHeader> FileChunkHeader::deserialize(const uint8_t* data, size_t size) {
    if (size < HEADER_SIZE) {
        return std::nullopt;
    }

    FileChunkHeader h;
    const uint8_t* ptr = data;

    // fileId
    h.fileId = 0;
    for (int i = 0; i < 8; ++i) {
        h.fileId = (h.fileId << 8) | *ptr++;
    }

    // offset
    h.offset = 0;
    for (int i = 0; i < 8; ++i) {
        h.offset = (h.offset << 8) | *ptr++;
    }

    // totalSize
    h.totalSize = 0;
    for (int i = 0; i < 8; ++i) {
        h.totalSize = (h.totalSize << 8) | *ptr++;
    }

    // chunkSize
    h.chunkSize = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
    ptr += 4;

    // isLast
    h.isLast = (*ptr != 0);

    return h;
}

// ═══════════════════════════════════════════════════════════
// SearchRequestPayload
// ═══════════════════════════════════════════════════════════

std::string SearchRequestPayload::toJson() const {
    json j = {
        {"query", query},
        {"limit", limit},
        {"offset", offset}
    };
    return j.dump();
}

std::optional<SearchRequestPayload> SearchRequestPayload::fromJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        SearchRequestPayload p;
        p.query = j.value("query", "");
        p.limit = j.value("limit", 50);
        p.offset = j.value("offset", 0);
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════
// PairingRequestPayload
// ═══════════════════════════════════════════════════════════

std::string PairingRequestPayload::toJson() const {
    json j = {
        {"pin", pin},
        {"deviceId", deviceId},
        {"deviceName", deviceName},
        {"deviceType", static_cast<int>(deviceType)}
    };
    return j.dump();
}

std::optional<PairingRequestPayload> PairingRequestPayload::fromJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        PairingRequestPayload p;
        p.pin = j.value("pin", "");
        p.deviceId = j.value("deviceId", "");
        p.deviceName = j.value("deviceName", "");
        p.deviceType = static_cast<DeviceType>(j.value("deviceType", 0));
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════
// PairingResponsePayload
// ═══════════════════════════════════════════════════════════

std::string PairingResponsePayload::toJson() const {
    json j = {
        {"success", success},
        {"errorCode", errorCode},
        {"errorMessage", errorMessage}
    };
    
    // Encode family secret as hex if present
    if (success && !familySecret.empty()) {
        std::string secretHex;
        secretHex.reserve(familySecret.size() * 2);
        for (uint8_t byte : familySecret) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", byte);
            secretHex.append(buf);
        }
        j["familySecret"] = secretHex;
    }
    
    return j.dump();
}

std::optional<PairingResponsePayload> PairingResponsePayload::fromJson(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        PairingResponsePayload p;
        p.success = j.value("success", false);
        p.errorCode = j.value("errorCode", "");
        p.errorMessage = j.value("errorMessage", "");
        
        // Decode family secret from hex
        if (j.contains("familySecret") && j["familySecret"].is_string()) {
            std::string secretHex = j["familySecret"].get<std::string>();
            p.familySecret.reserve(secretHex.size() / 2);
            for (size_t i = 0; i + 1 < secretHex.size(); i += 2) {
                uint8_t byte = static_cast<uint8_t>(
                    std::stoi(secretHex.substr(i, 2), nullptr, 16));
                p.familySecret.push_back(byte);
            }
        }
        
        return p;
    } catch (...) {
        return std::nullopt;
    }
}

// ═══════════════════════════════════════════════════════════
// generateRequestId
// ═══════════════════════════════════════════════════════════

std::string generateRequestId() {
    uint8_t bytes[16];
    RAND_bytes(bytes, sizeof(bytes));

    // Format as UUID
    char buf[37];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11],
             bytes[12], bytes[13], bytes[14], bytes[15]);
    return buf;
}

} // namespace FamilyVault

