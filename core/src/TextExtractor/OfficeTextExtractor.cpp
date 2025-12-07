#include "familyvault/TextExtractor.h"
#include <spdlog/spdlog.h>

#include <zip.h>
#include <pugixml.hpp>

#include <algorithm>
#include <sstream>
#include <vector>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// OfficeTextExtractor
// ═══════════════════════════════════════════════════════════

OfficeTextExtractor::OfficeTextExtractor() = default;
OfficeTextExtractor::~OfficeTextExtractor() = default;

bool OfficeTextExtractor::canHandle(const std::string& mimeType) const {
    static const std::vector<std::string> supported = {
        // Microsoft Office OpenXML
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",     // docx
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",           // xlsx
        "application/vnd.openxmlformats-officedocument.presentationml.presentation",   // pptx
        // OpenDocument Format
        "application/vnd.oasis.opendocument.text",          // odt
        "application/vnd.oasis.opendocument.spreadsheet",   // ods
        "application/vnd.oasis.opendocument.presentation",  // odp
    };
    
    return std::find(supported.begin(), supported.end(), mimeType) != supported.end();
}

std::optional<ExtractionResult> OfficeTextExtractor::extract(const std::string& filePath) {
    std::string ext = getExtension(filePath);
    
    if (ext == "docx") {
        return extractDocx(filePath);
    } else if (ext == "xlsx") {
        return extractXlsx(filePath);
    } else if (ext == "pptx") {
        return extractPptx(filePath);
    } else if (ext == "odt") {
        return extractOdt(filePath);
    } else if (ext == "ods") {
        return extractOds(filePath);
    } else if (ext == "odp") {
        // ODP похож на ODS, используем тот же подход
        return extractOds(filePath);
    }
    
    spdlog::warn("OfficeTextExtractor: unknown extension '{}'", ext);
    return std::nullopt;
}

std::string OfficeTextExtractor::getExtension(const std::string& filePath) const {
    auto dotPos = filePath.rfind('.');
    if (dotPos == std::string::npos) {
        return "";
    }
    std::string ext = filePath.substr(dotPos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string OfficeTextExtractor::readZipEntry(const std::string& archivePath,
                                              const std::string& entryName) {
    int err = 0;
    zip_t* archive = zip_open(archivePath.c_str(), ZIP_RDONLY, &err);
    if (!archive) {
        spdlog::warn("OfficeTextExtractor: failed to open archive '{}', error: {}", 
                    archivePath, err);
        return "";
    }
    
    // RAII wrapper для автоматического закрытия архива
    struct ZipGuard {
        zip_t* archive;
        ~ZipGuard() { if (archive) zip_close(archive); }
    } guard{archive};
    
    // Находим файл в архиве
    zip_int64_t index = zip_name_locate(archive, entryName.c_str(), 0);
    if (index < 0) {
        // Файл не найден - это может быть нормально (например, нет headers)
        return "";
    }
    
    // Получаем информацию о файле
    zip_stat_t stat;
    if (zip_stat_index(archive, index, 0, &stat) != 0) {
        spdlog::warn("OfficeTextExtractor: failed to stat '{}' in '{}'", 
                    entryName, archivePath);
        return "";
    }
    
    // Открываем файл для чтения
    zip_file_t* file = zip_fopen_index(archive, index, 0);
    if (!file) {
        spdlog::warn("OfficeTextExtractor: failed to open '{}' in '{}'", 
                    entryName, archivePath);
        return "";
    }
    
    // RAII для файла
    struct FileGuard {
        zip_file_t* file;
        ~FileGuard() { if (file) zip_fclose(file); }
    } fileGuard{file};
    
    // Читаем содержимое
    std::string content;
    content.resize(stat.size);
    
    zip_int64_t bytesRead = zip_fread(file, content.data(), stat.size);
    if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != stat.size) {
        spdlog::warn("OfficeTextExtractor: failed to read '{}' from '{}'", 
                    entryName, archivePath);
        return "";
    }
    
    return content;
}

std::string OfficeTextExtractor::extractTextFromXml(const std::string& xml,
                                                    const std::vector<std::string>& textTags) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml.c_str());
    
    if (!result) {
        spdlog::warn("OfficeTextExtractor: failed to parse XML: {}", result.description());
        return "";
    }
    
    std::ostringstream text;
    
    // Рекурсивно обходим документ и собираем текст из указанных тегов
    std::function<void(pugi::xml_node)> extractText = [&](pugi::xml_node node) {
        for (pugi::xml_node child : node.children()) {
            std::string nodeName = child.name();
            
            // Убираем namespace prefix
            auto colonPos = nodeName.find(':');
            if (colonPos != std::string::npos) {
                nodeName = nodeName.substr(colonPos + 1);
            }
            
            // Проверяем, является ли это текстовым узлом
            bool isTextNode = false;
            for (const auto& tag : textTags) {
                if (nodeName == tag) {
                    isTextNode = true;
                    break;
                }
            }
            
            if (isTextNode) {
                std::string nodeText = child.text().get();
                if (!nodeText.empty()) {
                    text << nodeText;
                }
            }
            
            // Рекурсивно обрабатываем детей
            extractText(child);
            
            // Добавляем пробел после параграфа
            if (nodeName == "p" || nodeName == "br" || nodeName == "row") {
                text << " ";
            }
        }
    };
    
    extractText(doc);
    return text.str();
}

// ═══════════════════════════════════════════════════════════
// DOCX
// ═══════════════════════════════════════════════════════════

std::optional<ExtractionResult> OfficeTextExtractor::extractDocx(const std::string& filePath) {
    // Основной документ
    std::string docXml = readZipEntry(filePath, "word/document.xml");
    if (docXml.empty()) {
        spdlog::warn("OfficeTextExtractor: no document.xml in DOCX '{}'", filePath);
        return std::nullopt;
    }
    
    // Извлекаем текст из <w:t> элементов (Word text)
    std::string text = extractTextFromXml(docXml, {"t"});
    
    // Опционально: headers и footers
    // word/header1.xml, word/footer1.xml, etc.
    for (int i = 1; i <= 3; ++i) {
        std::string headerXml = readZipEntry(filePath, "word/header" + std::to_string(i) + ".xml");
        if (!headerXml.empty()) {
            text += " " + extractTextFromXml(headerXml, {"t"});
        }
        
        std::string footerXml = readZipEntry(filePath, "word/footer" + std::to_string(i) + ".xml");
        if (!footerXml.empty()) {
            text += " " + extractTextFromXml(footerXml, {"t"});
        }
    }
    
    if (text.empty()) {
        return std::nullopt;
    }
    
    return ExtractionResult{
        .text = std::move(text),
        .method = "docx",
        .language = "",
        .confidence = 1.0
    };
}

// ═══════════════════════════════════════════════════════════
// XLSX
// ═══════════════════════════════════════════════════════════

std::optional<ExtractionResult> OfficeTextExtractor::extractXlsx(const std::string& filePath) {
    // Сначала читаем shared strings (общие строки)
    std::string sharedStringsXml = readZipEntry(filePath, "xl/sharedStrings.xml");
    
    std::vector<std::string> sharedStrings;
    if (!sharedStringsXml.empty()) {
        pugi::xml_document ssDoc;
        if (ssDoc.load_string(sharedStringsXml.c_str())) {
            // Извлекаем все <si><t>...</t></si>
            for (auto si : ssDoc.select_nodes("//si")) {
                std::string str;
                // Собираем все <t> внутри <si>
                for (auto t : si.node().select_nodes(".//t")) {
                    str += t.node().text().get();
                }
                sharedStrings.push_back(str);
            }
        }
    }
    
    std::ostringstream allText;
    
    // Читаем листы (sheet1.xml, sheet2.xml, ...)
    for (int sheetNum = 1; sheetNum <= 50; ++sheetNum) {
        std::string sheetXml = readZipEntry(filePath, 
            "xl/worksheets/sheet" + std::to_string(sheetNum) + ".xml");
        
        if (sheetXml.empty()) {
            break;  // Больше листов нет
        }
        
        pugi::xml_document sheetDoc;
        if (!sheetDoc.load_string(sheetXml.c_str())) {
            continue;
        }
        
        // Проходим по ячейкам
        for (auto cell : sheetDoc.select_nodes("//c")) {
            pugi::xml_node cellNode = cell.node();
            std::string cellType = cellNode.attribute("t").as_string();
            pugi::xml_node valueNode = cellNode.child("v");
            
            if (!valueNode) {
                continue;
            }
            
            std::string value = valueNode.text().get();
            
            if (cellType == "s") {
                // Shared string - индекс в sharedStrings
                try {
                    int idx = std::stoi(value);
                    if (idx >= 0 && idx < static_cast<int>(sharedStrings.size())) {
                        allText << sharedStrings[idx] << " ";
                    }
                } catch (...) {}
            } else if (cellType == "str" || cellType == "inlineStr") {
                // Inline string
                allText << value << " ";
            } else if (cellType.empty() || cellType == "n") {
                // Number or no type - оставляем число как есть
                allText << value << " ";
            }
        }
        
        allText << "\n";
    }
    
    std::string text = allText.str();
    if (text.empty()) {
        return std::nullopt;
    }
    
    return ExtractionResult{
        .text = std::move(text),
        .method = "xlsx",
        .language = "",
        .confidence = 1.0
    };
}

// ═══════════════════════════════════════════════════════════
// PPTX
// ═══════════════════════════════════════════════════════════

std::optional<ExtractionResult> OfficeTextExtractor::extractPptx(const std::string& filePath) {
    std::ostringstream allText;
    
    // Читаем слайды (slide1.xml, slide2.xml, ...)
    for (int slideNum = 1; slideNum <= 200; ++slideNum) {
        std::string slideXml = readZipEntry(filePath, 
            "ppt/slides/slide" + std::to_string(slideNum) + ".xml");
        
        if (slideXml.empty()) {
            break;  // Больше слайдов нет
        }
        
        // Извлекаем текст из <a:t> элементов (Drawing text)
        std::string slideText = extractTextFromXml(slideXml, {"t"});
        if (!slideText.empty()) {
            allText << slideText << "\n";
        }
    }
    
    // Также проверяем notes (заметки к слайдам)
    for (int noteNum = 1; noteNum <= 200; ++noteNum) {
        std::string noteXml = readZipEntry(filePath, 
            "ppt/notesSlides/notesSlide" + std::to_string(noteNum) + ".xml");
        
        if (noteXml.empty()) {
            break;
        }
        
        std::string noteText = extractTextFromXml(noteXml, {"t"});
        if (!noteText.empty()) {
            allText << noteText << " ";
        }
    }
    
    std::string text = allText.str();
    if (text.empty()) {
        return std::nullopt;
    }
    
    return ExtractionResult{
        .text = std::move(text),
        .method = "pptx",
        .language = "",
        .confidence = 1.0
    };
}

// ═══════════════════════════════════════════════════════════
// ODT (OpenDocument Text)
// ═══════════════════════════════════════════════════════════

std::optional<ExtractionResult> OfficeTextExtractor::extractOdt(const std::string& filePath) {
    // В ODF формате контент в content.xml
    std::string contentXml = readZipEntry(filePath, "content.xml");
    if (contentXml.empty()) {
        spdlog::warn("OfficeTextExtractor: no content.xml in ODT '{}'", filePath);
        return std::nullopt;
    }
    
    // Тексты в <text:p>, <text:h>, <text:span> и т.д.
    // Используем простой подход - извлекаем весь текстовый контент
    pugi::xml_document doc;
    if (!doc.load_string(contentXml.c_str())) {
        return std::nullopt;
    }
    
    std::ostringstream allText;
    
    // Рекурсивная функция для извлечения текста
    std::function<void(pugi::xml_node)> extractText = [&](pugi::xml_node node) {
        for (pugi::xml_node child : node.children()) {
            std::string nodeName = child.name();
            
            // Проверяем namespace text:
            if (nodeName.find("text:") == 0 || nodeName.find(":p") != std::string::npos ||
                nodeName.find(":h") != std::string::npos || nodeName.find(":span") != std::string::npos) {
                
                // Это текстовый элемент
                std::string nodeText = child.text().get();
                if (!nodeText.empty()) {
                    allText << nodeText;
                }
            }
            
            // Рекурсивно обрабатываем детей
            extractText(child);
            
            // Добавляем пробел/перенос после параграфа
            if (nodeName.find(":p") != std::string::npos || 
                nodeName.find(":h") != std::string::npos) {
                allText << "\n";
            }
        }
    };
    
    extractText(doc);
    
    std::string text = allText.str();
    if (text.empty()) {
        return std::nullopt;
    }
    
    return ExtractionResult{
        .text = std::move(text),
        .method = "odt",
        .language = "",
        .confidence = 1.0
    };
}

// ═══════════════════════════════════════════════════════════
// ODS (OpenDocument Spreadsheet)
// ═══════════════════════════════════════════════════════════

std::optional<ExtractionResult> OfficeTextExtractor::extractOds(const std::string& filePath) {
    std::string contentXml = readZipEntry(filePath, "content.xml");
    if (contentXml.empty()) {
        spdlog::warn("OfficeTextExtractor: no content.xml in ODS '{}'", filePath);
        return std::nullopt;
    }
    
    pugi::xml_document doc;
    if (!doc.load_string(contentXml.c_str())) {
        return std::nullopt;
    }
    
    std::ostringstream allText;
    
    // В ODS данные в <table:table-cell><text:p>...</text:p></table:table-cell>
    std::function<void(pugi::xml_node)> extractCells = [&](pugi::xml_node node) {
        for (pugi::xml_node child : node.children()) {
            std::string nodeName = child.name();
            
            // Ищем text:p внутри table:table-cell
            if (nodeName.find(":p") != std::string::npos) {
                std::string cellText = child.text().get();
                if (!cellText.empty()) {
                    allText << cellText << " ";
                }
            }
            
            extractCells(child);
        }
    };
    
    extractCells(doc);
    
    std::string text = allText.str();
    if (text.empty()) {
        return std::nullopt;
    }
    
    return ExtractionResult{
        .text = std::move(text),
        .method = "ods",
        .language = "",
        .confidence = 1.0
    };
}

} // namespace FamilyVault

