# Этап 5: P2P в локальной сети

## Цель
Реализовать безопасный обмен данными между устройствами в одной WiFi сети.

> **📘 Спецификации:** SecureStorage, P2P протокол, TLS PSK, C API — см. [SPECIFICATIONS.md](../SPECIFICATIONS.md)

---

## Задача 5.0: Безопасность P2P — аутентификация и шифрование

### Описание
Реализовать модель доверия и шифрование для P2P коммуникации.

### ⚠️ Критично для безопасности
```
Без этой задачи MVP небезопасен:
- Любой в сети может притвориться устройством семьи
- Данные передаются в открытом виде
- Возможен MITM attack
```

### Модель доверия: Family Pairing

```
Первое устройство (инициатор):
1. Генерирует family_secret (32 байта случайных данных)
2. Отображает 6-значный PIN или QR-код

Второе устройство (присоединяется):
1. Вводит PIN или сканирует QR
2. Получает family_secret
3. Оба устройства сохраняют family_secret в OS Secure Storage

Последующие подключения:
- Устройства с одинаковым family_secret — доверенные
- Используется стандартный TLS 1.3 PSK (Pre-Shared Key) режим
- PSK = HKDF(family_secret), identity = deviceId
- TLS сам проверяет PSK без передачи secret в открытом виде
```

### Архитектура безопасности

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   UDP Discovery (открытый)    TCP + TLS 1.3 PSK            │
│   ┌──────────────────────┐   ┌──────────────────────────┐  │
│   │ Только анонс:        │   │                          │  │
│   │ - deviceId           │   │ TLS 1.3 Pre-Shared Key:  │  │
│   │ - deviceName         │   │ - PSK = family_secret    │  │
│   │ - servicePort        │   │ - PSK identity = deviceId│  │
│   │                      │   │                          │  │
│   │ ❌ НЕ передаётся:    │   │ Всё шифруется с первого  │  │
│   │ - family_secret      │   │ байта TLS handshake!     │  │
│   │ - индекс файлов      │   │                          │  │
│   │ - имена файлов       │   │ ✅ После TLS handshake:  │  │
│   └──────────────────────┘   │ - Index sync             │  │
│                              │ - File transfer          │  │
│                              │ - Search queries         │  │
│                              └──────────────────────────┘  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Протокол: TLS 1.3 PSK Mode

Используем **стандартный TLS 1.3 с Pre-Shared Key** (RFC 8446, Section 4.2.11).
Это проверенный криптографический протокол, не изобретаем свой.

```
PSK (Pre-Shared Key) = HKDF(family_secret, "tls-psk", 32)
PSK Identity = deviceId (UUID)

TLS 1.3 PSK Handshake (стандартный):
┌────────────────────────────────────────────────────────────┐
│ Client                                    Server           │
│                                                            │
│ ClientHello + psk_identity(deviceId) ──────────────────►  │
│              + key_share                                   │
│                                                            │
│ ◄────────────────────────────── ServerHello + key_share   │
│                                 + pre_shared_key          │
│                                 {EncryptedExtensions}     │
│                                 {Finished}                │
│                                                            │
│ {Finished} ────────────────────────────────────────────►  │
│                                                            │
│ ═══════════ Encrypted Application Data ═══════════════   │
│                                                            │
└────────────────────────────────────────────────────────────┘

Если PSK не совпадает — TLS handshake fails, соединение закрывается.
Никаких данных в открытом виде не передаётся!
```

### Криптографические примитивы

```cpp
// Используем OpenSSL 1.1.1+ (поддержка TLS 1.3 PSK)

struct FamilyCredentials {
    std::array<uint8_t, 32> familySecret;  // Общий секрет семьи
    std::string deviceId;                   // UUID устройства (PSK identity)
};

// Деривация PSK из family_secret
std::array<uint8_t, 32> derivePsk(const std::array<uint8_t, 32>& familySecret) {
    // HKDF-SHA256(secret, salt="familyvault-psk", info="tls13")
    std::array<uint8_t, 32> psk;
    HKDF(EVP_sha256(), 
         familySecret.data(), familySecret.size(),
         "familyvault-psk", 15,  // salt
         "tls13", 5,             // info
         psk.data(), psk.size());
    return psk;
}

// Pairing: генерация PIN из family_secret
std::string generatePairingPin(const FamilyCredentials& creds) {
    // HKDF → 6 цифр
    uint32_t pin_raw;
    HKDF(EVP_sha256(), 
         creds.familySecret.data(), creds.familySecret.size(),
         "pairing-pin", 11, nullptr, 0,
         (uint8_t*)&pin_raw, sizeof(pin_raw));
    return fmt::format("{:06d}", pin_raw % 1000000);
}
```

### OpenSSL TLS 1.3 PSK Setup

```cpp
// Server-side PSK callback
static unsigned int psk_server_cb(SSL* ssl, const char* identity,
                                  unsigned char* psk, unsigned int max_psk_len) {
    // identity = deviceId from client
    // Проверяем что это известное устройство (опционально)
    
    auto familySecret = loadFamilySecret();  // Из secure storage
    auto derivedPsk = derivePsk(familySecret);
    
    if (derivedPsk.size() > max_psk_len) return 0;
    memcpy(psk, derivedPsk.data(), derivedPsk.size());
    return derivedPsk.size();
}

// Client-side PSK callback  
static unsigned int psk_client_cb(SSL* ssl, const char* hint,
                                  char* identity, unsigned int max_identity_len,
                                  unsigned char* psk, unsigned int max_psk_len) {
    auto creds = loadFamilyCredentials();
    
    // Set identity = deviceId
    strncpy(identity, creds.deviceId.c_str(), max_identity_len);
    
    // Set PSK
    auto derivedPsk = derivePsk(creds.familySecret);
    memcpy(psk, derivedPsk.data(), derivedPsk.size());
    return derivedPsk.size();
}

// SSL context setup
SSL_CTX* createSecureContext(bool isServer) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_method());
    
    // TLS 1.3 only
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    
    // PSK ciphersuites
    SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
    
    // Set PSK callback
    if (isServer) {
        SSL_CTX_set_psk_server_callback(ctx, psk_server_cb);
    } else {
        SSL_CTX_set_psk_client_callback(ctx, psk_client_cb);
    }
    
    return ctx;
}
```

### Обработка ошибок аутентификации

```cpp
// При неудачном PSK — OpenSSL вернёт ошибку handshake
if (SSL_do_handshake(ssl) != 1) {
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_SSL) {
        // PSK mismatch — не член семьи!
        logSecurityEvent("Auth failed", peerIp);
        applyRateLimit(peerIp);  // Защита от brute-force
    }
    SSL_free(ssl);
    close(socket);
    return;
}
// Успех — соединение зашифровано, можно обмениваться данными
```

### Реализация

> **📘 Классы безопасности:** `SecureStorage`, `FamilyPairing`, `TlsPskConnection` определены в [SPECIFICATIONS.md, разделы 6-7](../SPECIFICATIONS.md#6-securestorage--единственное-определение)

**Файлы для создания:**
```
core/include/familyvault/
├── SecureStorage.h      # Уже определён в спецификации
└── Network/
    ├── FamilyPairing.h  # Управление семейным pairing
    └── TlsPsk.h         # TLS 1.3 PSK соединение
```

**Ключи в SecureStorage:**
- `family_secret` — 32 байта, общий секрет семьи
- `device_id` — UUID устройства

### UI для Pairing

```
┌─────────────────────────────────────────────┐
│                                             │
│      🔐 Create Family Vault                 │
│                                             │
│  Share this code with family members:       │
│                                             │
│         ┌───────────────────┐              │
│         │                   │              │
│         │     [QR CODE]     │              │
│         │                   │              │
│         └───────────────────┘              │
│                                             │
│              or enter PIN:                  │
│                                             │
│            ╔═══╦═══╦═══╦═══╦═══╦═══╗       │
│            ║ 4 ║ 7 ║ 2 ║ 8 ║ 1 ║ 9 ║       │
│            ╚═══╩═══╩═══╩═══╩═══╩═══╝       │
│                                             │
│         Expires in 4:32                     │
│                                             │
│  [Cancel]                    [Regenerate]   │
│                                             │
└─────────────────────────────────────────────┘
```

### Критерии приёмки
- [ ] Pairing через PIN/QR работает
- [ ] family_secret хранится безопасно (Keychain/Credential Manager)
- [ ] Handshake проверяет membership без передачи secret
- [ ] TLS шифрует весь трафик после handshake
- [ ] Неавторизованные устройства не получают данные
- [ ] Rate limiting на неудачные попытки
- [ ] UI для создания семьи и присоединения

---

## Задача 5.1: Network Discovery — обнаружение устройств

### Описание
Реализовать обнаружение других устройств FamilyVault в локальной сети.

### Требования
1. UDP broadcast для анонса присутствия
2. Listening для обнаружения других
3. Информация об устройстве: ID, имя, тип, порт
4. Автоматическое обновление списка

### Протокол Discovery

> **📘 Протокол:** Формат UDP broadcast сообщений — см. [SPECIFICATIONS.md, раздел 7.1](../SPECIFICATIONS.md#71-discovery-udp-broadcast)

### Интерфейс

> **📘 Модели:** `DeviceInfo` struct определён в [SPECIFICATIONS.md, раздел 3](../SPECIFICATIONS.md#3-модели-данных-modelsh)

```cpp
// include/familyvault/Network/Discovery.h
#pragma once

#include "Models.h"  // DeviceInfo
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace FamilyVault {

class NetworkDiscovery {
public:
    static constexpr uint16_t BROADCAST_PORT = 45679;
    static constexpr uint16_t SERVICE_PORT = 45678;
    
    void start(const DeviceInfo& thisDevice);
    void stop();
    bool isRunning() const;
    
    std::vector<DeviceInfo> getDevices() const;
    
    using DeviceCallback = std::function<void(const DeviceInfo&)>;
    void onDeviceFound(DeviceCallback callback);
    void onDeviceLost(DeviceCallback callback);
    
private:
    void broadcastLoop();
    void listenLoop();
    void cleanupStaleDevices();
    
    DeviceInfo m_thisDevice;
    std::vector<DeviceInfo> m_devices;
    std::atomic<bool> m_running{false};
    std::thread m_broadcastThread;
    std::thread m_listenThread;
    std::mutex m_mutex;
    DeviceCallback m_onFound;
    DeviceCallback m_onLost;
};

} // namespace FamilyVault
```

### Критерии приёмки
- [ ] Устройства обнаруживают друг друга в LAN
- [ ] Список устройств обновляется автоматически
- [ ] Офлайн устройства помечаются через 15 сек
- [ ] Работает на Windows и Android

---

## Задача 5.2: P2P Protocol — протокол обмена

### Описание
Определить протокол обмена сообщениями между устройствами.

### Требования
1. **TCP + TLS 1.3 PSK** — стандартный TLS с Pre-Shared Key (см. задачу 5.0)
2. Бинарный формат с JSON payload
3. Поддержка больших сообщений (chunking)
4. Heartbeat для поддержания соединения

### Уровни протокола
```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
│  ┌───────────────────────────────────────────────────────┐ │
│  │ FamilyVault Protocol (JSON messages)                  │ │
│  │ - DeviceInfo, IndexSync, FileTransfer, etc.           │ │
│  └───────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                    TLS 1.3 PSK Layer                        │
│  ┌───────────────────────────────────────────────────────┐ │
│  │ Encryption + Authentication (OpenSSL)                 │ │
│  │ PSK = HKDF(family_secret), identity = deviceId        │ │
│  └───────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                    TCP Layer                                │
└─────────────────────────────────────────────────────────────┘

⚠️ ВСЕ данные шифруются TLS — нет plaintext фазы!
   TLS handshake сам проверяет PSK и устанавливает ключи.
```

### Формат сообщения (внутри TLS)
```
┌──────────────┬──────────────┬──────────────────────┐
│ Magic (4B)   │ Length (4B)  │ Payload (variable)   │
│ "FVLT"       │ uint32 BE    │ JSON or binary       │
└──────────────┴──────────────┴──────────────────────┘

Payload JSON:
{
  "type": "message_type",
  "requestId": "uuid",
  "data": { ... }
}
```

### Типы сообщений

> **📘 Протокол:** Полный список MessageType и формат фреймов — см. [SPECIFICATIONS.md, раздел 7.3](../SPECIFICATIONS.md#73-протокол-сообщений-внутри-tls)

### Критерии приёмки
- [ ] Протокол задокументирован
- [ ] Сообщения сериализуются/десериализуются
- [ ] Большие файлы передаются чанками
- [ ] Heartbeat поддерживает соединение

---

## Задача 5.3: PeerConnection — управление соединением

### Описание
Класс для установки и управления TCP соединением с peer.

### Требования
1. Connect/Accept соединения
2. **Аутентификация через family_secret (см. задачу 5.0)**
3. **TLS 1.3 шифрование после аутентификации**
4. Асинхронная отправка/приём сообщений
5. Reconnect при разрыве
6. Graceful disconnect

### Интерфейс
```cpp
// include/familyvault/PeerConnection.h
#pragma once

#include "NetworkProtocol.h"
#include "Security.h"  // FamilyPairing, TlsPskConnection
#include <functional>
#include <queue>
#include <thread>

namespace FamilyVault {

class PeerConnection {
public:
    enum class State {
        Disconnected,
        Connecting,
        Authenticating,   // Handshake in progress
        Connected,        // Authenticated + TLS active
        AuthFailed,       // Not a family member
        Error
    };
    
    // Требуется FamilyPairing для аутентификации
    explicit PeerConnection(std::shared_ptr<FamilyPairing> familyPairing);
    ~PeerConnection();
    
    // Соединение (включает аутентификацию)
    bool connect(const std::string& host, uint16_t port);
    bool accept(int socket);  // Для сервера
    void disconnect();
    
    State getState() const;
    DeviceInfo getPeerInfo() const;
    bool isAuthenticated() const { return m_state == State::Connected; }
    
    // Отправка/приём (только после аутентификации!)
    void send(const Message& msg);
    void sendFile(const std::string& filePath, 
                  std::function<void(int64_t sent, int64_t total)> onProgress);
    
    // Callbacks
    using MessageCallback = std::function<void(const Message&)>;
    using StateCallback = std::function<void(State)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    void onMessage(MessageCallback callback);
    void onStateChanged(StateCallback callback);
    void onError(ErrorCallback callback);
    
private:
    std::shared_ptr<FamilyPairing> m_familyPairing;
    std::unique_ptr<TlsPskConnection> m_tlsConn;  // TLS 1.3 PSK wrapper
    
    void receiveLoop();
    void sendLoop();
    bool performAuthentication();  // Проверка family_secret
    void startHeartbeat();
    
    State m_state = State::Disconnected;
    DeviceInfo m_peerInfo;
    
    std::queue<Message> m_sendQueue;
    std::mutex m_sendMutex;
    std::condition_variable m_sendCv;
    
    std::thread m_receiveThread;
    std::thread m_sendThread;
    
    MessageCallback m_onMessage;
    StateCallback m_onStateChanged;
    ErrorCallback m_onError;
};

} // namespace FamilyVault
```

### Критерии приёмки
- [ ] Соединение устанавливается
- [ ] **Аутентификация проверяет family membership**
- [ ] **TLS шифрует весь трафик после auth**
- [ ] **Неавторизованные устройства отклоняются**
- [ ] Handshake валидирует версию протокола
- [ ] Сообщения отправляются асинхронно
- [ ] Разрыв соединения детектируется

---

## Задача 5.4: PeerServer — приём входящих соединений

### Описание
TCP сервер для приёма соединений от других устройств.

### Требования
1. Listening на порту SERVICE_PORT
2. Accept нескольких соединений
3. Пул соединений с ограничением
4. Thread-safe

### Интерфейс
```cpp
// include/familyvault/PeerServer.h
#pragma once

#include "PeerConnection.h"
#include <vector>
#include <memory>

namespace FamilyVault {

class PeerServer {
public:
    static constexpr int MAX_CONNECTIONS = 10;
    
    PeerServer();
    ~PeerServer();
    
    bool start(uint16_t port = NetworkDiscovery::SERVICE_PORT);
    void stop();
    bool isRunning() const;
    
    std::vector<std::shared_ptr<PeerConnection>> getConnections() const;
    
    using ConnectionCallback = std::function<void(std::shared_ptr<PeerConnection>)>;
    void onNewConnection(ConnectionCallback callback);
    
private:
    void acceptLoop();
    
    int m_serverSocket = -1;
    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;
    
    std::vector<std::shared_ptr<PeerConnection>> m_connections;
    std::mutex m_connectionsMutex;
    
    ConnectionCallback m_onNewConnection;
};

} // namespace FamilyVault
```

### Критерии приёмки
- [ ] Сервер слушает на порту
- [ ] Входящие соединения принимаются
- [ ] Максимум соединений ограничен
- [ ] Закрытые соединения удаляются из пула

---

## Задача 5.5: IndexSync — синхронизация индекса

### Описание
Синхронизация индекса файлов между устройствами.

### Требования
1. Запрос полного индекса при первом подключении
2. Delta sync (только изменения с timestamp)
3. Merge индексов без конфликтов
4. Отметка источника файла (какое устройство)

### Алгоритм синхронизации
```
1. Устройство A подключается к B
2. A отправляет IndexRequest с lastSyncTimestamp
3. B отвечает IndexResponse с файлами:
   - Только visibility = Family (Private НЕ отправляются!)
   - Изменёнными после timestamp
4. A добавляет файлы B в свой индекс с пометкой source=B
5. Периодически (каждые 5 минут) повторяем delta sync

⚠️ ВАЖНО: Private файлы НИКОГДА не покидают устройство!
```

### Схема БД (уже включена в миграцию 001)
```sql
-- Колонки в таблице files:
-- source_device_id TEXT — NULL для локальных, UUID для remote
-- is_remote INTEGER — 0 для локальных, 1 для remote

-- Индекс idx_files_source уже создан в миграции
```

**Примечание:** Колонки source_device_id и is_remote уже включены в начальную миграцию (STAGE_1, Задача 1.2).

### Интерфейс
```cpp
// include/familyvault/IndexSync.h
#pragma once

#include "PeerConnection.h"
#include "IndexManager.h"

namespace FamilyVault {

class IndexSync {
public:
    IndexSync(std::shared_ptr<Database> db,
              std::shared_ptr<IndexManager> indexManager);
    
    // Синхронизация с peer
    void syncWith(std::shared_ptr<PeerConnection> peer);
    
    // Обработка входящих запросов
    void handleIndexRequest(std::shared_ptr<PeerConnection> peer,
                           const Message& request);
    
    // Получение изменений для отправки
    std::vector<FileRecord> getChangesSince(int64_t timestamp) const;
    
private:
    std::shared_ptr<Database> m_db;
    std::shared_ptr<IndexManager> m_indexManager;
    
    void mergeRemoteIndex(const std::string& deviceId,
                         const std::vector<FileRecord>& files);
};

} // namespace FamilyVault
```

### Критерии приёмки
- [ ] Полный sync работает при первом подключении
- [ ] Delta sync передаёт только изменения
- [ ] Удалённые файлы видны в поиске
- [ ] Источник файла отображается в UI

---

## Задача 5.6: RemoteFileAccess — доступ к удалённым файлам

### Описание
Получение файлов с других устройств.

### Требования
1. Запрос файла по ID
2. Streaming для больших файлов
3. Прогресс загрузки
4. Кэширование загруженных файлов
5. Обработка offline устройств

### Интерфейс
```cpp
// include/familyvault/RemoteFileAccess.h
#pragma once

#include "PeerConnection.h"
#include <functional>

namespace FamilyVault {

class RemoteFileAccess {
public:
    using ProgressCallback = std::function<void(int64_t received, int64_t total)>;
    using CompleteCallback = std::function<void(const std::string& localPath)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    RemoteFileAccess(std::shared_ptr<PeerConnection> connection);
    
    // Запрос файла
    void requestFile(int64_t fileId,
                    ProgressCallback onProgress,
                    CompleteCallback onComplete,
                    ErrorCallback onError);
    
    // Запрос thumbnail
    void requestThumbnail(int64_t fileId,
                         CompleteCallback onComplete,
                         ErrorCallback onError);
    
    // Отмена загрузки
    void cancel(int64_t fileId);
    
private:
    std::shared_ptr<PeerConnection> m_connection;
    std::string m_cacheDir;
    
    std::string getCachePath(int64_t fileId);
    void handleFileChunk(const Message& msg);
};

} // namespace FamilyVault
```

### Критерии приёмки
- [ ] Файлы скачиваются с других устройств
- [ ] Прогресс отображается
- [ ] Скачанные файлы кэшируются
- [ ] Ошибки обрабатываются gracefully

---

## Задача 5.7: NetworkManager — верхнеуровневый менеджер

### Описание
Координатор всех сетевых операций.

### Требования
1. Управление discovery, server, connections
2. Автоматическое подключение к известным устройствам
3. Автоматическая синхронизация
4. API для UI

### Интерфейс
```cpp
// include/familyvault/NetworkManager.h
#pragma once

#include "NetworkDiscovery.h"
#include "PeerServer.h"
#include "PeerConnection.h"
#include "IndexSync.h"

namespace FamilyVault {

struct NetworkStatus {
    bool isRunning;
    int discoveredDevices;
    int connectedDevices;
    std::string lastError;
};

class NetworkManager {
public:
    NetworkManager(std::shared_ptr<Database> db,
                   std::shared_ptr<IndexManager> indexManager);
    ~NetworkManager();
    
    // Lifecycle
    void start(const std::string& deviceName);
    void stop();
    
    // Устройства
    std::vector<DeviceInfo> getDiscoveredDevices() const;
    std::vector<DeviceInfo> getConnectedDevices() const;
    
    // Подключение вручную
    bool connectToDevice(const std::string& deviceId);
    void disconnectFromDevice(const std::string& deviceId);
    
    // Синхронизация
    void syncNow();
    
    // Файлы
    void requestFile(const std::string& deviceId, int64_t fileId,
                    RemoteFileAccess::ProgressCallback onProgress,
                    RemoteFileAccess::CompleteCallback onComplete,
                    RemoteFileAccess::ErrorCallback onError);
    
    // Статус
    NetworkStatus getStatus() const;
    
    // Callbacks
    void onDeviceDiscovered(std::function<void(const DeviceInfo&)> callback);
    void onDeviceConnected(std::function<void(const DeviceInfo&)> callback);
    void onDeviceDisconnected(std::function<void(const DeviceInfo&)> callback);
    void onSyncCompleted(std::function<void()> callback);
    
private:
    std::shared_ptr<Database> m_db;
    std::shared_ptr<IndexManager> m_indexManager;
    
    std::unique_ptr<NetworkDiscovery> m_discovery;
    std::unique_ptr<PeerServer> m_server;
    std::unique_ptr<IndexSync> m_indexSync;
    
    std::map<std::string, std::shared_ptr<PeerConnection>> m_connections;
    
    void onPeerDiscovered(const DeviceInfo& device);
    void onNewConnection(std::shared_ptr<PeerConnection> conn);
    void autoConnect();
    void autoSync();
};

} // namespace FamilyVault
```

### Критерии приёмки
- [ ] Start/stop работает корректно
- [ ] Автоподключение к найденным устройствам
- [ ] Автосинхронизация периодически
- [ ] UI получает обновления статуса

---

## Задача 5.8: Network C API

### Описание
C API для сетевых функций, для вызова из Flutter.

### C API

> **📘 Network C API:** Полные функции `fv_network_*`, `fv_pairing_*` определены в [SPECIFICATIONS.md, раздел 5](../SPECIFICATIONS.md#5-c-api-familyvault_ch--полная-версия)

### Критерии приёмки
- [ ] Все сетевые функции доступны через C API
- [ ] Callbacks работают из Dart isolate
- [ ] Нет утечек памяти

---

## Задача 5.9: Flutter UI — Devices Screen

### Описание
Flutter экран для управления сетевыми устройствами.

### Требования
1. Список обнаруженных/подключённых устройств
2. Статус подключения
3. Кнопка подключения/отключения
4. Индикатор синхронизации
5. Pull-to-refresh

### Провайдеры
```dart
@riverpod
class NetworkController extends _$NetworkController {
  @override
  FutureOr<NetworkState> build() async {
    final status = await NativeBridge.instance.getNetworkStatus();
    final devices = await NativeBridge.instance.getNetworkDevices();
    return NetworkState(status: status, devices: devices);
  }
  
  Future<void> connect(String deviceId) async {
    await NativeBridge.instance.networkConnect(deviceId);
    ref.invalidateSelf();
  }
  
  Future<void> disconnect(String deviceId) async {
    await NativeBridge.instance.networkDisconnect(deviceId);
    ref.invalidateSelf();
  }
  
  Future<void> syncNow() async {
    await NativeBridge.instance.networkSyncNow();
  }
}
```

### devices_screen.dart
```dart
class DevicesScreen extends ConsumerWidget {
  const DevicesScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final networkState = ref.watch(networkControllerProvider);
    
    return Scaffold(
      appBar: AppBar(
        title: const Text('Devices'),
        actions: [
          IconButton(
            icon: const Icon(Icons.sync),
            onPressed: () => ref.read(networkControllerProvider.notifier).syncNow(),
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: () => ref.refresh(networkControllerProvider.future),
        child: networkState.when(
          data: (state) => DevicesList(
            devices: state.devices,
            onConnect: (id) => ref.read(networkControllerProvider.notifier).connect(id),
            onDisconnect: (id) => ref.read(networkControllerProvider.notifier).disconnect(id),
          ),
          loading: () => const DevicesListSkeleton(),
          error: (e, _) => AppErrorWidget(message: e.toString()),
        ),
      ),
    );
  }
}

class DevicesList extends StatelessWidget {
  final List<DeviceInfo> devices;
  final ValueChanged<String> onConnect;
  final ValueChanged<String> onDisconnect;
  
  @override
  Widget build(BuildContext context) {
    if (devices.isEmpty) {
      return const EmptyDevicesView();
    }
    
    return ListView.builder(
      itemCount: devices.length,
      itemBuilder: (context, index) {
        final device = devices[index];
        return DeviceListTile(
          device: device,
          onTap: () => device.isConnected 
            ? onDisconnect(device.id) 
            : onConnect(device.id),
        );
      },
    );
  }
}

class DeviceListTile extends StatelessWidget {
  final DeviceInfo device;
  final VoidCallback onTap;
  
  @override
  Widget build(BuildContext context) {
    return ListTile(
      leading: Icon(
        device.type == 'desktop' ? Icons.computer : Icons.phone_android,
        size: 40,
        color: device.isOnline 
          ? Theme.of(context).colorScheme.primary 
          : Colors.grey,
      ),
      title: Text(device.name),
      subtitle: Text(
        device.isConnected 
          ? 'Connected • ${device.fileCount} files' 
          : device.isOnline 
            ? 'Available' 
            : 'Offline',
      ),
      trailing: device.isConnected
        ? const Icon(Icons.check_circle, color: Colors.green)
        : device.isOnline
          ? const Icon(Icons.circle_outlined)
          : null,
      onTap: device.isOnline ? onTap : null,
    );
  }
}
```

### Критерии приёмки
- [ ] Устройства отображаются в списке
- [ ] Статус обновляется автоматически
- [ ] Tap подключает/отключает
- [ ] Sync Now работает
- [ ] Pull-to-refresh обновляет список

---

## Задача 5.10: Android Network — специфика платформы

### Описание
Адаптация сетевого кода для Android.

### Требования
1. WiFi Multicast Lock для discovery
2. Работа в foreground service для стабильности
3. Battery optimization handling
4. Network state monitoring

### Android специфика
```kotlin
// Multicast lock для UDP broadcast
val wifi = getSystemService(WIFI_SERVICE) as WifiManager
val multicastLock = wifi.createMulticastLock("familyvault")
multicastLock.acquire()

// Foreground service для background работы
class NetworkService : Service() {
    override fun onCreate() {
        startForeground(NOTIFICATION_ID, createNotification())
    }
}
```

### Критерии приёмки
- [ ] Discovery работает на Android
- [ ] Работает в background (foreground service)
- [ ] Корректно обрабатывает смену WiFi
- [ ] Не убивается системой

---

## Задача 5.11: Тесты P2P

### Описание
Тесты для сетевых компонентов.

### Требования
1. Unit тесты для protocol parsing
2. Integration тесты для discovery (localhost)
3. Integration тесты для file transfer
4. Mock для сетевого слоя

### Критерии приёмки
- [ ] Тесты проходят в CI
- [ ] Discovery тестируется на localhost
- [ ] File transfer тестируется
- [ ] Timeout и ошибки покрыты

