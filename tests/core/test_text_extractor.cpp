// test_text_extractor.cpp — тесты TextExtractor
// Unit и integration тесты для всех экстракторов текста

#include <gtest/gtest.h>
#include "familyvault/TextExtractor.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;
using namespace FamilyVault;

// ═══════════════════════════════════════════════════════════
// Вспомогательные функции
// ═══════════════════════════════════════════════════════════

class TextExtractorTest : public ::testing::Test {
protected:
    std::string fixturesDir;
    std::string tempDir;

    void SetUp() override {
        // Путь к fixtures зависит от того, откуда запускаются тесты
        // CMake копирует fixtures в build директорию
        fixturesDir = "fixtures/text_extraction";
        
        // Создаём временную директорию для тестов
        tempDir = "temp_text_test_" + std::to_string(std::rand());
        fs::create_directories(tempDir);
    }

    void TearDown() override {
        // Удаляем временную директорию
        if (fs::exists(tempDir)) {
            fs::remove_all(tempDir);
        }
    }

    // Получить путь к fixture файлу
    std::string getFixturePath(const std::string& filename) {
        return fixturesDir + "/" + filename;
    }

    // Создать временный файл с содержимым
    std::string createTempFile(const std::string& name, const std::string& content) {
        std::string path = tempDir + "/" + name;
        std::ofstream file(path, std::ios::binary);
        file.write(content.data(), content.size());
        return path;
    }

    // Создать большой файл для тестирования лимитов
    std::string createLargeFile(const std::string& name, size_t sizeBytes) {
        std::string path = tempDir + "/" + name;
        std::ofstream file(path, std::ios::binary);
        
        std::string chunk = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ";
        while (file.tellp() < static_cast<std::streampos>(sizeBytes)) {
            file << chunk;
        }
        
        return path;
    }
};

// ═══════════════════════════════════════════════════════════
// TextExtractorRegistry Tests
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, RegistryCreateDefault) {
    auto registry = TextExtractorRegistry::createDefault();
    ASSERT_NE(registry, nullptr);
    
    // Должны быть зарегистрированы базовые экстракторы
    EXPECT_TRUE(registry->canExtract("text/plain"));
    EXPECT_TRUE(registry->canExtract("application/pdf"));
    EXPECT_TRUE(registry->canExtract("application/vnd.openxmlformats-officedocument.wordprocessingml.document"));
}

TEST_F(TextExtractorTest, RegistryGetSupportedMimeTypes) {
    auto registry = TextExtractorRegistry::createDefault();
    auto mimeTypes = registry->getSupportedMimeTypes();
    
    EXPECT_GT(mimeTypes.size(), 0);
    
    // Проверяем наличие основных типов
    auto contains = [&mimeTypes](const std::string& type) {
        return std::find(mimeTypes.begin(), mimeTypes.end(), type) != mimeTypes.end();
    };
    
    EXPECT_TRUE(contains("text/plain"));
    EXPECT_TRUE(contains("application/pdf"));
}

TEST_F(TextExtractorTest, RegistryGetExtractorName) {
    auto registry = TextExtractorRegistry::createDefault();
    
    EXPECT_EQ(registry->getExtractorName("text/plain"), "plain_text");
    EXPECT_EQ(registry->getExtractorName("application/pdf"), "pdf");
    EXPECT_EQ(registry->getExtractorName("application/vnd.openxmlformats-officedocument.wordprocessingml.document"), "office");
    
    // Неизвестный тип
    EXPECT_EQ(registry->getExtractorName("application/unknown"), "");
}

TEST_F(TextExtractorTest, RegistryCannotExtractUnknownType) {
    auto registry = TextExtractorRegistry::createDefault();
    
    EXPECT_FALSE(registry->canExtract("application/unknown-type"));
    EXPECT_FALSE(registry->canExtract("video/mp4"));
    EXPECT_FALSE(registry->canExtract("audio/mpeg"));
}

TEST_F(TextExtractorTest, RegistryExtractNonExistentFile) {
    auto registry = TextExtractorRegistry::createDefault();
    
    auto result = registry->extract("non_existent_file.txt", "text/plain");
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════
// PlainTextExtractor Tests
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, PlainTextCanHandle) {
    PlainTextExtractor extractor;
    
    // Должен обрабатывать
    EXPECT_TRUE(extractor.canHandle("text/plain"));
    EXPECT_TRUE(extractor.canHandle("text/html"));
    EXPECT_TRUE(extractor.canHandle("text/xml"));
    EXPECT_TRUE(extractor.canHandle("text/css"));
    EXPECT_TRUE(extractor.canHandle("text/markdown"));
    EXPECT_TRUE(extractor.canHandle("application/json"));
    EXPECT_TRUE(extractor.canHandle("application/xml"));
    EXPECT_TRUE(extractor.canHandle("application/javascript"));
    
    // Не должен обрабатывать
    EXPECT_FALSE(extractor.canHandle("application/pdf"));
    EXPECT_FALSE(extractor.canHandle("image/jpeg"));
    EXPECT_FALSE(extractor.canHandle("audio/mpeg"));
}

TEST_F(TextExtractorTest, PlainTextExtractSimpleTxt) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("simple.txt");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->text.empty());
    EXPECT_EQ(result->method, "plain_text");
    EXPECT_DOUBLE_EQ(result->confidence, 1.0);
    
    // Проверяем содержимое
    EXPECT_NE(result->text.find("Hello World"), std::string::npos);
    EXPECT_NE(result->text.find("FamilyVault"), std::string::npos);
}

TEST_F(TextExtractorTest, PlainTextExtractUtf8) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("utf8.txt");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->text.empty());
    
    // Проверяем UTF-8 содержимое (русский текст)
    EXPECT_NE(result->text.find("Привет"), std::string::npos);
}

TEST_F(TextExtractorTest, PlainTextExtractHtml) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("sample.html");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    
    // HTML теги должны быть удалены
    EXPECT_EQ(result->text.find("<html>"), std::string::npos);
    EXPECT_EQ(result->text.find("<body>"), std::string::npos);
    EXPECT_EQ(result->text.find("<script>"), std::string::npos);
    
    // Текст должен остаться
    EXPECT_NE(result->text.find("Hello HTML World"), std::string::npos);
    EXPECT_NE(result->text.find("paragraph"), std::string::npos);
    
    // HTML entities должны быть декодированы
    EXPECT_EQ(result->text.find("&nbsp;"), std::string::npos);
}

TEST_F(TextExtractorTest, PlainTextExtractXml) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("sample.xml");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    
    // XML теги должны быть удалены
    EXPECT_EQ(result->text.find("<root>"), std::string::npos);
    EXPECT_EQ(result->text.find("</root>"), std::string::npos);
    
    // Текстовое содержимое должно остаться
    EXPECT_NE(result->text.find("XML Test Document"), std::string::npos);
    
    // CDATA содержимое должно быть извлечено
    EXPECT_NE(result->text.find("CDATA section"), std::string::npos);
}

TEST_F(TextExtractorTest, PlainTextExtractJson) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("sample.json");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->text.empty());
    
    // JSON сохраняется как есть (это текстовый формат)
    EXPECT_NE(result->text.find("JSON Test Document"), std::string::npos);
}

TEST_F(TextExtractorTest, PlainTextExtractMarkdown) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("sample.md");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->text.find("Markdown Test Document"), std::string::npos);
}

TEST_F(TextExtractorTest, PlainTextExtractCsv) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("sample.csv");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->text.find("Name"), std::string::npos);
    EXPECT_NE(result->text.find("Value"), std::string::npos);
}

TEST_F(TextExtractorTest, PlainTextExtractEmptyFile) {
    PlainTextExtractor extractor;
    
    std::string path = getFixturePath("empty.txt");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    // Пустой файл должен вернуть nullopt
    EXPECT_FALSE(result.has_value());
}

TEST_F(TextExtractorTest, PlainTextExtractLargeFile) {
    PlainTextExtractor extractor;
    extractor.setMaxFileSize(1024); // 1KB лимит для теста
    
    // Создаём файл больше лимита
    std::string path = createLargeFile("large.txt", 5000);
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    // Текст должен быть обрезан до лимита
    EXPECT_LE(result->text.length(), 1024u);
}

TEST_F(TextExtractorTest, PlainTextExtractNonExistent) {
    PlainTextExtractor extractor;
    
    auto result = extractor.extract("this_file_does_not_exist.txt");
    
    EXPECT_FALSE(result.has_value());
}

TEST_F(TextExtractorTest, PlainTextExtractUtf16LE) {
    PlainTextExtractor extractor;
    
    // Создаём UTF-16 LE файл с BOM
    std::vector<char> utf16Data;
    // BOM
    utf16Data.push_back(static_cast<char>(0xFF));
    utf16Data.push_back(static_cast<char>(0xFE));
    // "Hello" in UTF-16 LE
    const char16_t text[] = u"Hello UTF-16";
    for (char16_t c : text) {
        if (c == 0) break;
        utf16Data.push_back(static_cast<char>(c & 0xFF));
        utf16Data.push_back(static_cast<char>((c >> 8) & 0xFF));
    }
    
    std::string path = createTempFile("utf16le.txt", std::string(utf16Data.begin(), utf16Data.end()));
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->text.find("Hello"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// PdfTextExtractor Tests
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, PdfCanHandle) {
    PdfTextExtractor extractor;
    
    EXPECT_TRUE(extractor.canHandle("application/pdf"));
    
    EXPECT_FALSE(extractor.canHandle("text/plain"));
    EXPECT_FALSE(extractor.canHandle("application/msword"));
}

TEST_F(TextExtractorTest, PdfExtractDocument) {
    PdfTextExtractor extractor;
    
    std::string path = getFixturePath("document.pdf");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    // Our minimal test PDF has few chars, so it may be marked as "pdf_sparse"
    EXPECT_TRUE(result->method == "pdf" || result->method == "pdf_sparse");
    EXPECT_FALSE(result->text.empty());
    
    // Проверяем содержимое PDF
    EXPECT_NE(result->text.find("Hello from PDF"), std::string::npos);
}

TEST_F(TextExtractorTest, PdfExtractEmptyPdf) {
    PdfTextExtractor extractor;
    
    std::string path = getFixturePath("empty.pdf");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    // Empty PDF may return nullopt or result with empty/very short text
    // and low confidence (pdf_sparse)
    if (result.has_value()) {
        // If it returns something, it should have low confidence
        EXPECT_TRUE(result->isLowConfidence() || result->text.empty());
    }
}

TEST_F(TextExtractorTest, PdfExtractCorrupted) {
    PdfTextExtractor extractor;
    
    std::string path = getFixturePath("corrupted.pdf");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    // Битый PDF не должен вызвать crash, должен вернуть nullopt
    EXPECT_FALSE(result.has_value());
}

TEST_F(TextExtractorTest, PdfExtractNonExistent) {
    PdfTextExtractor extractor;
    
    auto result = extractor.extract("this_file_does_not_exist.pdf");
    
    EXPECT_FALSE(result.has_value());
}

TEST_F(TextExtractorTest, PdfMaxPagesLimit) {
    PdfTextExtractor extractor;
    extractor.setMaxPages(1);
    
    std::string path = getFixturePath("document.pdf");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    // Должен работать с ограничением страниц
    if (result.has_value()) {
        EXPECT_FALSE(result->text.empty());
    }
}

// ═══════════════════════════════════════════════════════════
// OfficeTextExtractor Tests - DOCX
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, OfficeCanHandle) {
    OfficeTextExtractor extractor;
    
    // DOCX
    EXPECT_TRUE(extractor.canHandle("application/vnd.openxmlformats-officedocument.wordprocessingml.document"));
    // XLSX
    EXPECT_TRUE(extractor.canHandle("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"));
    // PPTX
    EXPECT_TRUE(extractor.canHandle("application/vnd.openxmlformats-officedocument.presentationml.presentation"));
    // ODT
    EXPECT_TRUE(extractor.canHandle("application/vnd.oasis.opendocument.text"));
    // ODS
    EXPECT_TRUE(extractor.canHandle("application/vnd.oasis.opendocument.spreadsheet"));
    
    // Не должен обрабатывать
    EXPECT_FALSE(extractor.canHandle("application/pdf"));
    EXPECT_FALSE(extractor.canHandle("text/plain"));
    EXPECT_FALSE(extractor.canHandle("application/msword")); // Старый .doc формат
}

TEST_F(TextExtractorTest, DocxExtract) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("document.docx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->method, "docx");
    EXPECT_FALSE(result->text.empty());
    
    // Проверяем содержимое
    EXPECT_NE(result->text.find("Hello from DOCX"), std::string::npos);
    EXPECT_NE(result->text.find("FamilyVault"), std::string::npos);
}

TEST_F(TextExtractorTest, DocxExtractRussian) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("document.docx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    // Проверяем русский текст
    EXPECT_NE(result->text.find("русском"), std::string::npos);
}

TEST_F(TextExtractorTest, DocxExtractEmpty) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("empty.docx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    // Пустой DOCX должен вернуть nullopt
    EXPECT_FALSE(result.has_value());
}

TEST_F(TextExtractorTest, DocxExtractCorrupted) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("corrupted.docx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    // Битый файл не должен вызвать crash
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════
// OfficeTextExtractor Tests - XLSX
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, XlsxExtract) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("data.xlsx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->method, "xlsx");
    EXPECT_FALSE(result->text.empty());
    
    // Проверяем содержимое из shared strings
    EXPECT_NE(result->text.find("Name"), std::string::npos);
    EXPECT_NE(result->text.find("Excel test data"), std::string::npos);
}

TEST_F(TextExtractorTest, XlsxExtractNumbers) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("data.xlsx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    // Числовые значения тоже должны быть извлечены
    EXPECT_NE(result->text.find("100"), std::string::npos);
    EXPECT_NE(result->text.find("200"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// OfficeTextExtractor Tests - PPTX
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, PptxExtract) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("presentation.pptx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->method, "pptx");
    EXPECT_FALSE(result->text.empty());
    
    // Проверяем содержимое слайдов
    EXPECT_NE(result->text.find("PowerPoint Test Slide"), std::string::npos);
    EXPECT_NE(result->text.find("FamilyVault presentation"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// OfficeTextExtractor Tests - ODT
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, OdtExtract) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("document.odt");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->method, "odt");
    EXPECT_FALSE(result->text.empty());
    
    // Проверяем содержимое
    EXPECT_NE(result->text.find("Hello from ODT"), std::string::npos);
}

TEST_F(TextExtractorTest, OdtExtractRussian) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("document.odt");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    // Проверяем русский текст
    EXPECT_NE(result->text.find("русском"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// OfficeTextExtractor Tests - ODS
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, OdsExtract) {
    OfficeTextExtractor extractor;
    
    std::string path = getFixturePath("data.ods");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->method, "ods");
    EXPECT_FALSE(result->text.empty());
    
    // Проверяем содержимое
    EXPECT_NE(result->text.find("ODS test item"), std::string::npos);
    EXPECT_NE(result->text.find("FamilyVault spreadsheet"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// ExtractionResult Tests
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, ExtractionResultIsEmpty) {
    ExtractionResult result;
    EXPECT_TRUE(result.isEmpty());
    
    result.text = "Some text";
    EXPECT_FALSE(result.isEmpty());
}

TEST_F(TextExtractorTest, ExtractionResultIsLowConfidence) {
    ExtractionResult result;
    result.confidence = 1.0;
    EXPECT_FALSE(result.isLowConfidence());
    
    result.confidence = 0.5;
    EXPECT_FALSE(result.isLowConfidence());
    
    result.confidence = 0.49;
    EXPECT_TRUE(result.isLowConfidence());
    
    result.confidence = 0.0;
    EXPECT_TRUE(result.isLowConfidence());
}

// ═══════════════════════════════════════════════════════════
// Integration Tests - через Registry
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, IntegrationPlainText) {
    auto registry = TextExtractorRegistry::createDefault();
    
    std::string path = getFixturePath("simple.txt");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = registry->extract(path, "text/plain");
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->method, "plain_text");
    EXPECT_NE(result->text.find("Hello World"), std::string::npos);
}

TEST_F(TextExtractorTest, IntegrationPdf) {
    auto registry = TextExtractorRegistry::createDefault();
    
    std::string path = getFixturePath("document.pdf");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = registry->extract(path, "application/pdf");
    
    ASSERT_TRUE(result.has_value());
    // Our minimal test PDF has few chars, so it may be marked as "pdf_sparse"
    EXPECT_TRUE(result->method == "pdf" || result->method == "pdf_sparse");
}

TEST_F(TextExtractorTest, IntegrationDocx) {
    auto registry = TextExtractorRegistry::createDefault();
    
    std::string path = getFixturePath("document.docx");
    if (!fs::exists(path)) {
        GTEST_SKIP() << "Fixture file not found: " << path;
    }
    
    auto result = registry->extract(path, 
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document");
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->method, "docx");
}

TEST_F(TextExtractorTest, IntegrationUnsupportedType) {
    auto registry = TextExtractorRegistry::createDefault();
    
    auto result = registry->extract("somefile.bin", "application/octet-stream");
    
    EXPECT_FALSE(result.has_value());
}

// ═══════════════════════════════════════════════════════════
// Edge Cases Tests
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, EdgeCaseOnlyWhitespace) {
    PlainTextExtractor extractor;
    
    std::string path = createTempFile("whitespace.txt", "   \n\t\n   \r\n   ");
    
    auto result = extractor.extract(path);
    
    // File with only whitespace may return nullopt or empty/single space string
    // Behavior depends on how regex normalization treats whitespace
    if (result.has_value()) {
        // If it returns something, text should be mostly empty or just whitespace
        EXPECT_LE(result->text.length(), 1u);
    }
}

TEST_F(TextExtractorTest, EdgeCaseVeryLongLine) {
    PlainTextExtractor extractor;
    
    // Создаём файл с очень длинной строкой без переносов
    std::string longLine(10000, 'a');
    std::string path = createTempFile("longline.txt", longLine);
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->text.empty());
}

TEST_F(TextExtractorTest, EdgeCaseBinaryData) {
    PlainTextExtractor extractor;
    
    // Создаём файл с бинарными данными (не текст)
    std::string binaryData;
    for (int i = 0; i < 256; ++i) {
        binaryData += static_cast<char>(i);
    }
    std::string path = createTempFile("binary.txt", binaryData);
    
    auto result = extractor.extract(path);
    
    // Бинарные данные могут быть прочитаны, но результат не определён
    // Главное - не должно быть crash
    // Result может быть или не быть
}

TEST_F(TextExtractorTest, EdgeCaseSpecialFilename) {
    PlainTextExtractor extractor;
    
    // Файл с пробелами и спецсимволами в имени
    std::string path = createTempFile("test file (1).txt", "Special filename test");
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->text.find("Special filename"), std::string::npos);
}

TEST_F(TextExtractorTest, EdgeCaseDeepHtmlNesting) {
    PlainTextExtractor extractor;
    
    // Глубоко вложенный HTML
    std::string html = "<html><body>";
    for (int i = 0; i < 100; ++i) {
        html += "<div>";
    }
    html += "Deep nested text";
    for (int i = 0; i < 100; ++i) {
        html += "</div>";
    }
    html += "</body></html>";
    
    std::string path = createTempFile("deep.html", html);
    
    auto result = extractor.extract(path);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->text.find("Deep nested text"), std::string::npos);
}

TEST_F(TextExtractorTest, EdgeCaseMalformedHtml) {
    PlainTextExtractor extractor;
    
    // Некорректный HTML (незакрытые теги)
    std::string html = "<html><body><div>Unclosed tags<p>More text";
    
    std::string path = createTempFile("malformed.html", html);
    
    auto result = extractor.extract(path);
    
    // Должен справиться с некорректным HTML
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result->text.find("Unclosed tags"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// Priority Tests
// ═══════════════════════════════════════════════════════════

TEST_F(TextExtractorTest, ExtractorPriority) {
    PlainTextExtractor plainText;
    PdfTextExtractor pdf;
    OfficeTextExtractor office;
    
    // PlainText имеет приоритет 10
    EXPECT_EQ(plainText.priority(), 10);
    
    // PDF и Office имеют приоритет 20
    EXPECT_EQ(pdf.priority(), 20);
    EXPECT_EQ(office.priority(), 20);
}


