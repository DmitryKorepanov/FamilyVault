// SecureStorage.cpp — Реализация безопасного хранения секретов
// Платформозависимая реализация

#include "familyvault/SecureStorage.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#endif

#if defined(__ANDROID__) || defined(__linux__)
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Windows Implementation
// ═══════════════════════════════════════════════════════════

#ifdef _WIN32

class SecureStorage::Impl {
public:
    Impl() {
        spdlog::debug("SecureStorage: Windows Credential Manager initialized");
    }

    ~Impl() = default;

    bool store(const std::string& key, const std::vector<uint8_t>& data) {
        if (key.empty() || data.empty()) {
            spdlog::warn("SecureStorage::store: empty key or data");
            return false;
        }

        // Конвертируем ключ в wide string
        std::wstring wideKey = toWideString(key);
        std::wstring targetName = L"FamilyVault:" + wideKey;

        CREDENTIALW cred = {};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<LPWSTR>(targetName.c_str());
        cred.CredentialBlobSize = static_cast<DWORD>(data.size());
        cred.CredentialBlob = const_cast<LPBYTE>(data.data());
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        cred.UserName = const_cast<LPWSTR>(L"FamilyVault");

        if (!CredWriteW(&cred, 0)) {
            DWORD error = GetLastError();
            spdlog::error("SecureStorage::store failed for key '{}': error {}", key, error);
            return false;
        }

        spdlog::debug("SecureStorage::store success for key '{}'", key);
        return true;
    }

    std::optional<std::vector<uint8_t>> retrieve(const std::string& key) {
        if (key.empty()) {
            return std::nullopt;
        }

        std::wstring wideKey = toWideString(key);
        std::wstring targetName = L"FamilyVault:" + wideKey;

        PCREDENTIALW pCred = nullptr;
        if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
            DWORD error = GetLastError();
            if (error != ERROR_NOT_FOUND) {
                spdlog::error("SecureStorage::retrieve failed for key '{}': error {}", key, error);
            }
            return std::nullopt;
        }

        std::vector<uint8_t> result(
            pCred->CredentialBlob,
            pCred->CredentialBlob + pCred->CredentialBlobSize
        );

        CredFree(pCred);
        spdlog::debug("SecureStorage::retrieve success for key '{}' ({} bytes)", key, result.size());
        return result;
    }

    bool remove(const std::string& key) {
        if (key.empty()) {
            return true;
        }

        std::wstring wideKey = toWideString(key);
        std::wstring targetName = L"FamilyVault:" + wideKey;

        if (!CredDeleteW(targetName.c_str(), CRED_TYPE_GENERIC, 0)) {
            DWORD error = GetLastError();
            if (error == ERROR_NOT_FOUND) {
                // Не существует — это OK
                return true;
            }
            spdlog::error("SecureStorage::remove failed for key '{}': error {}", key, error);
            return false;
        }

        spdlog::debug("SecureStorage::remove success for key '{}'", key);
        return true;
    }

    bool exists(const std::string& key) {
        if (key.empty()) {
            return false;
        }

        std::wstring wideKey = toWideString(key);
        std::wstring targetName = L"FamilyVault:" + wideKey;

        PCREDENTIALW pCred = nullptr;
        if (!CredReadW(targetName.c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
            return false;
        }

        CredFree(pCred);
        return true;
    }

private:
    std::wstring toWideString(const std::string& str) {
        if (str.empty()) return L"";
        
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (size <= 0) return L"";
        
        std::wstring result(size - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
        return result;
    }
};

#endif // _WIN32

// ═══════════════════════════════════════════════════════════
// Linux Implementation (простая файловая для разработки)
// В продакшене использовать libsecret
// ═══════════════════════════════════════════════════════════

#if defined(__linux__) && !defined(__ANDROID__)

#include <fstream>
#include <filesystem>
#include <openssl/evp.h>
#include <openssl/rand.h>

class SecureStorage::Impl {
public:
    Impl() {
        // Используем домашнюю директорию
        const char* home = std::getenv("HOME");
        if (home) {
            m_storageDir = std::string(home) + "/.config/familyvault/secure";
        } else {
            m_storageDir = "/tmp/familyvault/secure";
        }
        std::filesystem::create_directories(m_storageDir);
        spdlog::debug("SecureStorage: Linux file-based storage at {}", m_storageDir);
        spdlog::warn("SecureStorage: Using file-based storage (development only!)");
    }

    ~Impl() = default;

    bool store(const std::string& key, const std::vector<uint8_t>& data) {
        if (key.empty()) return false;
        
        std::string path = getPath(key);
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            spdlog::error("SecureStorage::store failed to open file for key '{}'", key);
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        spdlog::debug("SecureStorage::store success for key '{}'", key);
        return true;
    }

    std::optional<std::vector<uint8_t>> retrieve(const std::string& key) {
        if (key.empty()) return std::nullopt;
        
        std::string path = getPath(key);
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            return std::nullopt;
        }
        
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> result(size);
        file.read(reinterpret_cast<char*>(result.data()), size);
        
        spdlog::debug("SecureStorage::retrieve success for key '{}' ({} bytes)", key, result.size());
        return result;
    }

    bool remove(const std::string& key) {
        if (key.empty()) return true;
        
        std::string path = getPath(key);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return true;
    }

    bool exists(const std::string& key) {
        if (key.empty()) return false;
        return std::filesystem::exists(getPath(key));
    }

private:
    std::string m_storageDir;

    std::string getPath(const std::string& key) {
        // Заменяем точки на подчёркивания для безопасного имени файла
        std::string safeKey = key;
        for (char& c : safeKey) {
            if (c == '.' || c == '/' || c == '\\') c = '_';
        }
        return m_storageDir + "/" + safeKey + ".dat";
    }
};

#endif // __linux__

// ═══════════════════════════════════════════════════════════
// Android File-based Storage
// ═══════════════════════════════════════════════════════════
// 
// Используем файловое хранилище в приватной директории приложения.
// Данные шифруются AES-256-GCM с ключом, привязанным к устройству.
// Путь: /data/data/com.familyvault.familyvault/files/.secure/

#ifdef __ANDROID__

#include <unistd.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

class SecureStorage::Impl {
public:
    Impl() {
        // Получаем путь к приватной директории
        // На Android это обычно /data/data/<package>/files/
        m_basePath = getAndroidFilesDir();
        m_secureDir = m_basePath + "/.secure";
        
        // Создаём директорию если не существует
        mkdir(m_secureDir.c_str(), 0700);
        
        // Генерируем или загружаем device key для шифрования
        initDeviceKey();
        
        spdlog::debug("SecureStorage: Android file storage at {}", m_secureDir);
    }

    ~Impl() = default;

    bool store(const std::string& key, const std::vector<uint8_t>& data) {
        try {
            std::string path = getKeyPath(key);
            
            // Шифруем данные
            auto encrypted = encryptData(data);
            if (encrypted.empty()) {
                spdlog::error("SecureStorage::store: Encryption failed for key '{}'", key);
                return false;
            }
            
            // Записываем в файл
            std::ofstream file(path, std::ios::binary);
            if (!file) {
                spdlog::error("SecureStorage::store: Cannot open file for key '{}'", key);
                return false;
            }
            
            file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
            file.close();
            
            // Устанавливаем права доступа
            chmod(path.c_str(), 0600);
            
            spdlog::debug("SecureStorage::store: Stored key '{}' ({} bytes)", key, data.size());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("SecureStorage::store failed: {}", e.what());
            return false;
        }
    }

    std::optional<std::vector<uint8_t>> retrieve(const std::string& key) {
        try {
            std::string path = getKeyPath(key);
            
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) {
                spdlog::debug("SecureStorage::retrieve: Key '{}' not found", key);
                return std::nullopt;
            }
            
            auto size = file.tellg();
            file.seekg(0, std::ios::beg);
            
            std::vector<uint8_t> encrypted(size);
            file.read(reinterpret_cast<char*>(encrypted.data()), size);
            file.close();
            
            // Расшифровываем данные
            auto decrypted = decryptData(encrypted);
            if (!decrypted) {
                spdlog::error("SecureStorage::retrieve: Decryption failed for key '{}'", key);
                return std::nullopt;
            }
            
            spdlog::debug("SecureStorage::retrieve: Retrieved key '{}' ({} bytes)", key, decrypted->size());
            return decrypted;
        } catch (const std::exception& e) {
            spdlog::error("SecureStorage::retrieve failed: {}", e.what());
            return std::nullopt;
        }
    }

    bool remove(const std::string& key) {
        std::string path = getKeyPath(key);
        if (std::remove(path.c_str()) == 0) {
            spdlog::debug("SecureStorage::remove: Removed key '{}'", key);
            return true;
        }
        return false;
    }

    bool exists(const std::string& key) {
        std::string path = getKeyPath(key);
        struct stat st;
        return stat(path.c_str(), &st) == 0;
    }

private:
    std::string m_basePath;
    std::string m_secureDir;
    std::vector<uint8_t> m_deviceKey;
    
    std::string getAndroidFilesDir() {
        // Пробуем стандартные пути Android
        const char* paths[] = {
            "/data/data/com.familyvault.familyvault/files",
            "/data/user/0/com.familyvault.familyvault/files",
            "."  // fallback
        };
        
        for (const char* path : paths) {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
                return path;
            }
        }
        
        // Если ничего не найдено, создаём в текущей директории
        mkdir("./familyvault_data", 0700);
        return "./familyvault_data";
    }
    
    std::string getKeyPath(const std::string& key) {
        // Используем SHA256 от ключа как имя файла
        std::string safeKey;
        for (char c : key) {
            if (isalnum(c) || c == '_') {
                safeKey += c;
            } else {
                safeKey += '_';
            }
        }
        return m_secureDir + "/" + safeKey + ".enc";
    }
    
    void initDeviceKey() {
        std::string keyPath = m_secureDir + "/.device_key";
        
        // Пробуем загрузить существующий ключ
        std::ifstream keyFile(keyPath, std::ios::binary);
        if (keyFile) {
            m_deviceKey.resize(32);
            keyFile.read(reinterpret_cast<char*>(m_deviceKey.data()), 32);
            if (keyFile.gcount() == 32) {
                return;
            }
        }
        
        // Генерируем новый ключ
        m_deviceKey.resize(32);
        RAND_bytes(m_deviceKey.data(), 32);
        
        // Сохраняем ключ
        std::ofstream outFile(keyPath, std::ios::binary);
        outFile.write(reinterpret_cast<const char*>(m_deviceKey.data()), 32);
        chmod(keyPath.c_str(), 0600);
    }
    
    std::vector<uint8_t> encryptData(const std::vector<uint8_t>& data) {
        // AES-256-GCM
        std::vector<uint8_t> iv(12);
        RAND_bytes(iv.data(), 12);
        
        std::vector<uint8_t> result;
        result.reserve(12 + data.size() + 16);  // IV + ciphertext + tag
        result.insert(result.end(), iv.begin(), iv.end());
        
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, m_deviceKey.data(), iv.data());
        
        std::vector<uint8_t> ciphertext(data.size() + 16);
        int len;
        EVP_EncryptUpdate(ctx, ciphertext.data(), &len, data.data(), data.size());
        int ciphertext_len = len;
        
        EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
        ciphertext_len += len;
        
        std::vector<uint8_t> tag(16);
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data());
        EVP_CIPHER_CTX_free(ctx);
        
        result.insert(result.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);
        result.insert(result.end(), tag.begin(), tag.end());
        
        return result;
    }
    
    std::optional<std::vector<uint8_t>> decryptData(const std::vector<uint8_t>& encrypted) {
        if (encrypted.size() < 12 + 16) {  // IV + tag minimum
            return std::nullopt;
        }
        
        std::vector<uint8_t> iv(encrypted.begin(), encrypted.begin() + 12);
        std::vector<uint8_t> tag(encrypted.end() - 16, encrypted.end());
        std::vector<uint8_t> ciphertext(encrypted.begin() + 12, encrypted.end() - 16);
        
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, m_deviceKey.data(), iv.data());
        
        std::vector<uint8_t> plaintext(ciphertext.size());
        int len;
        EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size());
        int plaintext_len = len;
        
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data());
        
        int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
        EVP_CIPHER_CTX_free(ctx);
        
        if (ret <= 0) {
            return std::nullopt;  // Authentication failed
        }
        
        plaintext_len += len;
        plaintext.resize(plaintext_len);
        return plaintext;
    }
};

#endif // __ANDROID__

// ═══════════════════════════════════════════════════════════
// SecureStorage Public Interface
// ═══════════════════════════════════════════════════════════

SecureStorage::SecureStorage() : m_impl(std::make_unique<Impl>()) {
}

SecureStorage::~SecureStorage() = default;

bool SecureStorage::store(const std::string& key, const std::vector<uint8_t>& data) {
    return m_impl->store(key, data);
}

std::optional<std::vector<uint8_t>> SecureStorage::retrieve(const std::string& key) {
    return m_impl->retrieve(key);
}

bool SecureStorage::remove(const std::string& key) {
    return m_impl->remove(key);
}

bool SecureStorage::exists(const std::string& key) {
    return m_impl->exists(key);
}

bool SecureStorage::storeString(const std::string& key, const std::string& value) {
    std::vector<uint8_t> data(value.begin(), value.end());
    return store(key, data);
}

std::optional<std::string> SecureStorage::retrieveString(const std::string& key) {
    auto data = retrieve(key);
    if (!data) return std::nullopt;
    return std::string(data->begin(), data->end());
}

} // namespace FamilyVault

