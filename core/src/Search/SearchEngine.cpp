#include "familyvault/SearchEngine.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>

namespace FamilyVault {

SearchEngine::SearchEngine(std::shared_ptr<Database> db)
    : m_db(std::move(db)) {
}

SearchEngine::~SearchEngine() = default;

std::string SearchEngine::escapeFtsQuery(const std::string& text) {
    // Экранируем специальные символы FTS5
    std::string result;
    result.reserve(text.size() * 2);

    for (char c : text) {
        switch (c) {
            case '"':
            case '*':
            case '(':
            case ')':
            case ':':
            case '^':
                result += '"';
                result += c;
                result += '"';
                break;
            default:
                result += c;
        }
    }

    return result;
}

SearchQueryBuilt SearchEngine::buildSearchQuery(const SearchQuery& query, bool countOnly) {
    SearchQueryBuilt result;
    std::ostringstream sql;
    std::vector<SqlParam>& params = result.params;

    if (countOnly) {
        sql << "SELECT COUNT(*) FROM (";
    }

    // ═══════════════════════════════════════════════════════════
    // Local Files Query
    // ═══════════════════════════════════════════════════════════

    sql << R"(
        SELECT f.id, f.folder_id, f.relative_path, wf.path as folder_path, 
               f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by,
               NULL as cloud_account_id, NULL as cloud_id, 
               NULL as web_view_url, NULL as thumbnail_url
    )";

    // Score
    if (!query.text.empty()) {
        sql << ", bm25(files_fts) as score ";
        sql << ", snippet(files_fts, 2, '<b>', '</b>', '...', 32) as snippet ";
    } else {
        sql << ", 0.0 as score ";
        sql << ", NULL as snippet ";
    }

    sql << " FROM files f ";
    sql << " JOIN watched_folders wf ON f.folder_id = wf.id ";

    if (!query.text.empty()) {
        sql << " JOIN files_fts fts ON fts.rowid = f.id ";
    }

    std::vector<std::string> localConditions;
    if (!query.text.empty()) {
        localConditions.push_back("files_fts MATCH ?");
        params.push_back(escapeFtsQuery(query.text) + "*");
    }
    if (query.contentType) {
        localConditions.push_back("f.content_type = ?");
        params.push_back(static_cast<int>(*query.contentType));
    }
    if (query.extension) {
        localConditions.push_back("f.extension = ?");
        params.push_back(*query.extension);
    }
    if (query.folderId) {
        localConditions.push_back("f.folder_id = ?");
        params.push_back(*query.folderId);
    }
    if (query.dateFrom) {
        localConditions.push_back("f.modified_at >= ?");
        params.push_back(*query.dateFrom);
    }
    if (query.dateTo) {
        localConditions.push_back("f.modified_at <= ?");
        params.push_back(*query.dateTo);
    }
    if (query.minSize) {
        localConditions.push_back("f.size >= ?");
        params.push_back(*query.minSize);
    }
    if (query.maxSize) {
        localConditions.push_back("f.size <= ?");
        params.push_back(*query.maxSize);
    }
    if (query.visibility) {
        localConditions.push_back("COALESCE(f.visibility, wf.visibility) = ?");
        params.push_back(static_cast<int>(*query.visibility));
    }
    if (!query.includeRemote) {
        localConditions.push_back("f.is_remote = 0");
    }
    for (const auto& tag : query.tags) {
        localConditions.push_back(
            "EXISTS (SELECT 1 FROM file_tags ft JOIN tags t ON ft.tag_id = t.id "
            "WHERE ft.file_id = f.id AND t.name = ?)"
        );
        params.push_back(tag);
    }
    for (const auto& tag : query.excludeTags) {
        localConditions.push_back(
            "NOT EXISTS (SELECT 1 FROM file_tags ft JOIN tags t ON ft.tag_id = t.id "
            "WHERE ft.file_id = f.id AND t.name = ?)"
        );
        params.push_back(tag);
    }

    if (!localConditions.empty()) {
        sql << " WHERE ";
        for (size_t i = 0; i < localConditions.size(); ++i) {
            if (i > 0) sql << " AND ";
            sql << localConditions[i];
        }
    }

    // ═══════════════════════════════════════════════════════════
    // Cloud Files Query (UNION ALL)
    // ═══════════════════════════════════════════════════════════

    // Only include cloud files if filters are compatible
    // And if caller explicitly allowed remote/cloud results (default false)
    bool includeCloud = query.includeRemote;
    
    if (query.folderId) includeCloud = false; // Folder ID is for local watched_folders
    if (query.visibility) includeCloud = false; // Cloud files don't track visibility yet
    if (!query.tags.empty() || !query.excludeTags.empty()) includeCloud = false; // No tags yet
    
    if (includeCloud) {
        sql << " UNION ALL ";
        sql << R"(
            SELECT cf.id, 0 as folder_id, cf.path as relative_path, 
                   COALESCE(cwf.name, '') as folder_path,
                   cf.name, cf.extension, cf.size,
                   cf.mime_type, cf.content_type, cf.checksum, 
                   cf.created_at, cf.modified_at, cf.indexed_at,
                   1 as visibility, NULL as source_device_id, 1 as is_remote,
                   0 as sync_version, NULL as last_modified_by,
                   cf.account_id as cloud_account_id, cf.cloud_id,
                   cf.web_view_url, cf.thumbnail_url
        )";

        if (!query.text.empty()) {
            sql << ", bm25(cloud_files_fts) as score ";
            sql << ", snippet(cloud_files_fts, 0, '<b>', '</b>', '...', 32) as snippet ";
        } else {
            sql << ", 0.0 as score ";
            sql << ", NULL as snippet ";
        }

        sql << " FROM cloud_files cf ";
        sql << " LEFT JOIN cloud_watched_folders cwf ON cf.account_id = cwf.account_id "; 
        
        if (!query.text.empty()) {
            sql << " JOIN cloud_files_fts fts ON fts.rowid = cf.id ";
        }

        std::vector<std::string> cloudConditions;
        if (!query.text.empty()) {
            cloudConditions.push_back("cloud_files_fts MATCH ?");
            params.push_back(escapeFtsQuery(query.text) + "*");
        }
        if (query.contentType) {
            cloudConditions.push_back("cf.content_type = ?");
            params.push_back(static_cast<int>(*query.contentType));
        }
        if (query.extension) {
            cloudConditions.push_back("cf.extension = ?");
            params.push_back(*query.extension);
        }
        if (query.dateFrom) {
            cloudConditions.push_back("cf.modified_at >= ?");
            params.push_back(*query.dateFrom);
        }
        if (query.dateTo) {
            cloudConditions.push_back("cf.modified_at <= ?");
            params.push_back(*query.dateTo);
        }
        if (query.minSize) {
            cloudConditions.push_back("cf.size >= ?");
            params.push_back(*query.minSize);
        }
        if (query.maxSize) {
            cloudConditions.push_back("cf.size <= ?");
            params.push_back(*query.maxSize);
        }

        if (!cloudConditions.empty()) {
            sql << " WHERE ";
            for (size_t i = 0; i < cloudConditions.size(); ++i) {
                if (i > 0) sql << " AND ";
                sql << cloudConditions[i];
            }
        }
    }

    if (countOnly) {
        sql << ") as combined";
    } else {
        sql << " ORDER BY ";
        switch (query.sortBy) {
            case SortBy::Name:
                sql << "name " << (query.sortAsc ? "ASC" : "DESC");
                break;
            case SortBy::Date:
                sql << "modified_at " << (query.sortAsc ? "ASC" : "DESC");
                break;
            case SortBy::Size:
                sql << "size " << (query.sortAsc ? "ASC" : "DESC");
                break;
            case SortBy::Relevance:
            default:
                if (!query.text.empty()) {
                    sql << "score ASC"; 
                } else {
                    sql << "modified_at DESC";
                }
                break;
        }

        sql << " LIMIT ? OFFSET ?";
        params.push_back(query.limit);
        params.push_back(query.offset);
    }

    result.sql = sql.str();
    return result;
}

std::vector<SearchResult> SearchEngine::search(const SearchQuery& query) {
    auto built = buildSearchQuery(query, false);

    spdlog::debug("Search SQL: {}", built.sql);

    auto results = m_db->queryDynamic<SearchResult>(
        built.sql,
        [this, &query](sqlite3_stmt* stmt) {
            SearchResult r;
            r.file = mapFileRecord(stmt);
            // Column indices:
            // 0-21: FileRecord (mapFileRecord consumes these)
            // 22: score
            // 23: snippet (only if text search is active)
            
            // Check if we requested score/snippet (only for text search)
            if (!query.text.empty()) {
                r.score = Database::getDouble(stmt, 22);
                r.snippet = Database::getString(stmt, 23);
            } else {
                r.score = 0.0;
                r.snippet = ""; // Or mapFileRecord's name if needed as fallback
            }
            return r;
        },
        built.params
    );

    spdlog::info("Search '{}': {} results", query.text, results.size());
    return results;
}

std::vector<SearchResultCompact> SearchEngine::searchCompact(const SearchQuery& query) {
    auto built = buildSearchQuery(query, false);

    return m_db->queryDynamic<SearchResultCompact>(
        built.sql,
        [](sqlite3_stmt* stmt) {
            return mapSearchResultCompact(stmt);
        },
        built.params
    );
}

int64_t SearchEngine::countResults(const SearchQuery& query) {
    auto built = buildSearchQuery(query, true);
    return m_db->queryScalarDynamic(built.sql, built.params);
}

std::vector<std::string> SearchEngine::suggest(const std::string& prefix, int limit) {
    if (prefix.empty()) {
        return {};
    }

    std::string escapedPrefix = escapeFtsQuery(prefix);

    // Suggest from both tables?
    // For now stick to local files or UNION?
    // Let's UNION
    
    return m_db->query<std::string>(
        R"SQL(
        SELECT name FROM (
            SELECT DISTINCT name FROM files_fts
            WHERE files_fts MATCH ?
            UNION 
            SELECT DISTINCT name FROM cloud_files_fts
            WHERE cloud_files_fts MATCH ?
        )
        LIMIT ?
        )SQL",
        [](sqlite3_stmt* stmt) {
            return Database::getString(stmt, 0);
        },
        escapedPrefix + "*",
        escapedPrefix + "*",
        limit
    );
}

std::string SearchEngine::generateSnippet(int64_t fileId, const std::string& query) {
    // Only works for local files currently as we need to know the table
    // and this function assumes files_fts
    auto result = m_db->queryOne<std::string>(
        R"SQL(
        SELECT snippet(files_fts, 2, '<b>', '</b>', '...', 32)
        FROM files_fts
        WHERE rowid = ? AND files_fts MATCH ?
        )SQL",
        [](sqlite3_stmt* stmt) {
            return Database::getString(stmt, 0);
        },
        fileId,
        escapeFtsQuery(query) + "*"
    );

    return result.value_or("");
}

std::vector<FileRecord> SearchEngine::getByExtension(const std::string& ext, int limit) {
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return m_db->query<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, wf.path as folder_path,
               f.name, f.extension, f.size, f.mime_type, f.content_type,
               f.checksum, f.created_at, f.modified_at, f.indexed_at,
               COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by,
               NULL, NULL, NULL, NULL
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE LOWER(f.extension) = ?
        ORDER BY f.modified_at DESC
        LIMIT ?
        )SQL",
        mapFileRecord,
        lowerExt, limit
    );
}

std::vector<FileRecord> SearchEngine::getByContentType(ContentType type, int limit) {
    return m_db->query<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, wf.path as folder_path,
               f.name, f.extension, f.size, f.mime_type, f.content_type,
               f.checksum, f.created_at, f.modified_at, f.indexed_at,
               COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by,
               NULL, NULL, NULL, NULL
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        WHERE f.content_type = ?
        ORDER BY f.modified_at DESC
        LIMIT ?
        )SQL",
        mapFileRecord,
        static_cast<int>(type), limit
    );
}

std::vector<FileRecord> SearchEngine::getByTag(const std::string& tag, int limit) {
    return m_db->query<FileRecord>(
        R"SQL(
        SELECT f.id, f.folder_id, f.relative_path, wf.path as folder_path,
               f.name, f.extension, f.size, f.mime_type, f.content_type,
               f.checksum, f.created_at, f.modified_at, f.indexed_at,
               COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by,
               NULL, NULL, NULL, NULL
        FROM files f
        JOIN watched_folders wf ON f.folder_id = wf.id
        JOIN file_tags ft ON ft.file_id = f.id
        JOIN tags t ON ft.tag_id = t.id
        WHERE t.name = ?
        ORDER BY f.modified_at DESC
        LIMIT ?
        )SQL",
        mapFileRecord,
        tag, limit
    );
}

SearchResult SearchEngine::mapSearchResult(sqlite3_stmt* stmt) {
    SearchResult r;
    r.file = mapFileRecord(stmt);
    r.score = Database::getDouble(stmt, 22);
    return r;
}

SearchResultCompact SearchEngine::mapSearchResultCompact(sqlite3_stmt* stmt) {
    SearchResultCompact r;
    r.file.id = Database::getInt64(stmt, 0);
    r.file.folderId = Database::getInt64(stmt, 1);
    r.file.relativePath = Database::getString(stmt, 2);
    r.file.folderPath = Database::getString(stmt, 3);
    r.file.name = Database::getString(stmt, 4);
    r.file.extension = Database::getString(stmt, 5);
    r.file.size = Database::getInt64(stmt, 6);
    // Skip column 7 (mime_type)
    r.file.contentType = static_cast<ContentType>(Database::getInt(stmt, 8));
    // Skip columns 9-10 (checksum, created_at)
    r.file.modifiedAt = Database::getInt64(stmt, 11);
    // Skip columns 12-14 (indexed_at, visibility, source_device_id)
    r.file.isRemote = Database::getInt(stmt, 15) != 0;
    // Skip columns 16-17 (sync_version, last_modified_by)
    // 18-21 (cloud info)
    r.score = Database::getDouble(stmt, 22);
    
    // Determine hasThumbnail
    // Use thumbnailUrl if available (col 21), or check contentType for local files
    std::string thumbUrl = Database::getString(stmt, 21);
    if (!thumbUrl.empty()) {
        r.file.hasThumbnail = true;
    } else {
        r.file.hasThumbnail = r.file.contentType == ContentType::Image;
    }
    
    return r;
}

FileRecord SearchEngine::mapFileRecord(sqlite3_stmt* stmt) {
    FileRecord r;
    r.id = Database::getInt64(stmt, 0);
    r.folderId = Database::getInt64(stmt, 1);
    r.relativePath = Database::getString(stmt, 2);
    r.folderPath = Database::getString(stmt, 3);
    r.name = Database::getString(stmt, 4);
    r.extension = Database::getString(stmt, 5);
    r.size = Database::getInt64(stmt, 6);
    r.mimeType = Database::getString(stmt, 7);
    r.contentType = static_cast<ContentType>(Database::getInt(stmt, 8));
    r.checksum = Database::getStringOpt(stmt, 9);
    r.createdAt = Database::getInt64(stmt, 10);
    r.modifiedAt = Database::getInt64(stmt, 11);
    r.indexedAt = Database::getInt64(stmt, 12);
    r.visibility = static_cast<Visibility>(Database::getInt(stmt, 13));
    r.sourceDeviceId = Database::getStringOpt(stmt, 14);
    r.isRemote = Database::getInt(stmt, 15) != 0;
    r.syncVersion = Database::getInt64(stmt, 16);
    r.lastModifiedBy = Database::getStringOpt(stmt, 17);
    
    // New fields
    r.cloudAccountId = Database::getInt64Opt(stmt, 18);
    r.cloudId = Database::getStringOpt(stmt, 19);
    r.webViewUrl = Database::getStringOpt(stmt, 20);
    r.thumbnailUrl = Database::getStringOpt(stmt, 21);
    
    return r;
}

} // namespace FamilyVault
