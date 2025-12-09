// FamilyPairing.h — Управление семейным pairing
// Создание семьи, присоединение через PIN/QR, деривация PSK
// ЕДИНСТВЕННЫЙ ИСТОЧНИК ИСТИНЫ для FamilyPairing API

#pragma once

#include "export.h"
#include "SecureStorage.h"
#include "Types.h"
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <memory>
#include <chrono>
#include <cstdint>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Константы
// ═══════════════════════════════════════════════════════════

constexpr size_t FAMILY_SECRET_SIZE = 32;  // 256 бит
constexpr size_t PSK_SIZE = 32;            // 256 бит для TLS
constexpr size_t PIN_LENGTH = 6;           // 6 цифр
constexpr int PIN_TTL_SECONDS = 300;       // 5 минут
constexpr int MAX_PIN_ATTEMPTS = 3;        // До rate limit
constexpr int RATE_LIMIT_SECONDS = 30;     // Cooldown

// ═══════════════════════════════════════════════════════════
// Структуры данных
// ═══════════════════════════════════════════════════════════

/// Информация о pairing сессии (для UI)
struct PairingInfo {
    std::string pin;                    // 6-значный PIN
    std::string qrData;                 // Base64 JSON для QR-кода
    int64_t expiresAt;                  // Unix timestamp истечения
    int64_t createdAt;                  // Unix timestamp создания
    
    /// Сколько секунд осталось
    int secondsRemaining() const;
    
    /// Истёк ли PIN
    bool isExpired() const;
};

/// Данные из QR-кода
struct QRCodeData {
    std::string pin;                    // 6-значный PIN
    std::string host;                   // IP инициатора
    uint16_t port;                      // Порт для pairing
    std::vector<uint8_t> nonce;         // Уникальный nonce сессии (опционально)
    int64_t expiresAt;                  // Когда истекает
    
    /// Парсинг из base64 JSON
    static std::optional<QRCodeData> fromBase64(const std::string& base64Data);
    
    /// Парсинг из URL формата (fv://join?pin=...&host=...&port=...)
    static std::optional<QRCodeData> fromUrl(const std::string& url);
    
    /// Конвертация в base64 JSON
    std::string toBase64() const;
    
    /// Конвертация в URL формат
    std::string toUrl() const;
};

/// Результат присоединения
enum class JoinResult {
    Success = 0,
    InvalidPin = 1,
    Expired = 2,
    RateLimited = 3,
    NetworkError = 4,
    AlreadyConfigured = 5,
    InternalError = 99
};

// ═══════════════════════════════════════════════════════════
// FamilyPairing — управление семейным pairing
// ═══════════════════════════════════════════════════════════

class FV_API FamilyPairing {
public:
    /// Создать FamilyPairing с указанным SecureStorage
    explicit FamilyPairing(std::shared_ptr<SecureStorage> storage);
    ~FamilyPairing();

    // Запрет копирования
    FamilyPairing(const FamilyPairing&) = delete;
    FamilyPairing& operator=(const FamilyPairing&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Статус семьи
    // ═══════════════════════════════════════════════════════════

    /// Настроена ли семья (есть family_secret)
    bool isConfigured() const;

    /// Получить ID этого устройства
    std::string getDeviceId() const;

    /// Получить имя этого устройства
    std::string getDeviceName() const;

    /// Установить имя устройства
    void setDeviceName(const std::string& name);

    /// Получить тип устройства
    DeviceType getDeviceType() const;

    // ═══════════════════════════════════════════════════════════
    // Создание семьи (первое устройство)
    // ═══════════════════════════════════════════════════════════

    /// Создать новую семью
    /// Генерирует family_secret, сохраняет его, возвращает PIN/QR для других
    /// @return PairingInfo с PIN и QR данными
    std::optional<PairingInfo> createFamily();

    /// Обновить PIN (если истёк или нужен новый)
    std::optional<PairingInfo> regeneratePin();

    /// Запустить pairing сервер (начать приём входящих pairing запросов)
    /// Автоматически вызывается из createFamily/regeneratePin
    /// @param port Порт (0 = использовать PAIRING_PORT)
    /// @return true если успешно
    bool startPairingServer(uint16_t port = 0);

    /// Остановить pairing сервер
    void stopPairingServer();

    /// Проверить, запущен ли pairing сервер
    bool isPairingServerRunning() const;

    /// Получить порт pairing сервера
    uint16_t getPairingServerPort() const;

    /// Отменить активную pairing сессию
    void cancelPairing();

    /// Есть ли активная pairing сессия
    bool hasPendingPairing() const;

    // ═══════════════════════════════════════════════════════════
    // Присоединение к семье (последующие устройства)
    // ═══════════════════════════════════════════════════════════

    /// Присоединиться по PIN
    /// @param pin 6-значный PIN от инициатора
    /// @param initiatorHost IP инициатора (опционально, для прямого подключения)
    /// @param initiatorPort Порт инициатора
    /// @return Результат присоединения
    JoinResult joinByPin(const std::string& pin, 
                         const std::string& initiatorHost = "",
                         uint16_t initiatorPort = 0);

    /// Присоединиться по QR
    /// @param qrData Base64 данные из QR-кода
    /// @return Результат присоединения
    JoinResult joinByQR(const std::string& qrData);

    // ═══════════════════════════════════════════════════════════
    // Криптографические операции
    // ═══════════════════════════════════════════════════════════

    /// Получить PSK для TLS 1.3
    /// PSK = HKDF(family_secret, "familyvault-psk-v1", "tls13-psk")
    /// @return 32-байтовый PSK или nullopt если семья не настроена
    std::optional<std::array<uint8_t, PSK_SIZE>> derivePsk() const;

    /// Получить PSK Identity (deviceId для TLS)
    std::string getPskIdentity() const;

    // ═══════════════════════════════════════════════════════════
    // Сброс
    // ═══════════════════════════════════════════════════════════

    /// Сбросить семейную конфигурацию
    /// ОСТОРОЖНО: удаляет family_secret, устройство больше не сможет
    /// подключаться к семье без повторного pairing
    void reset();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// ═══════════════════════════════════════════════════════════
// Вспомогательные функции (для тестирования)
// ═══════════════════════════════════════════════════════════

namespace Crypto {

/// Генерация криптографически стойких случайных байт
FV_API std::vector<uint8_t> randomBytes(size_t count);

/// HKDF-SHA256
FV_API std::vector<uint8_t> hkdf(
    const std::vector<uint8_t>& ikm,    // Input key material
    const std::string& salt,
    const std::string& info,
    size_t outputLength
);

/// Генерация PIN из secret
FV_API std::string generatePin(const std::vector<uint8_t>& secret, 
                               const std::vector<uint8_t>& nonce);

/// Генерация UUID v4
FV_API std::string generateUUID();

} // namespace Crypto

} // namespace FamilyVault

