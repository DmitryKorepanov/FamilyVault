// NetworkManager.h — Координатор P2P сети
// Высокоуровневый API для Flutter

#pragma once

#include "../export.h"
#include "../Models.h"
#include "Discovery.h"
#include "PeerConnection.h"
#include "TlsPsk.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>

namespace FamilyVault {

// Forward declarations
class FamilyPairing;

// ═══════════════════════════════════════════════════════════
// NetworkManager — координатор P2P сети
// ═══════════════════════════════════════════════════════════

class FV_API NetworkManager {
public:
    /// Состояние сети
    enum class NetworkState {
        Stopped,        // Не запущен
        Starting,       // Запускается
        Running,        // Работает (discovery + server)
        Stopping,       // Останавливается
        Error           // Ошибка
    };

    /// Создать NetworkManager
    /// @param pairing FamilyPairing для аутентификации
    explicit NetworkManager(std::shared_ptr<FamilyPairing> pairing);
    ~NetworkManager();

    // Запрет копирования
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════

    /// Запустить сеть (discovery + server)
    /// @param port TCP порт для сервера (0 = auto)
    /// @return true если успешно
    bool start(uint16_t port = 0);

    /// Остановить сеть
    void stop();

    /// Получить состояние
    NetworkState getState() const;

    /// Проверить, запущен ли
    bool isRunning() const;

    // ═══════════════════════════════════════════════════════════
    // Devices
    // ═══════════════════════════════════════════════════════════

    /// Получить список обнаруженных устройств
    std::vector<DeviceInfo> getDiscoveredDevices() const;

    /// Получить список подключённых устройств
    std::vector<DeviceInfo> getConnectedDevices() const;

    /// Получить информацию об устройстве
    std::optional<DeviceInfo> getDevice(const std::string& deviceId) const;

    // ═══════════════════════════════════════════════════════════
    // Connections
    // ═══════════════════════════════════════════════════════════

    /// Подключиться к устройству
    /// @param deviceId ID устройства (должно быть в списке discovered)
    /// @return true если соединение инициировано
    bool connectToDevice(const std::string& deviceId);

    /// Подключиться к устройству по адресу
    /// @param host IP адрес
    /// @param port TCP порт
    /// @return true если соединение инициировано
    bool connectToAddress(const std::string& host, uint16_t port);

    /// Отключиться от устройства
    void disconnectFromDevice(const std::string& deviceId);

    /// Отключиться от всех устройств
    void disconnectAll();

    /// Проверить подключение к устройству
    bool isConnectedTo(const std::string& deviceId) const;

    // ═══════════════════════════════════════════════════════════
    // Messaging
    // ═══════════════════════════════════════════════════════════

    /// Отправить сообщение устройству
    bool sendTo(const std::string& deviceId, const Message& msg);

    /// Отправить сообщение всем подключённым устройствам
    void broadcast(const Message& msg);

    /// Получить PeerConnection для устройства (для прямой работы с соединением)
    /// @return shared_ptr на PeerConnection или nullptr если не подключён
    std::shared_ptr<PeerConnection> getPeerConnection(const std::string& deviceId);

    // ═══════════════════════════════════════════════════════════
    // Callbacks
    // ═══════════════════════════════════════════════════════════

    using DeviceCallback = std::function<void(const DeviceInfo&)>;
    using MessageCallback = std::function<void(const std::string& deviceId, const Message&)>;
    using StateCallback = std::function<void(NetworkState)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    /// Callback при обнаружении устройства
    void onDeviceDiscovered(DeviceCallback callback);

    /// Callback при потере устройства
    void onDeviceLost(DeviceCallback callback);

    /// Callback при подключении устройства
    void onDeviceConnected(DeviceCallback callback);

    /// Callback при отключении устройства
    void onDeviceDisconnected(DeviceCallback callback);

    /// Callback при получении сообщения
    void onMessage(MessageCallback callback);

    /// Callback при изменении состояния
    void onStateChanged(StateCallback callback);

    /// Callback при ошибке
    void onError(ErrorCallback callback);

    // ═══════════════════════════════════════════════════════════
    // Info
    // ═══════════════════════════════════════════════════════════

    /// Получить порт сервера
    uint16_t getServerPort() const;

    /// Получить локальные IP адреса
    std::vector<std::string> getLocalAddresses() const;

    /// Получить последнюю ошибку
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault

