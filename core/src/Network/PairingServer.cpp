// PairingServer.cpp — TCP сервер для первичного pairing (без TLS)

#include "familyvault/Network/PairingServer.h"
#include <spdlog/spdlog.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstring>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
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
    #include <errno.h>
    using socket_t = int;
    #define SOCKET_INVALID (-1)
    #define CLOSE_SOCKET close
    #define SOCKET_ERROR_CODE errno
#endif

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// PairingServer::Impl
// ═══════════════════════════════════════════════════════════

class PairingServer::Impl {
public:
    Impl() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            spdlog::error("PairingServer: WSAStartup failed");
        }
#endif
    }

    ~Impl() {
        stop();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // Helper to receive a complete message (handles TCP fragmentation)
    // Returns deserialized message or nullopt on error/timeout
    std::optional<Message> receiveCompleteMessage(socket_t sock, int timeoutSeconds = 10) {
        std::vector<uint8_t> accumulated;
        std::vector<uint8_t> buffer(4096);
        
        auto startTime = std::chrono::steady_clock::now();
        auto timeout = std::chrono::seconds(timeoutSeconds);
        
        while (std::chrono::steady_clock::now() - startTime < timeout) {
            int received = recv(sock, reinterpret_cast<char*>(buffer.data()), 
                               static_cast<int>(buffer.size()), 0);
            
            if (received < 0) {
                // Would block - try again if we have time
                #ifdef _WIN32
                if (WSAGetLastError() == WSAEWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                #else
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                #endif
                return std::nullopt;
            }
            
            if (received == 0) {
                // Connection closed
                return std::nullopt;
            }
            
            accumulated.insert(accumulated.end(), buffer.begin(), buffer.begin() + received);
            
            // Try to deserialize - if we have a complete message, return it
            if (accumulated.size() >= 8) {  // Minimum header size
                size_t msgSize = MessageSerializer::getMessageSize(accumulated.data(), accumulated.size());
                if (msgSize > 0 && msgSize <= accumulated.size()) {
                    return MessageSerializer::deserialize(accumulated.data(), msgSize);
                }
            }
        }
        
        spdlog::warn("PairingServer: Timeout receiving complete message");
        return std::nullopt;
    }

    bool start(uint16_t port, PairingServer::PairingCallback callback) {
        if (m_running) {
            m_lastError = "Server already running";
            return false;
        }

        m_callback = std::move(callback);

        // Create TCP socket
        m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_serverSocket == SOCKET_INVALID) {
            m_lastError = "Failed to create socket";
            spdlog::error("PairingServer: {}", m_lastError);
            return false;
        }

        // Allow address reuse
        int opt = 1;
#ifdef _WIN32
        setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

        // Bind to port
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(m_serverSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            m_lastError = "Failed to bind: " + std::to_string(SOCKET_ERROR_CODE);
            spdlog::error("PairingServer: {}", m_lastError);
            CLOSE_SOCKET(m_serverSocket);
            m_serverSocket = SOCKET_INVALID;
            return false;
        }

        // Get actual bound port (if port was 0)
        socklen_t addrLen = sizeof(addr);
        getsockname(m_serverSocket, reinterpret_cast<sockaddr*>(&addr), &addrLen);
        m_port = ntohs(addr.sin_port);

        // Listen
        if (listen(m_serverSocket, 5) < 0) {
            m_lastError = "Failed to listen: " + std::to_string(SOCKET_ERROR_CODE);
            spdlog::error("PairingServer: {}", m_lastError);
            CLOSE_SOCKET(m_serverSocket);
            m_serverSocket = SOCKET_INVALID;
            return false;
        }

        m_running = true;
        m_acceptThread = std::thread([this]() { acceptLoop(); });

        spdlog::info("PairingServer: Started on port {}", m_port);
        return true;
    }

    void stop() {
        if (!m_running) return;

        m_running = false;

        // Close server socket to unblock accept()
        if (m_serverSocket != SOCKET_INVALID) {
            CLOSE_SOCKET(m_serverSocket);
            m_serverSocket = SOCKET_INVALID;
        }

        if (m_acceptThread.joinable()) {
            m_acceptThread.join();
        }

        spdlog::info("PairingServer: Stopped");
    }

    bool isRunning() const { return m_running; }
    uint16_t getPort() const { return m_port; }
    std::string getLastError() const { return m_lastError; }

private:
    socket_t m_serverSocket = SOCKET_INVALID;
    uint16_t m_port = 0;
    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;
    PairingServer::PairingCallback m_callback;
    std::string m_lastError;

    void acceptLoop() {
        while (m_running) {
            sockaddr_in clientAddr{};
            socklen_t clientAddrLen = sizeof(clientAddr);

            socket_t clientSocket = accept(m_serverSocket, 
                reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);

            if (clientSocket == SOCKET_INVALID) {
                if (m_running) {
                    spdlog::debug("PairingServer: accept() failed: {}", SOCKET_ERROR_CODE);
                }
                continue;
            }

            // Get client IP
            char clientIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
            spdlog::info("PairingServer: Connection from {}", clientIp);

            // Handle client in same thread (pairing is quick, one at a time is fine)
            handleClient(clientSocket, clientIp);
        }
    }

    void handleClient(socket_t clientSocket, const char* clientIp) {
        // Set receive timeout
#ifdef _WIN32
        DWORD timeout = 10000;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, 
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        // Receive complete message (handles TCP fragmentation)
        auto msg = receiveCompleteMessage(clientSocket);
        if (!msg || msg->type != MessageType::PairingRequest) {
            spdlog::warn("PairingServer: Invalid message from {}", clientIp);
            
            // Send error response
            PairingResponsePayload errorResp;
            errorResp.success = false;
            errorResp.errorCode = "INVALID_REQUEST";
            errorResp.errorMessage = "Invalid pairing request";
            sendResponse(clientSocket, errorResp);
            
            CLOSE_SOCKET(clientSocket);
            return;
        }

        // Parse pairing request
        auto request = PairingRequestPayload::fromJson(msg->getJsonPayload());
        if (!request) {
            spdlog::warn("PairingServer: Failed to parse request from {}", clientIp);
            
            PairingResponsePayload errorResp;
            errorResp.success = false;
            errorResp.errorCode = "PARSE_ERROR";
            errorResp.errorMessage = "Failed to parse pairing request";
            sendResponse(clientSocket, errorResp);
            
            CLOSE_SOCKET(clientSocket);
            return;
        }

        spdlog::info("PairingServer: Pairing request from '{}' ({}), PIN: {}", 
                     request->deviceName, request->deviceId, request->pin);

        // Call callback to validate PIN and get response
        PairingResponsePayload response;
        if (m_callback) {
            response = m_callback(*request);
        } else {
            response.success = false;
            response.errorCode = "NO_HANDLER";
            response.errorMessage = "No pairing handler configured";
        }

        // Send response
        sendResponse(clientSocket, response);

        if (response.success) {
            spdlog::info("PairingServer: Successfully paired with '{}'", request->deviceName);
        } else {
            spdlog::warn("PairingServer: Pairing failed for '{}': {}", 
                         request->deviceName, response.errorMessage);
        }

        CLOSE_SOCKET(clientSocket);
    }

    void sendResponse(socket_t clientSocket, const PairingResponsePayload& response) {
        Message msg(MessageType::PairingResponse);
        msg.setJsonPayload(response.toJson());

        auto data = MessageSerializer::serialize(msg);
        send(clientSocket, reinterpret_cast<const char*>(data.data()), 
             static_cast<int>(data.size()), 0);
    }
};

// ═══════════════════════════════════════════════════════════
// PairingServer Public Interface
// ═══════════════════════════════════════════════════════════

PairingServer::PairingServer() : m_impl(std::make_unique<Impl>()) {}
PairingServer::~PairingServer() = default;

bool PairingServer::start(uint16_t port, PairingCallback callback) {
    return m_impl->start(port, std::move(callback));
}

void PairingServer::stop() {
    m_impl->stop();
}

bool PairingServer::isRunning() const {
    return m_impl->isRunning();
}

uint16_t PairingServer::getPort() const {
    return m_impl->getPort();
}

std::string PairingServer::getLastError() const {
    return m_impl->getLastError();
}

// ═══════════════════════════════════════════════════════════
// PairingClient::Impl
// ═══════════════════════════════════════════════════════════

class PairingClient::Impl {
public:
    Impl() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            spdlog::error("PairingClient: WSAStartup failed");
        }
#endif
    }

    ~Impl() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    // Helper to receive a complete message (handles TCP fragmentation)
    std::optional<Message> receiveCompleteMessage(socket_t sock, int timeoutMs) {
        std::vector<uint8_t> accumulated;
        std::vector<uint8_t> buffer(4096);
        
        auto startTime = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(timeoutMs);
        
        while (std::chrono::steady_clock::now() - startTime < timeout) {
            int received = recv(sock, reinterpret_cast<char*>(buffer.data()), 
                               static_cast<int>(buffer.size()), 0);
            
            if (received < 0) {
                #ifdef _WIN32
                if (WSAGetLastError() == WSAEWOULDBLOCK || WSAGetLastError() == WSAETIMEDOUT) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                #else
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                #endif
                return std::nullopt;
            }
            
            if (received == 0) {
                return std::nullopt;
            }
            
            accumulated.insert(accumulated.end(), buffer.begin(), buffer.begin() + received);
            
            if (accumulated.size() >= 8) {
                size_t msgSize = MessageSerializer::getMessageSize(accumulated.data(), accumulated.size());
                if (msgSize > 0 && msgSize <= accumulated.size()) {
                    return MessageSerializer::deserialize(accumulated.data(), msgSize);
                }
            }
        }
        
        return std::nullopt;
    }

    std::optional<PairingResponsePayload> pair(
        const std::string& host,
        uint16_t port,
        const PairingRequestPayload& request,
        int timeoutMs
    ) {
        // Create TCP socket
        socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == SOCKET_INVALID) {
            m_lastError = "Failed to create socket";
            return std::nullopt;
        }

        // Set connect timeout
#ifdef _WIN32
        DWORD timeout = timeoutMs;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, 
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, 
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        // Connect
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            m_lastError = "Invalid host address: " + host;
            CLOSE_SOCKET(sock);
            return std::nullopt;
        }

        spdlog::info("PairingClient: Connecting to {}:{}", host, port);

        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            m_lastError = "Failed to connect: " + std::to_string(SOCKET_ERROR_CODE);
            spdlog::error("PairingClient: {}", m_lastError);
            CLOSE_SOCKET(sock);
            return std::nullopt;
        }

        // Send pairing request
        Message msg(MessageType::PairingRequest);
        msg.setJsonPayload(request.toJson());
        auto data = MessageSerializer::serialize(msg);

        int sent = send(sock, reinterpret_cast<const char*>(data.data()), 
                        static_cast<int>(data.size()), 0);
        if (sent != static_cast<int>(data.size())) {
            m_lastError = "Failed to send request";
            CLOSE_SOCKET(sock);
            return std::nullopt;
        }

        spdlog::debug("PairingClient: Sent pairing request, waiting for response...");

        // Receive complete response (handles TCP fragmentation)
        auto response = receiveCompleteMessage(sock, timeoutMs);
        
        CLOSE_SOCKET(sock);

        if (!response || response->type != MessageType::PairingResponse) {
            m_lastError = "Invalid or incomplete response";
            return std::nullopt;
        }

        auto payload = PairingResponsePayload::fromJson(response->getJsonPayload());
        if (!payload) {
            m_lastError = "Failed to parse response";
            return std::nullopt;
        }

        if (payload->success) {
            spdlog::info("PairingClient: Pairing successful!");
        } else {
            spdlog::warn("PairingClient: Pairing failed: {}", payload->errorMessage);
            m_lastError = payload->errorMessage;
        }

        return payload;
    }

    std::string getLastError() const { return m_lastError; }

private:
    std::string m_lastError;
};

// ═══════════════════════════════════════════════════════════
// PairingClient Public Interface
// ═══════════════════════════════════════════════════════════

PairingClient::PairingClient() : m_impl(std::make_unique<Impl>()) {}
PairingClient::~PairingClient() = default;

std::optional<PairingResponsePayload> PairingClient::pair(
    const std::string& host,
    uint16_t port,
    const PairingRequestPayload& request,
    int timeoutMs
) {
    return m_impl->pair(host, port, request, timeoutMs);
}

std::string PairingClient::getLastError() const {
    return m_impl->getLastError();
}

} // namespace FamilyVault

