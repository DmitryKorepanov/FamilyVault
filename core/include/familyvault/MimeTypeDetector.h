#pragma once

#include "Types.h"
#include <string>

namespace FamilyVault {

/// Определение MIME типа файла по расширению и magic bytes
class MimeTypeDetector {
public:
    /// Определение по расширению (быстрый путь)
    static std::string detectByExtension(const std::string& extension);

    /// Определение по содержимому файла (magic bytes)
    static std::string detectByContent(const std::string& filePath);

    /// Комбинированное определение (сначала по расширению, затем по содержимому)
    static std::string detect(const std::string& filePath, const std::string& extension);

    /// Конвертация MIME -> ContentType
    static ContentType mimeToContentType(const std::string& mimeType);

    /// Получить расширение из имени файла
    static std::string extractExtension(const std::string& filename);
};

} // namespace FamilyVault

