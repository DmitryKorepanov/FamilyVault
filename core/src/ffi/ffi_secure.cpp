// ffi_secure.cpp — C API для SecureStorage и FamilyPairing

#include "ffi_internal.h"
#include "familyvault/familyvault_c.h"
#include "familyvault/SecureStorage.h"
#include "familyvault/FamilyPairing.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <cstring>

using namespace FamilyVault;
using json = nlohmann::json;

// Use shared error functions from ffi_internal.h
#define setError setLastError
#define clearError clearLastError

static char* allocString(const std::string& str) {
    char* result = static_cast<char*>(malloc(str.size() + 1));
    if (result) {
        memcpy(result, str.c_str(), str.size() + 1);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════
// Secure Storage
// ═══════════════════════════════════════════════════════════

FVSecureStorage fv_secure_create(void) {
    clearError();
    try {
        auto* storage = new SecureStorage();
        return reinterpret_cast<FVSecureStorage>(storage);
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_secure_destroy(FVSecureStorage storage) {
    if (storage) {
        delete reinterpret_cast<SecureStorage*>(storage);
    }
}

FVError fv_secure_store(FVSecureStorage storage, const char* key,
                        const uint8_t* data, int32_t size) {
    clearError();
    if (!storage || !key || !data || size <= 0) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* s = reinterpret_cast<SecureStorage*>(storage);
        std::vector<uint8_t> vec(data, data + size);
        if (s->store(key, vec)) {
            return FV_OK;
        }
        setError(FV_ERROR_IO, "Failed to store data");
        return FV_ERROR_IO;
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

int32_t fv_secure_retrieve(FVSecureStorage storage, const char* key,
                           uint8_t* out_data, int32_t max_size) {
    clearError();
    if (!storage || !key) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return -1;
    }

    try {
        auto* s = reinterpret_cast<SecureStorage*>(storage);
        auto result = s->retrieve(key);
        if (!result) {
            setError(FV_ERROR_NOT_FOUND, "Key not found");
            return -1;
        }

        int32_t dataSize = static_cast<int32_t>(result->size());
        
        // Если out_data=NULL, возвращаем только размер
        if (!out_data) {
            return dataSize;
        }

        if (max_size < dataSize) {
            setError(FV_ERROR_INVALID_ARGUMENT, "Buffer too small");
            return -1;
        }

        memcpy(out_data, result->data(), dataSize);
        return dataSize;
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return -1;
    }
}

FVError fv_secure_remove(FVSecureStorage storage, const char* key) {
    clearError();
    if (!storage || !key) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* s = reinterpret_cast<SecureStorage*>(storage);
        if (s->remove(key)) {
            return FV_OK;
        }
        setError(FV_ERROR_IO, "Failed to remove key");
        return FV_ERROR_IO;
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

int32_t fv_secure_exists(FVSecureStorage storage, const char* key) {
    clearError();
    if (!storage || !key) {
        return 0;
    }

    try {
        auto* s = reinterpret_cast<SecureStorage*>(storage);
        return s->exists(key) ? 1 : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

FVError fv_secure_store_string(FVSecureStorage storage, const char* key, const char* value) {
    clearError();
    if (!storage || !key || !value) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* s = reinterpret_cast<SecureStorage*>(storage);
        if (s->storeString(key, value)) {
            return FV_OK;
        }
        setError(FV_ERROR_IO, "Failed to store string");
        return FV_ERROR_IO;
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

char* fv_secure_retrieve_string(FVSecureStorage storage, const char* key) {
    clearError();
    if (!storage || !key) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return nullptr;
    }

    try {
        auto* s = reinterpret_cast<SecureStorage*>(storage);
        auto result = s->retrieveString(key);
        if (!result) {
            setError(FV_ERROR_NOT_FOUND, "Key not found");
            return nullptr;
        }
        return allocString(*result);
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

// ═══════════════════════════════════════════════════════════
// Family Pairing
// ═══════════════════════════════════════════════════════════

FVFamilyPairing fv_pairing_create(FVSecureStorage storage) {
    clearError();
    if (!storage) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Storage is required");
        return nullptr;
    }

    try {
        // Создаём shared_ptr который НЕ владеет storage (raw pointer управляется извне)
        auto* rawStorage = reinterpret_cast<SecureStorage*>(storage);
        auto sharedStorage = std::shared_ptr<SecureStorage>(rawStorage, [](SecureStorage*) {
            // No-op deleter — storage управляется через fv_secure_destroy
        });

        auto* pairing = new FamilyPairing(sharedStorage);
        return reinterpret_cast<FVFamilyPairing>(pairing);
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_pairing_destroy(FVFamilyPairing pairing) {
    if (pairing) {
        delete reinterpret_cast<FamilyPairing*>(pairing);
    }
}

int32_t fv_pairing_is_configured(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return 0;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return p->isConfigured() ? 1 : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

char* fv_pairing_get_device_id(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid pairing handle");
        return nullptr;
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return allocString(p->getDeviceId());
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

char* fv_pairing_get_device_name(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid pairing handle");
        return nullptr;
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return allocString(p->getDeviceName());
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_pairing_set_device_name(FVFamilyPairing pairing, const char* name) {
    clearError();
    if (!pairing || !name) return;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        p->setDeviceName(name);
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
    }
}

int32_t fv_pairing_get_device_type(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return 0;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return static_cast<int32_t>(p->getDeviceType());
    } catch (const std::exception&) {
        return 0;
    }
}

static char* pairingInfoToJson(const PairingInfo& info) {
    json j = {
        {"pin", info.pin},
        {"qrData", info.qrData},
        {"expiresAt", info.expiresAt},
        {"createdAt", info.createdAt}
    };
    return allocString(j.dump());
}

char* fv_pairing_create_family(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid pairing handle");
        return nullptr;
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        auto result = p->createFamily();
        if (!result) {
            setError(FV_ERROR_INTERNAL, "Failed to create family");
            return nullptr;
        }
        return pairingInfoToJson(*result);
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

char* fv_pairing_regenerate_pin(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid pairing handle");
        return nullptr;
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        auto result = p->regeneratePin();
        if (!result) {
            setError(FV_ERROR_INTERNAL, "Failed to regenerate PIN");
            return nullptr;
        }
        return pairingInfoToJson(*result);
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

void fv_pairing_cancel(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        p->cancelPairing();
    } catch (const std::exception&) {
        // Ignore
    }
}

int32_t fv_pairing_has_pending(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return 0;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return p->hasPendingPairing() ? 1 : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

int32_t fv_pairing_join_pin(FVFamilyPairing pairing, const char* pin,
                            const char* initiator_host, uint16_t initiator_port) {
    clearError();
    if (!pairing || !pin) {
        return static_cast<int32_t>(JoinResult::InternalError);
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        std::string host = initiator_host ? initiator_host : "";
        return static_cast<int32_t>(p->joinByPin(pin, host, initiator_port));
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return static_cast<int32_t>(JoinResult::InternalError);
    }
}

int32_t fv_pairing_join_qr(FVFamilyPairing pairing, const char* qr_data) {
    clearError();
    if (!pairing || !qr_data) {
        return static_cast<int32_t>(JoinResult::InternalError);
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return static_cast<int32_t>(p->joinByQR(qr_data));
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return static_cast<int32_t>(JoinResult::InternalError);
    }
}

void fv_pairing_reset(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        p->reset();
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
    }
}

int32_t fv_pairing_derive_psk(FVFamilyPairing pairing, uint8_t* out_psk) {
    clearError();
    if (!pairing || !out_psk) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid arguments");
        return 0;
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        auto psk = p->derivePsk();
        if (!psk) {
            setError(FV_ERROR_NOT_FOUND, "Family not configured");
            return 0;
        }
        memcpy(out_psk, psk->data(), psk->size());
        return static_cast<int32_t>(psk->size());
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return 0;
    }
}

char* fv_pairing_get_psk_identity(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid pairing handle");
        return nullptr;
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return allocString(p->getPskIdentity());
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

FVError fv_pairing_start_server(FVFamilyPairing pairing, uint16_t port) {
    clearError();
    if (!pairing) {
        setError(FV_ERROR_INVALID_ARGUMENT, "Invalid pairing handle");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        if (p->startPairingServer(port)) {
            return FV_OK;
        }
        setError(FV_ERROR_NETWORK, "Failed to start pairing server");
        return FV_ERROR_NETWORK;
    } catch (const std::exception& e) {
        setError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

void fv_pairing_stop_server(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        p->stopPairingServer();
    } catch (const std::exception&) {
        // Ignore
    }
}

int32_t fv_pairing_is_server_running(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return 0;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return p->isPairingServerRunning() ? 1 : 0;
    } catch (const std::exception&) {
        return 0;
    }
}

uint16_t fv_pairing_get_server_port(FVFamilyPairing pairing) {
    clearError();
    if (!pairing) return 0;

    try {
        auto* p = reinterpret_cast<FamilyPairing*>(pairing);
        return p->getPairingServerPort();
    } catch (const std::exception&) {
        return 0;
    }
}

