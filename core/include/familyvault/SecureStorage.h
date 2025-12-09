// SecureStorage.h — Безопасное хранение секретов
// Использует платформенные механизмы: Windows Credential Manager, Android Keystore
// ЕДИНСТВЕННЫЙ ИСТОЧНИК ИСТИНЫ для SecureStorage API

#pragma once

#include "export.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <memory>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// SecureStorage — хранение секретов в платформенном хранилище
// ═══════════════════════════════════════════════════════════

/// Безопасное хранилище для секретов (family_secret, device_id, tokens)
/// 
/// Платформенные реализации:
/// - Windows: Credential Manager (DPAPI под капотом)
/// - Android: EncryptedSharedPreferences / Keystore
/// - Linux: libsecret (GNOME Keyring / KWallet)
/// - macOS/iOS: Keychain (будущее)
class FV_API SecureStorage {
public:
    SecureStorage();
    ~SecureStorage();

    // Запрет копирования
    SecureStorage(const SecureStorage&) = delete;
    SecureStorage& operator=(const SecureStorage&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Основной интерфейс — бинарные данные
    // ═══════════════════════════════════════════════════════════

    /// Сохранить бинарные данные
    /// @param key Уникальный ключ (например: "family_secret", "device_id")
    /// @param data Данные для сохранения
    /// @return true если успешно
    bool store(const std::string& key, const std::vector<uint8_t>& data);

    /// Получить бинарные данные
    /// @param key Ключ
    /// @return Данные или nullopt если не найдено
    std::optional<std::vector<uint8_t>> retrieve(const std::string& key);

    /// Удалить данные по ключу
    /// @param key Ключ
    /// @return true если успешно удалено (или не существовало)
    bool remove(const std::string& key);

    /// Проверить существование ключа
    bool exists(const std::string& key);

    // ═══════════════════════════════════════════════════════════
    // Convenience методы для строк
    // ═══════════════════════════════════════════════════════════

    /// Сохранить строку
    bool storeString(const std::string& key, const std::string& value);

    /// Получить строку
    std::optional<std::string> retrieveString(const std::string& key);

    // ═══════════════════════════════════════════════════════════
    // Предопределённые ключи
    // ═══════════════════════════════════════════════════════════

    static constexpr const char* KEY_FAMILY_SECRET = "familyvault.family_secret";
    static constexpr const char* KEY_DEVICE_ID = "familyvault.device_id";
    static constexpr const char* KEY_DEVICE_NAME = "familyvault.device_name";

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace FamilyVault


