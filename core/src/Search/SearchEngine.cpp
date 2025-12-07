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
        sql << "SELECT COUNT(*) ";
    } else {
        sql << R"(
            SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
                   f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
                   f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
                   f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
        )";

        // Добавляем score если есть текстовый запрос
        if (!query.text.empty()) {
            sql << ", bm25(files_fts) as score ";
        } else {
            sql << ", 0.0 as score ";
        }
    }

    sql << " FROM files f ";
    sql << " JOIN watched_folders wf ON f.folder_id = wf.id ";

    // JOIN с FTS если есть текстовый запрос
    if (!query.text.empty()) {
        sql << " JOIN files_fts fts ON fts.rowid = f.id ";
    }

    // WHERE clause
    std::vector<std::string> conditions;

    // FTS поиск с parameter binding
    if (!query.text.empty()) {
        conditions.push_back("files_fts MATCH ?");
        params.push_back(escapeFtsQuery(query.text) + "*");
    }

    // Фильтр по типу контента
    if (query.contentType) {
        conditions.push_back("f.content_type = ?");
        params.push_back(static_cast<int>(*query.contentType));
    }

    // Фильтр по расширению с parameter binding
    if (query.extension) {
        conditions.push_back("f.extension = ?");
        params.push_back(*query.extension);
    }

    // Фильтр по папке
    if (query.folderId) {
        conditions.push_back("f.folder_id = ?");
        params.push_back(*query.folderId);
    }

    // Фильтр по дате
    if (query.dateFrom) {
        conditions.push_back("f.modified_at >= ?");
        params.push_back(*query.dateFrom);
    }
    if (query.dateTo) {
        conditions.push_back("f.modified_at <= ?");
        params.push_back(*query.dateTo);
    }

    // Фильтр по размеру
    if (query.minSize) {
        conditions.push_back("f.size >= ?");
        params.push_back(*query.minSize);
    }
    if (query.maxSize) {
        conditions.push_back("f.size <= ?");
        params.push_back(*query.maxSize);
    }

    // Фильтр по видимости
    if (query.visibility) {
        conditions.push_back("COALESCE(f.visibility, wf.visibility) = ?");
        params.push_back(static_cast<int>(*query.visibility));
    }

    // Локальные/удалённые
    if (!query.includeRemote) {
        conditions.push_back("f.is_remote = 0");
    }

    // Теги (все должны присутствовать) с parameter binding
    for (const auto& tag : query.tags) {
        conditions.push_back(
            "EXISTS (SELECT 1 FROM file_tags ft JOIN tags t ON ft.tag_id = t.id "
            "WHERE ft.file_id = f.id AND t.name = ?)"
        );
        params.push_back(tag);
    }

    // Исключённые теги с parameter binding
    for (const auto& tag : query.excludeTags) {
        conditions.push_back(
            "NOT EXISTS (SELECT 1 FROM file_tags ft JOIN tags t ON ft.tag_id = t.id "
            "WHERE ft.file_id = f.id AND t.name = ?)"
        );
        params.push_back(tag);
    }

    // Собираем WHERE
    if (!conditions.empty()) {
        sql << " WHERE ";
        for (size_t i = 0; i < conditions.size(); ++i) {
            if (i > 0) sql << " AND ";
            sql << conditions[i];
        }
    }

    // ORDER BY (только для не-count запросов)
    if (!countOnly) {
        sql << " ORDER BY ";
        switch (query.sortBy) {
            case SortBy::Name:
                sql << "f.name " << (query.sortAsc ? "ASC" : "DESC");
                break;
            case SortBy::Date:
                sql << "f.modified_at " << (query.sortAsc ? "ASC" : "DESC");
                break;
            case SortBy::Size:
                sql << "f.size " << (query.sortAsc ? "ASC" : "DESC");
                break;
            case SortBy::Relevance:
            default:
                if (!query.text.empty()) {
                    sql << "score ASC"; // BM25 возвращает отрицательные числа
                } else {
                    sql << "f.modified_at DESC";
                }
                break;
        }

        // LIMIT/OFFSET с parameter binding
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
            r.score = Database::getDouble(stmt, 17);

            // Генерируем snippet если есть текстовый запрос
            if (!query.text.empty()) {
                r.snippet = generateSnippet(r.file.id, query.text);
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

    return m_db->query<std::string>(
        R"SQL(
        SELECT DISTINCT name FROM files_fts
        WHERE files_fts MATCH ?
        LIMIT ?
        )SQL",
        [](sqlite3_stmt* stmt) {
            return Database::getString(stmt, 0);
        },
        escapedPrefix + "*",
        limit
    );
}

std::string SearchEngine::generateSnippet(int64_t fileId, const std::string& query) {
    // Используем FTS5 snippet функцию
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
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
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
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
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
        SELECT f.id, f.folder_id, f.relative_path, f.name, f.extension, f.size,
               f.mime_type, f.content_type, f.checksum, f.created_at, f.modified_at,
               f.indexed_at, COALESCE(f.visibility, wf.visibility) as visibility,
               f.source_device_id, f.is_remote, f.sync_version, f.last_modified_by
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
    r.score = Database::getDouble(stmt, 17);
    return r;
}

SearchResultCompact SearchEngine::mapSearchResultCompact(sqlite3_stmt* stmt) {
    SearchResultCompact r;
    r.file.id = Database::getInt64(stmt, 0);
    // Skip columns 1-2 (folder_id, relative_path)
    r.file.name = Database::getString(stmt, 3);
    r.file.extension = Database::getString(stmt, 4);
    r.file.size = Database::getInt64(stmt, 5);
    // Skip column 6 (mime_type)
    r.file.contentType = static_cast<ContentType>(Database::getInt(stmt, 7));
    // Skip columns 8-9 (checksum, created_at)
    r.file.modifiedAt = Database::getInt64(stmt, 10);
    // Skip columns 11-13 (indexed_at, visibility, source_device_id)
    r.file.isRemote = Database::getInt(stmt, 14) != 0;
    // Skip columns 15-16 (sync_version, last_modified_by)
    r.score = Database::getDouble(stmt, 17);
    r.file.hasThumbnail = false; // TODO: check thumbnail cache
    return r;
}

FileRecord SearchEngine::mapFileRecord(sqlite3_stmt* stmt) {
    FileRecord r;
    r.id = Database::getInt64(stmt, 0);
    r.folderId = Database::getInt64(stmt, 1);
    r.relativePath = Database::getString(stmt, 2);
    r.name = Database::getString(stmt, 3);
    r.extension = Database::getString(stmt, 4);
    r.size = Database::getInt64(stmt, 5);
    r.mimeType = Database::getString(stmt, 6);
    r.contentType = static_cast<ContentType>(Database::getInt(stmt, 7));
    r.checksum = Database::getStringOpt(stmt, 8);
    r.createdAt = Database::getInt64(stmt, 9);
    r.modifiedAt = Database::getInt64(stmt, 10);
    r.indexedAt = Database::getInt64(stmt, 11);
    r.visibility = static_cast<Visibility>(Database::getInt(stmt, 12));
    r.sourceDeviceId = Database::getStringOpt(stmt, 13);
    r.isRemote = Database::getInt(stmt, 14) != 0;
    r.syncVersion = Database::getInt64(stmt, 15);
    r.lastModifiedBy = Database::getStringOpt(stmt, 16);
    return r;
}

} // namespace FamilyVault
