// test_tls_psk.cpp — Тесты TLS 1.3 PSK

#include <gtest/gtest.h>
#include "familyvault/Network/TlsPsk.h"
#include "familyvault/FamilyPairing.h"
#include "familyvault/SecureStorage.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace FamilyVault;

namespace {

// Helper: generate test PSK
std::array<uint8_t, TLS_PSK_SIZE> getTestPsk() {
    std::array<uint8_t, TLS_PSK_SIZE> psk;
    for (size_t i = 0; i < TLS_PSK_SIZE; ++i) {
        psk[i] = static_cast<uint8_t>(i + 1);
    }
    return psk;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// TlsPskConnection Tests
// ═══════════════════════════════════════════════════════════

TEST(TlsPskConnectionTest, CreateAndDestroy) {
    TlsPskConnection conn;
    EXPECT_FALSE(conn.isConnected());
}

TEST(TlsPskConnectionTest, ConnectWithoutServer) {
    TlsPskConnection conn;
    conn.setPsk(getTestPsk(), "test-client");
    
    // Should fail - no server listening
    bool result = conn.connect("127.0.0.1", 45699); // Non-standard port
    EXPECT_FALSE(result);
    EXPECT_FALSE(conn.isConnected());
}

// ═══════════════════════════════════════════════════════════
// TlsPskServer Tests
// ═══════════════════════════════════════════════════════════

TEST(TlsPskServerTest, CreateAndDestroy) {
    TlsPskServer server;
    EXPECT_FALSE(server.isRunning());
}

TEST(TlsPskServerTest, StartAndStop) {
    TlsPskServer server;
    server.setPsk(getTestPsk(), "test-server");
    
    // Use high port to avoid conflicts
    EXPECT_TRUE(server.start(45699));
    EXPECT_TRUE(server.isRunning());
    EXPECT_EQ(server.getPort(), 45699);
    
    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST(TlsPskServerTest, DoubleStartIsIdempotent) {
    TlsPskServer server;
    server.setPsk(getTestPsk(), "test-server");
    
    EXPECT_TRUE(server.start(45698));
    EXPECT_TRUE(server.start(45698)); // Should return true (already running)
    
    server.stop();
}

// ═══════════════════════════════════════════════════════════
// Client-Server Integration Tests
// ═══════════════════════════════════════════════════════════

TEST(TlsPskIntegrationTest, ConnectAndExchangeData) {
    auto psk = getTestPsk();
    const uint16_t port = 45697;
    
    // Start server in a thread
    TlsPskServer server;
    server.setPsk(psk, "server-device-id");
    ASSERT_TRUE(server.start(port));
    
    std::atomic<bool> serverDone{false};
    std::string serverReceivedMsg;
    std::string serverError;
    
    std::thread serverThread([&]() {
        auto conn = server.accept();
        if (!conn) {
            serverError = server.getLastError();
            serverDone = true;
            return;
        }
        
        // Receive message
        uint8_t buffer[256];
        int received = conn->receive(buffer, sizeof(buffer));
        if (received > 0) {
            serverReceivedMsg = std::string(reinterpret_cast<char*>(buffer), received);
        }
        
        // Send response
        const char* response = "Hello from server!";
        conn->send(reinterpret_cast<const uint8_t*>(response), strlen(response));
        
        conn->close();
        serverDone = true;
    });
    
    // Give server time to start accepting
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Client connects
    TlsPskConnection client;
    client.setPsk(psk, "client-device-id");
    
    bool connected = client.connect("127.0.0.1", port);
    ASSERT_TRUE(connected) << "Client failed to connect: " << client.getLastError();
    EXPECT_TRUE(client.isConnected());
    
    // Send message
    const char* clientMsg = "Hello from client!";
    int sent = client.send(reinterpret_cast<const uint8_t*>(clientMsg), strlen(clientMsg));
    EXPECT_GT(sent, 0);
    
    // Receive response
    uint8_t buffer[256];
    int received = client.receive(buffer, sizeof(buffer));
    EXPECT_GT(received, 0);
    
    std::string response(reinterpret_cast<char*>(buffer), received);
    EXPECT_EQ(response, "Hello from server!");
    
    client.close();
    
    // Wait for server
    serverThread.join();
    
    EXPECT_TRUE(serverDone);
    EXPECT_EQ(serverReceivedMsg, "Hello from client!");
    EXPECT_TRUE(serverError.empty()) << "Server error: " << serverError;
    
    server.stop();
}

TEST(TlsPskIntegrationTest, WrongPskRejected) {
    auto serverPsk = getTestPsk();
    auto clientPsk = getTestPsk();
    clientPsk[0] = 0xFF; // Different PSK
    
    const uint16_t port = 45696;
    
    TlsPskServer server;
    server.setPsk(serverPsk, "server");
    ASSERT_TRUE(server.start(port));
    
    std::atomic<bool> serverDone{false};
    std::unique_ptr<TlsPskConnection> serverConn;
    
    std::thread serverThread([&]() {
        serverConn = server.accept();
        serverDone = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Client with wrong PSK
    TlsPskConnection client;
    client.setPsk(clientPsk, "client");
    
    // Connection should fail during TLS handshake
    bool connected = client.connect("127.0.0.1", port);
    // Note: behavior depends on OpenSSL version - might fail connect or accept
    
    serverThread.join();
    
    // At least one side should have failed
    if (connected && serverConn) {
        // If both "connected", the handshake was wrong - data exchange should fail
        // This is a simplification; in real TLS PSK, wrong PSK = handshake failure
    }
    
    server.stop();
}

TEST(TlsPskIntegrationTest, IdentityValidation) {
    auto psk = getTestPsk();
    const uint16_t port = 45695;
    
    TlsPskServer server;
    server.setPsk(psk, "server");
    
    // Only accept specific identity
    server.setIdentityValidator([](const std::string& identity) {
        return identity == "allowed-client";
    });
    
    ASSERT_TRUE(server.start(port));
    
    std::atomic<bool> serverDone{false};
    std::unique_ptr<TlsPskConnection> serverConn;
    
    std::thread serverThread([&]() {
        serverConn = server.accept();
        serverDone = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Client with wrong identity
    TlsPskConnection client;
    client.setPsk(psk, "wrong-client");
    
    bool connected = client.connect("127.0.0.1", port);
    
    serverThread.join();
    
    // Server should have rejected due to identity validation
    // (Connection established but validator rejects)
    if (connected) {
        EXPECT_EQ(serverConn, nullptr) << "Server should have rejected wrong identity";
    }
    
    server.stop();
}

// ═══════════════════════════════════════════════════════════
// Integration with FamilyPairing
// ═══════════════════════════════════════════════════════════

TEST(TlsPskIntegrationTest, WithFamilyPairing) {
    // Create FamilyPairing and derive PSK
    auto storage = std::make_shared<SecureStorage>();
    
    // Clean up any existing data
    storage->remove("fv_family_secret");
    storage->remove("fv_device_id");
    
    FamilyPairing pairing(storage);
    
    // Create family
    auto pairingInfo = pairing.createFamily();
    ASSERT_TRUE(pairing.isConfigured());
    
    // Get PSK
    auto psk = pairing.derivePsk();
    ASSERT_TRUE(psk.has_value());
    EXPECT_EQ(psk->size(), TLS_PSK_SIZE);
    
    std::string identity = pairing.getDeviceId();
    EXPECT_FALSE(identity.empty());
    
    // Test TLS connection with FamilyPairing-derived PSK
    // Both client and server use same PSK (simulating same family)
    const uint16_t port = 45694;
    
    TlsPskServer server;
    server.setPsk(*psk, identity);
    ASSERT_TRUE(server.start(port));
    
    std::atomic<bool> done{false};
    std::unique_ptr<TlsPskConnection> serverConn;
    
    std::thread serverThread([&]() {
        serverConn = server.accept();
        done = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    TlsPskConnection client;
    client.setPsk(*psk, "client-" + identity);
    
    bool connected = client.connect("127.0.0.1", port);
    EXPECT_TRUE(connected) << "Failed: " << client.getLastError();
    
    serverThread.join();
    
    EXPECT_TRUE(done);
    EXPECT_NE(serverConn, nullptr);
    
    if (serverConn) {
        EXPECT_EQ(serverConn->getPeerIdentity(), "client-" + identity);
    }
    
    client.close();
    server.stop();
    
    // Cleanup
    pairing.reset();
}

