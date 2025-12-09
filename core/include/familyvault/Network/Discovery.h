// Discovery.h — Обнаружение устройств в локальной сети через UDP broadcast
// См. SPECIFICATIONS.md, раздел 7.1

#pragma once

#include "../export.h"
#include "../Models.h"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Константы
// ═══════════════════════════════════════════════════════════

constexpr uint16_t DISCOVERY_PORT = 45679;      // UDP broadcast порт
constexpr uint16_t SERVICE_PORT = 45678;        // TCP service порт
constexpr int BROADCAST_INTERVAL_MS = 5000;     // Интервал анонсов (5 сек)
constexpr int DEVICE_TTL_MS = 15000;            // Устройство offline после 15 сек
constexpr int PROTOCOL_VERSION = 1;

// ═══════════════════════════════════════════════════════════
// NetworkDiscovery — обнаружение устройств в LAN
// ═══════════════════════════════════════════════════════════

class FV_API NetworkDiscovery {
public:
    NetworkDiscovery();
    ~NetworkDiscovery();

    // Запрет копирования
    NetworkDiscovery(const NetworkDiscovery&) = delete;
    NetworkDiscovery& operator=(const NetworkDiscovery&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════

    /// Запустить discovery
    /// @param thisDevice Информация об этом устройстве
    /// @return true если успешно запущен
    bool start(const DeviceInfo& thisDevice);

    /// Остановить discovery
    void stop();

    /// Запущен ли discovery
    bool isRunning() const;

    // ═══════════════════════════════════════════════════════════
    // Устройства
    // ═══════════════════════════════════════════════════════════

    /// Получить список найденных устройств
    std::vector<DeviceInfo> getDevices() const;

    /// Получить устройство по ID
    std::optional<DeviceInfo> getDevice(const std::string& deviceId) const;

    /// Количество найденных устройств
    size_t getDeviceCount() const;

    // ═══════════════════════════════════════════════════════════
    // Callbacks
    // ═══════════════════════════════════════════════════════════

    using DeviceCallback = std::function<void(const DeviceInfo&)>;

    /// Callback при обнаружении нового устройства
    void onDeviceFound(DeviceCallback callback);

    /// Callback при потере устройства (offline)
    void onDeviceLost(DeviceCallback callback);

    /// Callback при обновлении устройства
    void onDeviceUpdated(DeviceCallback callback);

    // ═══════════════════════════════════════════════════════════
    // Сетевые адреса
    // ═══════════════════════════════════════════════════════════

    /// Получить локальные IP адреса этого устройства
    static std::vector<std::string> getLocalIpAddresses();

    /// Получить broadcast адреса для локальных сетей
    static std::vector<std::string> getBroadcastAddresses();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault


