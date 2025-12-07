// TextExtractor.h — Интерфейс и реестр экстракторов текста
// Для полнотекстового поиска в документах

#pragma once

#include "export.h"
#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <functional>
#include <mutex>

namespace FamilyVault {

// ═══════════════════════════════════════════════════════════
// Результат извлечения текста
// ═══════════════════════════════════════════════════════════

struct ExtractionResult {
    std::string text;           // Извлечённый текст
    std::string method;         // "plain_text", "pdf", "docx", etc.
    std::string language;       // Определённый или указанный язык
    double confidence = 1.0;    // Уверенность (для OCR)
    
    bool isEmpty() const { return text.empty(); }
    bool isLowConfidence() const { return confidence < 0.5; }
};

// ═══════════════════════════════════════════════════════════
// Интерфейс экстрактора текста
// ═══════════════════════════════════════════════════════════

class FV_API ITextExtractor {
public:
    virtual ~ITextExtractor() = default;
    
    /// Имя экстрактора (для логирования)
    virtual std::string name() const = 0;
    
    /// Проверить, может ли экстрактор обработать данный MIME тип
    virtual bool canHandle(const std::string& mimeType) const = 0;
    
    /// Извлечь текст из файла
    /// @param filePath Путь к файлу
    /// @return Результат или nullopt если не удалось извлечь
    virtual std::optional<ExtractionResult> extract(const std::string& filePath) = 0;
    
    /// Приоритет экстрактора (выше = предпочтительнее)
    /// Используется когда несколько экстракторов могут обработать один тип
    virtual int priority() const { return 0; }
};

// ═══════════════════════════════════════════════════════════
// Реестр экстракторов
// ═══════════════════════════════════════════════════════════

class FV_API TextExtractorRegistry {
public:
    TextExtractorRegistry();
    ~TextExtractorRegistry();
    
    /// Зарегистрировать экстрактор
    void registerExtractor(std::shared_ptr<ITextExtractor> extractor);
    
    /// Извлечь текст из файла (синхронно)
    /// Автоматически выбирает подходящий экстрактор по MIME типу
    /// @param filePath Путь к файлу
    /// @param mimeType MIME тип файла
    /// @return Результат или nullopt если не удалось
    std::optional<ExtractionResult> extract(const std::string& filePath, 
                                            const std::string& mimeType);
    
    /// Проверить, есть ли экстрактор для данного MIME типа
    bool canExtract(const std::string& mimeType) const;
    
    /// Получить список поддерживаемых MIME типов
    std::vector<std::string> getSupportedMimeTypes() const;
    
    /// Получить имя экстрактора для MIME типа
    std::string getExtractorName(const std::string& mimeType) const;
    
    /// Создать реестр с базовыми экстракторами (PlainText, PDF, Office)
    static std::unique_ptr<TextExtractorRegistry> createDefault();
    
private:
    /// Найти лучший экстрактор для MIME типа
    std::shared_ptr<ITextExtractor> findExtractor(const std::string& mimeType) const;
    
    std::vector<std::shared_ptr<ITextExtractor>> m_extractors;
    mutable std::mutex m_mutex;
};

// ═══════════════════════════════════════════════════════════
// Базовые экстракторы
// ═══════════════════════════════════════════════════════════

/// Экстрактор для текстовых файлов (txt, md, html, xml, json, csv, log)
class FV_API PlainTextExtractor : public ITextExtractor {
public:
    PlainTextExtractor();
    ~PlainTextExtractor() override;
    
    std::string name() const override { return "plain_text"; }
    bool canHandle(const std::string& mimeType) const override;
    std::optional<ExtractionResult> extract(const std::string& filePath) override;
    int priority() const override { return 10; }
    
    /// Установить максимальный размер файла (по умолчанию 10MB)
    void setMaxFileSize(size_t bytes) { m_maxFileSize = bytes; }
    
private:
    /// Определить кодировку файла (UTF-8, UTF-16, CP1251)
    std::string detectEncoding(const std::vector<char>& data) const;
    
    /// Конвертировать из указанной кодировки в UTF-8
    std::string convertToUtf8(const std::vector<char>& data, 
                              const std::string& encoding) const;
    
    /// Удалить HTML теги, оставив текст
    std::string stripHtml(const std::string& html) const;
    
    /// Удалить XML теги, оставив текст
    std::string stripXml(const std::string& xml) const;
    
    /// Проверить, нужно ли удалять теги для данного MIME типа
    bool shouldStripTags(const std::string& mimeType) const;
    
    size_t m_maxFileSize = 10 * 1024 * 1024;  // 10MB
};

/// Экстрактор для PDF документов (требует Poppler)
class FV_API PdfTextExtractor : public ITextExtractor {
public:
    PdfTextExtractor();
    ~PdfTextExtractor() override;
    
    std::string name() const override { return "pdf"; }
    bool canHandle(const std::string& mimeType) const override;
    std::optional<ExtractionResult> extract(const std::string& filePath) override;
    int priority() const override { return 20; }
    
    /// Установить максимальное количество страниц для обработки
    void setMaxPages(int pages) { m_maxPages = pages; }
    
    /// Установить минимальное количество символов на страницу для определения скана
    void setMinCharsPerPage(int chars) { m_minCharsPerPage = chars; }
    
private:
    int m_maxPages = 100;
    int m_minCharsPerPage = 100;  // Меньше = возможно скан
};

/// Экстрактор для Office документов (DOCX, XLSX, PPTX, ODF)
class FV_API OfficeTextExtractor : public ITextExtractor {
public:
    OfficeTextExtractor();
    ~OfficeTextExtractor() override;
    
    std::string name() const override { return "office"; }
    bool canHandle(const std::string& mimeType) const override;
    std::optional<ExtractionResult> extract(const std::string& filePath) override;
    int priority() const override { return 20; }
    
private:
    /// Извлечь текст из DOCX
    std::optional<ExtractionResult> extractDocx(const std::string& filePath);
    
    /// Извлечь текст из XLSX
    std::optional<ExtractionResult> extractXlsx(const std::string& filePath);
    
    /// Извлечь текст из PPTX
    std::optional<ExtractionResult> extractPptx(const std::string& filePath);
    
    /// Извлечь текст из ODT
    std::optional<ExtractionResult> extractOdt(const std::string& filePath);
    
    /// Извлечь текст из ODS
    std::optional<ExtractionResult> extractOds(const std::string& filePath);
    
    /// Получить расширение файла (lowercase)
    std::string getExtension(const std::string& filePath) const;
    
    /// Прочитать содержимое файла из ZIP архива
    std::string readZipEntry(const std::string& archivePath, 
                            const std::string& entryName);
    
    /// Извлечь текст из XML, собрав содержимое указанных тегов
    std::string extractTextFromXml(const std::string& xml,
                                   const std::vector<std::string>& textTags);
};

} // namespace FamilyVault

