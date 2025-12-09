// FamilyPairing.cpp — Реализация семейного pairing
// Криптография через OpenSSL

#include "familyvault/FamilyPairing.h"
#include "familyvault/Network/PairingServer.h"
#include "familyvault/Network/NetworkProtocol.h"
#include "familyvault/Network/Discovery.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <chrono>
#include <mutex>
#include <random>

#ifdef _WIN32
#include <rpc.h>
#pragma comment(lib, "rpcrt4.lib")
#elif !defined(ANDROID) && !defined(__ANDROID__)
#include <uuid/uuid.h>
#endif

namespace FamilyVault {

using json = nlohmann::json;

// ═══════════════════════════════════════════════════════════
// Crypto namespace implementation
// ═══════════════════════════════════════════════════════════

namespace Crypto {

std::vector<uint8_t> randomBytes(size_t count) {
    std::vector<uint8_t> result(count);
    if (RAND_bytes(result.data(), static_cast<int>(count)) != 1) {
        spdlog::error("Crypto::randomBytes: RAND_bytes failed");
        throw std::runtime_error("Failed to generate random bytes");
    }
    return result;
}

std::vector<uint8_t> hkdf(
    const std::vector<uint8_t>& ikm,
    const std::string& salt,
    const std::string& info,
    size_t outputLength
) {
    std::vector<uint8_t> result(outputLength);

    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
    if (!pctx) {
        throw std::runtime_error("EVP_PKEY_CTX_new_id failed");
    }

    bool success = false;
    do {
        if (EVP_PKEY_derive_init(pctx) <= 0) break;
        if (EVP_PKEY_CTX_set_hkdf_md(pctx, EVP_sha256()) <= 0) break;
        if (EVP_PKEY_CTX_set1_hkdf_salt(pctx, 
            reinterpret_cast<const unsigned char*>(salt.data()), 
            static_cast<int>(salt.size())) <= 0) break;
        if (EVP_PKEY_CTX_set1_hkdf_key(pctx, 
            ikm.data(), 
            static_cast<int>(ikm.size())) <= 0) break;
        if (EVP_PKEY_CTX_add1_hkdf_info(pctx, 
            reinterpret_cast<const unsigned char*>(info.data()), 
            static_cast<int>(info.size())) <= 0) break;

        size_t outlen = outputLength;
        if (EVP_PKEY_derive(pctx, result.data(), &outlen) <= 0) break;

        success = true;
    } while (false);

    EVP_PKEY_CTX_free(pctx);

    if (!success) {
        throw std::runtime_error("HKDF derivation failed");
    }

    return result;
}

std::string generatePin(const std::vector<uint8_t>& secret, const std::vector<uint8_t>& nonce) {
    // Объединяем secret и nonce
    std::vector<uint8_t> combined;
    combined.reserve(secret.size() + nonce.size());
    combined.insert(combined.end(), secret.begin(), secret.end());
    combined.insert(combined.end(), nonce.begin(), nonce.end());

    // Деривируем 4 байта для PIN
    auto derived = hkdf(combined, "familyvault-pin", "pin-derivation", 4);

    // Конвертируем в число и берём 6 цифр
    uint32_t pinValue = 0;
    for (int i = 0; i < 4; i++) {
        pinValue = (pinValue << 8) | derived[i];
    }
    pinValue = pinValue % 1000000;

    // Форматируем с ведущими нулями
    char pinStr[7];
    snprintf(pinStr, sizeof(pinStr), "%06u", pinValue);
    return std::string(pinStr);
}

std::string generateUUID() {
#ifdef _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    RPC_CSTR uuidStr;
    UuidToStringA(&uuid, &uuidStr);
    std::string result(reinterpret_cast<char*>(uuidStr));
    RpcStringFreeA(&uuidStr);
    return result;
#elif defined(ANDROID) || defined(__ANDROID__)
    // Android: generate UUID from random bytes
    auto bytes = randomBytes(16);
    // Set version 4 (random) and variant 1 (RFC 4122)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;  // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80;  // Variant 1
    
    char uuidStr[37];
    snprintf(uuidStr, sizeof(uuidStr),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5],
        bytes[6], bytes[7],
        bytes[8], bytes[9],
        bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(uuidStr);
#else
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuidStr[37];
    uuid_unparse_lower(uuid, uuidStr);
    return std::string(uuidStr);
#endif
}

} // namespace Crypto

// ═══════════════════════════════════════════════════════════
// PairingInfo implementation
// ═══════════════════════════════════════════════════════════

int PairingInfo::secondsRemaining() const {
    auto now = std::chrono::system_clock::now();
    auto nowTs = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    return static_cast<int>(expiresAt - nowTs);
}

bool PairingInfo::isExpired() const {
    return secondsRemaining() <= 0;
}

// ═══════════════════════════════════════════════════════════
// QRCodeData implementation
// ═══════════════════════════════════════════════════════════

static std::string base64Encode(const std::string& input) {
    static const char* b64chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    result.reserve(((input.size() + 2) / 3) * 4);

    for (size_t i = 0; i < input.size(); i += 3) {
        int b = (input[i] & 0xFC) >> 2;
        result.push_back(b64chars[b]);

        b = (input[i] & 0x03) << 4;
        if (i + 1 < input.size()) {
            b |= (input[i + 1] & 0xF0) >> 4;
            result.push_back(b64chars[b]);
            b = (input[i + 1] & 0x0F) << 2;
            if (i + 2 < input.size()) {
                b |= (input[i + 2] & 0xC0) >> 6;
                result.push_back(b64chars[b]);
                b = input[i + 2] & 0x3F;
                result.push_back(b64chars[b]);
            } else {
                result.push_back(b64chars[b]);
                result.push_back('=');
            }
        } else {
            result.push_back(b64chars[b]);
            result.append("==");
        }
    }

    return result;
}

static std::string base64Decode(const std::string& input) {
    static const int b64index[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    std::string result;
    result.reserve((input.size() / 4) * 3);

    for (size_t i = 0; i < input.size(); i += 4) {
        int n = b64index[(unsigned char)input[i]] << 18;
        n |= b64index[(unsigned char)input[i + 1]] << 12;
        result.push_back((n >> 16) & 0xFF);

        if (input[i + 2] != '=') {
            n |= b64index[(unsigned char)input[i + 2]] << 6;
            result.push_back((n >> 8) & 0xFF);
        }
        if (input[i + 3] != '=') {
            n |= b64index[(unsigned char)input[i + 3]];
            result.push_back(n & 0xFF);
        }
    }

    return result;
}

std::optional<QRCodeData> QRCodeData::fromBase64(const std::string& base64Data) {
    try {
        std::string decoded = base64Decode(base64Data);
        auto j = json::parse(decoded);

        QRCodeData result;
        result.pin = j.value("pin", "");
        result.host = j.at("host").get<std::string>();
        result.port = j.at("port").get<uint16_t>();
        result.expiresAt = j.at("expires").get<int64_t>();

        // Decode nonce from hex (optional)
        if (j.contains("nonce")) {
            std::string nonceHex = j.at("nonce").get<std::string>();
            result.nonce.reserve(nonceHex.size() / 2);
            for (size_t i = 0; i < nonceHex.size(); i += 2) {
                uint8_t byte = static_cast<uint8_t>(
                    std::stoi(nonceHex.substr(i, 2), nullptr, 16));
                result.nonce.push_back(byte);
            }
        }

        return result;
    } catch (const std::exception& e) {
        spdlog::error("QRCodeData::fromBase64 failed: {}", e.what());
        return std::nullopt;
    }
}

std::optional<QRCodeData> QRCodeData::fromUrl(const std::string& url) {
    // Parse URL format: fv://join?pin=123456&host=192.168.1.100&port=45680
    try {
        if (url.substr(0, 10) != "fv://join?") {
            spdlog::error("QRCodeData::fromUrl: Invalid URL scheme");
            return std::nullopt;
        }
        
        std::string query = url.substr(10);
        QRCodeData result;
        result.port = 45680;  // default
        result.expiresAt = 0;
        
        // Parse query parameters
        size_t pos = 0;
        while (pos < query.size()) {
            size_t ampPos = query.find('&', pos);
            if (ampPos == std::string::npos) ampPos = query.size();
            
            std::string param = query.substr(pos, ampPos - pos);
            size_t eqPos = param.find('=');
            if (eqPos != std::string::npos) {
                std::string key = param.substr(0, eqPos);
                std::string value = param.substr(eqPos + 1);
                
                if (key == "pin") result.pin = value;
                else if (key == "host") result.host = value;
                else if (key == "port") result.port = static_cast<uint16_t>(std::stoi(value));
            }
            pos = ampPos + 1;
        }
        
        if (result.pin.empty() || result.host.empty()) {
            spdlog::error("QRCodeData::fromUrl: Missing pin or host");
            return std::nullopt;
        }
        
        return result;
    } catch (const std::exception& e) {
        spdlog::error("QRCodeData::fromUrl failed: {}", e.what());
        return std::nullopt;
    }
}

std::string QRCodeData::toBase64() const {
    // Convert nonce to hex
    std::string nonceHex;
    nonceHex.reserve(nonce.size() * 2);
    for (uint8_t byte : nonce) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", byte);
        nonceHex.append(buf);
    }

    json j = {
        {"pin", pin},
        {"host", host},
        {"port", port},
        {"nonce", nonceHex},
        {"expires", expiresAt}
    };

    return base64Encode(j.dump());
}

std::string QRCodeData::toUrl() const {
    return "fv://join?pin=" + pin + "&host=" + host + "&port=" + std::to_string(port);
}

// ═══════════════════════════════════════════════════════════
// FamilyPairing::Impl
// ═══════════════════════════════════════════════════════════

class FamilyPairing::Impl {
public:
    explicit Impl(std::shared_ptr<SecureStorage> storage)
        : m_storage(std::move(storage))
        , m_pairingServer(std::make_unique<PairingServer>()) {
        
        // Загружаем или генерируем device_id
        auto deviceIdOpt = m_storage->retrieveString(SecureStorage::KEY_DEVICE_ID);
        if (deviceIdOpt) {
            m_deviceId = *deviceIdOpt;
        } else {
            m_deviceId = Crypto::generateUUID();
            m_storage->storeString(SecureStorage::KEY_DEVICE_ID, m_deviceId);
            spdlog::info("FamilyPairing: Generated new device ID: {}", m_deviceId);
        }

        // Загружаем имя устройства
        auto deviceNameOpt = m_storage->retrieveString(SecureStorage::KEY_DEVICE_NAME);
        if (deviceNameOpt) {
            m_deviceName = *deviceNameOpt;
        } else {
            m_deviceName = getDefaultDeviceName();
            m_storage->storeString(SecureStorage::KEY_DEVICE_NAME, m_deviceName);
        }

        spdlog::info("FamilyPairing: Initialized, device='{}', configured={}", 
            m_deviceName, isConfigured());
    }

    bool isConfigured() const {
        return m_storage->exists(SecureStorage::KEY_FAMILY_SECRET);
    }

    std::string getDeviceId() const {
        return m_deviceId;
    }

    std::string getDeviceName() const {
        return m_deviceName;
    }

    void setDeviceName(const std::string& name) {
        m_deviceName = name;
        m_storage->storeString(SecureStorage::KEY_DEVICE_NAME, name);
    }

    DeviceType getDeviceType() const {
#ifdef __ANDROID__
        return DeviceType::Mobile;
#else
        return DeviceType::Desktop;
#endif
    }

    std::optional<PairingInfo> createFamily() {
        // Note: no lock here, startPairingServerInternal will lock
        
        // Генерируем новый family_secret
        auto familySecret = Crypto::randomBytes(FAMILY_SECRET_SIZE);
        
        // Сохраняем в secure storage
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_storage->store(SecureStorage::KEY_FAMILY_SECRET, familySecret)) {
                spdlog::error("FamilyPairing::createFamily: Failed to store family_secret");
                return std::nullopt;
            }
        }

        spdlog::info("FamilyPairing::createFamily: New family created");

        // Генерируем pairing info
        auto info = generatePairingInfoInternal(familySecret);
        
        // Автоматически запускаем pairing server
        if (info) {
            if (!startPairingServerInternal(0, familySecret)) {
                spdlog::warn("FamilyPairing::createFamily: Failed to start pairing server");
                // Продолжаем - PIN можно использовать вручную
            }
        }
        
        return info;
    }

    std::optional<PairingInfo> regeneratePin() {
        auto familySecret = m_storage->retrieve(SecureStorage::KEY_FAMILY_SECRET);
        if (!familySecret) {
            spdlog::error("FamilyPairing::regeneratePin: No family configured");
            return std::nullopt;
        }

        auto info = generatePairingInfoInternal(*familySecret);
        
        // Перезапускаем pairing server с новым PIN
        if (info) {
            stopPairingServer();  // Остановить старый
            if (!startPairingServerInternal(0, *familySecret)) {
                spdlog::warn("FamilyPairing::regeneratePin: Failed to restart pairing server");
            }
        }
        
        return info;
    }

    void cancelPairing() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingPairing.reset();
        spdlog::debug("FamilyPairing::cancelPairing: Pairing cancelled");
    }

    bool hasPendingPairing() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pendingPairing.has_value() && !m_pendingPairing->isExpired();
    }

    JoinResult joinByPin(const std::string& pin, 
                         const std::string& initiatorHost,
                         uint16_t initiatorPort) {
        if (isConfigured()) {
            return JoinResult::AlreadyConfigured;
        }

        if (initiatorHost.empty()) {
            spdlog::error("FamilyPairing::joinByPin: initiatorHost is required");
            return JoinResult::NetworkError;
        }

        uint16_t port = (initiatorPort == 0) ? PAIRING_PORT : initiatorPort;

        spdlog::info("FamilyPairing::joinByPin: Connecting to {}:{} with PIN {}", 
                     initiatorHost, port, pin);

        // Create pairing request
        PairingRequestPayload request;
        request.pin = pin;
        request.deviceId = m_deviceId;
        request.deviceName = m_deviceName;
        request.deviceType = getDeviceType();

        // Send pairing request
        PairingClient client;
        auto response = client.pair(initiatorHost, port, request, 15000);

        if (!response) {
            spdlog::error("FamilyPairing::joinByPin: Network error: {}", client.getLastError());
            return JoinResult::NetworkError;
        }

        if (!response->success) {
            spdlog::warn("FamilyPairing::joinByPin: Pairing failed: {} - {}", 
                         response->errorCode, response->errorMessage);
            
            if (response->errorCode == "INVALID_PIN") {
                return JoinResult::InvalidPin;
            } else if (response->errorCode == "EXPIRED") {
                return JoinResult::Expired;
            } else if (response->errorCode == "RATE_LIMITED") {
                return JoinResult::RateLimited;
            }
            return JoinResult::InternalError;
        }

        // Save family secret
        if (response->familySecret.size() != FAMILY_SECRET_SIZE) {
            spdlog::error("FamilyPairing::joinByPin: Invalid family_secret size: {}", 
                          response->familySecret.size());
            return JoinResult::InternalError;
        }

        if (!setFamilySecret(response->familySecret)) {
            spdlog::error("FamilyPairing::joinByPin: Failed to save family_secret");
            return JoinResult::InternalError;
        }

        spdlog::info("FamilyPairing::joinByPin: Successfully joined family!");
        return JoinResult::Success;
    }

    JoinResult joinByQR(const std::string& qrData) {
        // Try URL format first (fv://join?pin=...&host=...&port=...)
        auto qr = QRCodeData::fromUrl(qrData);
        if (!qr) {
            // Fallback to base64 JSON format
            qr = QRCodeData::fromBase64(qrData);
        }
        
        if (!qr) {
            spdlog::error("FamilyPairing::joinByQR: Failed to parse QR data");
            return JoinResult::InvalidPin;
        }

        if (qr->pin.empty()) {
            spdlog::error("FamilyPairing::joinByQR: No PIN in QR data");
            return JoinResult::InvalidPin;
        }

        // Check expiration if available
        if (qr->expiresAt > 0) {
            auto now = std::chrono::system_clock::now();
            auto nowTs = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();

            if (nowTs > qr->expiresAt) {
                return JoinResult::Expired;
            }
        }

        spdlog::info("FamilyPairing::joinByQR: Connecting to {}:{} with PIN", 
                     qr->host, qr->port);
        return joinByPin(qr->pin, qr->host, qr->port);
    }

    std::optional<std::array<uint8_t, PSK_SIZE>> derivePsk() const {
        auto familySecret = m_storage->retrieve(SecureStorage::KEY_FAMILY_SECRET);
        if (!familySecret) {
            return std::nullopt;
        }

        auto pskVec = Crypto::hkdf(
            *familySecret,
            "familyvault-psk-v1",
            "tls13-psk",
            PSK_SIZE
        );

        std::array<uint8_t, PSK_SIZE> result;
        std::copy(pskVec.begin(), pskVec.end(), result.begin());
        return result;
    }

    std::string getPskIdentity() const {
        return m_deviceId;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_storage->remove(SecureStorage::KEY_FAMILY_SECRET);
        m_pendingPairing.reset();
        spdlog::info("FamilyPairing::reset: Family configuration cleared");
    }

    // Для внутреннего использования — установить family_secret напрямую
    // (используется при успешном pairing)
    bool setFamilySecret(const std::vector<uint8_t>& secret) {
        if (secret.size() != FAMILY_SECRET_SIZE) {
            return false;
        }
        return m_storage->store(SecureStorage::KEY_FAMILY_SECRET, secret);
    }

    bool startPairingServerInternal(uint16_t port, const std::vector<uint8_t>& familySecret) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_pairingServer->isRunning()) {
            return true;  // Already running
        }

        uint16_t serverPort = (port == 0) ? PAIRING_PORT : port;

        // Start server with callback
        // Copy familySecret to ensure it lives for the lambda
        auto secretCopy = std::make_shared<std::vector<uint8_t>>(familySecret);
        return m_pairingServer->start(serverPort, [this, secretCopy](const PairingRequestPayload& request) {
            return handlePairingRequest(request, *secretCopy);
        });
    }

    bool startPairingServer(uint16_t port) {
        if (m_pairingServer->isRunning()) {
            return true;  // Already running
        }

        if (!isConfigured()) {
            spdlog::error("FamilyPairing::startPairingServer: Family not configured");
            return false;
        }

        // Get family secret for validation
        auto familySecret = m_storage->retrieve(SecureStorage::KEY_FAMILY_SECRET);
        if (!familySecret) {
            spdlog::error("FamilyPairing::startPairingServer: No family_secret");
            return false;
        }

        return startPairingServerInternal(port, *familySecret);
    }

    void stopPairingServer() {
        m_pairingServer->stop();
    }

    bool isPairingServerRunning() const {
        return m_pairingServer->isRunning();
    }

    uint16_t getPairingServerPort() const {
        return m_pairingServer->getPort();
    }

private:
    std::shared_ptr<SecureStorage> m_storage;
    std::unique_ptr<PairingServer> m_pairingServer;
    std::string m_deviceId;
    std::string m_deviceName;
    std::optional<PairingInfo> m_pendingPairing;
    std::vector<uint8_t> m_currentNonce;
    mutable std::mutex m_mutex;
    int m_failedAttempts = 0;
    std::chrono::steady_clock::time_point m_rateLimitUntil;

    PairingResponsePayload handlePairingRequest(const PairingRequestPayload& request,
                                                 const std::vector<uint8_t>& familySecret) {
        PairingResponsePayload response;
        
        // Check rate limit
        auto now = std::chrono::steady_clock::now();
        if (now < m_rateLimitUntil) {
            auto remainingSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                m_rateLimitUntil - now).count();
            response.success = false;
            response.errorCode = "RATE_LIMITED";
            response.errorMessage = "Too many attempts. Try again in " + 
                                    std::to_string(remainingSeconds) + " seconds";
            spdlog::warn("FamilyPairing: Rate limited request from '{}'", request.deviceName);
            return response;
        }

        // Check if we have a pending pairing session
        if (!m_pendingPairing || m_pendingPairing->isExpired()) {
            response.success = false;
            response.errorCode = "EXPIRED";
            response.errorMessage = "No active pairing session or session expired";
            spdlog::warn("FamilyPairing: No active pairing session for request from '{}'", 
                         request.deviceName);
            return response;
        }

        // Validate PIN
        std::string expectedPin = Crypto::generatePin(familySecret, m_currentNonce);
        
        if (request.pin != expectedPin) {
            m_failedAttempts++;
            spdlog::warn("FamilyPairing: Invalid PIN from '{}' (attempt {})", 
                         request.deviceName, m_failedAttempts);
            
            if (m_failedAttempts >= MAX_PIN_ATTEMPTS) {
                m_rateLimitUntil = now + std::chrono::seconds(RATE_LIMIT_SECONDS);
                m_failedAttempts = 0;
                response.success = false;
                response.errorCode = "RATE_LIMITED";
                response.errorMessage = "Too many invalid attempts. Locked for " + 
                                        std::to_string(RATE_LIMIT_SECONDS) + " seconds";
            } else {
                response.success = false;
                response.errorCode = "INVALID_PIN";
                response.errorMessage = "Invalid PIN. " + 
                                        std::to_string(MAX_PIN_ATTEMPTS - m_failedAttempts) + 
                                        " attempts remaining";
            }
            return response;
        }

        // PIN is valid - send family secret
        spdlog::info("FamilyPairing: Valid PIN from '{}' ({}) - sending family_secret", 
                     request.deviceName, request.deviceId);
        
        m_failedAttempts = 0;
        response.success = true;
        response.familySecret = familySecret;
        
        // Clear pending pairing after successful join
        // (optionally keep it for multiple devices to join)
        // m_pendingPairing.reset();
        
        return response;
    }

    std::optional<PairingInfo> generatePairingInfoInternal(const std::vector<uint8_t>& familySecret) {
        // Генерируем nonce для этой сессии
        m_currentNonce = Crypto::randomBytes(16);

        // Генерируем PIN
        std::string pin = Crypto::generatePin(familySecret, m_currentNonce);

        // Время истечения
        auto now = std::chrono::system_clock::now();
        auto expiresAt = now + std::chrono::seconds(PIN_TTL_SECONDS);
        int64_t expiresTs = std::chrono::duration_cast<std::chrono::seconds>(
            expiresAt.time_since_epoch()).count();
        int64_t createdTs = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        // Определяем реальный IP для QR
        std::string hostIp = "127.0.0.1";
        auto localIps = NetworkDiscovery::getLocalIpAddresses();
        if (!localIps.empty()) {
            // Предпочитаем приватные IP (192.168.x.x, 10.x.x.x, 172.16-31.x.x)
            for (const auto& ip : localIps) {
                if (ip.substr(0, 8) == "192.168." || 
                    ip.substr(0, 3) == "10." ||
                    (ip.substr(0, 4) == "172." && ip.size() > 6)) {
                    hostIp = ip;
                    break;
                }
            }
            if (hostIp == "127.0.0.1" && !localIps.empty()) {
                hostIp = localIps.front();
            }
        }

        // Порт pairing сервера (если запущен)
        uint16_t port = m_pairingServer->getPort();
        if (port == 0) {
            port = PAIRING_PORT;  // Дефолтный порт
        }

        // Генерируем QR данные
        QRCodeData qrData;
        qrData.pin = pin;
        qrData.host = hostIp;
        qrData.port = port;
        qrData.nonce = m_currentNonce;
        qrData.expiresAt = expiresTs;

        PairingInfo info;
        info.pin = pin;
        info.qrData = qrData.toBase64();  // или qrData.toUrl() для URL формата
        info.expiresAt = expiresTs;
        info.createdAt = createdTs;

        m_pendingPairing = info;

        spdlog::info("FamilyPairing: Generated PIN {}, host {}:{}, expires in {}s", 
                     pin, hostIp, port, PIN_TTL_SECONDS);
        return info;
    }

    std::string getDefaultDeviceName() {
#ifdef _WIN32
        char computerName[256];
        DWORD size = sizeof(computerName);
        if (GetComputerNameA(computerName, &size)) {
            return std::string(computerName);
        }
        return "Windows PC";
#elif defined(__ANDROID__)
        return "Android Device";
#else
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            return std::string(hostname);
        }
        return "Linux Device";
#endif
    }
};

// ═══════════════════════════════════════════════════════════
// FamilyPairing Public Interface
// ═══════════════════════════════════════════════════════════

FamilyPairing::FamilyPairing(std::shared_ptr<SecureStorage> storage)
    : m_impl(std::make_unique<Impl>(std::move(storage))) {
}

FamilyPairing::~FamilyPairing() = default;

bool FamilyPairing::isConfigured() const {
    return m_impl->isConfigured();
}

std::string FamilyPairing::getDeviceId() const {
    return m_impl->getDeviceId();
}

std::string FamilyPairing::getDeviceName() const {
    return m_impl->getDeviceName();
}

void FamilyPairing::setDeviceName(const std::string& name) {
    m_impl->setDeviceName(name);
}

DeviceType FamilyPairing::getDeviceType() const {
    return m_impl->getDeviceType();
}

std::optional<PairingInfo> FamilyPairing::createFamily() {
    return m_impl->createFamily();
}

std::optional<PairingInfo> FamilyPairing::regeneratePin() {
    return m_impl->regeneratePin();
}

void FamilyPairing::cancelPairing() {
    m_impl->cancelPairing();
}

bool FamilyPairing::hasPendingPairing() const {
    return m_impl->hasPendingPairing();
}

JoinResult FamilyPairing::joinByPin(const std::string& pin,
                                     const std::string& initiatorHost,
                                     uint16_t initiatorPort) {
    return m_impl->joinByPin(pin, initiatorHost, initiatorPort);
}

JoinResult FamilyPairing::joinByQR(const std::string& qrData) {
    return m_impl->joinByQR(qrData);
}

std::optional<std::array<uint8_t, PSK_SIZE>> FamilyPairing::derivePsk() const {
    return m_impl->derivePsk();
}

std::string FamilyPairing::getPskIdentity() const {
    return m_impl->getPskIdentity();
}

void FamilyPairing::reset() {
    m_impl->reset();
}

bool FamilyPairing::startPairingServer(uint16_t port) {
    return m_impl->startPairingServer(port);
}

void FamilyPairing::stopPairingServer() {
    m_impl->stopPairingServer();
}

bool FamilyPairing::isPairingServerRunning() const {
    return m_impl->isPairingServerRunning();
}

uint16_t FamilyPairing::getPairingServerPort() const {
    return m_impl->getPairingServerPort();
}

} // namespace FamilyVault

