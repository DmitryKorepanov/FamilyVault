# Этап 3: Извлечение текста

## Цель
Добавить извлечение текста из документов для полнотекстового поиска. OCR как опциональная фича.

---

## Задача 3.1: TextExtractor — абстракция

### Описание
Создать интерфейс и реестр экстракторов текста.

### Требования
1. Интерфейс ITextExtractor
2. Реестр экстракторов с автоматическим выбором
3. Асинхронное извлечение
4. Сохранение результата в БД

### Интерфейс
```cpp
// include/familyvault/TextExtractor.h
#pragma once

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <functional>

namespace FamilyVault {

struct ExtractionResult {
    std::string text;
    std::string method;      // "pdf", "docx", "ocr"
    std::string language;    // detected or specified
    double confidence = 1.0; // for OCR
};

class ITextExtractor {
public:
    virtual ~ITextExtractor() = default;
    
    virtual std::string name() const = 0;
    virtual bool canHandle(const std::string& mimeType) const = 0;
    virtual std::optional<ExtractionResult> extract(const std::string& filePath) = 0;
    virtual int priority() const { return 0; } // higher = preferred
};

class TextExtractorRegistry {
public:
    void registerExtractor(std::shared_ptr<ITextExtractor> extractor);
    std::optional<ExtractionResult> extract(const std::string& filePath, 
                                            const std::string& mimeType);
    
    // Асинхронное извлечение
    using Callback = std::function<void(std::optional<ExtractionResult>)>;
    void extractAsync(const std::string& filePath,
                     const std::string& mimeType,
                     Callback onComplete);
    
private:
    std::vector<std::shared_ptr<ITextExtractor>> m_extractors;
};

} // namespace FamilyVault
```

### Критерии приёмки
- [ ] Интерфейс позволяет добавлять новые экстракторы
- [ ] Автовыбор по MIME типу
- [ ] Fallback если preferred extractor не справился
- [ ] Асинхронное API не блокирует

---

## Задача 3.2: PlainTextExtractor — простой текст

### Описание
Экстрактор для текстовых файлов.

### Требования
1. Поддержка: txt, md, html, xml, json, csv, log
2. Определение кодировки (UTF-8, UTF-16, CP1251)
3. Удаление HTML тегов для .html

### Поддерживаемые MIME типы
- text/plain
- text/markdown
- text/html
- text/xml
- application/json
- text/csv

### Критерии приёмки
- [ ] UTF-8 файлы читаются корректно
- [ ] UTF-16 (BOM) определяется
- [ ] HTML теги удаляются, текст остаётся
- [ ] Большие файлы (>10MB) обрезаются

### Реализация
```cpp
class PlainTextExtractor : public ITextExtractor {
public:
    std::string name() const override { return "plain_text"; }
    
    bool canHandle(const std::string& mimeType) const override {
        return mimeType.starts_with("text/") ||
               mimeType == "application/json" ||
               mimeType == "application/xml";
    }
    
    std::optional<ExtractionResult> extract(const std::string& filePath) override {
        // 1. Detect encoding
        // 2. Read file with correct encoding
        // 3. Strip HTML if needed
        // 4. Limit size
    }
    
private:
    std::string detectEncoding(const std::string& filePath);
    std::string stripHtml(const std::string& html);
};
```

---

## Задача 3.3: PdfTextExtractor — PDF документы

### Описание
Экстрактор текста из PDF файлов с использованием Poppler.

### Требования
1. Извлечение текста из текстовых PDF
2. Определение что PDF — скан (мало текста)
3. Лимит страниц для обработки
4. Обработка защищённых PDF (пропуск)

### Зависимости
- poppler-cpp (vcpkg: poppler)

### Критерии приёмки
- [ ] Текстовые PDF читаются корректно
- [ ] Сканы определяются (возврат пустого результата или флаг)
- [ ] Многостраничные PDF обрабатываются
- [ ] Защищённые PDF не вызывают crash

### Реализация
```cpp
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>

class PdfTextExtractor : public ITextExtractor {
public:
    std::string name() const override { return "pdf"; }
    
    bool canHandle(const std::string& mimeType) const override {
        return mimeType == "application/pdf";
    }
    
    std::optional<ExtractionResult> extract(const std::string& filePath) override {
        auto doc = poppler::document::load_from_file(filePath);
        if (!doc || doc->is_locked()) {
            return std::nullopt;
        }
        
        std::string fullText;
        int pageLimit = std::min(doc->pages(), m_maxPages);
        
        for (int i = 0; i < pageLimit; ++i) {
            auto page = doc->create_page(i);
            if (page) {
                auto text = page->text().to_utf8();
                fullText += text;
                fullText += "\n\n";
            }
        }
        
        // Check if it's a scan (very little text per page)
        double avgCharsPerPage = fullText.length() / (double)pageLimit;
        if (avgCharsPerPage < 100) {
            // Probably a scan, mark for OCR
            return ExtractionResult{
                .text = fullText,
                .method = "pdf_sparse",
                .confidence = 0.3
            };
        }
        
        return ExtractionResult{
            .text = fullText,
            .method = "pdf",
            .confidence = 1.0
        };
    }
    
    void setMaxPages(int max) { m_maxPages = max; }
    
private:
    int m_maxPages = 50;
};
```

---

## Задача 3.4: OfficeTextExtractor — Office документы

### Описание
Экстрактор для DOCX, XLSX, PPTX (OpenXML формат).

### Требования
1. Распаковка ZIP архива
2. Парсинг XML содержимого
3. Извлечение текста из всех частей документа
4. Поддержка: docx, xlsx, pptx, odt, ods, odp

### Зависимости
- libzip (vcpkg)
- pugixml (vcpkg)

### MIME типы
- application/vnd.openxmlformats-officedocument.wordprocessingml.document (docx)
- application/vnd.openxmlformats-officedocument.spreadsheetml.sheet (xlsx)
- application/vnd.openxmlformats-officedocument.presentationml.presentation (pptx)
- application/vnd.oasis.opendocument.text (odt)
- application/vnd.oasis.opendocument.spreadsheet (ods)

### Критерии приёмки
- [ ] DOCX: текст из параграфов, таблиц, headers/footers
- [ ] XLSX: текст из всех ячеек всех листов
- [ ] PPTX: текст со всех слайдов
- [ ] ODF форматы поддерживаются

### Реализация
```cpp
#include <zip.h>
#include <pugixml.hpp>

class OfficeTextExtractor : public ITextExtractor {
public:
    std::string name() const override { return "office"; }
    
    bool canHandle(const std::string& mimeType) const override {
        static const std::vector<std::string> supported = {
            "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
            "application/vnd.openxmlformats-officedocument.presentationml.presentation",
            "application/vnd.oasis.opendocument.text",
            // ...
        };
        return std::find(supported.begin(), supported.end(), mimeType) != supported.end();
    }
    
    std::optional<ExtractionResult> extract(const std::string& filePath) override {
        // Determine format
        auto ext = getExtension(filePath);
        
        if (ext == "docx") return extractDocx(filePath);
        if (ext == "xlsx") return extractXlsx(filePath);
        if (ext == "pptx") return extractPptx(filePath);
        if (ext == "odt") return extractOdt(filePath);
        
        return std::nullopt;
    }
    
private:
    std::optional<ExtractionResult> extractDocx(const std::string& path);
    std::optional<ExtractionResult> extractXlsx(const std::string& path);
    std::optional<ExtractionResult> extractPptx(const std::string& path);
    std::optional<ExtractionResult> extractOdt(const std::string& path);
    
    std::string readZipEntry(zip_t* archive, const char* entryName);
    std::string extractTextFromXml(const std::string& xml, const std::string& textTag);
};
```

**Извлечение текста из DOCX:**
```cpp
std::optional<ExtractionResult> OfficeTextExtractor::extractDocx(const std::string& path) {
    int err;
    zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, &err);
    if (!archive) return std::nullopt;
    
    std::string text;
    
    // Main document
    auto docXml = readZipEntry(archive, "word/document.xml");
    if (!docXml.empty()) {
        pugi::xml_document doc;
        doc.load_string(docXml.c_str());
        
        // Extract all <w:t> elements
        for (auto node : doc.select_nodes("//w:t")) {
            text += node.node().text().get();
            text += " ";
        }
    }
    
    // Headers, footers (optional)
    // ...
    
    zip_close(archive);
    
    return ExtractionResult{
        .text = text,
        .method = "docx",
        .confidence = 1.0
    };
}
```

---

## Задача 3.5: OcrTextExtractor — распознавание текста (опционально)

### Описание
Экстрактор на основе Tesseract OCR для изображений и сканов.

### Требования
1. Поддержка: jpg, png, tiff, bmp
2. Выбор языков распознавания
3. Настройка качества vs скорости
4. Определение уверенности результата
5. Возможность отключения в настройках

### Зависимости
- tesseract (vcpkg)
- leptonica (vcpkg, transitive)
- tessdata (языковые данные) — eng, rus как минимум

### Критерии приёмки
- [ ] Печатный текст на изображениях распознаётся
- [ ] Русский и английский языки работают
- [ ] Low confidence результаты отмечаются
- [ ] OCR можно отключить в настройках
- [ ] Не падает на битых изображениях

### Реализация
```cpp
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

class OcrTextExtractor : public ITextExtractor {
public:
    OcrTextExtractor(const std::vector<std::string>& languages = {"eng", "rus"}) {
        m_api = std::make_unique<tesseract::TessBaseAPI>();
        
        std::string langStr;
        for (const auto& lang : languages) {
            if (!langStr.empty()) langStr += "+";
            langStr += lang;
        }
        
        // tessdata path - can be set via TESSDATA_PREFIX env
        if (m_api->Init(nullptr, langStr.c_str()) != 0) {
            throw std::runtime_error("Failed to initialize Tesseract");
        }
        
        // Set page segmentation mode
        m_api->SetPageSegMode(tesseract::PSM_AUTO);
    }
    
    ~OcrTextExtractor() {
        if (m_api) m_api->End();
    }
    
    std::string name() const override { return "ocr"; }
    
    bool canHandle(const std::string& mimeType) const override {
        return mimeType.starts_with("image/") &&
               mimeType != "image/gif"; // animated gifs problematic
    }
    
    int priority() const override { return -10; } // low priority, slow
    
    std::optional<ExtractionResult> extract(const std::string& filePath) override {
        Pix* image = pixRead(filePath.c_str());
        if (!image) return std::nullopt;
        
        m_api->SetImage(image);
        
        char* text = m_api->GetUTF8Text();
        int confidence = m_api->MeanTextConf();
        
        std::string result(text ? text : "");
        delete[] text;
        pixDestroy(&image);
        
        // Skip if confidence too low or no meaningful text
        if (confidence < 30 || result.length() < 10) {
            return std::nullopt;
        }
        
        // Detect language from result
        std::string detectedLang = detectLanguage(result);
        
        return ExtractionResult{
            .text = result,
            .method = "ocr",
            .language = detectedLang,
            .confidence = confidence / 100.0
        };
    }
    
    void setLanguages(const std::vector<std::string>& languages);
    
private:
    std::unique_ptr<tesseract::TessBaseAPI> m_api;
    std::string detectLanguage(const std::string& text);
};
```

---

## Задача 3.6: ContentIndexer — интеграция с индексом

### Описание
Интегрировать извлечение текста в процесс индексации.

### Требования
1. Извлечение запускается после базовой индексации
2. Фоновая обработка в отдельном потоке
3. Приоритизация (новые файлы сначала)
4. Сохранение в file_content и обновление FTS
5. UI индикация прогресса

### Критерии приёмки
- [ ] Текст извлекается для новых файлов
- [ ] Не блокирует основной поиск
- [ ] FTS обновляется после извлечения
- [ ] Можно запустить переиндексацию вручную

### Интеграция
```cpp
class ContentIndexer {
public:
    ContentIndexer(std::shared_ptr<Database> db,
                   std::shared_ptr<TextExtractorRegistry> extractors);
    
    // Запуск фоновой обработки
    void start();
    void stop();
    
    // Обработка конкретного файла
    void processFile(int64_t fileId);
    
    // Переиндексация всех
    void reindexAll(std::function<void(int, int)> onProgress);
    
    // Статус
    struct Status {
        int pending = 0;
        int processed = 0;
        bool isRunning = false;
    };
    Status getStatus() const;
    
// Callbacks для прогресса (вызываются из C++ через FFI)
    using ProgressCallback = std::function<void(int processed, int total)>;
    using FileProcessedCallback = std::function<void(int64_t fileId)>;
    
    void setProgressCallback(ProgressCallback cb);
    void setFileProcessedCallback(FileProcessedCallback cb);
    
private:
    void processQueue();
    void saveContent(int64_t fileId, const ExtractionResult& result);
    void updateFts(int64_t fileId, const std::string& content);
};
```

---

## Задача 3.7: Тесты извлечения текста

### Описание
Unit и integration тесты для экстракторов.

### Требования
1. Тестовые файлы: pdf, docx, txt с известным содержимым
2. Тесты каждого экстрактора
3. Тесты edge cases (пустые файлы, большие файлы, битые файлы)

### Тестовые файлы
```
tests/fixtures/text_extraction/
├── simple.txt              # "Hello World"
├── utf8.txt                # Unicode текст
├── document.pdf            # PDF с известным текстом
├── scan.pdf                # PDF-скан (для OCR теста)
├── report.docx             # Word документ
├── data.xlsx               # Excel файл
├── presentation.pptx       # PowerPoint
├── image_with_text.png     # Картинка с текстом для OCR
├── empty.pdf               # Пустой PDF
└── corrupted.docx          # Битый файл
```

### Критерии приёмки
- [ ] Все экстракторы покрыты тестами
- [ ] Тесты проходят в CI
- [ ] Edge cases обрабатываются без crash

