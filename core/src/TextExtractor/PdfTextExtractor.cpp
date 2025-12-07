#include "familyvault/TextExtractor.h"
#include <spdlog/spdlog.h>

// Poppler headers
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#include <poppler/cpp/poppler-global.h>

#include <algorithm>
#include <memory>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// PdfTextExtractor
// ═══════════════════════════════════════════════════════════

PdfTextExtractor::PdfTextExtractor() = default;
PdfTextExtractor::~PdfTextExtractor() = default;

bool PdfTextExtractor::canHandle(const std::string& mimeType) const {
    return mimeType == "application/pdf";
}

std::optional<ExtractionResult> PdfTextExtractor::extract(const std::string& filePath) {
    // Загружаем PDF документ
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(filePath));
    
    if (!doc) {
        spdlog::warn("PdfTextExtractor: failed to load PDF '{}'", filePath);
        return std::nullopt;
    }
    
    // Проверяем на защиту паролем
    if (doc->is_locked()) {
        spdlog::info("PdfTextExtractor: PDF '{}' is password protected, skipping", filePath);
        return std::nullopt;
    }
    
    int totalPages = doc->pages();
    if (totalPages <= 0) {
        spdlog::debug("PdfTextExtractor: PDF '{}' has no pages", filePath);
        return std::nullopt;
    }
    
    // Ограничиваем количество страниц
    int pageLimit = std::min(totalPages, m_maxPages);
    
    std::string fullText;
    fullText.reserve(pageLimit * 2000); // Примерная оценка
    
    int totalChars = 0;
    
    for (int i = 0; i < pageLimit; ++i) {
        std::unique_ptr<poppler::page> page(doc->create_page(i));
        if (!page) {
            continue;
        }
        
        // Извлекаем текст со страницы
        poppler::byte_array textData = page->text().to_utf8();
        std::string pageText(textData.begin(), textData.end());
        
        if (!pageText.empty()) {
            fullText += pageText;
            fullText += "\n\n";
            totalChars += static_cast<int>(pageText.length());
        }
    }
    
    // Определяем, является ли PDF сканом
    // Если очень мало текста на страницу - вероятно это скан
    double avgCharsPerPage = static_cast<double>(totalChars) / pageLimit;
    
    if (avgCharsPerPage < m_minCharsPerPage) {
        spdlog::info("PdfTextExtractor: PDF '{}' appears to be a scan (avg {} chars/page)", 
                    filePath, avgCharsPerPage);
        
        // Возвращаем то что есть, но с низкой уверенностью
        if (fullText.empty()) {
            return std::nullopt;
        }
        
        return ExtractionResult{
            .text = std::move(fullText),
            .method = "pdf_sparse",  // Помечаем как sparse/scan
            .language = "",
            .confidence = 0.3  // Низкая уверенность
        };
    }
    
    if (fullText.empty()) {
        return std::nullopt;
    }
    
    // Удаляем лишние пробелы и переносы строк
    // (PDF часто содержит много мусорных пробелов)
    std::string cleanText;
    cleanText.reserve(fullText.size());
    
    bool lastWasSpace = true; // Начинаем с true чтобы убрать ведущие пробелы
    for (char c : fullText) {
        if (c == ' ' || c == '\t') {
            if (!lastWasSpace) {
                cleanText += ' ';
                lastWasSpace = true;
            }
        } else if (c == '\n' || c == '\r') {
            if (!lastWasSpace) {
                cleanText += '\n';
                lastWasSpace = true;
            }
        } else {
            cleanText += c;
            lastWasSpace = false;
        }
    }
    
    // Trim trailing
    while (!cleanText.empty() && (cleanText.back() == ' ' || cleanText.back() == '\n')) {
        cleanText.pop_back();
    }
    
    return ExtractionResult{
        .text = std::move(cleanText),
        .method = "pdf",
        .language = "",
        .confidence = 1.0
    };
}

} // namespace FamilyVault

