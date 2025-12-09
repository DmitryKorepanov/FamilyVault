// Discovery.cpp — Реализация UDP broadcast для обнаружения устройств

#include "familyvault/Network/Discovery.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    using socket_t = SOCKET;
    #define SOCKET_INVALID INVALID_SOCKET
    #define CLOSE_SOCKET closesocket
    #define SOCKET_ERROR_CODE WSAGetLastError()
#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <ifaddrs.h>
    #include <net/if.h>
    using socket_t = int;
    #define SOCKET_INVALID (-1)
    #define CLOSE_SOCKET close
    #define SOCKET_ERROR_CODE errno
#endif

namespace FamilyVault {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// ═══════════════════════════════════════════════════════════
// DiscoveredDevice — внутренняя структура с timestamp
// ═══════════════════════════════════════════════════════════

struct DiscoveredDevice {
    DeviceInfo info;
    Clock::time_point lastSeen;
};

// ═══════════════════════════════════════════════════════════
// NetworkDiscovery::Impl
// ═══════════════════════════════════════════════════════════

class NetworkDiscovery::Impl {
public:
    Impl() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            spdlog::error("Discovery: WSAStartup failed");
        }
#endif
    }

    ~Impl() {
        stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool start(const DeviceInfo& thisDevice) {
        if (m_running) return true;

        m_thisDevice = thisDevice;
        m_thisDevice.isOnline = true;

        // Создаём сокеты
        if (!createSockets()) {
            return false;
        }

        m_running = true;

        // Запускаем потоки
        m_broadcastThread = std::thread([this]() { broadcastLoop(); });
        m_listenThread = std::thread([this]() { listenLoop(); });
        m_cleanupThread = std::thread([this]() { cleanupLoop(); });

        spdlog::info("Discovery: Started for device '{}' ({})", 
            m_thisDevice.deviceName, m_thisDevice.deviceId);
        return true;
    }

    void stop() {
        if (!m_running) return;
        
        m_running = false;

        // Закрываем сокеты чтобы разблокировать потоки
        if (m_sendSocket != SOCKET_INVALID) {
            CLOSE_SOCKET(m_sendSocket);
            m_sendSocket = SOCKET_INVALID;
        }
        if (m_recvSocket != SOCKET_INVALID) {
            CLOSE_SOCKET(m_recvSocket);
            m_recvSocket = SOCKET_INVALID;
        }

        // Ждём завершения потоков
        if (m_broadcastThread.joinable()) m_broadcastThread.join();
        if (m_listenThread.joinable()) m_listenThread.join();
        if (m_cleanupThread.joinable()) m_cleanupThread.join();

        // Очищаем устройства
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_devices.clear();
        }

        spdlog::info("Discovery: Stopped");
    }

    bool isRunning() const {
        return m_running;
    }

    std::vector<DeviceInfo> getDevices() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<DeviceInfo> result;
        result.reserve(m_devices.size());
        for (const auto& [id, device] : m_devices) {
            result.push_back(device.info);
        }
        return result;
    }

    std::optional<DeviceInfo> getDevice(const std::string& deviceId) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_devices.find(deviceId);
        if (it != m_devices.end()) {
            return it->second.info;
        }
        return std::nullopt;
    }

    size_t getDeviceCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_devices.size();
    }

    void onDeviceFound(DeviceCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onFound = std::move(callback);
    }

    void onDeviceLost(DeviceCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onLost = std::move(callback);
    }

    void onDeviceUpdated(DeviceCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onUpdated = std::move(callback);
    }

private:
    DeviceInfo m_thisDevice;
    std::atomic<bool> m_running{false};

    socket_t m_sendSocket = SOCKET_INVALID;
    socket_t m_recvSocket = SOCKET_INVALID;

    std::thread m_broadcastThread;
    std::thread m_listenThread;
    std::thread m_cleanupThread;

    mutable std::mutex m_mutex;
    std::map<std::string, DiscoveredDevice> m_devices;

    std::mutex m_callbackMutex;
    DeviceCallback m_onFound;
    DeviceCallback m_onLost;
    DeviceCallback m_onUpdated;

    bool createSockets() {
        // Socket для отправки broadcast
        m_sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_sendSocket == SOCKET_INVALID) {
            spdlog::error("Discovery: Failed to create send socket: {}", SOCKET_ERROR_CODE);
            return false;
        }

        // Включаем broadcast
        int broadcastEnable = 1;
        if (setsockopt(m_sendSocket, SOL_SOCKET, SO_BROADCAST, 
                       reinterpret_cast<char*>(&broadcastEnable), sizeof(broadcastEnable)) < 0) {
            spdlog::error("Discovery: Failed to enable broadcast: {}", SOCKET_ERROR_CODE);
            CLOSE_SOCKET(m_sendSocket);
            m_sendSocket = SOCKET_INVALID;
            return false;
        }

        // Socket для приёма
        m_recvSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_recvSocket == SOCKET_INVALID) {
            spdlog::error("Discovery: Failed to create recv socket: {}", SOCKET_ERROR_CODE);
            CLOSE_SOCKET(m_sendSocket);
            m_sendSocket = SOCKET_INVALID;
            return false;
        }

        // Разрешаем reuse address
        int reuseAddr = 1;
        setsockopt(m_recvSocket, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<char*>(&reuseAddr), sizeof(reuseAddr));

        // Bind на discovery порт
        sockaddr_in bindAddr{};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(DISCOVERY_PORT);
        bindAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(m_recvSocket, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0) {
            spdlog::error("Discovery: Failed to bind recv socket: {}", SOCKET_ERROR_CODE);
            CLOSE_SOCKET(m_sendSocket);
            CLOSE_SOCKET(m_recvSocket);
            m_sendSocket = SOCKET_INVALID;
            m_recvSocket = SOCKET_INVALID;
            return false;
        }

        // Устанавливаем timeout для recv (1 секунда)
#ifdef _WIN32
        DWORD timeout = 1000;
#else
        timeval timeout{1, 0};
#endif
        setsockopt(m_recvSocket, SOL_SOCKET, SO_RCVTIMEO, 
                   reinterpret_cast<char*>(&timeout), sizeof(timeout));

        return true;
    }

    std::string createAnnounceMessage() const {
        json msg = {
            {"app", "FamilyVault"},
            {"protocolVersion", PROTOCOL_VERSION},
            {"minProtocolVersion", 1},
            {"deviceId", m_thisDevice.deviceId},
            {"deviceName", m_thisDevice.deviceName},
            {"deviceType", static_cast<int>(m_thisDevice.deviceType)},
            {"servicePort", m_thisDevice.servicePort}  // Use actual configured port, not constant
        };
        return msg.dump();
    }

    void broadcastLoop() {
        spdlog::debug("Discovery: Broadcast thread started");
        
        std::string message = createAnnounceMessage();
        auto broadcastAddrs = NetworkDiscovery::getBroadcastAddresses();

        while (m_running) {
            for (const auto& addr : broadcastAddrs) {
                sockaddr_in destAddr{};
                destAddr.sin_family = AF_INET;
                destAddr.sin_port = htons(DISCOVERY_PORT);
                inet_pton(AF_INET, addr.c_str(), &destAddr.sin_addr);

                sendto(m_sendSocket, message.c_str(), static_cast<int>(message.size()), 0,
                       reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr));
            }

            // Спим с проверкой флага
            for (int i = 0; i < BROADCAST_INTERVAL_MS / 100 && m_running; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        spdlog::debug("Discovery: Broadcast thread stopped");
    }

    void listenLoop() {
        spdlog::debug("Discovery: Listen thread started");

        char buffer[2048];
        sockaddr_in senderAddr{};
        socklen_t senderLen = sizeof(senderAddr);

        while (m_running) {
            int received = recvfrom(m_recvSocket, buffer, sizeof(buffer) - 1, 0,
                                    reinterpret_cast<sockaddr*>(&senderAddr), &senderLen);

            if (received <= 0) {
                continue; // Timeout или ошибка
            }

            buffer[received] = '\0';

            // Получаем IP отправителя
            char senderIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &senderAddr.sin_addr, senderIp, sizeof(senderIp));

            processMessage(buffer, senderIp);
        }

        spdlog::debug("Discovery: Listen thread stopped");
    }

    void processMessage(const char* data, const char* senderIp) {
        try {
            auto msg = json::parse(data);

            // Проверяем формат
            if (msg.value("app", "") != "FamilyVault") return;
            
            std::string deviceId = msg.value("deviceId", "");
            if (deviceId.empty()) return;

            // Игнорируем себя
            if (deviceId == m_thisDevice.deviceId) return;

            // Проверяем версию протокола
            int version = msg.value("protocolVersion", 0);
            int minVersion = msg.value("minProtocolVersion", 0);
            if (version < 1 || PROTOCOL_VERSION < minVersion) {
                spdlog::debug("Discovery: Incompatible protocol version from {}", senderIp);
                return;
            }

            // Создаём DeviceInfo
            DeviceInfo info;
            info.deviceId = deviceId;
            info.deviceName = msg.value("deviceName", "Unknown");
            info.deviceType = static_cast<DeviceType>(msg.value("deviceType", 0));
            info.ipAddress = senderIp;
            info.servicePort = msg.value("servicePort", SERVICE_PORT);
            info.lastSeenAt = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            info.isOnline = true;
            info.isConnected = false;

            updateDevice(info);

        } catch (const std::exception& e) {
            spdlog::debug("Discovery: Failed to parse message from {}: {}", senderIp, e.what());
        }
    }

    void updateDevice(const DeviceInfo& info) {
        bool isNew = false;
        bool isUpdated = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_devices.find(info.deviceId);
            if (it == m_devices.end()) {
                m_devices[info.deviceId] = {info, Clock::now()};
                isNew = true;
                spdlog::info("Discovery: New device '{}' at {}", info.deviceName, info.ipAddress);
            } else {
                // Проверяем изменения
                if (it->second.info.ipAddress != info.ipAddress ||
                    it->second.info.deviceName != info.deviceName) {
                    isUpdated = true;
                }
                it->second.info = info;
                it->second.lastSeen = Clock::now();
            }
        }

        // Вызываем callbacks вне lock
        if (isNew) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_onFound) m_onFound(info);
        } else if (isUpdated) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_onUpdated) m_onUpdated(info);
        }
    }

    void cleanupLoop() {
        spdlog::debug("Discovery: Cleanup thread started");

        while (m_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!m_running) break;

            auto now = Clock::now();
            std::vector<DeviceInfo> lostDevices;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto it = m_devices.begin(); it != m_devices.end(); ) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - it->second.lastSeen).count();
                    
                    if (elapsed > DEVICE_TTL_MS) {
                        spdlog::info("Discovery: Device '{}' went offline", it->second.info.deviceName);
                        lostDevices.push_back(it->second.info);
                        it = m_devices.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // Вызываем callbacks вне lock
            for (const auto& device : lostDevices) {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                if (m_onLost) m_onLost(device);
            }
        }

        spdlog::debug("Discovery: Cleanup thread stopped");
    }
};

// ═══════════════════════════════════════════════════════════
// NetworkDiscovery Public Interface
// ═══════════════════════════════════════════════════════════

NetworkDiscovery::NetworkDiscovery() : m_impl(std::make_unique<Impl>()) {}
NetworkDiscovery::~NetworkDiscovery() = default;

bool NetworkDiscovery::start(const DeviceInfo& thisDevice) {
    return m_impl->start(thisDevice);
}

void NetworkDiscovery::stop() {
    m_impl->stop();
}

bool NetworkDiscovery::isRunning() const {
    return m_impl->isRunning();
}

std::vector<DeviceInfo> NetworkDiscovery::getDevices() const {
    return m_impl->getDevices();
}

std::optional<DeviceInfo> NetworkDiscovery::getDevice(const std::string& deviceId) const {
    return m_impl->getDevice(deviceId);
}

size_t NetworkDiscovery::getDeviceCount() const {
    return m_impl->getDeviceCount();
}

void NetworkDiscovery::onDeviceFound(DeviceCallback callback) {
    m_impl->onDeviceFound(std::move(callback));
}

void NetworkDiscovery::onDeviceLost(DeviceCallback callback) {
    m_impl->onDeviceLost(std::move(callback));
}

void NetworkDiscovery::onDeviceUpdated(DeviceCallback callback) {
    m_impl->onDeviceUpdated(std::move(callback));
}

// ═══════════════════════════════════════════════════════════
// Static helpers
// ═══════════════════════════════════════════════════════════

std::vector<std::string> NetworkDiscovery::getLocalIpAddresses() {
    std::vector<std::string> addresses;

#ifdef _WIN32
    ULONG bufferSize = 15000;
    std::vector<uint8_t> buffer(bufferSize);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    ULONG result = GetAdaptersAddresses(AF_INET, 
        GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST, 
        nullptr, adapters, &bufferSize);

    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, 
            GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST, 
            nullptr, adapters, &bufferSize);
    }

    if (result == NO_ERROR) {
        for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp) continue;
            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

            for (auto* addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
                if (addr->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sin = reinterpret_cast<sockaddr_in*>(addr->Address.lpSockaddr);
                    char ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                    if (strcmp(ip, "127.0.0.1") != 0) {
                        addresses.push_back(ip);
                    }
                }
            }
        }
    }
#else
    struct ifaddrs* ifap;
    if (getifaddrs(&ifap) == 0) {
        for (auto* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!(ifa->ifa_flags & IFF_UP)) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            addresses.push_back(ip);
        }
        freeifaddrs(ifap);
    }
#endif

    return addresses;
}

std::vector<std::string> NetworkDiscovery::getBroadcastAddresses() {
    std::vector<std::string> broadcasts;

#ifdef _WIN32
    ULONG bufferSize = 15000;
    std::vector<uint8_t> buffer(bufferSize);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    ULONG result = GetAdaptersAddresses(AF_INET, 
        GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST, 
        nullptr, adapters, &bufferSize);

    if (result == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(bufferSize);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(AF_INET, 
            GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_MULTICAST, 
            nullptr, adapters, &bufferSize);
    }

    if (result == NO_ERROR) {
        for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus != IfOperStatusUp) continue;
            if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

            for (auto* addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
                if (addr->Address.lpSockaddr->sa_family == AF_INET) {
                    auto* sin = reinterpret_cast<sockaddr_in*>(addr->Address.lpSockaddr);
                    
                    // Вычисляем broadcast: IP | ~mask
                    uint32_t ip = ntohl(sin->sin_addr.s_addr);
                    uint32_t mask = 0xFFFFFFFF << (32 - addr->OnLinkPrefixLength);
                    uint32_t broadcast = ip | ~mask;

                    in_addr broadcastAddr;
                    broadcastAddr.s_addr = htonl(broadcast);
                    char broadcastStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &broadcastAddr, broadcastStr, sizeof(broadcastStr));
                    broadcasts.push_back(broadcastStr);
                }
            }
        }
    }
#else
    struct ifaddrs* ifap;
    if (getifaddrs(&ifap) == 0) {
        for (auto* ifa = ifap; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            if (!(ifa->ifa_flags & IFF_UP)) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            if (!(ifa->ifa_flags & IFF_BROADCAST)) continue;
            if (!ifa->ifa_broadaddr) continue;

            auto* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            broadcasts.push_back(ip);
        }
        freeifaddrs(ifap);
    }
#endif

    // Fallback: general broadcast
    if (broadcasts.empty()) {
        broadcasts.push_back("255.255.255.255");
    }

    return broadcasts;
}

} // namespace FamilyVault

