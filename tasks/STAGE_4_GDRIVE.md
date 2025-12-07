# Этап 4: Google Drive интеграция

## Цель
Добавить Google Drive как источник данных с единым поиском по локальным и облачным файлам.

---

## Задача 4.1: OAuth2 Client — авторизация Google

### Описание
Реализовать OAuth2 flow для Google Drive API.

### Требования
1. Регистрация приложения в Google Cloud Console
2. OAuth2 Authorization Code flow
3. Сохранение и обновление токенов
4. Безопасное хранение credentials

### Предварительная настройка
1. Создать проект в Google Cloud Console
2. Включить Google Drive API
3. Создать OAuth2 Client ID (Desktop app)
4. Получить client_id и client_secret

### OAuth2 Flow
```
1. User clicks "Connect Google Drive"
2. Open browser: https://accounts.google.com/o/oauth2/auth
   ?client_id=...
   &redirect_uri=http://localhost:PORT/callback
   &response_type=code
   &scope=https://www.googleapis.com/auth/drive.readonly
   &access_type=offline
3. User authorizes
4. Google redirects to localhost:PORT/callback?code=...
5. Exchange code for tokens
6. Save refresh_token securely
```

### Критерии приёмки
- [ ] OAuth flow работает в desktop приложении
- [ ] Токены сохраняются зашифрованно
- [ ] Refresh token работает
- [ ] Можно отключить аккаунт

### Интерфейс
```cpp
// include/familyvault/GoogleAuth.h
#pragma once

#include <string>
#include <optional>
#include <functional>

namespace FamilyVault {

struct GoogleCredentials {
    std::string accessToken;
    std::string refreshToken;
    int64_t expiresAt;  // unix timestamp
    std::string email;
};

class GoogleAuth {
public:
    GoogleAuth(const std::string& clientId, const std::string& clientSecret);
    
    // Начать OAuth flow
    // Возвращает URL для открытия в браузере
    std::string startAuthFlow(uint16_t localPort = 8089);
    
    // Ожидание callback от браузера
    // Запускает локальный HTTP сервер на указанном порту
    void waitForCallback(std::function<void(std::optional<GoogleCredentials>)> onComplete);
    
    // Обновление токена
    std::optional<GoogleCredentials> refreshAccessToken(const std::string& refreshToken);
    
    // Проверка и обновление если нужно
    std::optional<GoogleCredentials> ensureValidToken(GoogleCredentials& creds);
    
    // Отзыв токена
    bool revokeToken(const std::string& token);
    
private:
    std::string m_clientId;
    std::string m_clientSecret;
    std::string m_codeVerifier;  // PKCE
    
    std::string generateCodeVerifier();
    std::string generateCodeChallenge(const std::string& verifier);
};

} // namespace FamilyVault
```

---

## Задача 4.2: GoogleDriveClient — работа с API

### Описание
HTTP клиент для Google Drive API.

### Требования
1. Листинг файлов (с пагинацией)
2. Получение метаданных файла
3. Скачивание файлов
4. Получение thumbnails
5. Отслеживание изменений (Changes API)

### API Endpoints
- GET /drive/v3/files — список файлов
- GET /drive/v3/files/{fileId} — метаданные
- GET /drive/v3/files/{fileId}?alt=media — скачивание
- GET /drive/v3/changes — изменения

### Критерии приёмки
- [ ] Листинг возвращает все файлы (с пагинацией)
- [ ] Метаданные включают: имя, размер, MIME, даты, thumbnails
- [ ] Скачивание работает для файлов до 100MB
- [ ] Changes API позволяет инкрементальную синхронизацию

### Интерфейс
```cpp
// include/familyvault/GoogleDriveClient.h
#pragma once

#include "Models.h"
#include "GoogleAuth.h"
#include <vector>
#include <optional>

namespace FamilyVault {

struct GoogleDriveFile {
    std::string id;
    std::string name;
    std::string mimeType;
    int64_t size;
    int64_t createdTime;
    int64_t modifiedTime;
    std::string thumbnailLink;
    std::string webViewLink;
    std::vector<std::string> parents;  // folder IDs
    bool trashed = false;
};

struct GoogleDriveChanges {
    std::vector<GoogleDriveFile> changed;
    std::vector<std::string> removed;  // file IDs
    std::string newStartPageToken;
};

class GoogleDriveClient {
public:
    explicit GoogleDriveClient(GoogleCredentials credentials);
    
    // Листинг
    struct ListOptions {
        std::string query;       // Drive query language
        std::string pageToken;
        int pageSize = 100;
        std::string orderBy = "modifiedTime desc";
        bool includeTrash = false;
    };
    
    struct ListResult {
        std::vector<GoogleDriveFile> files;
        std::string nextPageToken;
    };
    
    ListResult listFiles(const ListOptions& options = {});
    std::vector<GoogleDriveFile> listAllFiles();  // handles pagination
    
    // Отдельный файл
    std::optional<GoogleDriveFile> getFile(const std::string& fileId);
    
    // Скачивание
    std::vector<uint8_t> downloadFile(const std::string& fileId);
    void downloadFileToPath(const std::string& fileId, const std::string& destPath);
    
    // Thumbnails
    std::optional<std::vector<uint8_t>> getThumbnail(const std::string& fileId);
    
    // Changes API
    std::string getStartPageToken();
    GoogleDriveChanges getChanges(const std::string& pageToken);
    
    // Refresh token if needed
    void refreshTokenIfNeeded();
    
private:
    GoogleCredentials m_credentials;
    
    std::string makeRequest(const std::string& endpoint, 
                           const std::map<std::string, std::string>& params = {});
    std::vector<uint8_t> downloadRequest(const std::string& endpoint);
};

} // namespace FamilyVault
```

---

## Задача 4.3: GoogleDriveAdapter — интеграция с индексом

### Описание
Адаптер для индексации Google Drive как источника данных.

### Требования
1. Реализация IStorageAdapter для Google Drive
2. Первоначальное сканирование (full sync)
3. Инкрементальная синхронизация (Changes API)
4. Маппинг GoogleDriveFile → FileRecord

### Критерии приёмки
- [ ] Файлы из Google Drive появляются в индексе
- [ ] Поиск находит файлы из Google Drive
- [ ] Инкрементальная синхронизация работает
- [ ] Удалённые из Drive файлы удаляются из индекса

### Интерфейс
```cpp
// include/familyvault/GoogleDriveAdapter.h
#pragma once

#include "IStorageAdapter.h"
#include "GoogleDriveClient.h"
#include "Database.h"

namespace FamilyVault {

class GoogleDriveAdapter : public IStorageAdapter {
public:
    GoogleDriveAdapter(std::shared_ptr<Database> db,
                       GoogleCredentials credentials);
    
    // IStorageAdapter
    std::string getType() const override { return "google_drive"; }
    std::string getName() const override { return m_accountEmail; }
    bool isAvailable() const override;
    
    // Синхронизация
    void fullSync(ScanProgressCallback onProgress = nullptr);
    void incrementalSync();
    
    // Доступ к файлам
    std::vector<uint8_t> readFile(const std::string& fileId) override;
    std::optional<std::vector<uint8_t>> getThumbnail(const std::string& fileId);
    
    // Change tracking
    std::string getChangeToken() const;
    void setChangeToken(const std::string& token);
    
private:
    std::shared_ptr<Database> m_db;
    std::unique_ptr<GoogleDriveClient> m_client;
    std::string m_accountEmail;
    int64_t m_accountId;  // ID в таблице cloud_accounts
    
    FileRecord mapToFileRecord(const GoogleDriveFile& gfile);
    void saveToDatabase(const std::vector<GoogleDriveFile>& files);
    void removeFromDatabase(const std::vector<std::string>& fileIds);
};

} // namespace FamilyVault
```

---

## Задача 4.4: CloudAccountManager — управление облачными аккаунтами

### Описание
Менеджер для управления подключёнными облачными аккаунтами.

### Требования
1. Добавление/удаление аккаунтов
2. **Хранение credentials в OS Secure Storage** (НЕ в БД!)
3. Периодическая синхронизация
4. UI для управления

### Схема БД (только метаданные, без секретов!)
```sql
CREATE TABLE cloud_accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type TEXT NOT NULL,           -- 'google_drive', 'dropbox', etc
    email TEXT NOT NULL,
    display_name TEXT,
    -- ⚠️ credentials НЕ хранятся в БД! См. SecureStorage ниже
    change_token TEXT,            -- Для incremental sync (не секрет)
    last_sync_at INTEGER,
    file_count INTEGER DEFAULT 0,
    enabled INTEGER DEFAULT 1,
    created_at INTEGER DEFAULT (strftime('%s', 'now'))
);
```

### Хранение OAuth токенов — OS Secure Storage

> **📘 SecureStorage:** Интерфейс и платформенные реализации определены в [SPECIFICATIONS.md, раздел 6](../SPECIFICATIONS.md#6-securestorage--единственное-определение)

**Ключ хранения:** `cloud_account_{id}` — JSON с токенами.

```cpp
// Пример использования в CloudAccountManager:
void CloudAccountManager::addAccount(const GoogleCredentials& creds) {
    json tokenJson = {
        {"access_token", creds.accessToken},
        {"refresh_token", creds.refreshToken},
        {"expires_at", creds.expiresAt}
    };
    // Токены в OS secure storage
    m_secureStorage.storeString(
        fmt::format("cloud_account_{}", accountId),
        tokenJson.dump()
    );
    
    // В БД только метаданные (email, display_name)
    m_db->execute(
        "INSERT INTO cloud_accounts (type, email) VALUES (?, ?)",
        "google_drive", creds.email
    );
}
```

### Критерии приёмки
- [ ] Можно добавить несколько Google аккаунтов
- [ ] **OAuth токены хранятся в OS Secure Storage, не в SQLite**
- [ ] При удалении аккаунта — токены удаляются из secure storage
- [ ] Периодическая синхронизация работает
- [ ] Можно отключить/включить аккаунт

---

## Задача 4.5: Desktop UI — настройки облака

### Описание
UI для управления облачными аккаунтами в desktop приложении.

### Требования
1. Раздел в настройках "Cloud Accounts"
2. Кнопка "Connect Google Drive"
3. Список подключённых аккаунтов
4. Статус синхронизации для каждого
5. Кнопки: Sync Now, Disconnect

### Макет
```
┌─────────────────────────────────────────────┐
│ Cloud Accounts                              │
├─────────────────────────────────────────────┤
│                                             │
│  ☁️ Google Drive                            │
│  ┌─────────────────────────────────────┐   │
│  │ ✅ john@gmail.com                    │   │
│  │    15,234 files • Last sync: 5m ago  │   │
│  │    [Sync Now] [Disconnect]           │   │
│  └─────────────────────────────────────┘   │
│                                             │
│  [+ Connect Google Drive]                   │
│                                             │
│  ─────────────────────────────────────────  │
│  ☁️ Dropbox (coming soon)                   │
│  ☁️ OneDrive (coming soon)                  │
│                                             │
└─────────────────────────────────────────────┘
```

### Критерии приёмки
- [ ] OAuth flow открывает браузер
- [ ] После авторизации аккаунт появляется в списке
- [ ] Статус синхронизации обновляется
- [ ] Disconnect удаляет аккаунт и файлы из индекса

---

## Задача 4.6: Unified Search — объединённый поиск

### Описание
Обновить поиск для работы с локальными и облачными файлами.

### Требования
1. Поиск ищет везде по умолчанию
2. Фильтр по источнику (Local, Google Drive, All)
3. Индикация источника в результатах
4. Доступ к облачным файлам (открытие в браузере или скачивание)

### Обновление UI результатов
```
┌─────────────────────────────────────────────────────┐
│ 📄 report.pdf                         [☁️ GDrive]   │
│    "quarterly report with financial..."             │
│    📁 Work/Reports • 2.3 MB • Modified: Jan 15     │
│    [Download] [Open in Drive]                       │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ 📄 notes.txt                          [💻 Local]    │
│    "meeting notes from monday..."                   │
│    📁 Documents • 12 KB • Modified: Jan 10         │
│    [Open] [Open Folder]                             │
└─────────────────────────────────────────────────────┘
```

### Критерии приёмки
- [ ] Поиск возвращает результаты из всех источников
- [ ] Источник визуально отличается
- [ ] Действия соответствуют типу источника
- [ ] Фильтр по источнику работает

---

## Задача 4.7: Тесты Google Drive интеграции

### Описание
Тесты для Google Drive компонентов.

### Требования
1. Mock для Google API
2. Тесты OAuth flow
3. Тесты GoogleDriveClient
4. Тесты синхронизации

### Критерии приёмки
- [ ] Тесты не требуют реального Google аккаунта
- [ ] Покрыты основные сценарии
- [ ] Тесты проходят в CI

