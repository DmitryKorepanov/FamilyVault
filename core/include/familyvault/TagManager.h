#pragma once

#include "Database.h"
#include "Models.h"
#include <memory>
#include <vector>

namespace FamilyVault {

class TagManager {
public:
    explicit TagManager(std::shared_ptr<Database> db);
    ~TagManager();

    // ═══════════════════════════════════════════════════════════
    // Управление тегами файла
    // ═══════════════════════════════════════════════════════════

    /// Добавить тег к файлу
    void addTag(int64_t fileId, const std::string& tag, TagSource source = TagSource::User);

    /// Удалить тег с файла
    void removeTag(int64_t fileId, const std::string& tag);

    /// Получить все теги файла
    std::vector<std::string> getFileTags(int64_t fileId) const;

    /// Генерация автотегов для файла
    void generateAutoTags(int64_t fileId, const FileRecord& file);

    // ═══════════════════════════════════════════════════════════
    // Работа с каталогом тегов
    // ═══════════════════════════════════════════════════════════

    /// Получить все теги с количеством файлов
    std::vector<Tag> getAllTags() const;

    /// Получить популярные теги
    std::vector<Tag> getPopularTags(int limit = 20) const;

    /// Поиск тегов по префиксу
    std::vector<Tag> searchTags(const std::string& prefix) const;

    // ═══════════════════════════════════════════════════════════
    // Пакетные операции
    // ═══════════════════════════════════════════════════════════

    /// Добавить тег к нескольким файлам
    void addTagToFiles(const std::vector<int64_t>& fileIds, const std::string& tag);

    /// Удалить тег с нескольких файлов
    void removeTagFromFiles(const std::vector<int64_t>& fileIds, const std::string& tag);

    /// Получить файлы по тегу
    std::vector<FileRecord> getFilesByTag(const std::string& tag, int limit = 100, int offset = 0) const;

    /// Подсчёт файлов с тегом
    int64_t countFilesByTag(const std::string& tag) const;

private:
    std::shared_ptr<Database> m_db;

    /// Получить или создать тег
    int64_t getOrCreateTag(const std::string& name, TagSource source = TagSource::User);

    /// Генерация автотегов
    std::vector<std::pair<std::string, TagSource>> generateAutoTagsForFile(const FileRecord& file);

    /// Маппер Tag
    static Tag mapTag(sqlite3_stmt* stmt);
};

} // namespace FamilyVault

