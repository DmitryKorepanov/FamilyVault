#include "familyvault/CloudAccountManager.h"
#include "familyvault/MimeTypeDetector.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace FamilyVault {

namespace {
int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

constexpr const char* ACCOUNT_SELECT_SQL = R"SQL(
    SELECT id, type, email, display_name, avatar_url,
           change_token, last_sync_at, file_count, enabled, created_at
    FROM cloud_accounts
)SQL";

constexpr const char* FOLDER_SELECT_SQL = R"SQL(
    SELECT id, account_id, cloud_id, name, path, enabled, last_sync_at
    FROM cloud_watched_folders
)SQL";
} // namespace

CloudAccountManager::CloudAccountManager(std::shared_ptr<Database> db)
    : m_db(std::move(db)) {
    if (!m_db) {
        throw std::invalid_argument("Database instance is required");
    }
}

CloudAccountManager::~CloudAccountManager() = default;

CloudAccount CloudAccountManager::addAccount(const std::string& type,
                                             const std::string& email,
                                             const std::optional<std::string>& displayName,
                                             const std::optional<std::string>& avatarUrl) {
    if (type.empty()) {
        throw std::invalid_argument("Cloud account type is required");
    }
    if (email.empty()) {
        throw std::invalid_argument("Cloud account email is required");
    }

    try {
        m_db->execute(
            R"SQL(
            INSERT INTO cloud_accounts (type, email, display_name, avatar_url)
            VALUES (?, ?, ?, ?)
            )SQL",
            type,
            email,
            displayName ? displayName->c_str() : nullptr,
            avatarUrl ? avatarUrl->c_str() : nullptr);
    } catch (const DatabaseException& ex) {
        // Check if it's a UNIQUE constraint violation
        std::string errorMsg = ex.what();
        if (errorMsg.find("UNIQUE constraint failed") != std::string::npos) {
            throw DatabaseException("Cloud account already exists: " + type + " " + email);
        }
        // Re-throw other database errors
        throw;
    }

    auto account = getAccount(m_db->lastInsertId());
    if (!account) {
        throw DatabaseException("Failed to fetch newly created cloud account");
    }

    spdlog::info("Cloud account added: {} ({})", email, type);
    return *account;
}

std::optional<CloudAccount> CloudAccountManager::getAccount(int64_t accountId) const {
    return m_db->queryOne<CloudAccount>(
        std::string(ACCOUNT_SELECT_SQL) + " WHERE id = ?",
        mapAccount,
        accountId);
}

std::vector<CloudAccount> CloudAccountManager::getAccounts() const {
    return m_db->query<CloudAccount>(
        std::string(ACCOUNT_SELECT_SQL) + " ORDER BY created_at DESC",
        mapAccount);
}

bool CloudAccountManager::removeAccount(int64_t accountId) {
    m_db->execute("DELETE FROM cloud_accounts WHERE id = ?", accountId);
    bool removed = m_db->changesCount() > 0;
    if (removed) {
        spdlog::info("Cloud account {} removed", accountId);
    }
    return removed;
}

bool CloudAccountManager::setAccountEnabled(int64_t accountId, bool enabled) {
    m_db->execute(
        "UPDATE cloud_accounts SET enabled = ? WHERE id = ?",
        enabled ? 1 : 0,
        accountId);
    return m_db->changesCount() > 0;
}

bool CloudAccountManager::updateSyncState(int64_t accountId,
                                          int64_t fileCount,
                                          const std::optional<std::string>& changeToken,
                                          std::optional<int64_t> lastSyncAt) {
    int64_t syncTimestamp = lastSyncAt.value_or(nowSeconds());
    m_db->execute(
        R"SQL(
        UPDATE cloud_accounts
        SET file_count = ?, change_token = ?, last_sync_at = ?
        WHERE id = ?
        )SQL",
        fileCount,
        changeToken ? changeToken->c_str() : nullptr,
        syncTimestamp,
        accountId);
    return m_db->changesCount() > 0;
}

CloudWatchedFolder CloudAccountManager::addWatchedFolder(
    int64_t accountId,
    const std::string& cloudId,
    const std::string& name,
    const std::optional<std::string>& path) {
    if (cloudId.empty()) {
        throw std::invalid_argument("cloudId is required");
    }
    if (name.empty()) {
        throw std::invalid_argument("Folder name is required");
    }

    if (!ensureAccountExists(accountId)) {
        throw DatabaseException("Cloud account not found");
    }

    try {
        m_db->execute(
            R"SQL(
            INSERT INTO cloud_watched_folders (account_id, cloud_id, name, path)
            VALUES (?, ?, ?, ?)
            )SQL",
            accountId,
            cloudId,
            name,
            path ? path->c_str() : nullptr);
    } catch (const DatabaseException& ex) {
        // Check if it's a UNIQUE constraint violation
        std::string errorMsg = ex.what();
        if (errorMsg.find("UNIQUE constraint failed") != std::string::npos) {
            throw DatabaseException("Cloud folder already tracked: " + cloudId);
        }
        // Re-throw other database errors
        throw;
    }

    auto folder = m_db->queryOne<CloudWatchedFolder>(
        std::string(FOLDER_SELECT_SQL) + " WHERE id = ?",
        mapWatchedFolder,
        m_db->lastInsertId());

    if (!folder) {
        throw DatabaseException("Failed to fetch created cloud folder");
    }

    return *folder;
}

std::vector<CloudWatchedFolder> CloudAccountManager::getWatchedFolders(int64_t accountId) const {
    return m_db->query<CloudWatchedFolder>(
        std::string(FOLDER_SELECT_SQL) + " WHERE account_id = ? ORDER BY name",
        mapWatchedFolder,
        accountId);
}

bool CloudAccountManager::removeWatchedFolder(int64_t folderId) {
    m_db->execute("DELETE FROM cloud_watched_folders WHERE id = ?", folderId);
    return m_db->changesCount() > 0;
}

bool CloudAccountManager::setWatchedFolderEnabled(int64_t folderId, bool enabled) {
    m_db->execute(
        "UPDATE cloud_watched_folders SET enabled = ? WHERE id = ?",
        enabled ? 1 : 0,
        folderId);
    return m_db->changesCount() > 0;
}

bool CloudAccountManager::upsertCloudFile(int64_t accountId, const CloudFile& file) {
    if (file.cloudId.empty() || file.name.empty()) {
        throw std::invalid_argument("cloudId and name are required");
    }

    std::string extension = MimeTypeDetector::extractExtension(file.name);
    ContentType contentType = MimeTypeDetector::mimeToContentType(file.mimeType);

    m_db->execute(
        R"SQL(
        INSERT INTO cloud_files (
            account_id, cloud_id, name, mime_type, size, 
            created_at, modified_at, parent_cloud_id, path, 
            thumbnail_url, web_view_url, checksum, indexed_at,
            extension, content_type
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(account_id, cloud_id) DO UPDATE SET
            name = excluded.name,
            mime_type = excluded.mime_type,
            size = excluded.size,
            created_at = excluded.created_at,
            modified_at = excluded.modified_at,
            parent_cloud_id = excluded.parent_cloud_id,
            path = excluded.path,
            thumbnail_url = excluded.thumbnail_url,
            web_view_url = excluded.web_view_url,
            checksum = excluded.checksum,
            indexed_at = excluded.indexed_at,
            extension = excluded.extension,
            content_type = excluded.content_type
        )SQL",
        accountId,
        file.cloudId,
        file.name,
        file.mimeType,
        file.size,
        file.createdAt,
        file.modifiedAt,
        file.parentCloudId ? file.parentCloudId->c_str() : nullptr,
        file.path ? file.path->c_str() : nullptr,
        file.thumbnailUrl ? file.thumbnailUrl->c_str() : nullptr,
        file.webViewUrl ? file.webViewUrl->c_str() : nullptr,
        file.checksum ? file.checksum->c_str() : nullptr,
        file.indexedAt > 0 ? file.indexedAt : nowSeconds(),
        extension,
        static_cast<int>(contentType)
    );

    return true;
}

bool CloudAccountManager::removeCloudFile(int64_t accountId, const std::string& cloudId) {
    m_db->execute(
        "DELETE FROM cloud_files WHERE account_id = ? AND cloud_id = ?",
        accountId, cloudId
    );
    return m_db->changesCount() > 0;
}

bool CloudAccountManager::removeAllCloudFiles(int64_t accountId) {
    m_db->execute(
        "DELETE FROM cloud_files WHERE account_id = ?",
        accountId
    );
    return m_db->changesCount() > 0;
}

bool CloudAccountManager::ensureAccountExists(int64_t accountId) const {
    return m_db->queryScalar(
               "SELECT COUNT(1) FROM cloud_accounts WHERE id = ?",
               accountId) > 0;
}

CloudAccount CloudAccountManager::mapAccount(sqlite3_stmt* stmt) {
    CloudAccount account;
    account.id = Database::getInt64(stmt, 0);
    account.type = Database::getString(stmt, 1);
    account.email = Database::getString(stmt, 2);
    account.displayName = Database::getStringOpt(stmt, 3);
    account.avatarUrl = Database::getStringOpt(stmt, 4);
    account.changeToken = Database::getStringOpt(stmt, 5);
    account.lastSyncAt = Database::getInt64Opt(stmt, 6);
    account.fileCount = Database::getInt64(stmt, 7);
    account.enabled = Database::getInt(stmt, 8) != 0;
    account.createdAt = Database::getInt64(stmt, 9);
    return account;
}

CloudWatchedFolder CloudAccountManager::mapWatchedFolder(sqlite3_stmt* stmt) {
    CloudWatchedFolder folder;
    folder.id = Database::getInt64(stmt, 0);
    folder.accountId = Database::getInt64(stmt, 1);
    folder.cloudId = Database::getString(stmt, 2);
    folder.name = Database::getString(stmt, 3);
    folder.path = Database::getStringOpt(stmt, 4);
    folder.enabled = Database::getInt(stmt, 5) != 0;
    folder.lastSyncAt = Database::getInt64Opt(stmt, 6);
    return folder;
}

} // namespace FamilyVault
