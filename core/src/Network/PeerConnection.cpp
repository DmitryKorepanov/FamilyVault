// PeerConnection.cpp — P2P Connection implementation

#include "familyvault/Network/PeerConnection.h"
#include "familyvault/FamilyPairing.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <map>

namespace FamilyVault {

using Clock = std::chrono::steady_clock;

// ═══════════════════════════════════════════════════════════
// PeerConnection::Impl
// ═══════════════════════════════════════════════════════════

class PeerConnection::Impl {
public:
    explicit Impl(std::shared_ptr<FamilyPairing> pairing)
        : m_pairing(std::move(pairing)) {}

    ~Impl() {
        disconnect();
    }

    bool connect(const std::string& host, uint16_t port) {
        if (m_state != State::Disconnected) {
            m_lastError = "Already connected or connecting";
            return false;
        }

        setState(State::Connecting);

        // Get PSK from FamilyPairing
        auto psk = m_pairing->derivePsk();
        if (!psk) {
            m_lastError = "Family not configured - no PSK available";
            setState(State::Error);
            return false;
        }

        // Create TLS connection
        m_tlsConn = std::make_unique<TlsPskConnection>();
        m_tlsConn->setPsk(*psk, m_pairing->getDeviceId());

        // Connect
        if (!m_tlsConn->connect(host, port)) {
            m_lastError = "TLS connect failed: " + m_tlsConn->getLastError();
            m_tlsConn.reset();
            setState(State::Error);
            return false;
        }

        m_peerAddress = host + ":" + std::to_string(port);
        m_peerId = m_tlsConn->getPeerIdentity();
        m_isIncoming = false;  // We initiated this connection

        // Exchange device info
        if (!exchangeDeviceInfo()) {
            m_tlsConn->close();
            m_tlsConn.reset();
            setState(State::Error);
            return false;
        }

        setState(State::Connected);
        startReceiveLoop();
        startHeartbeat();

        spdlog::info("PeerConnection: Connected to {} ({})", m_peerInfo.deviceName, m_peerId);
        return true;
    }

    bool acceptConnection(std::unique_ptr<TlsPskConnection> tlsConnection) {
        if (m_state != State::Disconnected) {
            m_lastError = "Already connected";
            return false;
        }

        if (!tlsConnection || !tlsConnection->isConnected()) {
            m_lastError = "Invalid TLS connection";
            return false;
        }

        setState(State::Connecting);

        m_tlsConn = std::move(tlsConnection);
        m_peerId = m_tlsConn->getPeerIdentity();
        m_peerAddress = m_tlsConn->getRemoteAddress();
        m_isIncoming = true;  // We accepted this connection (server side)

        // Exchange device info
        if (!exchangeDeviceInfo()) {
            m_tlsConn->close();
            m_tlsConn.reset();
            setState(State::Error);
            return false;
        }

        setState(State::Connected);
        startReceiveLoop();
        startHeartbeat();

        spdlog::info("PeerConnection: Accepted connection from {} ({})", m_peerInfo.deviceName, m_peerId);
        return true;
    }

    void disconnect() {
        if (m_state == State::Disconnected) return;

        setState(State::Disconnecting);
        m_running = false;

        // Send disconnect message
        if (m_tlsConn && m_tlsConn->isConnected()) {
            Message disconnectMsg(MessageType::Disconnect);
            sendMessageInternal(disconnectMsg);
        }

        // Stop threads - but don't join if called from the same thread!
        m_heartbeatCv.notify_all();
        
        auto thisThreadId = std::this_thread::get_id();
        
        if (m_receiveThread.joinable() && m_receiveThread.get_id() != thisThreadId) {
            m_receiveThread.join();
        } else if (m_receiveThread.joinable()) {
            m_receiveThread.detach(); // Detach if called from receive thread
        }
        
        if (m_heartbeatThread.joinable() && m_heartbeatThread.get_id() != thisThreadId) {
            m_heartbeatThread.join();
        } else if (m_heartbeatThread.joinable()) {
            m_heartbeatThread.detach(); // Detach if called from heartbeat thread
        }

        // Close connection under mutex
        {
            std::lock_guard<std::mutex> lock(m_tlsMutex);
            if (m_tlsConn) {
                m_tlsConn->close();
                m_tlsConn.reset();
            }
        }

        setState(State::Disconnected);
        spdlog::info("PeerConnection: Disconnected from {}", m_peerId);
    }

    State getState() const { return m_state; }
    bool isConnected() const { return m_state == State::Connected; }

    DeviceInfo getPeerInfo() const { return m_peerInfo; }
    std::string getPeerId() const { return m_peerId; }
    std::string getPeerAddress() const { return m_peerAddress; }

    bool sendMessage(const Message& msg) {
        if (!isConnected()) {
            m_lastError = "Not connected";
            return false;
        }
        return sendMessageInternal(msg);
    }

    std::optional<Message> sendAndWait(const Message& msg, int timeoutMs) {
        if (!isConnected()) return std::nullopt;

        // Store request ID for matching
        std::string reqId = msg.requestId.empty() ? generateRequestId() : msg.requestId;
        Message msgCopy = msg;
        msgCopy.requestId = reqId;

        // Setup pending response
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingResponses[reqId] = std::nullopt;
        }

        // Send message
        if (!sendMessageInternal(msgCopy)) {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            m_pendingResponses.erase(reqId);
            return std::nullopt;
        }

        // Wait for response
        auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        std::unique_lock<std::mutex> lock(m_pendingMutex);
        while (!m_pendingResponses[reqId].has_value()) {
            if (m_pendingCv.wait_until(lock, deadline) == std::cv_status::timeout) {
                m_pendingResponses.erase(reqId);
                return std::nullopt;
            }
            if (!isConnected()) {
                m_pendingResponses.erase(reqId);
                return std::nullopt;
            }
        }

        auto result = std::move(m_pendingResponses[reqId]);
        m_pendingResponses.erase(reqId);
        return result;
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

    std::string getLastError() const { return m_lastError; }

private:
    std::shared_ptr<FamilyPairing> m_pairing;
    std::unique_ptr<TlsPskConnection> m_tlsConn;
    mutable std::mutex m_tlsMutex;  // Protects m_tlsConn access during disconnect
    
    std::atomic<State> m_state{State::Disconnected};
    std::atomic<bool> m_running{false};
    bool m_isIncoming{false};  // True if we accepted this connection (server side)
    
    DeviceInfo m_peerInfo;
    std::string m_peerId;
    std::string m_peerAddress;
    std::string m_lastError;

    std::thread m_receiveThread;
    std::thread m_heartbeatThread;
    
    std::mutex m_sendMutex;
    std::mutex m_callbackMutex;
    
    MessageCallback m_onMessage;
    StateCallback m_onStateChanged;
    ErrorCallback m_onError;

    // For sendAndWait
    std::mutex m_pendingMutex;
    std::condition_variable m_pendingCv;
    std::map<std::string, std::optional<Message>> m_pendingResponses;

    // For heartbeat
    std::mutex m_heartbeatMutex;
    std::condition_variable m_heartbeatCv;
    Clock::time_point m_lastReceived;

    void setState(State newState) {
        State oldState = m_state.exchange(newState);
        if (oldState != newState) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            if (m_onStateChanged) {
                m_onStateChanged(newState);
            }
        }
    }

    void reportError(const std::string& error) {
        m_lastError = error;
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_onError) {
            m_onError(error);
        }
    }

    bool sendMessageInternal(const Message& msg) {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        
        if (!m_tlsConn || !m_tlsConn->isConnected()) {
            return false;
        }

        auto data = MessageSerializer::serialize(msg);
        
        // Loop until all bytes are sent (SSL_write may do partial writes)
        size_t totalSent = 0;
        while (totalSent < data.size()) {
            int sent = m_tlsConn->send(data.data() + totalSent, data.size() - totalSent);
            if (sent <= 0) {
                spdlog::error("PeerConnection: TLS send failed after {} of {} bytes", 
                              totalSent, data.size());
                return false;
            }
            totalSent += sent;
        }
        return true;
    }

    // Helper to receive a complete message with timeout
    std::optional<Message> receiveFullMessage(std::chrono::seconds timeout) {
        std::vector<uint8_t> accumulated;
        std::vector<uint8_t> buffer(4096);
        
        auto startTime = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - startTime < timeout) {
            int received = m_tlsConn->receive(buffer.data(), buffer.size());
            if (received < 0) {
                return std::nullopt;
            }
            if (received == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            accumulated.insert(accumulated.end(), buffer.begin(), buffer.begin() + received);
            
            // Check if we have a complete message
            if (accumulated.size() >= 8) {
                size_t msgSize = MessageSerializer::getMessageSize(accumulated.data(), accumulated.size());
                if (msgSize > 0 && msgSize <= accumulated.size()) {
                    return MessageSerializer::deserialize(accumulated.data(), msgSize);
                }
            }
        }
        
        return std::nullopt;
    }
    
    bool exchangeDeviceInfo() {
        // Send our device info
        Message infoMsg(MessageType::DeviceInfo, generateRequestId());
        DeviceInfoPayload payload;
        payload.deviceId = m_pairing->getDeviceId();
        payload.deviceName = m_pairing->getDeviceName();
        payload.deviceType = m_pairing->getDeviceType();
        payload.protocolVersion = MESSAGE_PROTOCOL_VERSION;
        payload.fileCount = 0;  // TODO: get actual count
        payload.lastSyncTimestamp = 0;
        infoMsg.setJsonPayload(payload.toJson());

        if (!sendMessageInternal(infoMsg)) {
            m_lastError = "Failed to send device info";
            return false;
        }

        // Receive peer's device info - loop until full message received
        // TLS can return partial fragments, so we need to accumulate
        auto response = receiveFullMessage(std::chrono::seconds(10));
        if (!response || response->type != MessageType::DeviceInfo) {
            m_lastError = response ? "Invalid device info response" : "Timeout waiting for device info";
            return false;
        }

        auto peerPayload = DeviceInfoPayload::fromJson(response->getJsonPayload());
        if (!peerPayload) {
            m_lastError = "Failed to parse device info";
            return false;
        }

        // Security check: For incoming connections, verify announced ID matches TLS identity
        // TLS-PSK only transmits identity client→server, so:
        // - Server (m_isIncoming=true): Has authentic client ID from TLS, must verify
        // - Client (m_isIncoming=false): Gets server ID from exchange, trust it (TLS succeeded)
        std::string tlsIdentity = m_tlsConn->getPeerIdentity();
        if (m_isIncoming && !tlsIdentity.empty() && tlsIdentity != peerPayload->deviceId) {
            spdlog::error("PeerConnection: Identity spoofing detected! TLS='{}', announced='{}'",
                          tlsIdentity, peerPayload->deviceId);
            m_lastError = "Identity mismatch - possible spoofing attempt";
            return false;
        }

        // Store peer info
        m_peerInfo.deviceId = peerPayload->deviceId;
        m_peerInfo.deviceName = peerPayload->deviceName;
        m_peerInfo.deviceType = peerPayload->deviceType;
        m_peerInfo.fileCount = peerPayload->fileCount;
        m_peerInfo.isOnline = true;
        m_peerInfo.isConnected = true;

        // For client connections, update m_peerId to the server's announced ID
        // (TLS PSK identity is only sent client→server, client needs the server's ID)
        if (!m_isIncoming) {
            m_peerId = peerPayload->deviceId;
        }
        // For server, m_peerId was already set correctly from TLS identity

        m_lastReceived = Clock::now();
        spdlog::debug("PeerConnection: Exchanged device info with {} ({})", 
                      m_peerInfo.deviceName, m_peerId);
        return true;
    }

    void startReceiveLoop() {
        m_running = true;
        m_receiveThread = std::thread([this]() { receiveLoop(); });
    }

    void receiveLoop() {
        std::vector<uint8_t> buffer(16 * 1024);
        std::vector<uint8_t> accumulated;
        constexpr size_t MAX_ACCUMULATED_SIZE = 16 * 1024 * 1024;  // 16MB limit

        while (m_running) {
            // Check connection under mutex to avoid race with disconnect()
            TlsPskConnection* conn = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_tlsMutex);
                if (!m_tlsConn || !m_tlsConn->isConnected()) {
                    break;
                }
                conn = m_tlsConn.get();
            }
            
            int received = conn->receive(buffer.data(), buffer.size());
            if (received <= 0) {
                if (m_running) {
                    reportError("Connection lost");
                    disconnect();
                }
                break;
            }

            m_lastReceived = Clock::now();
            accumulated.insert(accumulated.end(), buffer.begin(), buffer.begin() + received);
            
            // Protect against OOM from malicious/buggy peers
            if (accumulated.size() > MAX_ACCUMULATED_SIZE) {
                spdlog::error("PeerConnection: Accumulated buffer overflow, disconnecting");
                reportError("Protocol violation: message too large");
                disconnect();
                break;
            }

            // Process complete messages
            while (accumulated.size() >= 8) {
                size_t msgSize = MessageSerializer::getMessageSize(accumulated.data(), accumulated.size());
                
                // Check for protocol violation (message claims to be larger than MAX_MESSAGE_SIZE)
                // getMessageSize returns 0 for oversized messages, but we need to detect this
                // explicitly to close the connection rather than waiting forever
                if (msgSize == 0 && accumulated.size() >= 8) {
                    // Check if this is "need more data" vs "invalid header"
                    uint32_t claimedLen = (accumulated[4] << 24) | (accumulated[5] << 16) | 
                                          (accumulated[6] << 8) | accumulated[7];
                    if (claimedLen > MAX_MESSAGE_SIZE) {
                        spdlog::error("PeerConnection: Message size {} exceeds limit, disconnecting", claimedLen);
                        reportError("Protocol violation: message too large");
                        disconnect();
                        return;
                    }
                }
                
                if (msgSize == 0 || msgSize > accumulated.size()) {
                    break;  // Need more data
                }

                auto msg = MessageSerializer::deserialize(accumulated.data(), msgSize);
                accumulated.erase(accumulated.begin(), accumulated.begin() + msgSize);

                if (msg) {
                    handleMessage(*msg);
                }
            }
        }
    }

    void handleMessage(const Message& msg) {
        spdlog::debug("PeerConnection: Received {} from {}", messageTypeName(msg.type), m_peerId);

        switch (msg.type) {
            case MessageType::Heartbeat:
                // Respond with ack
                {
                    Message ack(MessageType::HeartbeatAck, msg.requestId);
                    sendMessageInternal(ack);
                }
                break;

            case MessageType::HeartbeatAck:
                // Just update lastReceived (already done above)
                break;

            case MessageType::Disconnect:
                spdlog::info("PeerConnection: Peer {} requested disconnect", m_peerId);
                disconnect();
                break;

            default:
                // Check if it's a response to a pending request
                if (!msg.requestId.empty()) {
                    std::lock_guard<std::mutex> lock(m_pendingMutex);
                    auto it = m_pendingResponses.find(msg.requestId);
                    if (it != m_pendingResponses.end()) {
                        it->second = msg;
                        m_pendingCv.notify_all();
                        return;
                    }
                }

                // Otherwise, notify callback
                {
                    std::lock_guard<std::mutex> lock(m_callbackMutex);
                    if (m_onMessage) {
                        m_onMessage(msg);
                    }
                }
                break;
        }
    }

    void startHeartbeat() {
        m_heartbeatThread = std::thread([this]() { heartbeatLoop(); });
    }

    void heartbeatLoop() {
        while (m_running) {
            std::unique_lock<std::mutex> lock(m_heartbeatMutex);
            m_heartbeatCv.wait_for(lock, std::chrono::seconds(HEARTBEAT_INTERVAL_SEC));

            if (!m_running) break;

            // Check if connection is stale
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                Clock::now() - m_lastReceived).count();
            
            if (elapsed > CONNECTION_TIMEOUT_SEC) {
                spdlog::warn("PeerConnection: Connection to {} timed out", m_peerId);
                reportError("Connection timeout");
                disconnect();
                break;
            }

            // Send heartbeat
            Message heartbeat(MessageType::Heartbeat, generateRequestId());
            sendMessageInternal(heartbeat);
        }
    }
};

// ═══════════════════════════════════════════════════════════
// PeerConnection Public Interface
// ═══════════════════════════════════════════════════════════

PeerConnection::PeerConnection(std::shared_ptr<FamilyPairing> pairing)
    : m_impl(std::make_unique<Impl>(std::move(pairing))) {}

PeerConnection::~PeerConnection() = default;

bool PeerConnection::connect(const std::string& host, uint16_t port) {
    return m_impl->connect(host, port);
}

bool PeerConnection::acceptConnection(std::unique_ptr<TlsPskConnection> tlsConnection) {
    return m_impl->acceptConnection(std::move(tlsConnection));
}

void PeerConnection::disconnect() {
    m_impl->disconnect();
}

PeerConnection::State PeerConnection::getState() const {
    return m_impl->getState();
}

bool PeerConnection::isConnected() const {
    return m_impl->isConnected();
}

DeviceInfo PeerConnection::getPeerInfo() const {
    return m_impl->getPeerInfo();
}

std::string PeerConnection::getPeerId() const {
    return m_impl->getPeerId();
}

std::string PeerConnection::getPeerAddress() const {
    return m_impl->getPeerAddress();
}

bool PeerConnection::sendMessage(const Message& msg) {
    return m_impl->sendMessage(msg);
}

std::optional<Message> PeerConnection::sendAndWait(const Message& msg, int timeoutMs) {
    return m_impl->sendAndWait(msg, timeoutMs);
}

void PeerConnection::onMessage(MessageCallback callback) {
    m_impl->onMessage(std::move(callback));
}

void PeerConnection::onStateChanged(StateCallback callback) {
    m_impl->onStateChanged(std::move(callback));
}

void PeerConnection::onError(ErrorCallback callback) {
    m_impl->onError(std::move(callback));
}

std::string PeerConnection::getLastError() const {
    return m_impl->getLastError();
}

} // namespace FamilyVault

