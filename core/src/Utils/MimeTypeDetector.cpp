#include "familyvault/MimeTypeDetector.h"
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <cctype>
#include <array>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Таблица расширений -> MIME
// ═══════════════════════════════════════════════════════════

static const std::unordered_map<std::string, std::string> EXTENSION_TO_MIME = {
    // Images
    {"jpg", "image/jpeg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"gif", "image/gif"},
    {"webp", "image/webp"},
    {"bmp", "image/bmp"},
    {"svg", "image/svg+xml"},
    {"ico", "image/x-icon"},
    {"tiff", "image/tiff"},
    {"tif", "image/tiff"},
    {"heic", "image/heic"},
    {"heif", "image/heif"},
    {"raw", "image/raw"},
    {"cr2", "image/x-canon-cr2"},
    {"nef", "image/x-nikon-nef"},

    // Video
    {"mp4", "video/mp4"},
    {"avi", "video/x-msvideo"},
    {"mkv", "video/x-matroska"},
    {"mov", "video/quicktime"},
    {"wmv", "video/x-ms-wmv"},
    {"flv", "video/x-flv"},
    {"webm", "video/webm"},
    {"m4v", "video/x-m4v"},
    {"3gp", "video/3gpp"},

    // Audio
    {"mp3", "audio/mpeg"},
    {"wav", "audio/wav"},
    {"ogg", "audio/ogg"},
    {"flac", "audio/flac"},
    {"aac", "audio/aac"},
    {"m4a", "audio/mp4"},
    {"wma", "audio/x-ms-wma"},
    {"opus", "audio/opus"},

    // Documents
    {"pdf", "application/pdf"},
    {"doc", "application/msword"},
    {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"xls", "application/vnd.ms-excel"},
    {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"ppt", "application/vnd.ms-powerpoint"},
    {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {"odt", "application/vnd.oasis.opendocument.text"},
    {"ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {"odp", "application/vnd.oasis.opendocument.presentation"},
    {"txt", "text/plain"},
    {"rtf", "application/rtf"},
    {"csv", "text/csv"},
    {"md", "text/markdown"},
    {"html", "text/html"},
    {"htm", "text/html"},
    {"xml", "application/xml"},
    {"json", "application/json"},
    {"yaml", "application/x-yaml"},
    {"yml", "application/x-yaml"},

    // Archives
    {"zip", "application/zip"},
    {"rar", "application/vnd.rar"},
    {"7z", "application/x-7z-compressed"},
    {"tar", "application/x-tar"},
    {"gz", "application/gzip"},
    {"bz2", "application/x-bzip2"},
    {"xz", "application/x-xz"},

    // Code
    {"js", "application/javascript"},
    {"ts", "application/typescript"},
    {"py", "text/x-python"},
    {"cpp", "text/x-c++src"},
    {"c", "text/x-csrc"},
    {"h", "text/x-chdr"},
    {"hpp", "text/x-c++hdr"},
    {"java", "text/x-java-source"},
    {"cs", "text/x-csharp"},
    {"go", "text/x-go"},
    {"rs", "text/x-rust"},
    {"rb", "text/x-ruby"},
    {"php", "text/x-php"},
    {"swift", "text/x-swift"},
    {"kt", "text/x-kotlin"},
    {"dart", "application/dart"},

    // Other
    {"exe", "application/x-msdownload"},
    {"dll", "application/x-msdownload"},
    {"so", "application/x-sharedlib"},
    {"apk", "application/vnd.android.package-archive"},
    {"dmg", "application/x-apple-diskimage"},
    {"iso", "application/x-iso9660-image"},
};

// ═══════════════════════════════════════════════════════════
// Magic bytes для определения типа
// ═══════════════════════════════════════════════════════════

struct MagicSignature {
    std::array<uint8_t, 16> bytes;
    size_t length;
    size_t offset;
    const char* mimeType;
};

static const std::array<MagicSignature, 15> MAGIC_SIGNATURES = {{
    // JPEG
    {{0xFF, 0xD8, 0xFF}, 3, 0, "image/jpeg"},
    // PNG
    {{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A}, 8, 0, "image/png"},
    // GIF
    {{0x47, 0x49, 0x46, 0x38}, 4, 0, "image/gif"},
    // WebP (RIFF....WEBP)
    {{0x52, 0x49, 0x46, 0x46}, 4, 0, "image/webp"}, // Проверяем только RIFF
    // PDF
    {{0x25, 0x50, 0x44, 0x46}, 4, 0, "application/pdf"},
    // ZIP (также DOCX, XLSX, etc.)
    {{0x50, 0x4B, 0x03, 0x04}, 4, 0, "application/zip"},
    // RAR
    {{0x52, 0x61, 0x72, 0x21, 0x1A, 0x07}, 6, 0, "application/vnd.rar"},
    // 7z
    {{0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}, 6, 0, "application/x-7z-compressed"},
    // MP3 (ID3)
    {{0x49, 0x44, 0x33}, 3, 0, "audio/mpeg"},
    // MP3 (frame sync)
    {{0xFF, 0xFB}, 2, 0, "audio/mpeg"},
    // MP4/M4A
    {{0x66, 0x74, 0x79, 0x70}, 4, 4, "video/mp4"}, // "ftyp" at offset 4
    // AVI
    {{0x52, 0x49, 0x46, 0x46}, 4, 0, "video/x-msvideo"}, // RIFF
    // BMP
    {{0x42, 0x4D}, 2, 0, "image/bmp"},
    // GZIP
    {{0x1F, 0x8B}, 2, 0, "application/gzip"},
    // EXE/DLL
    {{0x4D, 0x5A}, 2, 0, "application/x-msdownload"},
}};

// ═══════════════════════════════════════════════════════════
// Реализация
// ═══════════════════════════════════════════════════════════

std::string MimeTypeDetector::extractExtension(const std::string& filename) {
    auto dotPos = filename.rfind('.');
    if (dotPos == std::string::npos || dotPos == filename.length() - 1) {
        return "";
    }
    std::string ext = filename.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

std::string MimeTypeDetector::detectByExtension(const std::string& extension) {
    std::string ext = extension;
    // Убираем точку если есть
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto it = EXTENSION_TO_MIME.find(ext);
    if (it != EXTENSION_TO_MIME.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

std::string MimeTypeDetector::detectByContent(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return "application/octet-stream";
    }

    // Читаем первые 32 байта
    std::array<uint8_t, 32> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    auto bytesRead = file.gcount();

    if (bytesRead < 2) {
        return "application/octet-stream";
    }

    // Проверяем сигнатуры
    for (const auto& sig : MAGIC_SIGNATURES) {
        if (sig.offset + sig.length > static_cast<size_t>(bytesRead)) {
            continue;
        }

        bool matches = true;
        for (size_t i = 0; i < sig.length; ++i) {
            if (header[sig.offset + i] != sig.bytes[i]) {
                matches = false;
                break;
            }
        }

        if (matches) {
            // Специальная проверка для WebP (RIFF....WEBP)
            if (std::string(sig.mimeType) == "image/webp") {
                if (bytesRead >= 12 &&
                    header[8] == 'W' && header[9] == 'E' &&
                    header[10] == 'B' && header[11] == 'P') {
                    return "image/webp";
                }
                // Это может быть AVI, продолжаем поиск
                continue;
            }
            return sig.mimeType;
        }
    }

    return "application/octet-stream";
}

std::string MimeTypeDetector::detect(const std::string& filePath, const std::string& extension) {
    // Сначала пробуем по расширению
    std::string mime = detectByExtension(extension);

    // Если не определилось — по содержимому
    if (mime == "application/octet-stream") {
        mime = detectByContent(filePath);
    }

    return mime;
}

ContentType MimeTypeDetector::mimeToContentType(const std::string& mimeType) {
    return contentTypeFromMime(mimeType);
}

} // namespace FamilyVault

