#pragma once

#include "Database.h"
#include "Models.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace FamilyVault {

class CloudAccountManager {
public:
    explicit CloudAccountManager(std::shared_ptr<Database> db);
    ~CloudAccountManager();

    CloudAccount addAccount(const std::string& type,
                            const std::string& email,
                            const std::optional<std::string>& displayName = std::nullopt,
                            const std::optional<std::string>& avatarUrl = std::nullopt);

    std::optional<CloudAccount> getAccount(int64_t accountId) const;
    std::vector<CloudAccount> getAccounts() const;

    bool removeAccount(int64_t accountId);
    bool setAccountEnabled(int64_t accountId, bool enabled);
    bool updateSyncState(int64_t accountId,
                         int64_t fileCount,
                         const std::optional<std::string>& changeToken,
                         std::optional<int64_t> lastSyncAt = std::nullopt);

    CloudWatchedFolder addWatchedFolder(int64_t accountId,
                                        const std::string& cloudId,
                                        const std::string& name,
                                        const std::optional<std::string>& path = std::nullopt);

    std::vector<CloudWatchedFolder> getWatchedFolders(int64_t accountId) const;
    bool removeWatchedFolder(int64_t folderId);
    bool setWatchedFolderEnabled(int64_t folderId, bool enabled);

    bool upsertCloudFile(int64_t accountId, const CloudFile& file);
    bool removeCloudFile(int64_t accountId, const std::string& cloudId);
    bool removeAllCloudFiles(int64_t accountId);

private:
    std::shared_ptr<Database> m_db;

    bool ensureAccountExists(int64_t accountId) const;

    static CloudAccount mapAccount(sqlite3_stmt* stmt);
    static CloudWatchedFolder mapWatchedFolder(sqlite3_stmt* stmt);
};

} // namespace FamilyVault
