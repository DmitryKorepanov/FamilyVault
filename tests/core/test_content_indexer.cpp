// test_content_indexer.cpp — тесты ContentIndexer

#include <gtest/gtest.h>
#include "familyvault/Database.h"
#include "familyvault/IndexManager.h"
#include "familyvault/ContentIndexer.h"
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace FamilyVault;

class ContentIndexerTest : public ::testing::Test {
protected:
    std::string testDbPath;
    std::string testFolderPath;
    std::shared_ptr<Database> db;
    std::unique_ptr<IndexManager> indexManager;
    std::unique_ptr<ContentIndexer> contentIndexer;

    void SetUp() override {
        testDbPath = "test_content_indexer_" + std::to_string(std::rand()) + ".db";
        testFolderPath = "test_content_folder_" + std::to_string(std::rand());

        // Создаём тестовую папку с текстовыми файлами
        fs::create_directories(testFolderPath);
        createTestFile(testFolderPath + "/doc1.txt", "Hello World content for indexing");
        createTestFile(testFolderPath + "/doc2.txt", "Another document with searchable text");
        createTestFile(testFolderPath + "/doc3.txt", "Third file for content extraction test");

        db = std::make_shared<Database>(testDbPath);
        db->initialize();
        indexManager = std::make_unique<IndexManager>(db);
        contentIndexer = std::make_unique<ContentIndexer>(db);
    }

    void TearDown() override {
        // Ensure indexer is stopped before cleanup
        if (contentIndexer && contentIndexer->isRunning()) {
            contentIndexer->stop(true);
        }
        
        contentIndexer.reset();
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
    
    void indexTestFolder() {
        int64_t folderId = indexManager->addFolder(testFolderPath, "Test Folder");
        indexManager->scanFolder(folderId);
    }
};

// ═══════════════════════════════════════════════════════════
// Basic Start/Stop Tests
// ═══════════════════════════════════════════════════════════

TEST_F(ContentIndexerTest, InitialStateNotRunning) {
    EXPECT_FALSE(contentIndexer->isRunning());
}

TEST_F(ContentIndexerTest, StartWithoutFiles) {
    // Starting with no files should be safe
    contentIndexer->start();
    
    // Give it a moment to process (empty queue)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should still be running (waiting for work)
    EXPECT_TRUE(contentIndexer->isRunning());
    
    contentIndexer->stop(true);
    EXPECT_FALSE(contentIndexer->isRunning());
}

TEST_F(ContentIndexerTest, StartIsIdempotent) {
    contentIndexer->start();
    EXPECT_TRUE(contentIndexer->isRunning());
    
    // Calling start again should be safe
    contentIndexer->start();
    EXPECT_TRUE(contentIndexer->isRunning());
    
    contentIndexer->stop(true);
    EXPECT_FALSE(contentIndexer->isRunning());
}

TEST_F(ContentIndexerTest, StopIsIdempotent) {
    contentIndexer->start();
    contentIndexer->stop(true);
    EXPECT_FALSE(contentIndexer->isRunning());
    
    // Calling stop again should be safe
    contentIndexer->stop(true);
    EXPECT_FALSE(contentIndexer->isRunning());
}

TEST_F(ContentIndexerTest, StopWhenNotRunning) {
    // Stopping when not running should be safe
    EXPECT_FALSE(contentIndexer->isRunning());
    contentIndexer->stop(true);
    EXPECT_FALSE(contentIndexer->isRunning());
}

// ═══════════════════════════════════════════════════════════
// Enqueue Tests
// ═══════════════════════════════════════════════════════════

TEST_F(ContentIndexerTest, EnqueueUnprocessedReturnsCount) {
    indexTestFolder();
    
    int pending = contentIndexer->enqueueUnprocessed();
    
    // Should have at least our 3 test files
    EXPECT_GE(pending, 3);
}

TEST_F(ContentIndexerTest, EnqueueUnprocessedEmptyDb) {
    // No files indexed yet
    int pending = contentIndexer->enqueueUnprocessed();
    EXPECT_EQ(pending, 0);
}

TEST_F(ContentIndexerTest, EnqueueAfterProcessing) {
    indexTestFolder();
    
    int pending1 = contentIndexer->enqueueUnprocessed();
    EXPECT_GE(pending1, 3);
    
    // Start and wait for processing
    contentIndexer->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    contentIndexer->stop(true);
    
    // After processing, enqueue should return 0 (or fewer)
    int pending2 = contentIndexer->enqueueUnprocessed();
    EXPECT_LT(pending2, pending1);
}

// ═══════════════════════════════════════════════════════════
// Processing Tests
// ═══════════════════════════════════════════════════════════

TEST_F(ContentIndexerTest, ProcessesFilesSuccessfully) {
    indexTestFolder();
    
    contentIndexer->enqueueUnprocessed();
    contentIndexer->start();
    
    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    contentIndexer->stop(true);
    
    auto status = contentIndexer->getStatus();
    EXPECT_GT(status.processed, 0);
}

TEST_F(ContentIndexerTest, GetPendingCount) {
    indexTestFolder();
    contentIndexer->enqueueUnprocessed();
    
    int pending = contentIndexer->getPendingCount();
    EXPECT_GE(pending, 3);
}

// ═══════════════════════════════════════════════════════════
// Restart Tests (the bug we fixed!)
// ═══════════════════════════════════════════════════════════

TEST_F(ContentIndexerTest, RestartAfterStop) {
    indexTestFolder();
    contentIndexer->enqueueUnprocessed();
    
    // Start and immediately stop
    contentIndexer->start();
    EXPECT_TRUE(contentIndexer->isRunning());
    contentIndexer->stop(true);
    EXPECT_FALSE(contentIndexer->isRunning());
    
    // Should be able to restart
    contentIndexer->start();
    EXPECT_TRUE(contentIndexer->isRunning());
    
    contentIndexer->stop(true);
}

TEST_F(ContentIndexerTest, MultipleStartStopCycles) {
    for (int i = 0; i < 5; i++) {
        contentIndexer->start();
        EXPECT_TRUE(contentIndexer->isRunning()) << "Cycle " << i << " start failed";
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        contentIndexer->stop(true);
        EXPECT_FALSE(contentIndexer->isRunning()) << "Cycle " << i << " stop failed";
    }
}

TEST_F(ContentIndexerTest, StopWithWaitTrue) {
    // Test that stop(true) waits for complete shutdown
    indexTestFolder();
    contentIndexer->enqueueUnprocessed();
    
    contentIndexer->start();
    EXPECT_TRUE(contentIndexer->isRunning());
    
    // Stop with wait=true should block until fully stopped
    contentIndexer->stop(true);
    
    // After stop(true) returns, isRunning must be false
    EXPECT_FALSE(contentIndexer->isRunning());
}

// ═══════════════════════════════════════════════════════════
// Concurrent Access Tests
// ═══════════════════════════════════════════════════════════

TEST_F(ContentIndexerTest, SequentialStartStop) {
    // Test sequential start/stop cycles (simpler than concurrent)
    for (int i = 0; i < 3; i++) {
        contentIndexer->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        contentIndexer->stop(true);
    }
    
    EXPECT_FALSE(contentIndexer->isRunning());
}


