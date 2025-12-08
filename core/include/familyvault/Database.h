#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include <stdexcept>
#include <variant>
#include <type_traits>

#include <sqlite3.h>

struct sqlite3;
struct sqlite3_stmt;

namespace FamilyVault {

/// Тип параметра для динамических запросов
using SqlParam = std::variant<int, int64_t, double, std::string>;

/// Исключение базы данных
class DatabaseException : public std::runtime_error {
public:
    explicit DatabaseException(const std::string& message)
        : std::runtime_error(message) {}
};

/// RAII обёртка над SQLite соединением
class Database {
public:
    explicit Database(const std::string& dbPath);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;

    /// Инициализация БД: применяет миграции
    void initialize();

    /// Выполнение SQL без возврата данных
    void execute(const std::string& sql);

    /// Выполнение SQL с параметрами
    template<typename... Args>
    void execute(const std::string& sql, Args&&... args) {
        auto stmt = prepare(sql);
        bindAll(stmt, 1, std::forward<Args>(args)...);
        step(stmt);
        finalize(stmt);
    }

    /// Запрос с маппером результатов
    template<typename T, typename Mapper, typename... Args>
    std::vector<T> query(const std::string& sql, Mapper mapper, Args&&... args) {
        auto stmt = prepare(sql);
        bindAll(stmt, 1, std::forward<Args>(args)...);

        std::vector<T> results;
        while (stepRow(stmt)) {
            results.push_back(mapper(stmt));
        }
        finalize(stmt);
        return results;
    }

    /// Запрос одной записи
    template<typename T, typename Mapper, typename... Args>
    std::optional<T> queryOne(const std::string& sql, Mapper mapper, Args&&... args) {
        auto stmt = prepare(sql);
        bindAll(stmt, 1, std::forward<Args>(args)...);

        std::optional<T> result;
        if (stepRow(stmt)) {
            result = mapper(stmt);
        }
        finalize(stmt);
        return result;
    }

    /// Запрос скалярного значения (int64)
    template<typename... Args>
    int64_t queryScalar(const std::string& sql, Args&&... args) {
        auto stmt = prepare(sql);
        bindAll(stmt, 1, std::forward<Args>(args)...);

        int64_t result = 0;
        if (stepRow(stmt)) {
            result = getInt64(stmt, 0);
        }
        finalize(stmt);
        return result;
    }

    /// Запрос с динамическими параметрами (для построения запросов в runtime)
    template<typename T, typename Mapper>
    std::vector<T> queryDynamic(const std::string& sql, Mapper mapper, 
                                const std::vector<SqlParam>& params) {
        auto stmt = prepare(sql);
        bindDynamicParams(stmt, params);

        std::vector<T> results;
        while (stepRow(stmt)) {
            results.push_back(mapper(stmt));
        }
        finalize(stmt);
        return results;
    }

    /// Запрос скалярного значения с динамическими параметрами
    int64_t queryScalarDynamic(const std::string& sql, const std::vector<SqlParam>& params) {
        auto stmt = prepare(sql);
        bindDynamicParams(stmt, params);

        int64_t result = 0;
        if (stepRow(stmt)) {
            result = getInt64(stmt, 0);
        }
        finalize(stmt);
        return result;
    }

    /// ID последней вставленной записи
    int64_t lastInsertId() const;

    /// Количество изменённых строк
    int changesCount() const;

    /// Транзакции
    void beginTransaction();
    void commit();
    void rollback();

    /// RAII транзакция
    class Transaction {
    public:
        explicit Transaction(Database& db);
        ~Transaction();
        void commit();
        void rollback();

    private:
        Database* m_db;
        bool m_finished = false;
    };

    /// Хелперы для чтения из stmt
    static int getInt(sqlite3_stmt* stmt, int col);
    static int64_t getInt64(sqlite3_stmt* stmt, int col);
    static double getDouble(sqlite3_stmt* stmt, int col);
    static std::string getString(sqlite3_stmt* stmt, int col);
    static std::optional<std::string> getStringOpt(sqlite3_stmt* stmt, int col);
    static std::optional<int64_t> getInt64Opt(sqlite3_stmt* stmt, int col);
    static std::optional<double> getDoubleOpt(sqlite3_stmt* stmt, int col);
    static bool isNull(sqlite3_stmt* stmt, int col);

private:
    sqlite3* m_db = nullptr;
    std::string m_dbPath;

    // Миграции
    void applyMigrations();
    int getCurrentVersion();
    void setVersion(int version, const std::string& description);

    // Prepared statements
    sqlite3_stmt* prepare(const std::string& sql);
    void step(sqlite3_stmt* stmt);
    bool stepRow(sqlite3_stmt* stmt);
    void finalize(sqlite3_stmt* stmt);

    // Привязка параметров
    void bindParameter(sqlite3_stmt* stmt, int index, int value);
    void bindParameter(sqlite3_stmt* stmt, int index, int64_t value);
    // Явная перегрузка для long long если int64_t != long long
    // (на Windows int64_t = long long, на Linux ARM64 int64_t = long)
    template<typename T>
    std::enable_if_t<
        std::is_same_v<T, long long> && !std::is_same_v<int64_t, long long>,
        void
    > bindParameter(sqlite3_stmt* stmt, int index, T value) {
        sqlite3_bind_int64(stmt, index, static_cast<int64_t>(value));
    }
    void bindParameter(sqlite3_stmt* stmt, int index, double value);
    void bindParameter(sqlite3_stmt* stmt, int index, const std::string& value);
    void bindParameter(sqlite3_stmt* stmt, int index, const char* value);
    void bindParameter(sqlite3_stmt* stmt, int index, std::nullptr_t);

    // Рекурсивная привязка всех параметров
    template<typename T, typename... Rest>
    void bindAll(sqlite3_stmt* stmt, int index, T&& first, Rest&&... rest) {
        bindParameter(stmt, index, std::forward<T>(first));
        if constexpr (sizeof...(rest) > 0) {
            bindAll(stmt, index + 1, std::forward<Rest>(rest)...);
        }
    }

    void bindAll(sqlite3_stmt*, int) {} // База рекурсии

    // Привязка динамических параметров
    void bindDynamicParams(sqlite3_stmt* stmt, const std::vector<SqlParam>& params) {
        for (size_t i = 0; i < params.size(); ++i) {
            int idx = static_cast<int>(i + 1);
            std::visit([this, stmt, idx](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, int>) {
                    bindParameter(stmt, idx, arg);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    bindParameter(stmt, idx, arg);
                } else if constexpr (std::is_same_v<T, double>) {
                    bindParameter(stmt, idx, arg);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    bindParameter(stmt, idx, arg);
                }
            }, params[i]);
        }
    }
};

} // namespace FamilyVault

