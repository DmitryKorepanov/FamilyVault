// PairingServer.h — TCP сервер для первичного pairing (без TLS)
// Используется для обмена family_secret при присоединении нового устройства

#pragma once

#include "../export.h"
#include "../Models.h"
#include "NetworkProtocol.h"
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <cstdint>

namespace FamilyVault {

// Forward declarations
class FamilyPairing;

// ═══════════════════════════════════════════════════════════
// PairingServer — TCP сервер для pairing
// ═══════════════════════════════════════════════════════════

class FV_API PairingServer {
public:
    /// Callback при получении запроса на pairing
    /// @param request PairingRequestPayload с данными запроса
    /// @return PairingResponsePayload с ответом
    using PairingCallback = std::function<PairingResponsePayload(const PairingRequestPayload&)>;

    PairingServer();
    ~PairingServer();

    // Запрет копирования
    PairingServer(const PairingServer&) = delete;
    PairingServer& operator=(const PairingServer&) = delete;

    /// Запустить сервер
    /// @param port Порт (по умолчанию PAIRING_PORT = 45680)
    /// @param callback Callback для обработки запросов
    /// @return true если успешно
    bool start(uint16_t port = PAIRING_PORT, PairingCallback callback = nullptr);

    /// Остановить сервер
    void stop();

    /// Проверить, запущен ли сервер
    bool isRunning() const;

    /// Получить порт сервера
    uint16_t getPort() const;

    /// Получить последнюю ошибку
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// ═══════════════════════════════════════════════════════════
// PairingClient — TCP клиент для pairing
// ═══════════════════════════════════════════════════════════

class FV_API PairingClient {
public:
    PairingClient();
    ~PairingClient();

    // Запрет копирования
    PairingClient(const PairingClient&) = delete;
    PairingClient& operator=(const PairingClient&) = delete;

    /// Выполнить pairing
    /// @param host IP адрес сервера
    /// @param port Порт сервера
    /// @param request Запрос на pairing
    /// @param timeoutMs Таймаут в миллисекундах
    /// @return Ответ сервера или nullopt при ошибке
    std::optional<PairingResponsePayload> pair(
        const std::string& host,
        uint16_t port,
        const PairingRequestPayload& request,
        int timeoutMs = 10000
    );

    /// Получить последнюю ошибку
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault

