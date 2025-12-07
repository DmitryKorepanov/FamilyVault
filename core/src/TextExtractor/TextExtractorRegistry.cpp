#include "familyvault/TextExtractor.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// TextExtractorRegistry
// ═══════════════════════════════════════════════════════════

TextExtractorRegistry::TextExtractorRegistry() = default;
TextExtractorRegistry::~TextExtractorRegistry() = default;

void TextExtractorRegistry::registerExtractor(std::shared_ptr<ITextExtractor> extractor) {
    if (!extractor) {
        spdlog::warn("Attempted to register null extractor");
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Проверяем, не зарегистрирован ли уже такой экстрактор
    for (const auto& existing : m_extractors) {
        if (existing->name() == extractor->name()) {
            spdlog::warn("Extractor '{}' already registered", extractor->name());
            return;
        }
    }
    
    m_extractors.push_back(std::move(extractor));
    spdlog::debug("Registered text extractor: {}", m_extractors.back()->name());
}

std::optional<ExtractionResult> TextExtractorRegistry::extract(
    const std::string& filePath, 
    const std::string& mimeType) 
{
    auto extractor = findExtractor(mimeType);
    if (!extractor) {
        spdlog::debug("No extractor found for MIME type: {}", mimeType);
        return std::nullopt;
    }
    
    spdlog::debug("Extracting text from '{}' using '{}'", filePath, extractor->name());
    
    try {
        auto result = extractor->extract(filePath);
        if (result) {
            spdlog::debug("Extracted {} chars from '{}'", result->text.length(), filePath);
        } else {
            spdlog::debug("Extractor '{}' returned no result for '{}'", 
                         extractor->name(), filePath);
        }
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Text extraction failed for '{}': {}", filePath, e.what());
        return std::nullopt;
    }
}

bool TextExtractorRegistry::canExtract(const std::string& mimeType) const {
    return findExtractor(mimeType) != nullptr;
}

std::vector<std::string> TextExtractorRegistry::getSupportedMimeTypes() const {
    // Возвращаем статический список известных поддерживаемых типов
    // В реальности каждый экстрактор сам определяет через canHandle()
    return {
        // PlainText
        "text/plain",
        "text/markdown", 
        "text/html",
        "text/xml",
        "text/css",
        "text/csv",
        "application/json",
        "application/xml",
        "application/javascript",
        // PDF
        "application/pdf",
        // Office
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        "application/vnd.openxmlformats-officedocument.presentationml.presentation",
        "application/vnd.oasis.opendocument.text",
        "application/vnd.oasis.opendocument.spreadsheet",
        "application/vnd.oasis.opendocument.presentation",
    };
}

std::string TextExtractorRegistry::getExtractorName(const std::string& mimeType) const {
    auto extractor = findExtractor(mimeType);
    return extractor ? extractor->name() : "";
}

std::shared_ptr<ITextExtractor> TextExtractorRegistry::findExtractor(
    const std::string& mimeType) const 
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::shared_ptr<ITextExtractor> best;
    int bestPriority = -1000;
    
    for (const auto& extractor : m_extractors) {
        if (extractor->canHandle(mimeType)) {
            int priority = extractor->priority();
            if (priority > bestPriority) {
                bestPriority = priority;
                best = extractor;
            }
        }
    }
    
    return best;
}

std::unique_ptr<TextExtractorRegistry> TextExtractorRegistry::createDefault() {
    auto registry = std::make_unique<TextExtractorRegistry>();
    
    // Регистрируем базовые экстракторы
    registry->registerExtractor(std::make_shared<PlainTextExtractor>());
    registry->registerExtractor(std::make_shared<PdfTextExtractor>());
    registry->registerExtractor(std::make_shared<OfficeTextExtractor>());
    
    spdlog::info("Created default TextExtractorRegistry with {} extractors",
                 registry->m_extractors.size());
    
    return registry;
}

} // namespace FamilyVault

