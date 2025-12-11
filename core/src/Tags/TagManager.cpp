#include "familyvault/TagManager.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace FamilyVault {

TagManager::TagManager(std::shared_ptr<Database> db)
    : m_db(std::move(db)) {
}

TagManager::~TagManager() = default;

int64_t TagManager::getOrCreateTag(const std::string& name, TagSource source) {
    // Пробуем найти существующий
    auto existing = m_db->queryOne<int64_t>(
        "SELECT id FROM tags WHERE name = ?",
        [](sqlite3_stmt* stmt) { return Database::getInt64(stmt, 0); },
        name
    );

    if (existing) {
        return *existing;
    }

    // Создаём новый
    m_db->execute("INSERT INTO tags (name, source) VALUES (?, ?)",
                  name, static_cast<int>(source));
    return m_db->lastInsertId();
}

void TagManager::addTag(int64_t fileId, const std::string& tag, TagSource source) {
    int64_t tagId = getOrCreateTag(tag, source);

    // INSERT OR IGNORE для избежания дубликатов
    m_db->execute(
        "INSERT OR IGNORE INTO file_tags (file_id, tag_id) VALUES (?, ?)",
        fileId, tagId
    );
}

void TagManager::removeTag(int64_t fileId, const std::string& tag) {
    m_db->execute(
        R"SQL(
        DELETE FROM file_tags 
        WHERE file_id = ? 
        AND tag_id = (SELECT id FROM tags WHERE name = ?)
        )SQL",
        fileId, tag
    );
}

std::vector<std::string> TagManager::getFileTags(int64_t fileId) const {
    return m_db->query<std::string>(
        R"SQL(
        SELECT t.name 
        FROM tags t
        JOIN file_tags ft ON ft.tag_id = t.id
        WHERE ft.file_id = ?
        ORDER BY t.name
        )SQL",
        [](sqlite3_stmt* stmt) { return Database::getString(stmt, 0); },
        fileId
    );
}

std::vector<std::pair<std::string, TagSource>> TagManager::generateAutoTagsForFile(const FileRecord& file) {
    std::vector<std::pair<std::string, TagSource>> tags;

    // Тег по типу контента
    switch (file.contentType) {
        case ContentType::Image:
            tags.emplace_back("image", TagSource::Auto);
            break;
        case ContentType::Video:
            tags.emplace_back("video", TagSource::Auto);
            break;
        case ContentType::Audio:
            tags.emplace_back("audio", TagSource::Auto);
            break;
        case ContentType::Document:
            tags.emplace_back("document", TagSource::Auto);
            break;
        case ContentType::Archive:
            tags.emplace_back("archive", TagSource::Auto);
            break;
        default:
            break;
    }

    // Тег по расширению
    if (!file.extension.empty()) {
        tags.emplace_back(file.extension, TagSource::Auto);
    }

    // Теги по дате
    if (file.modifiedAt > 0) {
        std::time_t time = static_cast<std::time_t>(file.modifiedAt);
        std::tm tmBuf{};
        std::tm* tm = &tmBuf;

#ifdef _WIN32
        if (localtime_s(&tmBuf, &time) != 0) tm = nullptr;
#else
        tm = std::localtime(&time);
#endif

        if (tm) {
            // Год
            tags.emplace_back(std::to_string(1900 + tm->tm_year), TagSource::Auto);

            // Месяц
            static const char* months[] = {
                "january", "february", "march", "april",
                "may", "june", "july", "august",
                "september", "october", "november", "december"
            };
            tags.emplace_back(months[tm->tm_mon], TagSource::Auto);

            // Сезон
            int month = tm->tm_mon;
            if (month >= 2 && month <= 4) {
                tags.emplace_back("spring", TagSource::Auto);
            } else if (month >= 5 && month <= 7) {
                tags.emplace_back("summer", TagSource::Auto);
            } else if (month >= 8 && month <= 10) {
                tags.emplace_back("autumn", TagSource::Auto);
            } else {
                tags.emplace_back("winter", TagSource::Auto);
            }
        }
    }

    // Теги по размеру
    if (file.size > 100 * 1024 * 1024) {
        tags.emplace_back("large", TagSource::Auto);  // >100MB
    } else if (file.size < 10 * 1024) {
        tags.emplace_back("tiny", TagSource::Auto);   // <10KB
    }

    return tags;
}

void TagManager::generateAutoTags(int64_t fileId, const FileRecord& file) {
    auto autoTags = generateAutoTagsForFile(file);

    for (const auto& [tagName, source] : autoTags) {
        addTag(fileId, tagName, source);
    }

    spdlog::debug("Generated {} auto tags for file {}", autoTags.size(), fileId);
}

std::vector<Tag> TagManager::getAllTags() const {
    return m_db->query<Tag>(
        R"SQL(
        SELECT t.id, t.name, t.source, COUNT(ft.file_id) as file_count
        FROM tags t
        LEFT JOIN file_tags ft ON ft.tag_id = t.id
        GROUP BY t.id
        ORDER BY t.name
        )SQL",
        mapTag
    );
}

std::vector<Tag> TagManager::getPopularTags(int limit) const {
    return m_db->query<Tag>(
        R"SQL(
        SELECT t.id, t.name, t.source, COUNT(ft.file_id) as file_count
        FROM tags t
        JOIN file_tags ft ON ft.tag_id = t.id
        GROUP BY t.id
        ORDER BY file_count DESC
        LIMIT ?
        )SQL",
        mapTag,
        limit
    );
}

std::vector<Tag> TagManager::searchTags(const std::string& prefix) const {
    std::string pattern = prefix + "%";
    return m_db->query<Tag>(
        R"SQL(
        SELECT t.id, t.name, t.source, COUNT(ft.file_id) as file_count
        FROM tags t
        LEFT JOIN file_tags ft ON ft.tag_id = t.id
        WHERE t.name LIKE ?
        GROUP BY t.id
        ORDER BY file_count DESC, t.name
        LIMIT 20
        )SQL",
        mapTag,
        pattern
    );
}

void TagManager::addTagToFiles(const std::vector<int64_t>& fileIds, const std::string& tag) {
    Database::Transaction tx(*m_db);

    int64_t tagId = getOrCreateTag(tag, TagSource::User);

    for (int64_t fileId : fileIds) {
        m_db->execute(
            "INSERT OR IGNORE INTO file_tags (file_id, tag_id) VALUES (?, ?)",
            fileId, tagId
        );
    }

    tx.commit();
    spdlog::info("Added tag '{}' to {} files", tag, fileIds.size());
}

void TagManager::removeTagFromFiles(const std::vector<int64_t>& fileIds, const std::string& tag) {
    Database::Transaction tx(*m_db);

    for (int64_t fileId : fileIds) {
        m_db->execute(
            R"SQL(
            DELETE FROM file_tags 
            WHERE file_id = ? 
            AND tag_id = (SELECT id FROM tags WHERE name = ?)
            )SQL",
            fileId, tag
        );
    }

    tx.commit();
}

std::vector<FileRecord> TagManager::getFilesByTag(const std::string& tag, int limit, int offset) const {
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
        LIMIT ? OFFSET ?
        )SQL",
        [](sqlite3_stmt* stmt) {
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
        },
        tag, limit, offset
    );
}

int64_t TagManager::countFilesByTag(const std::string& tag) const {
    return m_db->queryScalar(
        "SELECT COUNT(*) FROM file_tags ft JOIN tags t ON ft.tag_id = t.id WHERE t.name = ?",
        tag
    );
}

Tag TagManager::mapTag(sqlite3_stmt* stmt) {
    Tag t;
    t.id = Database::getInt64(stmt, 0);
    t.name = Database::getString(stmt, 1);
    t.source = static_cast<TagSource>(Database::getInt(stmt, 2));
    t.fileCount = Database::getInt64(stmt, 3);
    return t;
}

} // namespace FamilyVault

