// test_database.cpp — тесты Database

#include <gtest/gtest.h>
#include "familyvault/Database.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace FamilyVault;

class DatabaseTest : public ::testing::Test {
protected:
    std::string testDbPath;

    void SetUp() override {
        testDbPath = "test_db_" + std::to_string(std::rand()) + ".db";
    }

    void TearDown() override {
        if (fs::exists(testDbPath)) {
            fs::remove(testDbPath);
        }
        // Удаляем WAL файлы
        fs::remove(testDbPath + "-wal");
        fs::remove(testDbPath + "-shm");
    }
};

TEST_F(DatabaseTest, CreateAndOpenDatabase) {
    {
        Database db(testDbPath);
        EXPECT_TRUE(fs::exists(testDbPath));
    }
    // БД должна закрыться при выходе из scope
    EXPECT_TRUE(fs::exists(testDbPath));
}

TEST_F(DatabaseTest, InitializeCreatesSchemaVersion) {
    Database db(testDbPath);
    db.initialize();

    auto version = db.queryOne<int>(
        "SELECT MAX(version) FROM schema_version",
        [](sqlite3_stmt* stmt) { return Database::getInt(stmt, 0); }
    );

    ASSERT_TRUE(version.has_value());
    EXPECT_GE(*version, 1);
}

TEST_F(DatabaseTest, ExecuteSimpleQuery) {
    Database db(testDbPath);
    db.initialize();

    db.execute("INSERT INTO settings (key, value) VALUES ('test_key', 'test_value')");

    auto value = db.queryOne<std::string>(
        "SELECT value FROM settings WHERE key = ?",
        [](sqlite3_stmt* stmt) { return Database::getString(stmt, 0); },
        "test_key"
    );

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "test_value");
}

TEST_F(DatabaseTest, TransactionCommit) {
    Database db(testDbPath);
    db.initialize();

    {
        Database::Transaction tx(db);
        db.execute("INSERT INTO settings (key, value) VALUES ('tx_key', 'tx_value')");
        tx.commit();
    }

    auto value = db.queryOne<std::string>(
        "SELECT value FROM settings WHERE key = ?",
        [](sqlite3_stmt* stmt) { return Database::getString(stmt, 0); },
        "tx_key"
    );

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "tx_value");
}

TEST_F(DatabaseTest, TransactionRollback) {
    Database db(testDbPath);
    db.initialize();

    {
        Database::Transaction tx(db);
        db.execute("INSERT INTO settings (key, value) VALUES ('rollback_key', 'rollback_value')");
        tx.rollback();
    }

    auto value = db.queryOne<std::string>(
        "SELECT value FROM settings WHERE key = ?",
        [](sqlite3_stmt* stmt) { return Database::getString(stmt, 0); },
        "rollback_key"
    );

    EXPECT_FALSE(value.has_value());
}

TEST_F(DatabaseTest, LastInsertId) {
    Database db(testDbPath);
    db.initialize();

    db.execute("INSERT INTO watched_folders (path, name) VALUES ('/test/path', 'Test')");
    int64_t id1 = db.lastInsertId();

    db.execute("INSERT INTO watched_folders (path, name) VALUES ('/test/path2', 'Test2')");
    int64_t id2 = db.lastInsertId();

    EXPECT_GT(id1, 0);
    EXPECT_GT(id2, id1);
}

TEST_F(DatabaseTest, QueryMultipleRows) {
    Database db(testDbPath);
    db.initialize();

    db.execute("INSERT INTO settings (key, value) VALUES ('key1', 'value1')");
    db.execute("INSERT INTO settings (key, value) VALUES ('key2', 'value2')");
    db.execute("INSERT INTO settings (key, value) VALUES ('key3', 'value3')");

    auto results = db.query<std::string>(
        "SELECT value FROM settings ORDER BY key",
        [](sqlite3_stmt* stmt) { return Database::getString(stmt, 0); }
    );

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0], "value1");
    EXPECT_EQ(results[1], "value2");
    EXPECT_EQ(results[2], "value3");
}

TEST_F(DatabaseTest, TablesCreated) {
    Database db(testDbPath);
    db.initialize();

    auto tables = db.query<std::string>(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
        [](sqlite3_stmt* stmt) { return Database::getString(stmt, 0); }
    );

    // Проверяем основные таблицы
    auto hasTable = [&tables](const std::string& name) {
        return std::find(tables.begin(), tables.end(), name) != tables.end();
    };

    EXPECT_TRUE(hasTable("schema_version"));
    EXPECT_TRUE(hasTable("settings"));
    EXPECT_TRUE(hasTable("watched_folders"));
    EXPECT_TRUE(hasTable("files"));
    EXPECT_TRUE(hasTable("tags"));
    EXPECT_TRUE(hasTable("file_tags"));
    EXPECT_TRUE(hasTable("image_metadata"));
    EXPECT_TRUE(hasTable("file_content"));
}

