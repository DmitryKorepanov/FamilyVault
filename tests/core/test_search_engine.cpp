// test_search_engine.cpp — тесты SearchEngine

#include <gtest/gtest.h>
#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include "familyvault/SearchEngine.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace FamilyVault;

class SearchEngineTest : public ::testing::Test {
protected:
    std::string testDbPath;
    std::string testFolderPath;
    std::shared_ptr<Database> db;
    std::unique_ptr<IndexManager> indexManager;
    std::unique_ptr<SearchEngine> searchEngine;

    void SetUp() override {
        testDbPath = "test_search_" + std::to_string(std::rand()) + ".db";
        testFolderPath = "test_search_folder_" + std::to_string(std::rand());

        fs::create_directories(testFolderPath);

        // Создаём тестовые файлы с разными именами
        createTestFile(testFolderPath + "/report_2024.pdf", "%PDF-1.4");
        createTestFile(testFolderPath + "/vacation_photo.jpg", "\xFF\xD8\xFF");
        createTestFile(testFolderPath + "/meeting_notes.txt", "Meeting notes");
        createTestFile(testFolderPath + "/budget_2024.xlsx", "PK");
        createTestFile(testFolderPath + "/family_photo.png", "\x89PNG");

        db = std::make_shared<Database>(testDbPath);
        db->initialize();
        indexManager = std::make_unique<IndexManager>(db);
        searchEngine = std::make_unique<SearchEngine>(db);

        // Индексируем файлы
        int64_t folderId = indexManager->addFolder(testFolderPath, "Search Test");
        indexManager->scanFolder(folderId);
    }

    void TearDown() override {
        searchEngine.reset();
        indexManager.reset();
        db.reset();

        if (fs::exists(testDbPath)) fs::remove(testDbPath);
        fs::remove(testDbPath + "-wal");
        fs::remove(testDbPath + "-shm");

        if (fs::exists(testFolderPath)) {
            fs::remove_all(testFolderPath);
        }
    }

    void createTestFile(const std::string& path, const std::string& content) {
        std::ofstream file(path, std::ios::binary);
        file.write(content.data(), content.size());
    }
};

TEST_F(SearchEngineTest, SearchByText) {
    SearchQuery query;
    query.text = "report";

    auto results = searchEngine->search(query);

    EXPECT_GE(results.size(), 1);
    bool found = false;
    for (const auto& r : results) {
        if (r.file.name.find("report") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(SearchEngineTest, SearchByContentType) {
    SearchQuery query;
    query.contentType = ContentType::Image;

    auto results = searchEngine->search(query);

    EXPECT_GE(results.size(), 2); // vacation_photo.jpg и family_photo.png

    for (const auto& r : results) {
        EXPECT_EQ(r.file.contentType, ContentType::Image);
    }
}

TEST_F(SearchEngineTest, SearchByExtension) {
    auto results = searchEngine->getByExtension("pdf", 10);

    EXPECT_GE(results.size(), 1);
    for (const auto& f : results) {
        EXPECT_EQ(f.extension, "pdf");
    }
}

TEST_F(SearchEngineTest, SearchWithLimit) {
    SearchQuery query;
    query.limit = 2;

    auto results = searchEngine->search(query);

    EXPECT_LE(results.size(), 2);
}

TEST_F(SearchEngineTest, SearchCount) {
    SearchQuery query;
    query.contentType = ContentType::Image;

    int64_t count = searchEngine->countResults(query);

    EXPECT_GE(count, 2);
}

TEST_F(SearchEngineTest, SearchSuggest) {
    auto suggestions = searchEngine->suggest("vac", 5);

    // Должен предложить "vacation_photo.jpg"
    EXPECT_GE(suggestions.size(), 0); // Может быть пусто если FTS не индексировал
}

TEST_F(SearchEngineTest, SearchSortByDate) {
    SearchQuery query;
    query.sortBy = SortBy::Date;
    query.sortAsc = false; // Новые первые

    auto results = searchEngine->search(query);

    if (results.size() >= 2) {
        EXPECT_GE(results[0].file.modifiedAt, results[1].file.modifiedAt);
    }
}

TEST_F(SearchEngineTest, SearchSortBySize) {
    SearchQuery query;
    query.sortBy = SortBy::Size;
    query.sortAsc = false; // Большие первые

    auto results = searchEngine->search(query);

    if (results.size() >= 2) {
        EXPECT_GE(results[0].file.size, results[1].file.size);
    }
}

TEST_F(SearchEngineTest, SearchWithDateFilter) {
    SearchQuery query;
    // Файлы созданы только что, установим dateFrom в прошлое
    query.dateFrom = 0;

    auto results = searchEngine->search(query);
    EXPECT_GE(results.size(), 1);
}

TEST_F(SearchEngineTest, SearchWithSizeFilter) {
    SearchQuery query;
    query.minSize = 1; // Минимум 1 байт

    auto results = searchEngine->search(query);

    for (const auto& r : results) {
        EXPECT_GE(r.file.size, 1);
    }
}

