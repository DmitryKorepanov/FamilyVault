#pragma once

#include <cstdint>
#include <string>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Типы контента
// ═══════════════════════════════════════════════════════════

enum class ContentType : int32_t {
    Unknown = 0,
    Image = 1,
    Video = 2,
    Audio = 3,
    Document = 4,
    Archive = 5,
    Other = 99
};

const char* contentTypeToString(ContentType type);
ContentType contentTypeFromString(const std::string& str);
ContentType contentTypeFromMime(const std::string& mimeType);

// ═══════════════════════════════════════════════════════════
// Видимость файлов (Private = только локально, Family = синхронизируется)
// ═══════════════════════════════════════════════════════════

enum class Visibility : int32_t {
    Private = 0,    // Только на этом устройстве
    Family = 1      // Виден семье, синхронизируется через P2P
};

const char* visibilityToString(Visibility v);
Visibility visibilityFromString(const std::string& str);

// ═══════════════════════════════════════════════════════════
// Источник тегов
// ═══════════════════════════════════════════════════════════

enum class TagSource : int32_t {
    User = 0,   // Добавлен пользователем
    Auto = 1,   // Автоматический (по типу/дате/размеру)
    AI = 2      // От AI (в будущем)
};

const char* tagSourceToString(TagSource source);
TagSource tagSourceFromString(const std::string& str);

// ═══════════════════════════════════════════════════════════
// Состояние P2P соединения
// ═══════════════════════════════════════════════════════════

enum class ConnectionState : int32_t {
    Disconnected = 0,
    Connecting = 1,
    Authenticating = 2,
    Connected = 3,
    AuthFailed = 4,
    Error = 99
};

// ═══════════════════════════════════════════════════════════
// Типы устройств
// ═══════════════════════════════════════════════════════════

enum class DeviceType : int32_t {
    Desktop = 0,
    Mobile = 1,
    Tablet = 2
};

// ═══════════════════════════════════════════════════════════
// Порядок сортировки
// ═══════════════════════════════════════════════════════════

enum class SortBy : int32_t {
    Relevance = 0,
    Name = 1,
    Date = 2,
    Size = 3
};

} // namespace FamilyVault

