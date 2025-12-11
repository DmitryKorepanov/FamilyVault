#include <gtest/gtest.h>
#include "familyvault/CloudAccountManager.h"
#include "familyvault/SearchEngine.h"
#include "familyvault/Database.h"
#include "familyvault/Models.h"
#include <filesystem>
#include <memory>
#include <chrono>
#include <thread>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace FamilyVault;

class CloudFilesTest : public ::testing::Test {
protected:
    std::string testDbPath;
    std::shared_ptr<Database> db;
    std::unique_ptr<CloudAccountManager> accountManager;
    std::unique_ptr<SearchEngine> searchEngine;
    int64_t accountId;

    void SetUp() override {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        static int counter = 0;
        testDbPath = "test_cloud_files_" + std::to_string(now) + "_" + std::to_string(counter++) + ".db";
        
        if (fs::exists(testDbPath)) fs::remove(testDbPath);
        
        db = std::make_shared<Database>(testDbPath);
        db->initialize();
        accountManager = std::make_unique<CloudAccountManager>(db);
        searchEngine = std::make_unique<SearchEngine>(db);

        // Setup a test account
        auto account = accountManager->addAccount("google_drive", "test@example.com", 
                                                 std::make_optional<std::string>("Test User"));
        accountId = account.id;
    }

    void TearDown() override {
        searchEngine.reset();
        accountManager.reset();
        db.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (fs::exists(testDbPath)) fs::remove(testDbPath);
        if (fs::exists(testDbPath + "-wal")) fs::remove(testDbPath + "-wal");
        if (fs::exists(testDbPath + "-shm")) fs::remove(testDbPath + "-shm");
    }
};

TEST_F(CloudFilesTest, UpsertCloudFile) {
    CloudFile file;
    file.cloudId = "file1";
    file.name = "test_image.jpg";
    file.mimeType = "image/jpeg";
    file.size = 1024;
    file.modifiedAt = 1000;
    file.path = "Photos/test_image.jpg";

    EXPECT_TRUE(accountManager->upsertCloudFile(accountId, file));

    // Verify it's in DB
    int count = db->queryScalar("SELECT COUNT(*) FROM cloud_files WHERE cloud_id = ?", std::string("file1"));
    EXPECT_EQ(count, 1);

    // Verify extension and content_type were derived
    auto record = db->queryOne<std::pair<std::string, int>>(
        "SELECT extension, content_type FROM cloud_files WHERE cloud_id = ?",
        [](sqlite3_stmt* stmt) {
            return std::make_pair(Database::getString(stmt, 0), Database::getInt(stmt, 1));
        },
        std::string("file1")
    );
    
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->first, "jpg");
    EXPECT_EQ(static_cast<ContentType>(record->second), ContentType::Image);
}

TEST_F(CloudFilesTest, UpsertCloudFileUpdatesExisting) {
    CloudFile file;
    file.cloudId = "file1";
    file.name = "test.txt";
    file.mimeType = "text/plain";
    file.size = 100;
    
    EXPECT_TRUE(accountManager->upsertCloudFile(accountId, file));

    // Update
    file.name = "updated.txt";
    file.size = 200;
    EXPECT_TRUE(accountManager->upsertCloudFile(accountId, file));

    // Verify update
    auto name = db->queryOne<std::string>("SELECT name FROM cloud_files WHERE cloud_id = ?", 
        [](sqlite3_stmt* stmt) {
            return Database::getString(stmt, 0);
        }, 
        std::string("file1")).value_or("");
    EXPECT_EQ(name, "updated.txt");
    
    int64_t size = db->queryScalar("SELECT size FROM cloud_files WHERE cloud_id = ?", std::string("file1"));
    EXPECT_EQ(size, 200);
}

TEST_F(CloudFilesTest, RemoveCloudFile) {
    CloudFile file;
    file.cloudId = "file1";
    file.name = "test.txt";
    accountManager->upsertCloudFile(accountId, file);

    EXPECT_TRUE(accountManager->removeCloudFile(accountId, "file1"));
    
    int count = db->queryScalar("SELECT COUNT(*) FROM cloud_files WHERE cloud_id = ?", std::string("file1"));
    EXPECT_EQ(count, 0);
}

TEST_F(CloudFilesTest, RemoveAllCloudFiles) {
    CloudFile file1;
    file1.cloudId = "file1";
    file1.name = "test1.txt";
    accountManager->upsertCloudFile(accountId, file1);

    CloudFile file2;
    file2.cloudId = "file2";
    file2.name = "test2.txt";
    accountManager->upsertCloudFile(accountId, file2);

    EXPECT_TRUE(accountManager->removeAllCloudFiles(accountId));
    
    int count = db->queryScalar("SELECT COUNT(*) FROM cloud_files WHERE account_id = ?", accountId);
    EXPECT_EQ(count, 0);
}

TEST_F(CloudFilesTest, UnifiedSearchIncludesCloudFiles) {
    // Add a local file
    // We need to add a watched folder first
    db->execute("INSERT INTO watched_folders (path, name) VALUES (?, ?)", "/tmp/local", "Local");
    int64_t folderId = db->lastInsertId();
    db->execute(
        "INSERT INTO files (folder_id, relative_path, name, size, modified_at, extension, content_type) VALUES (?, ?, ?, ?, ?, ?, ?)",
        folderId, "local.txt", "local.txt", 100, 1000, "txt", static_cast<int>(ContentType::Document)
    );

    // Add a cloud file
    CloudFile cFile;
    cFile.cloudId = "cloud1";
    cFile.name = "cloud.txt";
    cFile.mimeType = "text/plain";
    cFile.size = 200;
    cFile.modifiedAt = 2000;
    accountManager->upsertCloudFile(accountId, cFile);

    // Search for ".txt" files implicitly via extension filter or just list all
    SearchQuery query;
    query.limit = 100;
    
    auto results = searchEngine->search(query);
    // Should have 2 results
    EXPECT_EQ(results.size(), 2);
    
    bool foundLocal = false;
    bool foundCloud = false;
    
    for (const auto& r : results) {
        if (r.file.name == "local.txt") {
            foundLocal = true;
            EXPECT_TRUE(r.file.isLocal());
            EXPECT_FALSE(r.file.isRemote);
        } else if (r.file.name == "cloud.txt") {
            foundCloud = true;
            EXPECT_FALSE(r.file.isLocal());
            EXPECT_TRUE(r.file.isRemote);
            EXPECT_EQ(r.file.cloudId, "cloud1");
            EXPECT_EQ(r.file.cloudAccountId, accountId);
        }
    }
    
    EXPECT_TRUE(foundLocal);
    EXPECT_TRUE(foundCloud);
}

TEST_F(CloudFilesTest, UnifiedSearchTextSearch) {
    // Add local file
    db->execute("INSERT INTO watched_folders (path, name) VALUES (?, ?)", "/tmp/local", "Local");
    int64_t folderId = db->lastInsertId();
    db->execute(
        "INSERT INTO files (folder_id, relative_path, name, size, extension, content_type) VALUES (?, ?, ?, ?, ?, ?)",
        folderId, "vacation_local.jpg", "vacation_local.jpg", 100, "jpg", 1
    );

    // Add cloud file
    CloudFile cFile;
    cFile.cloudId = "cloud1";
    cFile.name = "vacation_cloud.jpg";
    cFile.mimeType = "image/jpeg";
    accountManager->upsertCloudFile(accountId, cFile);

    // Search for "vacation"
    SearchQuery query;
    query.text = "vacation";
    
    auto results = searchEngine->search(query);
    EXPECT_EQ(results.size(), 2);
}

TEST_F(CloudFilesTest, UnifiedSearchFilters) {
    // Local: Document
    db->execute("INSERT INTO watched_folders (path, name) VALUES (?, ?)", "/tmp/local", "Local");
    int64_t folderId = db->lastInsertId();
    db->execute(
        "INSERT INTO files (folder_id, relative_path, name, size, extension, content_type) VALUES (?, ?, ?, ?, ?, ?)",
        folderId, "doc.pdf", "doc.pdf", 100, "pdf", static_cast<int>(ContentType::Document)
    );

    // Cloud: Image
    CloudFile cFile;
    cFile.cloudId = "cloud1";
    cFile.name = "img.jpg";
    cFile.mimeType = "image/jpeg"; // Will be detected as ContentType::Image
    accountManager->upsertCloudFile(accountId, cFile);

    // Filter by Image
    SearchQuery query;
    query.contentType = ContentType::Image;
    
    auto results = searchEngine->search(query);
    // Should find the image, but maybe local one too if it was an image?
    // Local was doc.pdf (Document). Cloud was img.jpg (Image).
    // The previous test setup inserted a local "vacation_local.jpg" in a different test function.
    // This is a new test function, DB is fresh.
    
    EXPECT_EQ(results.size(), 1);
    if (!results.empty()) {
        EXPECT_EQ(results[0].file.name, "img.jpg");
    }
    
    // Filter by Document
    query.contentType = ContentType::Document;
    results = searchEngine->search(query);
    EXPECT_EQ(results.size(), 1);
    if (!results.empty()) {
        EXPECT_EQ(results[0].file.name, "doc.pdf");
    }
}

TEST_F(CloudFilesTest, SuggestIncludesCloudFiles) {
    // Add cloud file
    CloudFile cFile;
    cFile.cloudId = "cloud1";
    cFile.name = "unique_cloud_filename.txt";
    cFile.mimeType = "text/plain";
    accountManager->upsertCloudFile(accountId, cFile);

    auto suggestions = searchEngine->suggest("unique_cloud", 10);
    EXPECT_FALSE(suggestions.empty());
    EXPECT_EQ(suggestions[0], "unique_cloud_filename.txt");
}
