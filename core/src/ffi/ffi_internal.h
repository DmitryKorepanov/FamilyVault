// ffi_internal.h — Internal shared declarations for FFI implementation

#ifndef FFI_INTERNAL_H
#define FFI_INTERNAL_H

#include "familyvault/familyvault_c.h"
#include "familyvault/Database.h"
#include <string>
#include <memory>
#include <atomic>
#include <cstring>

#ifdef _WIN32
    #define fv_strdup _strdup
#else
    #define fv_strdup strdup
#endif

// Thread-local error state
extern thread_local FVError g_lastError;
extern thread_local std::string g_lastErrorMessage;

// Error handling functions (defined in familyvault_c.cpp)
// Note: default argument only in declaration, not in definition
void setLastError(FVError error, const std::string& message = "");
inline void clearLastError() { setLastError(FV_OK); }

// String allocation (defined in familyvault_c.cpp)
char* alloc_string(const std::string& str);

// ═══════════════════════════════════════════════════════════
// DatabaseHolder - shared wrapper for Database with ref-counting
// ═══════════════════════════════════════════════════════════

/// Wraps Database with reference counting for safe manager lifetime
class DatabaseHolder {
public:
    explicit DatabaseHolder(const std::string& path)
        : m_database(std::make_shared<FamilyVault::Database>(path))
        , m_refCount(1)
        , m_initialized(false)
    {}

    std::shared_ptr<FamilyVault::Database> getDatabase() const { return m_database; }
    
    void addRef() {
        m_refCount.fetch_add(1, std::memory_order_relaxed);
    }
    
    int release() {
        return m_refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
    }
    
    int refCount() const {
        return m_refCount.load(std::memory_order_relaxed);
    }
    
    void initialize() {
        if (!m_initialized) {
            m_database->initialize();
            m_initialized = true;
        }
    }
    
    bool isInitialized() const { return m_initialized; }
    
private:
    std::shared_ptr<FamilyVault::Database> m_database;
    std::atomic<int> m_refCount;
    bool m_initialized;
};

#endif // FFI_INTERNAL_H

