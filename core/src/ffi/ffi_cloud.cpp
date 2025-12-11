#include "ffi_internal.h"
#include "familyvault/CloudAccountManager.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>

using json = nlohmann::json;
using namespace FamilyVault;

namespace {

std::optional<std::string> toOptional(const char* value) {
    if (!value) {
        return std::nullopt;
    }
    return std::string(value);
}

json cloudAccountToJson(const CloudAccount& account) {
    json j;
    j["id"] = account.id;
    j["type"] = account.type;
    j["email"] = account.email;
    j["displayName"] = account.displayName ? json(*account.displayName) : json(nullptr);
    j["avatarUrl"] = account.avatarUrl ? json(*account.avatarUrl) : json(nullptr);
    j["changeToken"] = account.changeToken ? json(*account.changeToken) : json(nullptr);
    j["lastSyncAt"] = account.lastSyncAt ? json(*account.lastSyncAt) : json(nullptr);
    j["fileCount"] = account.fileCount;
    j["enabled"] = account.enabled;
    j["createdAt"] = account.createdAt;
    return j;
}

json cloudFolderToJson(const CloudWatchedFolder& folder) {
    json j;
    j["id"] = folder.id;
    j["accountId"] = folder.accountId;
    j["cloudId"] = folder.cloudId;
    j["name"] = folder.name;
    j["path"] = folder.path ? json(*folder.path) : json(nullptr);
    j["enabled"] = folder.enabled;
    j["lastSyncAt"] = folder.lastSyncAt ? json(*folder.lastSyncAt) : json(nullptr);
    return j;
}

FVError mapDatabaseException(const DatabaseException& ex) {
    std::string_view message = ex.what();
    if (message.find("not found") != std::string_view::npos) {
        return FV_ERROR_NOT_FOUND;
    }
    if (message.find("UNIQUE") != std::string_view::npos) {
        return FV_ERROR_ALREADY_EXISTS;
    }
    return FV_ERROR_DATABASE;
}

} // namespace

extern "C" {

int64_t fv_cloud_account_add(FVDatabase db,
                             const char* type,
                             const char* email,
                             const char* display_name,
                             const char* avatar_url) {
    if (!db || !type || !email) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Database, type and email are required");
        return -1;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        auto account = manager.addAccount(
            type,
            email,
            toOptional(display_name),
            toOptional(avatar_url));

        setLastError(FV_OK);
        return account.id;
    } catch (const DatabaseException& ex) {
        setLastError(mapDatabaseException(ex), ex.what());
        return -1;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_INTERNAL, ex.what());
        return -1;
    }
}

char* fv_cloud_account_list(FVDatabase db) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return nullptr;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        auto accounts = manager.getAccounts();
        json arr = json::array();
        for (const auto& account : accounts) {
            arr.push_back(cloudAccountToJson(account));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return nullptr;
    }
}

bool fv_cloud_account_remove(FVDatabase db, int64_t account_id) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        bool removed = manager.removeAccount(account_id);
        if (!removed) {
            setLastError(FV_ERROR_NOT_FOUND, "Cloud account not found");
            return false;
        }
        setLastError(FV_OK);
        return true;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

bool fv_cloud_account_set_enabled(FVDatabase db, int64_t account_id, bool enabled) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        bool updated = manager.setAccountEnabled(account_id, enabled);
        if (!updated) {
            setLastError(FV_ERROR_NOT_FOUND, "Cloud account not found");
            return false;
        }
        setLastError(FV_OK);
        return true;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

bool fv_cloud_account_update_sync(FVDatabase db,
                                  int64_t account_id,
                                  int64_t file_count,
                                  const char* change_token) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        bool updated = manager.updateSyncState(
            account_id,
            file_count,
            toOptional(change_token));
        if (!updated) {
            setLastError(FV_ERROR_NOT_FOUND, "Cloud account not found");
            return false;
        }
        setLastError(FV_OK);
        return true;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

int64_t fv_cloud_folder_add(FVDatabase db,
                            int64_t account_id,
                            const char* cloud_id,
                            const char* name,
                            const char* path) {
    if (!db || !cloud_id || !name) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Database, cloudId and name are required");
        return -1;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        auto folder = manager.addWatchedFolder(
            account_id,
            cloud_id,
            name,
            toOptional(path));
        setLastError(FV_OK);
        return folder.id;
    } catch (const DatabaseException& ex) {
        setLastError(mapDatabaseException(ex), ex.what());
        return -1;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return -1;
    }
}

bool fv_cloud_folder_remove(FVDatabase db, int64_t folder_id) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        bool removed = manager.removeWatchedFolder(folder_id);
        if (!removed) {
            setLastError(FV_ERROR_NOT_FOUND, "Cloud folder not found");
            return false;
        }
        setLastError(FV_OK);
        return true;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

char* fv_cloud_folder_list(FVDatabase db, int64_t account_id) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return nullptr;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        auto folders = manager.getWatchedFolders(account_id);
        json arr = json::array();
        for (const auto& folder : folders) {
            arr.push_back(cloudFolderToJson(folder));
        }
        setLastError(FV_OK);
        return alloc_string(arr.dump());
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return nullptr;
    }
}

bool fv_cloud_folder_set_enabled(FVDatabase db, int64_t folder_id, bool enabled) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Null database handle");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        bool updated = manager.setWatchedFolderEnabled(folder_id, enabled);
        if (!updated) {
            setLastError(FV_ERROR_NOT_FOUND, "Cloud folder not found");
            return false;
        }
        setLastError(FV_OK);
        return true;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

bool fv_cloud_file_upsert(FVDatabase db, int64_t account_id, const char* file_json) {
    if (!db || !file_json) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Database and file JSON are required");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        
        json j = json::parse(file_json);
        CloudFile file;
        
        // Required fields
        file.cloudId = j["cloudId"].get<std::string>();
        file.name = j["name"].get<std::string>();
        file.mimeType = j.value("mimeType", "application/octet-stream");
        
        // Optional fields
        file.size = j.value("size", 0LL);
        file.createdAt = j.value("createdAt", 0LL);
        file.modifiedAt = j.value("modifiedAt", 0LL);
        
        if (j.contains("parentCloudId") && !j["parentCloudId"].is_null()) {
            file.parentCloudId = j["parentCloudId"].get<std::string>();
        }
        if (j.contains("path") && !j["path"].is_null()) {
            file.path = j["path"].get<std::string>();
        }
        if (j.contains("thumbnailUrl") && !j["thumbnailUrl"].is_null()) {
            file.thumbnailUrl = j["thumbnailUrl"].get<std::string>();
        }
        if (j.contains("webViewUrl") && !j["webViewUrl"].is_null()) {
            file.webViewUrl = j["webViewUrl"].get<std::string>();
        }
        if (j.contains("checksum") && !j["checksum"].is_null()) {
            file.checksum = j["checksum"].get<std::string>();
        }
        file.indexedAt = j.value("indexedAt", 0LL);

        bool result = manager.upsertCloudFile(account_id, file);
        if (!result) {
            setLastError(FV_ERROR_INTERNAL, "Failed to upsert cloud file");
            return false;
        }
        
        setLastError(FV_OK);
        return true;
    } catch (const json::exception& ex) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, std::string("JSON parse error: ") + ex.what());
        return false;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

bool fv_cloud_file_remove(FVDatabase db, int64_t account_id, const char* cloud_id) {
    if (!db || !cloud_id) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Database and cloudId are required");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        bool removed = manager.removeCloudFile(account_id, cloud_id);
        
        // It's okay if it doesn't exist, we just want it gone. 
        // But removeCloudFile returns false if 0 rows affected.
        // Let's treat it as success for the caller, or just follow C++ API behavior.
        // Usually remove APIs return false if nothing found.
        
        setLastError(FV_OK);
        return removed;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

bool fv_cloud_file_remove_all(FVDatabase db, int64_t account_id) {
    if (!db) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "Database is required");
        return false;
    }

    try {
        auto* holder = reinterpret_cast<DatabaseHolder*>(db);
        CloudAccountManager manager(holder->getDatabase());
        bool removed = manager.removeAllCloudFiles(account_id);
        setLastError(FV_OK);
        return removed;
    } catch (const std::exception& ex) {
        setLastError(FV_ERROR_DATABASE, ex.what());
        return false;
    }
}

} // extern "C"
