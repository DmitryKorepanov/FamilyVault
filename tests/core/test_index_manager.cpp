// test_index_manager.cpp — тесты IndexManager

#include <gtest/gtest.h>
#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace FamilyVault;

class IndexManagerTest : public ::testing::Test {
protected:
    std::string testDbPath;
    std::string testFolderPath;
    std::shared_ptr<Database> db;
    std::unique_ptr<IndexManager> indexManager;

    void SetUp() override {
        testDbPath = "test_index_" + std::to_string(std::rand()) + ".db";
        testFolderPath = "test_folder_" + std::to_string(std::rand());

        // Создаём тестовую папку
        fs::create_directories(testFolderPath);

        // Создаём тестовые файлы
        createTestFile(testFolderPath + "/test1.txt", "Hello World");
        createTestFile(testFolderPath + "/test2.jpg", "\xFF\xD8\xFF"); // JPEG header
        createTestFile(testFolderPath + "/subdir/test3.pdf", "%PDF");
        fs::create_directories(testFolderPath + "/subdir");
        createTestFile(testFolderPath + "/subdir/test3.pdf", "%PDF-1.4");

        db = std::make_shared<Database>(testDbPath);
        db->initialize();
        indexManager = std::make_unique<IndexManager>(db);
    }

    void TearDown() override {
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
        fs::create_directories(fs::path(path).parent_path());
        std::ofstream file(path, std::ios::binary);
        file.write(content.data(), content.size());
    }
};

TEST_F(IndexManagerTest, AddFolder) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "Test Folder");

    EXPECT_GT(folderId, 0);

    auto folder = indexManager->getFolder(folderId);
    ASSERT_TRUE(folder.has_value());
    EXPECT_EQ(folder->name, "Test Folder");
    EXPECT_TRUE(folder->enabled);
    EXPECT_EQ(folder->defaultVisibility, Visibility::Family);
}

TEST_F(IndexManagerTest, AddFolderWithVisibility) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "Private Folder",
                                                Visibility::Private);

    auto folder = indexManager->getFolder(folderId);
    ASSERT_TRUE(folder.has_value());
    EXPECT_EQ(folder->defaultVisibility, Visibility::Private);
}

TEST_F(IndexManagerTest, GetFolders) {
    indexManager->addFolder(testFolderPath, "Folder 1");

    auto folders = indexManager->getFolders();
    EXPECT_EQ(folders.size(), 1);
    EXPECT_EQ(folders[0].name, "Folder 1");
}

TEST_F(IndexManagerTest, RemoveFolder) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "To Remove");

    indexManager->removeFolder(folderId);

    auto folder = indexManager->getFolder(folderId);
    EXPECT_FALSE(folder.has_value());
}

TEST_F(IndexManagerTest, ScanFolder) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "Scan Test");

    int progressCalls = 0;
    indexManager->scanFolder(folderId, [&progressCalls](const ScanProgress& p) {
        progressCalls++;
    });

    // Проверяем что файлы проиндексированы
    auto stats = indexManager->getStats();
    EXPECT_GE(stats.totalFiles, 2); // Минимум 2 файла

    auto folder = indexManager->getFolder(folderId);
    ASSERT_TRUE(folder.has_value());
    EXPECT_GT(folder->fileCount, 0);
    EXPECT_GT(folder->lastScanAt, 0);
}

TEST_F(IndexManagerTest, GetRecentFiles) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "Recent Test");
    indexManager->scanFolder(folderId);

    auto recentFiles = indexManager->getRecentFiles(10);
    EXPECT_GE(recentFiles.size(), 2);

    // Проверяем структуру файла
    if (!recentFiles.empty()) {
        const auto& file = recentFiles[0];
        EXPECT_GT(file.id, 0);
        EXPECT_FALSE(file.name.empty());
        EXPECT_GT(file.indexedAt, 0);
    }
}

TEST_F(IndexManagerTest, GetFileByPath) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "Path Test");
    indexManager->scanFolder(folderId);

    auto file = indexManager->getFileByPath(folderId, "test1.txt");
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->name, "test1.txt");
    EXPECT_EQ(file->extension, "txt");
}

TEST_F(IndexManagerTest, SetFolderEnabled) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "Enable Test");

    indexManager->setFolderEnabled(folderId, false);

    auto folder = indexManager->getFolder(folderId);
    ASSERT_TRUE(folder.has_value());
    EXPECT_FALSE(folder->enabled);

    indexManager->setFolderEnabled(folderId, true);
    folder = indexManager->getFolder(folderId);
    EXPECT_TRUE(folder->enabled);
}

TEST_F(IndexManagerTest, Stats) {
    int64_t folderId = indexManager->addFolder(testFolderPath, "Stats Test");
    indexManager->scanFolder(folderId);

    auto stats = indexManager->getStats();
    EXPECT_EQ(stats.totalFolders, 1);
    EXPECT_GE(stats.totalFiles, 2);
    EXPECT_GT(stats.totalSize, 0);
}

