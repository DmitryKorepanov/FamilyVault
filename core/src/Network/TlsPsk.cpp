// TlsPsk.cpp — TLS 1.3 PSK Implementation using OpenSSL

#include "familyvault/Network/TlsPsk.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <spdlog/spdlog.h>
#include <cstring>

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
    #include <netdb.h>
    #include <poll.h>
    using socket_t = int;
    #define SOCKET_INVALID (-1)
    #define CLOSE_SOCKET ::close
    #define SOCKET_ERROR_CODE errno
#endif

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// OpenSSL initialization
// ═══════════════════════════════════════════════════════════

namespace {

class OpenSslInit {
public:
    OpenSslInit() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        // OpenSSL 1.1+ auto-initializes, but we can still call these
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
    }
    
    ~OpenSslInit() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

static OpenSslInit g_openSslInit;

std::string getOpenSslError() {
    unsigned long err = ERR_get_error();
    if (err == 0) return "Unknown SSL error";
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    return buf;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// TlsPskConnection::Impl
// ═══════════════════════════════════════════════════════════

class TlsPskConnection::Impl {
public:
    Impl() = default;
    
    ~Impl() {
        close();
    }

    void setPsk(const std::array<uint8_t, TLS_PSK_SIZE>& psk, const std::string& identity) {
        m_psk = psk;
        m_identity = identity;
    }

    bool connect(const std::string& host, uint16_t port) {
        if (m_connected) {
            m_lastError = "Already connected";
            return false;
        }

        // Create socket
        m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_socket == SOCKET_INVALID) {
            m_lastError = "Failed to create socket: " + std::to_string(SOCKET_ERROR_CODE);
            return false;
        }

        // Resolve address
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            // Try DNS resolution
            hostent* he = gethostbyname(host.c_str());
            if (!he) {
                CLOSE_SOCKET(m_socket);
                m_socket = SOCKET_INVALID;
                m_lastError = "Failed to resolve host: " + host;
                return false;
            }
            memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
        }

        // Connect
        if (::connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            CLOSE_SOCKET(m_socket);
            m_socket = SOCKET_INVALID;
            m_lastError = "Failed to connect: " + std::to_string(SOCKET_ERROR_CODE);
            return false;
        }

        m_remoteAddress = host + ":" + std::to_string(port);

        // Setup TLS client
        if (!setupTlsClient()) {
            CLOSE_SOCKET(m_socket);
            m_socket = SOCKET_INVALID;
            return false;
        }

        m_connected = true;
        spdlog::info("TLS PSK: Connected to {}", m_remoteAddress);
        return true;
    }

    bool accept(int socket) {
        if (m_connected) {
            m_lastError = "Already connected";
            return false;
        }

        m_socket = static_cast<socket_t>(socket);

        // Setup TLS server
        if (!setupTlsServer()) {
            m_socket = SOCKET_INVALID; // Don't close - caller's socket
            return false;
        }

        m_connected = true;
        spdlog::info("TLS PSK: Accepted connection from {}", m_remoteAddress);
        return true;
    }

    void close() {
        if (m_ssl) {
            SSL_shutdown(m_ssl);
            SSL_free(m_ssl);
            m_ssl = nullptr;
        }
        if (m_ctx) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
        }
        if (m_socket != SOCKET_INVALID) {
            CLOSE_SOCKET(m_socket);
            m_socket = SOCKET_INVALID;
        }
        m_connected = false;
    }

    bool isConnected() const {
        return m_connected && m_ssl != nullptr;
    }

    int send(const uint8_t* data, size_t size) {
        if (!isConnected()) {
            m_lastError = "Not connected";
            return -1;
        }

        int sent = SSL_write(m_ssl, data, static_cast<int>(size));
        if (sent <= 0) {
            int err = SSL_get_error(m_ssl, sent);
            if (err == SSL_ERROR_ZERO_RETURN) {
                m_connected = false;
                return 0;
            }
            m_lastError = "SSL_write error: " + std::to_string(err);
            return -1;
        }
        return sent;
    }

    int receive(uint8_t* buffer, size_t maxSize) {
        if (!isConnected()) {
            m_lastError = "Not connected";
            return -1;
        }

        int received = SSL_read(m_ssl, buffer, static_cast<int>(maxSize));
        if (received <= 0) {
            int err = SSL_get_error(m_ssl, received);
            if (err == SSL_ERROR_ZERO_RETURN) {
                m_connected = false;
                return 0;
            }
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                return 0; // Non-blocking would retry
            }
            m_lastError = "SSL_read error: " + std::to_string(err);
            return -1;
        }
        return received;
    }

    std::string getPeerIdentity() const { return m_peerIdentity; }
    std::string getLocalAddress() const { return m_localAddress; }
    std::string getRemoteAddress() const { return m_remoteAddress; }
    std::string getLastError() const { return m_lastError; }

private:
    socket_t m_socket = SOCKET_INVALID;
    SSL_CTX* m_ctx = nullptr;
    SSL* m_ssl = nullptr;
    bool m_connected = false;

    std::array<uint8_t, TLS_PSK_SIZE> m_psk{};
    std::string m_identity;
    std::string m_peerIdentity;
    std::string m_localAddress;
    std::string m_remoteAddress;
    std::string m_lastError;

    bool setupTlsClient() {
        // Create TLS 1.3 client context
        m_ctx = SSL_CTX_new(TLS_client_method());
        if (!m_ctx) {
            m_lastError = "Failed to create SSL context: " + getOpenSslError();
            return false;
        }

        // Set minimum TLS version to 1.3
        SSL_CTX_set_min_proto_version(m_ctx, TLS1_3_VERSION);
        SSL_CTX_set_max_proto_version(m_ctx, TLS1_3_VERSION);

        // Set ciphersuites for TLS 1.3 PSK
        SSL_CTX_set_ciphersuites(m_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");

        // Set PSK callback
        SSL_CTX_set_psk_use_session_callback(m_ctx, pskClientCallback);

        // Create SSL object
        m_ssl = SSL_new(m_ctx);
        if (!m_ssl) {
            m_lastError = "Failed to create SSL object: " + getOpenSslError();
            return false;
        }

        // Store 'this' pointer for callback
        SSL_set_app_data(m_ssl, this);

        // Attach socket
        SSL_set_fd(m_ssl, static_cast<int>(m_socket));

        // Perform handshake
        int result = SSL_connect(m_ssl);
        if (result != 1) {
            int err = SSL_get_error(m_ssl, result);
            m_lastError = "TLS handshake failed: " + std::to_string(err) + " - " + getOpenSslError();
            return false;
        }

        spdlog::debug("TLS PSK: Client handshake complete");
        return true;
    }

    bool setupTlsServer() {
        // Create TLS 1.3 server context
        m_ctx = SSL_CTX_new(TLS_server_method());
        if (!m_ctx) {
            m_lastError = "Failed to create SSL context: " + getOpenSslError();
            return false;
        }

        // Set minimum TLS version to 1.3
        SSL_CTX_set_min_proto_version(m_ctx, TLS1_3_VERSION);
        SSL_CTX_set_max_proto_version(m_ctx, TLS1_3_VERSION);

        // Set ciphersuites for TLS 1.3 PSK
        SSL_CTX_set_ciphersuites(m_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");

        // Set PSK callback
        SSL_CTX_set_psk_find_session_callback(m_ctx, pskServerCallback);

        // Create SSL object
        m_ssl = SSL_new(m_ctx);
        if (!m_ssl) {
            m_lastError = "Failed to create SSL object: " + getOpenSslError();
            return false;
        }

        // Store 'this' pointer for callback
        SSL_set_app_data(m_ssl, this);

        // Attach socket
        SSL_set_fd(m_ssl, static_cast<int>(m_socket));

        // Perform handshake
        int result = SSL_accept(m_ssl);
        if (result != 1) {
            int err = SSL_get_error(m_ssl, result);
            m_lastError = "TLS handshake failed: " + std::to_string(err) + " - " + getOpenSslError();
            return false;
        }

        spdlog::debug("TLS PSK: Server handshake complete, peer: {}", m_peerIdentity);
        return true;
    }

    // TLS 1.3 PSK client callback
    static int pskClientCallback(SSL* ssl, const EVP_MD* md,
                                 const unsigned char** id, size_t* idlen,
                                 SSL_SESSION** sess) {
        auto* self = static_cast<Impl*>(SSL_get_app_data(ssl));
        if (!self) return 0;

        // Create a session with our PSK
        SSL_SESSION* session = SSL_SESSION_new();
        if (!session) return 0;

        // Set the cipher - must match server's preference
        const SSL_CIPHER* cipher = SSL_get_pending_cipher(ssl);
        if (!cipher) {
            // Fallback to first available TLS 1.3 cipher
            cipher = SSL_CIPHER_find(ssl, reinterpret_cast<const unsigned char*>("\x13\x02")); // TLS_AES_256_GCM_SHA384
        }
        if (cipher) {
            SSL_SESSION_set_cipher(session, cipher);
        }

        // Set protocol version
        SSL_SESSION_set_protocol_version(session, TLS1_3_VERSION);

        // Set PSK
        if (SSL_SESSION_set1_master_key(session, self->m_psk.data(), self->m_psk.size()) != 1) {
            SSL_SESSION_free(session);
            return 0;
        }

        // Set identity
        *id = reinterpret_cast<const unsigned char*>(self->m_identity.c_str());
        *idlen = self->m_identity.size();
        *sess = session;

        return 1;
    }

    // TLS 1.3 PSK server callback
    static int pskServerCallback(SSL* ssl, const unsigned char* identity,
                                 size_t identity_len, SSL_SESSION** sess) {
        auto* self = static_cast<Impl*>(SSL_get_app_data(ssl));
        if (!self) return 0;

        // Store peer identity
        self->m_peerIdentity = std::string(reinterpret_cast<const char*>(identity), identity_len);
        spdlog::debug("TLS PSK: Server received identity: {}", self->m_peerIdentity);

        // Create session with our PSK
        SSL_SESSION* session = SSL_SESSION_new();
        if (!session) return 0;

        // Set cipher
        const SSL_CIPHER* cipher = SSL_CIPHER_find(ssl, reinterpret_cast<const unsigned char*>("\x13\x02"));
        if (cipher) {
            SSL_SESSION_set_cipher(session, cipher);
        }

        SSL_SESSION_set_protocol_version(session, TLS1_3_VERSION);

        if (SSL_SESSION_set1_master_key(session, self->m_psk.data(), self->m_psk.size()) != 1) {
            SSL_SESSION_free(session);
            return 0;
        }

        *sess = session;
        return 1;
    }
};

// ═══════════════════════════════════════════════════════════
// TlsPskConnection Public Interface
// ═══════════════════════════════════════════════════════════

TlsPskConnection::TlsPskConnection() : m_impl(std::make_unique<Impl>()) {}
TlsPskConnection::~TlsPskConnection() = default;

TlsPskConnection::TlsPskConnection(TlsPskConnection&& other) noexcept = default;
TlsPskConnection& TlsPskConnection::operator=(TlsPskConnection&& other) noexcept = default;

void TlsPskConnection::setPsk(const std::array<uint8_t, TLS_PSK_SIZE>& psk, const std::string& identity) {
    m_impl->setPsk(psk, identity);
}

bool TlsPskConnection::connect(const std::string& host, uint16_t port) {
    return m_impl->connect(host, port);
}

bool TlsPskConnection::accept(int socket) {
    return m_impl->accept(socket);
}

void TlsPskConnection::close() {
    m_impl->close();
}

bool TlsPskConnection::isConnected() const {
    return m_impl->isConnected();
}

int TlsPskConnection::send(const uint8_t* data, size_t size) {
    return m_impl->send(data, size);
}

int TlsPskConnection::send(const std::vector<uint8_t>& data) {
    return send(data.data(), data.size());
}

int TlsPskConnection::receive(uint8_t* buffer, size_t maxSize) {
    return m_impl->receive(buffer, maxSize);
}

std::vector<uint8_t> TlsPskConnection::receive(size_t maxSize) {
    std::vector<uint8_t> buffer(maxSize);
    int received = receive(buffer.data(), maxSize);
    if (received > 0) {
        buffer.resize(received);
        return buffer;
    }
    return {};
}

std::string TlsPskConnection::getPeerIdentity() const { return m_impl->getPeerIdentity(); }
std::string TlsPskConnection::getLocalAddress() const { return m_impl->getLocalAddress(); }
std::string TlsPskConnection::getRemoteAddress() const { return m_impl->getRemoteAddress(); }
std::string TlsPskConnection::getLastError() const { return m_impl->getLastError(); }

// ═══════════════════════════════════════════════════════════
// TlsPskServer::Impl
// ═══════════════════════════════════════════════════════════

class TlsPskServer::Impl {
public:
    Impl() = default;
    
    ~Impl() {
        stop();
    }

    void setPsk(const std::array<uint8_t, TLS_PSK_SIZE>& psk, const std::string& localIdentity) {
        m_psk = psk;
        m_localIdentity = localIdentity;
    }

    void setIdentityValidator(IdentityValidator validator) {
        m_identityValidator = std::move(validator);
    }

    bool start(uint16_t port) {
        if (m_running) return true;

        // Create listening socket
        m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSocket == SOCKET_INVALID) {
            m_lastError = "Failed to create socket: " + std::to_string(SOCKET_ERROR_CODE);
            return false;
        }

        // Allow address reuse
        int reuseAddr = 1;
        setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<char*>(&reuseAddr), sizeof(reuseAddr));

        // Bind
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            m_lastError = "Failed to bind: " + std::to_string(SOCKET_ERROR_CODE);
            CLOSE_SOCKET(m_listenSocket);
            m_listenSocket = SOCKET_INVALID;
            return false;
        }

        // Listen
        if (listen(m_listenSocket, 10) < 0) {
            m_lastError = "Failed to listen: " + std::to_string(SOCKET_ERROR_CODE);
            CLOSE_SOCKET(m_listenSocket);
            m_listenSocket = SOCKET_INVALID;
            return false;
        }

        // Get actual port (important when port=0 for auto-pick)
        if (port == 0) {
            sockaddr_in boundAddr{};
            socklen_t addrLen = sizeof(boundAddr);
            if (getsockname(m_listenSocket, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) == 0) {
                m_port = ntohs(boundAddr.sin_port);
            } else {
                m_lastError = "Failed to get bound port";
                CLOSE_SOCKET(m_listenSocket);
                m_listenSocket = SOCKET_INVALID;
                return false;
            }
        } else {
            m_port = port;
        }
        
        m_running = true;
        spdlog::info("TLS PSK Server: Started on port {}", m_port);
        return true;
    }

    void stop() {
        m_running = false;
        if (m_listenSocket != SOCKET_INVALID) {
            CLOSE_SOCKET(m_listenSocket);
            m_listenSocket = SOCKET_INVALID;
        }
        spdlog::info("TLS PSK Server: Stopped");
    }

    bool isRunning() const { return m_running; }

    std::unique_ptr<TlsPskConnection> accept() {
        if (!m_running) return nullptr;

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        socket_t clientSocket = ::accept(m_listenSocket,
                                          reinterpret_cast<sockaddr*>(&clientAddr),
                                          &clientLen);

        if (clientSocket == SOCKET_INVALID) {
            if (m_running) {
                m_lastError = "Accept failed: " + std::to_string(SOCKET_ERROR_CODE);
            }
            return nullptr;
        }

        // Get client IP
        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
        spdlog::debug("TLS PSK Server: Incoming connection from {}", clientIp);

        // Create TLS connection
        auto conn = std::make_unique<TlsPskConnection>();
        conn->setPsk(m_psk, m_localIdentity);

        if (!conn->accept(static_cast<int>(clientSocket))) {
            spdlog::warn("TLS PSK Server: Handshake failed from {}: {}", 
                        clientIp, conn->getLastError());
            CLOSE_SOCKET(clientSocket);
            return nullptr;
        }

        // Validate identity if validator is set
        if (m_identityValidator) {
            std::string peerIdentity = conn->getPeerIdentity();
            if (!m_identityValidator(peerIdentity)) {
                spdlog::warn("TLS PSK Server: Identity rejected: {}", peerIdentity);
                conn->close();
                return nullptr;
            }
        }

        return conn;
    }

    uint16_t getPort() const { return m_port; }
    std::string getLastError() const { return m_lastError; }

private:
    socket_t m_listenSocket = SOCKET_INVALID;
    uint16_t m_port = 0;
    bool m_running = false;

    std::array<uint8_t, TLS_PSK_SIZE> m_psk{};
    std::string m_localIdentity;
    IdentityValidator m_identityValidator;
    std::string m_lastError;
};

// ═══════════════════════════════════════════════════════════
// TlsPskServer Public Interface
// ═══════════════════════════════════════════════════════════

TlsPskServer::TlsPskServer() : m_impl(std::make_unique<Impl>()) {}
TlsPskServer::~TlsPskServer() = default;

void TlsPskServer::setPsk(const std::array<uint8_t, TLS_PSK_SIZE>& psk, const std::string& localIdentity) {
    m_impl->setPsk(psk, localIdentity);
}

void TlsPskServer::setIdentityValidator(IdentityValidator validator) {
    m_impl->setIdentityValidator(std::move(validator));
}

bool TlsPskServer::start(uint16_t port) {
    return m_impl->start(port);
}

void TlsPskServer::stop() {
    m_impl->stop();
}

bool TlsPskServer::isRunning() const {
    return m_impl->isRunning();
}

std::unique_ptr<TlsPskConnection> TlsPskServer::accept() {
    return m_impl->accept();
}

uint16_t TlsPskServer::getPort() const {
    return m_impl->getPort();
}

std::string TlsPskServer::getLastError() const {
    return m_impl->getLastError();
}

} // namespace FamilyVault

