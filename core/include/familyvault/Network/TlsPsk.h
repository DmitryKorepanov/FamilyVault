// TlsPsk.h — TLS 1.3 PSK Connection
// См. SPECIFICATIONS.md, раздел 7.2

#pragma once

#include "../export.h"
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <functional>
#include <cstdint>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Константы
// ═══════════════════════════════════════════════════════════

// PSK_SIZE defined in FamilyPairing.h (32 bytes)
constexpr size_t TLS_PSK_SIZE = 32;             // 256-bit PSK
constexpr uint16_t TLS_SERVICE_PORT = 45678;    // TCP service port
constexpr int TLS_HANDSHAKE_TIMEOUT_MS = 5000;
constexpr int TLS_READ_TIMEOUT_MS = 30000;

// ═══════════════════════════════════════════════════════════
// TlsPskConnection — клиентское TLS PSK соединение
// ═══════════════════════════════════════════════════════════

class FV_API TlsPskConnection {
public:
    TlsPskConnection();
    ~TlsPskConnection();

    // Запрет копирования
    TlsPskConnection(const TlsPskConnection&) = delete;
    TlsPskConnection& operator=(const TlsPskConnection&) = delete;

    // Move semantics
    TlsPskConnection(TlsPskConnection&& other) noexcept;
    TlsPskConnection& operator=(TlsPskConnection&& other) noexcept;

    // ═══════════════════════════════════════════════════════════
    // Connection
    // ═══════════════════════════════════════════════════════════

    /// Установить PSK и identity для соединения
    void setPsk(const std::array<uint8_t, TLS_PSK_SIZE>& psk, const std::string& identity);

    /// Подключиться к серверу (клиентский режим)
    /// @param host IP или hostname
    /// @param port TCP порт
    /// @return true если соединение установлено и TLS handshake успешен
    bool connect(const std::string& host, uint16_t port);

    /// Принять соединение (серверный режим)
    /// @param socket Уже принятый TCP сокет
    /// @return true если TLS handshake успешен
    bool accept(int socket);

    /// Закрыть соединение
    void close();

    /// Проверить, установлено ли соединение
    bool isConnected() const;

    // ═══════════════════════════════════════════════════════════
    // Data transfer
    // ═══════════════════════════════════════════════════════════

    /// Отправить данные
    /// @return количество отправленных байт или -1 при ошибке
    int send(const uint8_t* data, size_t size);

    /// Отправить данные (vector)
    int send(const std::vector<uint8_t>& data);

    /// Получить данные
    /// @param buffer Буфер для данных
    /// @param maxSize Максимальный размер
    /// @return количество полученных байт, 0 при закрытии, -1 при ошибке
    int receive(uint8_t* buffer, size_t maxSize);

    /// Получить данные (vector)
    std::vector<uint8_t> receive(size_t maxSize);

    // ═══════════════════════════════════════════════════════════
    // Info
    // ═══════════════════════════════════════════════════════════

    /// Получить identity пира (после handshake)
    std::string getPeerIdentity() const;

    /// Получить локальный IP
    std::string getLocalAddress() const;

    /// Получить удалённый IP
    std::string getRemoteAddress() const;

    /// Получить последнюю ошибку
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// ═══════════════════════════════════════════════════════════
// TlsPskServer — TCP сервер с TLS PSK
// ═══════════════════════════════════════════════════════════

class FV_API TlsPskServer {
public:
    TlsPskServer();
    ~TlsPskServer();

    // Запрет копирования
    TlsPskServer(const TlsPskServer&) = delete;
    TlsPskServer& operator=(const TlsPskServer&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════

    /// Установить PSK для всех входящих соединений
    /// @param psk 32-байтовый Pre-Shared Key
    /// @param localIdentity Identity этого сервера
    void setPsk(const std::array<uint8_t, TLS_PSK_SIZE>& psk, const std::string& localIdentity);

    /// Callback для проверки identity клиента (опционально)
    using IdentityValidator = std::function<bool(const std::string& identity)>;
    void setIdentityValidator(IdentityValidator validator);

    /// Запустить сервер
    /// @param port TCP порт (default: 45678)
    /// @return true если сервер запущен
    bool start(uint16_t port = TLS_SERVICE_PORT);

    /// Остановить сервер
    void stop();

    /// Проверить, запущен ли сервер
    bool isRunning() const;

    // ═══════════════════════════════════════════════════════════
    // Accept
    // ═══════════════════════════════════════════════════════════

    /// Принять входящее соединение (блокирующий вызов)
    /// @return TLS соединение или nullptr при ошибке/остановке
    std::unique_ptr<TlsPskConnection> accept();

    /// Получить порт сервера
    uint16_t getPort() const;

    /// Получить последнюю ошибку
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault

