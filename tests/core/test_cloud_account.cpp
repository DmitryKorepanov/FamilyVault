#include <gtest/gtest.h>
#include "familyvault/CloudAccountManager.h"
#include "familyvault/Database.h"
#include <filesystem>
#include <memory>
#include <chrono>
#include <thread>
#include <optional>
#include <string>
#include <cstdint>

namespace fs = std::filesystem;
using namespace FamilyVault;

class CloudAccountManagerTest : public ::testing::Test {
protected:
    std::string testDbPath;
    std::shared_ptr<Database> db;
    std::unique_ptr<CloudAccountManager> manager;

    void SetUp() override {
        // Use timestamp + counter for unique DB names to avoid collisions
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        static int counter = 0;
        testDbPath = "test_cloud_account_" + std::to_string(now) + "_" + std::to_string(counter++) + ".db";
        
        // Ensure no leftover from previous failed runs
        cleanupDb(testDbPath);
        
        db = std::make_shared<Database>(testDbPath);
        db->initialize();
        manager = std::make_unique<CloudAccountManager>(db);
    }

    void TearDown() override {
        // Explicitly destroy manager first
        manager.reset();
        
        // Force database closure by resetting shared_ptr
        // This ensures all prepared statements are finalized
        db.reset();
        
        // Small delay to ensure file handles are released (Windows specific)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Now safe to delete files
        cleanupDb(testDbPath);
    }

    static void cleanupDb(const std::string& path) {
        if (fs::exists(path)) fs::remove(path);
        if (fs::exists(path + "-wal")) fs::remove(path + "-wal");
        if (fs::exists(path + "-shm")) fs::remove(path + "-shm");
    }
};

TEST_F(CloudAccountManagerTest, AddAccountPersistsMetadata) {
    auto account = manager->addAccount(
        "google_drive",
        "user@example.com",
        std::make_optional<std::string>("User"),
        std::make_optional<std::string>("https://avatar"));
    EXPECT_GT(account.id, 0);

    auto accounts = manager->getAccounts();
    ASSERT_EQ(accounts.size(), 1);
    EXPECT_EQ(accounts[0].email, "user@example.com");
    ASSERT_TRUE(accounts[0].displayName.has_value());
    EXPECT_EQ(accounts[0].displayName.value(), "User");
    ASSERT_TRUE(accounts[0].avatarUrl.has_value());
}

TEST_F(CloudAccountManagerTest, DuplicateAccountRejected) {
    manager->addAccount("google_drive", "dup@example.com", std::nullopt, std::nullopt);
    EXPECT_THROW(manager->addAccount("google_drive", "dup@example.com", std::nullopt, std::nullopt),
                 DatabaseException);
}

TEST_F(CloudAccountManagerTest, UpdateSyncStateStoresCounters) {
    auto account = manager->addAccount("google_drive", "sync@example.com", std::nullopt, std::nullopt);
    bool updated = manager->updateSyncState(account.id, 42LL, std::make_optional<std::string>("token-1"));
    EXPECT_TRUE(updated);

    auto refreshed = manager->getAccount(account.id);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_EQ(refreshed->fileCount, 42LL);
    ASSERT_TRUE(refreshed->changeToken.has_value());
    EXPECT_EQ(refreshed->changeToken.value(), "token-1");
}

TEST_F(CloudAccountManagerTest, WatchedFolderLifecycle) {
    auto account = manager->addAccount("google_drive", "folders@example.com", std::nullopt, std::nullopt);
    auto folder = manager->addWatchedFolder(
        account.id,
        "folderId",
        "Photos",
        std::make_optional<std::string>("My Drive/Photos"));
    EXPECT_GT(folder.id, 0);

    auto folders = manager->getWatchedFolders(account.id);
    ASSERT_EQ(folders.size(), 1);
    EXPECT_EQ(folders[0].cloudId, "folderId");

    bool disabled = manager->setWatchedFolderEnabled(folder.id, false);
    EXPECT_TRUE(disabled);

    bool removed = manager->removeWatchedFolder(folder.id);
    EXPECT_TRUE(removed);
    EXPECT_TRUE(manager->getWatchedFolders(account.id).empty());
}

TEST_F(CloudAccountManagerTest, RemovingAccountRemovesFolders) {
    auto account = manager->addAccount("google_drive", "cascade@example.com", std::nullopt, std::nullopt);
    manager->addWatchedFolder(account.id, "folder", "Docs", std::nullopt);

    bool removed = manager->removeAccount(account.id);
    EXPECT_TRUE(removed);

    // Removing again should report not found
    EXPECT_FALSE(manager->removeAccount(account.id));
}

TEST_F(CloudAccountManagerTest, SetAccountEnabledTogglesState) {
    auto account = manager->addAccount("google_drive", "toggle@example.com", std::nullopt, std::nullopt);
    EXPECT_TRUE(manager->setAccountEnabled(account.id, false));

    auto refreshed = manager->getAccount(account.id);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_FALSE(refreshed->enabled);

    EXPECT_TRUE(manager->setAccountEnabled(account.id, true));
    refreshed = manager->getAccount(account.id);
    ASSERT_TRUE(refreshed.has_value());
    EXPECT_TRUE(refreshed->enabled);
}

// ═══════════════════════════════════════════════════════════
// Migration Tests
// ═══════════════════════════════════════════════════════════

// Helper function for cleanup
namespace {
void cleanupTestDb(const std::string& path) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (fs::exists(path)) fs::remove(path);
    if (fs::exists(path + "-wal")) fs::remove(path + "-wal");
    if (fs::exists(path + "-shm")) fs::remove(path + "-shm");
}
}

TEST(CloudMigrationTest, Migration3CreatesCloudSchema) {
    // Create temporary database
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string testDbPath = "test_migration3_" + std::to_string(now) + ".db";
    
    // Ensure clean state
    cleanupTestDb(testDbPath);
    
    {
        auto db = std::make_shared<Database>(testDbPath);
        db->initialize();
        
        // Verify cloud_accounts table exists
        auto accountTableExists = db->queryScalar(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cloud_accounts'"
        );
        EXPECT_EQ(accountTableExists, 1LL);
        
        // Verify cloud_watched_folders table exists
        auto folderTableExists = db->queryScalar(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cloud_watched_folders'"
        );
        EXPECT_EQ(folderTableExists, 1LL);
        
        // Verify cloud_files table exists
        auto filesTableExists = db->queryScalar(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cloud_files'"
        );
        EXPECT_EQ(filesTableExists, 1LL);
        
        // Verify cloud_files_fts table exists
        auto ftsTableExists = db->queryScalar(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='cloud_files_fts'"
        );
        EXPECT_EQ(ftsTableExists, 1LL);
        
        // Verify indexes exist
        auto indexCount = db->queryScalar(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name LIKE 'idx_cloud_files_%'"
        );
        EXPECT_EQ(indexCount, 3LL); // idx_cloud_files_account, _name, _modified
        
        // Verify FTS triggers exist
        auto triggerCount = db->queryScalar(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='trigger' AND name LIKE 'cloud_files_fts_%'"
        );
        EXPECT_EQ(triggerCount, 3LL); // insert, delete, update triggers
    }
    
    // Cleanup
    cleanupTestDb(testDbPath);
}

TEST(CloudMigrationTest, FTSTriggersWorkCorrectly) {
    // Create temporary database
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string testDbPath = "test_fts_" + std::to_string(now) + ".db";
    
    // Ensure clean state
    cleanupTestDb(testDbPath);
    
    {
        auto db = std::make_shared<Database>(testDbPath);
        db->initialize();
        
        // Create account first
        db->execute(
            "INSERT INTO cloud_accounts (type, email) VALUES (?, ?)",
            "test_provider", "test@example.com"
        );
        int64_t accountId = db->lastInsertId();
        
        // Insert a cloud file
        db->execute(
            R"SQL(
            INSERT INTO cloud_files (account_id, cloud_id, name, path, mime_type, size)
            VALUES (?, ?, ?, ?, ?, ?)
            )SQL",
            accountId, "file1", "test_document.pdf", "Documents/test_document.pdf", 
            "application/pdf", static_cast<int64_t>(12345)
        );
        int64_t fileId = db->lastInsertId();
        
        // Verify FTS entry was created by trigger
        auto ftsCount = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_files_fts WHERE rowid = ?",
            fileId
        );
        EXPECT_EQ(ftsCount, 1LL);
        
        // Verify FTS search works
        auto searchResults = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_files_fts WHERE cloud_files_fts MATCH 'test_document'"
        );
        EXPECT_EQ(searchResults, 1LL);
        
        // Update file name
        db->execute(
            "UPDATE cloud_files SET name = ?, path = ? WHERE id = ?",
            "renamed_file.pdf", "Documents/renamed_file.pdf", fileId
        );
        
        // Verify FTS was updated
        auto updatedSearch = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_files_fts WHERE cloud_files_fts MATCH 'renamed_file'"
        );
        EXPECT_EQ(updatedSearch, 1LL);
        
        // Old name should not match
        auto oldSearch = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_files_fts WHERE cloud_files_fts MATCH 'test_document'"
        );
        EXPECT_EQ(oldSearch, 0LL);
        
        // Delete file
        db->execute("DELETE FROM cloud_files WHERE id = ?", fileId);
        
        // Verify FTS entry was deleted
        auto deletedCount = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_files_fts WHERE rowid = ?",
            fileId
        );
        EXPECT_EQ(deletedCount, 0LL);
    }
    
    // Cleanup
    cleanupTestDb(testDbPath);
}

TEST(CloudMigrationTest, ForeignKeyCascadeWorks) {
    // Create temporary database
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string testDbPath = "test_cascade_" + std::to_string(now) + ".db";
    
    // Ensure clean state
    cleanupTestDb(testDbPath);
    
    {
        auto db = std::make_shared<Database>(testDbPath);
        db->initialize();
        CloudAccountManager manager(db);
        
        // Add account with folders
        auto account = manager.addAccount("test_type", "cascade@example.com", std::nullopt, std::nullopt);
        manager.addWatchedFolder(account.id, "folder1", "Folder 1", std::nullopt);
        manager.addWatchedFolder(account.id, "folder2", "Folder 2", std::nullopt);
        
        // Add cloud files
        db->execute(
            "INSERT INTO cloud_files (account_id, cloud_id, name) VALUES (?, ?, ?)",
            account.id, "file1", "test1.txt"
        );
        db->execute(
            "INSERT INTO cloud_files (account_id, cloud_id, name) VALUES (?, ?, ?)",
            account.id, "file2", "test2.txt"
        );
        
        // Verify data exists
        auto folderCount = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_watched_folders WHERE account_id = ?",
            account.id
        );
        EXPECT_EQ(folderCount, 2LL);
        
        auto fileCount = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_files WHERE account_id = ?",
            account.id
        );
        EXPECT_EQ(fileCount, 2LL);
        
        // Delete account - should cascade delete folders and files
        manager.removeAccount(account.id);
        
        // Verify all related data is deleted
        auto remainingFolders = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_watched_folders WHERE account_id = ?",
            account.id
        );
        EXPECT_EQ(remainingFolders, 0LL);
        
        auto remainingFiles = db->queryScalar(
            "SELECT COUNT(*) FROM cloud_files WHERE account_id = ?",
            account.id
        );
        EXPECT_EQ(remainingFiles, 0LL);
    }
    
    // Cleanup
    cleanupTestDb(testDbPath);
}
