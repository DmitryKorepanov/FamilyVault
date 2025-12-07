#include "familyvault/Types.h"
#include <algorithm>
#include <cctype>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// ContentType
// ═══════════════════════════════════════════════════════════

const char* contentTypeToString(ContentType type) {
    switch (type) {
        case ContentType::Unknown:  return "unknown";
        case ContentType::Image:    return "image";
        case ContentType::Video:    return "video";
        case ContentType::Audio:    return "audio";
        case ContentType::Document: return "document";
        case ContentType::Archive:  return "archive";
        case ContentType::Other:    return "other";
        default:                    return "unknown";
    }
}

ContentType contentTypeFromString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "image")    return ContentType::Image;
    if (lower == "video")    return ContentType::Video;
    if (lower == "audio")    return ContentType::Audio;
    if (lower == "document") return ContentType::Document;
    if (lower == "archive")  return ContentType::Archive;
    if (lower == "other")    return ContentType::Other;
    return ContentType::Unknown;
}

ContentType contentTypeFromMime(const std::string& mimeType) {
    if (mimeType.empty()) return ContentType::Unknown;

    // Извлекаем категорию (часть до /)
    auto slashPos = mimeType.find('/');
    std::string category = (slashPos != std::string::npos)
                               ? mimeType.substr(0, slashPos)
                               : mimeType;

    std::transform(category.begin(), category.end(), category.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (category == "image") return ContentType::Image;
    if (category == "video") return ContentType::Video;
    if (category == "audio") return ContentType::Audio;

    // Документы
    if (category == "text") return ContentType::Document;
    if (mimeType.find("pdf") != std::string::npos) return ContentType::Document;
    if (mimeType.find("document") != std::string::npos) return ContentType::Document;
    if (mimeType.find("spreadsheet") != std::string::npos) return ContentType::Document;
    if (mimeType.find("presentation") != std::string::npos) return ContentType::Document;
    if (mimeType.find("msword") != std::string::npos) return ContentType::Document;

    // Архивы
    if (mimeType.find("zip") != std::string::npos) return ContentType::Archive;
    if (mimeType.find("rar") != std::string::npos) return ContentType::Archive;
    if (mimeType.find("7z") != std::string::npos) return ContentType::Archive;
    if (mimeType.find("tar") != std::string::npos) return ContentType::Archive;
    if (mimeType.find("gzip") != std::string::npos) return ContentType::Archive;

    return ContentType::Other;
}

// ═══════════════════════════════════════════════════════════
// Visibility
// ═══════════════════════════════════════════════════════════

const char* visibilityToString(Visibility v) {
    switch (v) {
        case Visibility::Private: return "private";
        case Visibility::Family:  return "family";
        default:                  return "family";
    }
}

Visibility visibilityFromString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "private") return Visibility::Private;
    return Visibility::Family;
}

// ═══════════════════════════════════════════════════════════
// TagSource
// ═══════════════════════════════════════════════════════════

const char* tagSourceToString(TagSource source) {
    switch (source) {
        case TagSource::User: return "user";
        case TagSource::Auto: return "auto";
        case TagSource::AI:   return "ai";
        default:              return "user";
    }
}

TagSource tagSourceFromString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "auto") return TagSource::Auto;
    if (lower == "ai")   return TagSource::AI;
    return TagSource::User;
}

} // namespace FamilyVault

