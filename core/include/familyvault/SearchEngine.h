#pragma once

#include "Database.h"
#include "Models.h"
#include <memory>
#include <vector>
#include <utility>

namespace FamilyVault {

/// Результат построения SQL запроса: SQL строка + параметры для binding
struct SearchQueryBuilt {
    std::string sql;
    std::vector<SqlParam> params;
};

class SearchEngine {
public:
    explicit SearchEngine(std::shared_ptr<Database> db);
    ~SearchEngine();

    /// Поиск файлов по запросу
    std::vector<SearchResult> search(const SearchQuery& query);

    /// Поиск с компактными результатами (для UI списков)
    std::vector<SearchResultCompact> searchCompact(const SearchQuery& query);

    /// Подсчёт результатов без получения данных
    int64_t countResults(const SearchQuery& query);

    /// Автодополнение для поиска
    std::vector<std::string> suggest(const std::string& prefix, int limit = 10);

    /// Быстрые фильтры
    std::vector<FileRecord> getByExtension(const std::string& ext, int limit = 100);
    std::vector<FileRecord> getByContentType(ContentType type, int limit = 100);
    std::vector<FileRecord> getByTag(const std::string& tag, int limit = 100);

private:
    std::shared_ptr<Database> m_db;

    /// Построение SQL запроса с параметрами (безопасно от SQL injection)
    SearchQueryBuilt buildSearchQuery(const SearchQuery& query, bool countOnly = false);

    /// Экранирование FTS запроса (для специальных символов FTS5)
    std::string escapeFtsQuery(const std::string& text);

    /// Генерация snippet'а
    std::string generateSnippet(int64_t fileId, const std::string& query);

    /// Маппер результатов
    static SearchResult mapSearchResult(sqlite3_stmt* stmt);
    static SearchResultCompact mapSearchResultCompact(sqlite3_stmt* stmt);
    static FileRecord mapFileRecord(sqlite3_stmt* stmt);
};

} // namespace FamilyVault

