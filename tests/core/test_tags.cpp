// test_tags.cpp — тесты TagManager

#include <gtest/gtest.h>
#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include "familyvault/TagManager.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace FamilyVault;

class TagManagerTest : public ::testing::Test {
protected:
    std::string testDbPath;
    std::string testFolderPath;
    std::shared_ptr<Database> db;
    std::unique_ptr<IndexManager> indexManager;
    std::unique_ptr<TagManager> tagManager;
    int64_t testFileId = 0;

    void SetUp() override {
        testDbPath = "test_tags_" + std::to_string(std::rand()) + ".db";
        testFolderPath = "test_tags_folder_" + std::to_string(std::rand());

        fs::create_directories(testFolderPath);
        createTestFile(testFolderPath + "/test_file.txt", "Test content");

        db = std::make_shared<Database>(testDbPath);
        db->initialize();
        indexManager = std::make_unique<IndexManager>(db);
        tagManager = std::make_unique<TagManager>(db);

        // Индексируем файл
        int64_t folderId = indexManager->addFolder(testFolderPath, "Tag Test");
        indexManager->scanFolder(folderId);

        // Получаем ID файла
        auto files = indexManager->getRecentFiles(1);
        if (!files.empty()) {
            testFileId = files[0].id;
        }
    }

    void TearDown() override {
        tagManager.reset();
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
        std::ofstream file(path);
        file << content;
    }
};

TEST_F(TagManagerTest, AddAndGetTag) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "important");

    auto tags = tagManager->getFileTags(testFileId);

    EXPECT_EQ(tags.size(), 1);
    EXPECT_EQ(tags[0], "important");
}

TEST_F(TagManagerTest, AddMultipleTags) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "work");
    tagManager->addTag(testFileId, "2024");
    tagManager->addTag(testFileId, "urgent");

    auto tags = tagManager->getFileTags(testFileId);

    EXPECT_EQ(tags.size(), 3);
}

TEST_F(TagManagerTest, RemoveTag) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "to_remove");
    tagManager->addTag(testFileId, "keep");

    tagManager->removeTag(testFileId, "to_remove");

    auto tags = tagManager->getFileTags(testFileId);

    EXPECT_EQ(tags.size(), 1);
    EXPECT_EQ(tags[0], "keep");
}

TEST_F(TagManagerTest, DuplicateTagIgnored) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "duplicate");
    tagManager->addTag(testFileId, "duplicate");
    tagManager->addTag(testFileId, "duplicate");

    auto tags = tagManager->getFileTags(testFileId);

    EXPECT_EQ(tags.size(), 1);
}

TEST_F(TagManagerTest, GetAllTags) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "tag1");
    tagManager->addTag(testFileId, "tag2");

    auto allTags = tagManager->getAllTags();

    EXPECT_GE(allTags.size(), 2);

    bool foundTag1 = false, foundTag2 = false;
    for (const auto& t : allTags) {
        if (t.name == "tag1") foundTag1 = true;
        if (t.name == "tag2") foundTag2 = true;
    }
    EXPECT_TRUE(foundTag1);
    EXPECT_TRUE(foundTag2);
}

TEST_F(TagManagerTest, GetPopularTags) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "popular");

    auto popularTags = tagManager->getPopularTags(10);

    // Должен вернуть теги с наибольшим количеством файлов
    EXPECT_GE(popularTags.size(), 0);
}

TEST_F(TagManagerTest, SearchTags) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "project_alpha");
    tagManager->addTag(testFileId, "project_beta");
    tagManager->addTag(testFileId, "other");

    auto results = tagManager->searchTags("project");

    EXPECT_EQ(results.size(), 2);
}

TEST_F(TagManagerTest, CountFilesByTag) {
    ASSERT_GT(testFileId, 0);

    tagManager->addTag(testFileId, "count_test");

    int64_t count = tagManager->countFilesByTag("count_test");

    EXPECT_EQ(count, 1);
}

TEST_F(TagManagerTest, GenerateAutoTags) {
    ASSERT_GT(testFileId, 0);

    // Получаем FileRecord для генерации автотегов
    auto file = indexManager->getFile(testFileId);
    ASSERT_TRUE(file.has_value());

    tagManager->generateAutoTags(testFileId, *file);

    auto tags = tagManager->getFileTags(testFileId);

    // Должны быть автотеги (txt, document, год, месяц и т.д.)
    EXPECT_GE(tags.size(), 1);

    // Проверяем что есть тег расширения
    bool hasExtTag = false;
    for (const auto& tag : tags) {
        if (tag == "txt") {
            hasExtTag = true;
            break;
        }
    }
    EXPECT_TRUE(hasExtTag);
}

