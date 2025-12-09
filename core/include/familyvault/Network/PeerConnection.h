// PeerConnection.h — Управление P2P соединением
// См. SPECIFICATIONS.md, раздел 7

#pragma once

#include "../export.h"
#include "../Models.h"
#include "NetworkProtocol.h"
#include "TlsPsk.h"
#include <string>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace FamilyVault {

// Forward declaration
class FamilyPairing;

// ═══════════════════════════════════════════════════════════
// PeerConnection — управление соединением с пиром
// ═══════════════════════════════════════════════════════════

class FV_API PeerConnection {
public:
    /// Состояние соединения
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Disconnecting,
        Error
    };

    /// Создать соединение
    /// @param pairing FamilyPairing для получения PSK и deviceId
    explicit PeerConnection(std::shared_ptr<FamilyPairing> pairing);
    ~PeerConnection();

    // Запрет копирования
    PeerConnection(const PeerConnection&) = delete;
    PeerConnection& operator=(const PeerConnection&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Connection
    // ═══════════════════════════════════════════════════════════

    /// Подключиться к пиру (клиентский режим)
    /// @param host IP адрес
    /// @param port TCP порт
    /// @return true если соединение установлено
    bool connect(const std::string& host, uint16_t port);

    /// Принять соединение (серверный режим)
    /// @param tlsConnection Уже установленное TLS соединение
    /// @return true если успешно
    bool acceptConnection(std::unique_ptr<TlsPskConnection> tlsConnection);

    /// Отключиться
    void disconnect();

    /// Получить состояние
    State getState() const;

    /// Проверить соединение
    bool isConnected() const;

    // ═══════════════════════════════════════════════════════════
    // Peer info
    // ═══════════════════════════════════════════════════════════

    /// Получить информацию о пире
    DeviceInfo getPeerInfo() const;

    /// Получить ID пира
    std::string getPeerId() const;

    /// Получить адрес пира
    std::string getPeerAddress() const;

    // ═══════════════════════════════════════════════════════════
    // Messaging
    // ═══════════════════════════════════════════════════════════

    /// Отправить сообщение
    /// @return true если сообщение поставлено в очередь
    bool sendMessage(const Message& msg);

    /// Отправить сообщение и ждать ответа
    /// @param msg Сообщение для отправки
    /// @param timeoutMs Таймаут ожидания (мс)
    /// @return Ответ или nullopt при таймауте/ошибке
    std::optional<Message> sendAndWait(const Message& msg, int timeoutMs = 5000);

    // ═══════════════════════════════════════════════════════════
    // Callbacks
    // ═══════════════════════════════════════════════════════════

    using MessageCallback = std::function<void(const Message&)>;
    using StateCallback = std::function<void(State)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    /// Callback при получении сообщения
    void onMessage(MessageCallback callback);

    /// Callback при изменении состояния
    void onStateChanged(StateCallback callback);

    /// Callback при ошибке
    void onError(ErrorCallback callback);

    // ═══════════════════════════════════════════════════════════
    // Error info
    // ═══════════════════════════════════════════════════════════

    /// Получить последнюю ошибку
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault


