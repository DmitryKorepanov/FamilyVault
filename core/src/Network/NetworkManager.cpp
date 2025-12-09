// NetworkManager.cpp — P2P Network coordinator implementation

#include "familyvault/Network/NetworkManager.h"
#include "familyvault/FamilyPairing.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// NetworkManager::Impl
// ═══════════════════════════════════════════════════════════

class NetworkManager::Impl {
public:
    explicit Impl(std::shared_ptr<FamilyPairing> pairing)
        : m_pairing(std::move(pairing))
        , m_discovery(std::make_unique<NetworkDiscovery>())
        , m_server(std::make_unique<TlsPskServer>()) {}

    ~Impl() {
        stop();
    }

    bool start(uint16_t port) {
        if (m_state != NetworkState::Stopped) {
            m_lastError = "Already running";
            return false;
        }

        if (!m_pairing->isConfigured()) {
            m_lastError = "Family not configured";
            return false;
        }

        setState(NetworkState::Starting);

        // Get PSK
        auto psk = m_pairing->derivePsk();
        if (!psk) {
            m_lastError = "Failed to derive PSK";
            setState(NetworkState::Error);
            return false;
        }

        // Setup server
        m_server->setPsk(*psk, m_pairing->getDeviceId());
        
        // Start server (port=0 means auto-pick, TLS_SERVICE_PORT is default)
        uint16_t requestedPort = (port == 0) ? TLS_SERVICE_PORT : port;
        
        if (!m_server->start(requestedPort)) {
            // If default port fails, try auto-pick
            if (requestedPort == TLS_SERVICE_PORT && port == 0) {
                spdlog::info("NetworkManager: Default port {} busy, trying auto-pick", TLS_SERVICE_PORT);
                if (!m_server->start(0)) {
                    m_lastError = "Failed to start server: " + m_server->getLastError();
                    setState(NetworkState::Error);
                    return false;
                }
            } else {
                m_lastError = "Failed to start server: " + m_server->getLastError();
                setState(NetworkState::Error);
                return false;
            }
        }
        
        // Get actual bound port (may differ from requested if auto-picked)
        uint16_t actualPort = m_server->getPort();

        // Setup discovery callbacks
        m_discovery->onDeviceFound([this](const DeviceInfo& info) {
            handleDeviceDiscovered(info);
        });
        m_discovery->onDeviceLost([this](const DeviceInfo& info) {
            handleDeviceLost(info);
        });

        // Start discovery with actual port
        DeviceInfo thisDevice;
        thisDevice.deviceId = m_pairing->getDeviceId();
        thisDevice.deviceName = m_pairing->getDeviceName();
        thisDevice.deviceType = m_pairing->getDeviceType();
        thisDevice.servicePort = actualPort;
        thisDevice.isOnline = true;

        if (!m_discovery->start(thisDevice)) {
            m_server->stop();
            m_lastError = "Failed to start discovery";
            setState(NetworkState::Error);
            return false;
        }

        // Start accept thread
        m_running = true;
        m_acceptThread = std::thread([this]() { acceptLoop(); });

        setState(NetworkState::Running);
        spdlog::info("NetworkManager: Started on port {}", actualPort);
        return true;
    }

    void stop() {
        if (m_state == NetworkState::Stopped) return;

        setState(NetworkState::Stopping);
        m_running = false;

        // Stop discovery
        m_discovery->stop();

        // Stop server
        m_server->stop();

        // Wait for accept thread
        if (m_acceptThread.joinable()) {
            m_acceptThread.join();
        }

        // Wait for any pending connect threads to finish
        {
            std::lock_guard<std::mutex> lock(m_connectThreadsMutex);
            for (auto& t : m_connectThreads) {
                if (t.joinable()) {
                    t.join();
                }
            }
            m_connectThreads.clear();
        }

        // Disconnect all peers (avoid deadlock: copy out, release lock, then disconnect)
        std::vector<std::shared_ptr<PeerConnection>> peersToDisconnect;
        {
            std::lock_guard<std::mutex> lock(m_peersMutex);
            peersToDisconnect.reserve(m_peers.size());
            for (auto& [id, conn] : m_peers) {
                peersToDisconnect.push_back(conn);
            }
            m_peers.clear();  // Clear now so callbacks find nothing to erase
        }
        // Now disconnect without holding the lock (callbacks can safely re-lock)
        for (auto& conn : peersToDisconnect) {
            conn->disconnect();
        }

        setState(NetworkState::Stopped);
        spdlog::info("NetworkManager: Stopped");
    }

    NetworkState getState() const { return m_state; }
    bool isRunning() const { return m_state == NetworkState::Running; }

    std::vector<DeviceInfo> getDiscoveredDevices() const {
        return m_discovery->getDevices();
    }

    std::vector<DeviceInfo> getConnectedDevices() const {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        std::vector<DeviceInfo> result;
        result.reserve(m_peers.size());
        for (const auto& [id, conn] : m_peers) {
            if (conn->isConnected()) {
                result.push_back(conn->getPeerInfo());
            }
        }
        return result;
    }

    std::optional<DeviceInfo> getDevice(const std::string& deviceId) const {
        // Check connected first
        {
            std::lock_guard<std::mutex> lock(m_peersMutex);
            auto it = m_peers.find(deviceId);
            if (it != m_peers.end() && it->second->isConnected()) {
                return it->second->getPeerInfo();
            }
        }
        // Check discovered
        return m_discovery->getDevice(deviceId);
    }

    bool connectToDevice(const std::string& deviceId) {
        auto device = m_discovery->getDevice(deviceId);
        if (!device) {
            m_lastError = "Device not found: " + deviceId;
            return false;
        }
        return connectToAddress(device->ipAddress, device->servicePort);
    }

    bool connectToAddress(const std::string& host, uint16_t port) {
        if (!isRunning()) {
            m_lastError = "NetworkManager not running";
            return false;
        }

        // Spawn async connection thread to avoid blocking caller
        // TLS handshake can take several seconds
        // Track thread to join on shutdown (prevents use-after-free)
        std::thread connectThread([this, host, port]() {
            // Early exit if shutting down
            if (!m_running) {
                spdlog::debug("NetworkManager: Connect cancelled - shutting down");
                return;
            }
            
            auto conn = std::make_shared<PeerConnection>(m_pairing);
            
            // Setup callbacks
            setupPeerCallbacks(conn.get());

            if (!conn->connect(host, port)) {
                // Check again after potentially long connect
                if (!m_running) return;
                
                std::string error = conn->getLastError();
                spdlog::error("NetworkManager: Async connect failed: {}", error);
                // Notify error callback
                {
                    std::lock_guard<std::mutex> lock(m_callbackMutex);
                    if (m_onError) {
                        m_onError("Connection failed: " + error);
                    }
                }
                return;
            }

            // Final check before modifying state
            if (!m_running) {
                spdlog::debug("NetworkManager: Connect succeeded but shutting down, dropping connection");
                conn->disconnect();
                return;
            }

            std::string peerId = conn->getPeerId();
            DeviceInfo peerInfo = conn->getPeerInfo();
            
            // Check for and disconnect existing connection to same peer
            std::shared_ptr<PeerConnection> existingPeer;
            {
                std::lock_guard<std::mutex> lock(m_peersMutex);
                if (!m_running) return;  // Check inside lock too
                
                auto it = m_peers.find(peerId);
                if (it != m_peers.end()) {
                    existingPeer = it->second;
                    m_peers.erase(it);
                    spdlog::warn("NetworkManager: Replacing existing connection to {}", peerId);
                }
                m_peers[peerId] = conn;
            }
            
            // Disconnect old peer outside lock
            if (existingPeer) {
                existingPeer->disconnect();
            }

            // Notify connected callback
            handleDeviceConnected(peerInfo);
            
            spdlog::info("NetworkManager: Async connect to {}:{} succeeded ({})", 
                         host, port, peerId);
        });
        
        // Track thread for cleanup (joined at stop())
        // Limit concurrent connection attempts to avoid thread exhaustion
        constexpr size_t MAX_PENDING_CONNECTS = 10;
        
        // Join oldest threads if we have too many pending
        std::vector<std::thread> toJoin;
        {
            std::lock_guard<std::mutex> lock(m_connectThreadsMutex);
            
            while (m_connectThreads.size() >= MAX_PENDING_CONNECTS && !m_connectThreads.empty()) {
                toJoin.push_back(std::move(m_connectThreads.front()));
                m_connectThreads.erase(m_connectThreads.begin());
            }
            
            m_connectThreads.push_back(std::move(connectThread));
        }
        
        // Join outside lock to avoid blocking
        for (auto& t : toJoin) {
            if (t.joinable()) {
                t.join();
            }
        }

        return true;  // Returns immediately, result comes via callback
    }

    void disconnectFromDevice(const std::string& deviceId) {
        std::shared_ptr<PeerConnection> peer;
        {
            std::lock_guard<std::mutex> lock(m_peersMutex);
            auto it = m_peers.find(deviceId);
            if (it != m_peers.end()) {
                peer = it->second;
            }
        }
        // Disconnect outside lock - the onStateChanged callback will handle
        // cleanup (erase from m_peers) and notify (handleDeviceDisconnected)
        if (peer) {
            peer->disconnect();
        }
    }

    void disconnectAll() {
        // Avoid deadlock: copy out shared_ptrs, release lock, then disconnect
        std::vector<std::shared_ptr<PeerConnection>> peersToDisconnect;
        {
            std::lock_guard<std::mutex> lock(m_peersMutex);
            peersToDisconnect.reserve(m_peers.size());
            for (auto& [id, conn] : m_peers) {
                peersToDisconnect.push_back(conn);
            }
            m_peers.clear();  // Clear now so callbacks find nothing to erase
        }
        // Disconnect without holding the lock
        for (auto& conn : peersToDisconnect) {
            conn->disconnect();
        }
    }

    bool isConnectedTo(const std::string& deviceId) const {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        auto it = m_peers.find(deviceId);
        return it != m_peers.end() && it->second->isConnected();
    }

    std::shared_ptr<PeerConnection> getPeerConnection(const std::string& deviceId) {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        auto it = m_peers.find(deviceId);
        if (it != m_peers.end() && it->second->isConnected()) {
            return it->second;
        }
        return nullptr;
    }

    bool sendTo(const std::string& deviceId, const Message& msg) {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        auto it = m_peers.find(deviceId);
        if (it == m_peers.end() || !it->second->isConnected()) {
            return false;
        }
        return it->second->sendMessage(msg);
    }

    void broadcast(const Message& msg) {
        std::lock_guard<std::mutex> lock(m_peersMutex);
        for (auto& [id, conn] : m_peers) {
            if (conn->isConnected()) {
                conn->sendMessage(msg);
            }
        }
    }

    void onDeviceDiscovered(DeviceCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onDiscovered = std::move(callback);
    }

    void onDeviceLost(DeviceCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onLost = std::move(callback);
    }

    void onDeviceConnected(DeviceCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onConnected = std::move(callback);
    }

    void onDeviceDisconnected(DeviceCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onDisconnected = std::move(callback);
    }

    void onMessage(MessageCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onMessage = std::move(callback);
    }

    void onStateChanged(StateCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onStateChanged = std::move(callback);
    }

    void onError(ErrorCallback callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_onError = std::move(callback);
    }

    uint16_t getServerPort() const {
        return m_server->getPort();
    }

    std::vector<std::string> getLocalAddresses() const {
        return NetworkDiscovery::getLocalIpAddresses();
    }

    std::string getLastError() const { return m_lastError; }

private:
    std::shared_ptr<FamilyPairing> m_pairing;
    std::unique_ptr<NetworkDiscovery> m_discovery;
    std::unique_ptr<TlsPskServer> m_server;

    std::atomic<NetworkState> m_state{NetworkState::Stopped};
    std::atomic<bool> m_running{false};
    std::string m_lastError;

    std::thread m_acceptThread;

    mutable std::mutex m_peersMutex;
    std::map<std::string, std::shared_ptr<PeerConnection>> m_peers;

    std::mutex m_callbackMutex;
    DeviceCallback m_onDiscovered;
    DeviceCallback m_onLost;
    DeviceCallback m_onConnected;
    DeviceCallback m_onDisconnected;
    MessageCallback m_onMessage;
    StateCallback m_onStateChanged;
    ErrorCallback m_onError;
    
    // Async connect thread tracking (to avoid use-after-free on shutdown)
    std::mutex m_connectThreadsMutex;
    std::vector<std::thread> m_connectThreads;

    void setState(NetworkState newState) {
        NetworkState oldState = m_state.exchange(newState);
        if (oldState != newState) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_onStateChanged) {
                m_onStateChanged(newState);
            }
        }
    }

    void setupPeerCallbacks(PeerConnection* conn) {
        conn->onMessage([this, conn](const Message& msg) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_onMessage) {
                m_onMessage(conn->getPeerId(), msg);
            }
        });

        conn->onStateChanged([this, conn](PeerConnection::State state) {
            if (state == PeerConnection::State::Disconnected ||
                state == PeerConnection::State::Error) {
                auto info = conn->getPeerInfo();
                handleDeviceDisconnected(info);
                
                // Remove from peers
                std::lock_guard<std::mutex> lock(m_peersMutex);
                m_peers.erase(conn->getPeerId());
            }
        });

        conn->onError([this](const std::string& error) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_onError) {
                m_onError(error);
            }
        });
    }

    void acceptLoop() {
        while (m_running) {
            auto tlsConn = m_server->accept();
            if (!tlsConn) {
                if (m_running) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                continue;
            }

            // Create peer connection
            auto conn = std::make_shared<PeerConnection>(m_pairing);
            setupPeerCallbacks(conn.get());

            if (!conn->acceptConnection(std::move(tlsConn))) {
                spdlog::warn("NetworkManager: Failed to accept connection: {}", conn->getLastError());
                continue;
            }

            std::string peerId = conn->getPeerId();
            auto peerInfo = conn->getPeerInfo();

            {
                std::lock_guard<std::mutex> lock(m_peersMutex);
                // Check if already connected
                if (m_peers.count(peerId) > 0) {
                    spdlog::debug("NetworkManager: Already connected to {}, rejecting duplicate", peerId);
                    conn->disconnect();
                    continue;
                }
                m_peers[peerId] = std::move(conn);
            }

            handleDeviceConnected(peerInfo);
        }
    }

    void handleDeviceDiscovered(const DeviceInfo& info) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onDiscovered) {
            m_onDiscovered(info);
        }
    }

    void handleDeviceLost(const DeviceInfo& info) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onLost) {
            m_onLost(info);
        }
    }

    void handleDeviceConnected(const DeviceInfo& info) {
        spdlog::info("NetworkManager: Device connected: {} ({})", info.deviceName, info.deviceId);
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onConnected) {
            m_onConnected(info);
        }
    }

    void handleDeviceDisconnected(const DeviceInfo& info) {
        spdlog::info("NetworkManager: Device disconnected: {} ({})", info.deviceName, info.deviceId);
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onDisconnected) {
            m_onDisconnected(info);
        }
    }
};

// ═══════════════════════════════════════════════════════════
// NetworkManager Public Interface
// ═══════════════════════════════════════════════════════════

NetworkManager::NetworkManager(std::shared_ptr<FamilyPairing> pairing)
    : m_impl(std::make_unique<Impl>(std::move(pairing))) {}

NetworkManager::~NetworkManager() = default;

bool NetworkManager::start(uint16_t port) { return m_impl->start(port); }
void NetworkManager::stop() { m_impl->stop(); }
NetworkManager::NetworkState NetworkManager::getState() const { return m_impl->getState(); }
bool NetworkManager::isRunning() const { return m_impl->isRunning(); }

std::vector<DeviceInfo> NetworkManager::getDiscoveredDevices() const { return m_impl->getDiscoveredDevices(); }
std::vector<DeviceInfo> NetworkManager::getConnectedDevices() const { return m_impl->getConnectedDevices(); }
std::optional<DeviceInfo> NetworkManager::getDevice(const std::string& deviceId) const { return m_impl->getDevice(deviceId); }

bool NetworkManager::connectToDevice(const std::string& deviceId) { return m_impl->connectToDevice(deviceId); }
bool NetworkManager::connectToAddress(const std::string& host, uint16_t port) { return m_impl->connectToAddress(host, port); }
void NetworkManager::disconnectFromDevice(const std::string& deviceId) { m_impl->disconnectFromDevice(deviceId); }
void NetworkManager::disconnectAll() { m_impl->disconnectAll(); }
bool NetworkManager::isConnectedTo(const std::string& deviceId) const { return m_impl->isConnectedTo(deviceId); }

bool NetworkManager::sendTo(const std::string& deviceId, const Message& msg) { return m_impl->sendTo(deviceId, msg); }
void NetworkManager::broadcast(const Message& msg) { m_impl->broadcast(msg); }
std::shared_ptr<PeerConnection> NetworkManager::getPeerConnection(const std::string& deviceId) { return m_impl->getPeerConnection(deviceId); }

void NetworkManager::onDeviceDiscovered(DeviceCallback callback) { m_impl->onDeviceDiscovered(std::move(callback)); }
void NetworkManager::onDeviceLost(DeviceCallback callback) { m_impl->onDeviceLost(std::move(callback)); }
void NetworkManager::onDeviceConnected(DeviceCallback callback) { m_impl->onDeviceConnected(std::move(callback)); }
void NetworkManager::onDeviceDisconnected(DeviceCallback callback) { m_impl->onDeviceDisconnected(std::move(callback)); }
void NetworkManager::onMessage(MessageCallback callback) { m_impl->onMessage(std::move(callback)); }
void NetworkManager::onStateChanged(StateCallback callback) { m_impl->onStateChanged(std::move(callback)); }
void NetworkManager::onError(ErrorCallback callback) { m_impl->onError(std::move(callback)); }

uint16_t NetworkManager::getServerPort() const { return m_impl->getServerPort(); }
std::vector<std::string> NetworkManager::getLocalAddresses() const { return m_impl->getLocalAddresses(); }
std::string NetworkManager::getLastError() const { return m_impl->getLastError(); }

} // namespace FamilyVault

