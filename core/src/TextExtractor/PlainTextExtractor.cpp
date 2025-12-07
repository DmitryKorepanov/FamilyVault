#include "familyvault/TextExtractor.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <algorithm>
#include <regex>
#include <cstring>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// PlainTextExtractor
// ═══════════════════════════════════════════════════════════

PlainTextExtractor::PlainTextExtractor() = default;
PlainTextExtractor::~PlainTextExtractor() = default;

bool PlainTextExtractor::canHandle(const std::string& mimeType) const {
    // Текстовые типы
    if (mimeType.starts_with("text/")) {
        return true;
    }
    
    // Дополнительные текстовые форматы
    static const std::vector<std::string> supported = {
        "application/json",
        "application/xml",
        "application/javascript",
        "application/x-javascript",
        "application/typescript",
        "application/x-yaml",
        "application/x-sh",
        "application/x-python",
        "application/x-perl",
        "application/x-ruby",
    };
    
    return std::find(supported.begin(), supported.end(), mimeType) != supported.end();
}

std::optional<ExtractionResult> PlainTextExtractor::extract(const std::string& filePath) {
    // Читаем файл
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) {
        spdlog::warn("PlainTextExtractor: cannot open file '{}'", filePath);
        return std::nullopt;
    }
    
    auto fileSize = file.tellg();
    if (fileSize < 0) {
        return std::nullopt;
    }
    
    // Проверяем размер
    size_t size = static_cast<size_t>(fileSize);
    if (size > m_maxFileSize) {
        spdlog::debug("PlainTextExtractor: file too large ({} bytes), truncating", size);
        size = m_maxFileSize;
    }
    
    // Читаем содержимое
    file.seekg(0, std::ios::beg);
    std::vector<char> data(size);
    if (!file.read(data.data(), size)) {
        spdlog::warn("PlainTextExtractor: failed to read file '{}'", filePath);
        return std::nullopt;
    }
    
    // Определяем кодировку
    std::string encoding = detectEncoding(data);
    
    // Конвертируем в UTF-8
    std::string text = convertToUtf8(data, encoding);
    
    if (text.empty()) {
        return std::nullopt;
    }
    
    // Определяем MIME тип для решения о strip тегов
    // Ищем расширение файла
    std::string ext;
    auto dotPos = filePath.rfind('.');
    if (dotPos != std::string::npos) {
        ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }
    
    // Удаляем теги если нужно
    if (ext == "html" || ext == "htm" || ext == "xhtml") {
        text = stripHtml(text);
    } else if (ext == "xml" || ext == "svg") {
        text = stripXml(text);
    }
    
    // Нормализуем пробелы
    // Заменяем множественные пробелы/переносы на один пробел
    static std::regex multiSpace(R"(\s+)");
    text = std::regex_replace(text, multiSpace, " ");
    
    // Trim
    auto start = text.find_first_not_of(" \t\n\r");
    auto end = text.find_last_not_of(" \t\n\r");
    if (start != std::string::npos && end != std::string::npos) {
        text = text.substr(start, end - start + 1);
    }
    
    if (text.empty()) {
        return std::nullopt;
    }
    
    return ExtractionResult{
        .text = std::move(text),
        .method = "plain_text",
        .language = "",  // TODO: детекция языка
        .confidence = 1.0
    };
}

std::string PlainTextExtractor::detectEncoding(const std::vector<char>& data) const {
    if (data.size() < 2) {
        return "utf-8";
    }
    
    // Проверяем BOM
    unsigned char b0 = static_cast<unsigned char>(data[0]);
    unsigned char b1 = static_cast<unsigned char>(data[1]);
    
    // UTF-16 LE BOM: FF FE
    if (b0 == 0xFF && b1 == 0xFE) {
        return "utf-16le";
    }
    
    // UTF-16 BE BOM: FE FF
    if (b0 == 0xFE && b1 == 0xFF) {
        return "utf-16be";
    }
    
    // UTF-8 BOM: EF BB BF
    if (data.size() >= 3) {
        unsigned char b2 = static_cast<unsigned char>(data[2]);
        if (b0 == 0xEF && b1 == 0xBB && b2 == 0xBF) {
            return "utf-8-bom";
        }
    }
    
    // Эвристика: проверяем валидность UTF-8
    bool validUtf8 = true;
    size_t i = 0;
    while (i < data.size() && i < 1000) { // Проверяем первые 1000 байт
        unsigned char c = static_cast<unsigned char>(data[i]);
        
        if (c < 0x80) {
            // ASCII
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 >= data.size() || (static_cast<unsigned char>(data[i + 1]) & 0xC0) != 0x80) {
                validUtf8 = false;
                break;
            }
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 >= data.size() ||
                (static_cast<unsigned char>(data[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(data[i + 2]) & 0xC0) != 0x80) {
                validUtf8 = false;
                break;
            }
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 >= data.size() ||
                (static_cast<unsigned char>(data[i + 1]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(data[i + 2]) & 0xC0) != 0x80 ||
                (static_cast<unsigned char>(data[i + 3]) & 0xC0) != 0x80) {
                validUtf8 = false;
                break;
            }
            i += 4;
        } else {
            validUtf8 = false;
            break;
        }
    }
    
    if (validUtf8) {
        return "utf-8";
    }
    
    // Эвристика для CP1251 (Windows-1251)
    // Если много байтов в диапазоне 0xC0-0xFF (кириллица в CP1251)
    int cyrillicCount = 0;
    for (size_t j = 0; j < data.size() && j < 1000; ++j) {
        unsigned char c = static_cast<unsigned char>(data[j]);
        if (c >= 0xC0 && c <= 0xFF) {
            cyrillicCount++;
        }
    }
    
    if (cyrillicCount > 50) {
        return "cp1251";
    }
    
    // По умолчанию считаем UTF-8 (или Latin-1)
    return "utf-8";
}

std::string PlainTextExtractor::convertToUtf8(const std::vector<char>& data,
                                               const std::string& encoding) const {
    if (data.empty()) {
        return "";
    }
    
    if (encoding == "utf-8" || encoding == "utf-8-bom") {
        size_t start = (encoding == "utf-8-bom") ? 3 : 0;
        return std::string(data.begin() + start, data.end());
    }
    
    if (encoding == "utf-16le" || encoding == "utf-16be") {
        // Простая конвертация UTF-16 -> UTF-8
        // Пропускаем BOM (2 байта)
        std::string result;
        result.reserve(data.size());
        
        bool isBE = (encoding == "utf-16be");
        
        for (size_t i = 2; i + 1 < data.size(); i += 2) {
            uint16_t codeUnit;
            if (isBE) {
                codeUnit = (static_cast<unsigned char>(data[i]) << 8) | 
                           static_cast<unsigned char>(data[i + 1]);
            } else {
                codeUnit = static_cast<unsigned char>(data[i]) | 
                           (static_cast<unsigned char>(data[i + 1]) << 8);
            }
            
            // Конвертируем codeUnit в UTF-8
            if (codeUnit < 0x80) {
                result += static_cast<char>(codeUnit);
            } else if (codeUnit < 0x800) {
                result += static_cast<char>(0xC0 | (codeUnit >> 6));
                result += static_cast<char>(0x80 | (codeUnit & 0x3F));
            } else {
                // BMP characters (не обрабатываем суррогатные пары для простоты)
                result += static_cast<char>(0xE0 | (codeUnit >> 12));
                result += static_cast<char>(0x80 | ((codeUnit >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (codeUnit & 0x3F));
            }
        }
        
        return result;
    }
    
    if (encoding == "cp1251") {
        // Конвертация CP1251 -> UTF-8
        // Таблица перекодировки для символов 0x80-0xFF
        static const uint16_t cp1251ToUnicode[128] = {
            0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
            0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
            0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
            0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
            0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
            0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
            0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
            0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
            0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
            0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
            0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
            0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
            0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
            0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
            0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
            0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
        };
        
        std::string result;
        result.reserve(data.size() * 2); // На случай многобайтных символов
        
        for (char c : data) {
            unsigned char uc = static_cast<unsigned char>(c);
            
            if (uc < 0x80) {
                result += c;
            } else {
                uint16_t unicode = cp1251ToUnicode[uc - 0x80];
                
                if (unicode < 0x800) {
                    result += static_cast<char>(0xC0 | (unicode >> 6));
                    result += static_cast<char>(0x80 | (unicode & 0x3F));
                } else {
                    result += static_cast<char>(0xE0 | (unicode >> 12));
                    result += static_cast<char>(0x80 | ((unicode >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (unicode & 0x3F));
                }
            }
        }
        
        return result;
    }
    
    // Fallback: просто копируем как есть (для ASCII/Latin-1)
    return std::string(data.begin(), data.end());
}

std::string PlainTextExtractor::stripHtml(const std::string& html) const {
    std::string result;
    result.reserve(html.size());
    
    bool inTag = false;
    bool inScript = false;
    bool inStyle = false;
    
    size_t i = 0;
    while (i < html.size()) {
        char c = html[i];
        
        if (c == '<') {
            // Проверяем на script/style
            std::string tagName;
            size_t j = i + 1;
            while (j < html.size() && j < i + 10 && html[j] != '>' && html[j] != ' ') {
                tagName += static_cast<char>(std::tolower(html[j]));
                j++;
            }
            
            if (tagName == "script") {
                inScript = true;
            } else if (tagName == "/script") {
                inScript = false;
            } else if (tagName == "style") {
                inStyle = true;
            } else if (tagName == "/style") {
                inStyle = false;
            }
            
            inTag = true;
        } else if (c == '>') {
            inTag = false;
            // Добавляем пробел после закрывающего тега
            if (!result.empty() && result.back() != ' ') {
                result += ' ';
            }
        } else if (!inTag && !inScript && !inStyle) {
            // Декодируем HTML entities
            if (c == '&') {
                std::string entity;
                size_t j = i + 1;
                while (j < html.size() && j < i + 10 && html[j] != ';' && html[j] != ' ') {
                    entity += html[j];
                    j++;
                }
                if (j < html.size() && html[j] == ';') {
                    if (entity == "nbsp" || entity == "#160") {
                        result += ' ';
                    } else if (entity == "lt") {
                        result += '<';
                    } else if (entity == "gt") {
                        result += '>';
                    } else if (entity == "amp") {
                        result += '&';
                    } else if (entity == "quot") {
                        result += '"';
                    } else if (entity == "apos") {
                        result += '\'';
                    } else {
                        // Неизвестная entity - пропускаем или оставляем как есть
                        result += ' ';
                    }
                    i = j;
                } else {
                    result += c;
                }
            } else {
                result += c;
            }
        }
        
        i++;
    }
    
    return result;
}

std::string PlainTextExtractor::stripXml(const std::string& xml) const {
    // Для XML используем более простой подход - просто удаляем теги
    std::string result;
    result.reserve(xml.size());
    
    bool inTag = false;
    bool inCDATA = false;
    
    size_t i = 0;
    while (i < xml.size()) {
        // Проверяем CDATA
        if (i + 8 < xml.size() && xml.substr(i, 9) == "<![CDATA[") {
            inCDATA = true;
            i += 9;
            continue;
        }
        if (inCDATA && i + 2 < xml.size() && xml.substr(i, 3) == "]]>") {
            inCDATA = false;
            i += 3;
            continue;
        }
        
        char c = xml[i];
        
        if (inCDATA) {
            result += c;
        } else if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
            if (!result.empty() && result.back() != ' ') {
                result += ' ';
            }
        } else if (!inTag) {
            result += c;
        }
        
        i++;
    }
    
    return result;
}

bool PlainTextExtractor::shouldStripTags(const std::string& mimeType) const {
    return mimeType == "text/html" || 
           mimeType == "text/xml" || 
           mimeType == "application/xml" ||
           mimeType == "application/xhtml+xml";
}

} // namespace FamilyVault

