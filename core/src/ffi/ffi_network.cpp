// ffi_network.cpp — C API для Network Discovery

#include "ffi_internal.h"
#include "familyvault/familyvault_c.h"
#include "familyvault/Network/Discovery.h"
#include "familyvault/FamilyPairing.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <memory>
#include <string>

using json = nlohmann::json;

namespace {

// Wrapper для хранения состояния discovery
struct DiscoveryWrapper {
    std::unique_ptr<FamilyVault::NetworkDiscovery> discovery;
    FVDeviceCallback callback = nullptr;
    void* userData = nullptr;
};

std::string deviceInfoToJson(const FamilyVault::DeviceInfo& info) {
    json j = {
        {"deviceId", info.deviceId},
        {"deviceName", info.deviceName},
        {"deviceType", static_cast<int>(info.deviceType)},
        {"ipAddress", info.ipAddress},
        {"servicePort", info.servicePort},
        {"lastSeenAt", info.lastSeenAt},
        {"isOnline", info.isOnline},
        {"isConnected", info.isConnected},
        {"fileCount", info.fileCount},
        {"lastSyncAt", info.lastSyncAt}
    };
    return j.dump();
}

char* allocString(const std::string& str) {
    if (str.empty()) return nullptr;
    return fv_strdup(str.c_str());
}

// Helper to allocate heap string for async callbacks
// Caller (Dart) must call fv_free_string after reading
const char* allocEventJson(const std::string& json) {
    return fv_strdup(json.c_str());
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════
// Network Discovery C API
// ═══════════════════════════════════════════════════════════

FV_API FVNetworkDiscovery fv_discovery_create(void) {
    try {
        auto wrapper = new DiscoveryWrapper();
        wrapper->discovery = std::make_unique<FamilyVault::NetworkDiscovery>();
        clearLastError();
        return reinterpret_cast<FVNetworkDiscovery>(wrapper);
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

FV_API void fv_discovery_destroy(FVNetworkDiscovery discovery) {
    if (!discovery) return;
    
    auto wrapper = reinterpret_cast<DiscoveryWrapper*>(discovery);
    if (wrapper->discovery) {
        wrapper->discovery->stop();
    }
    delete wrapper;
}

FV_API FVError fv_discovery_start(FVNetworkDiscovery discovery,
                                   FVFamilyPairing pairing,
                                   FVDeviceCallback callback, void* user_data) {
    if (!discovery) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "discovery is null");
        return FV_ERROR_INVALID_ARGUMENT;
    }
    if (!pairing) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "pairing is null");
        return FV_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto wrapper = reinterpret_cast<DiscoveryWrapper*>(discovery);
        auto pairingPtr = reinterpret_cast<FamilyVault::FamilyPairing*>(pairing);

        // Сохраняем callback
        wrapper->callback = callback;
        wrapper->userData = user_data;

        // Создаём DeviceInfo из FamilyPairing
        FamilyVault::DeviceInfo thisDevice;
        thisDevice.deviceId = pairingPtr->getDeviceId();
        thisDevice.deviceName = pairingPtr->getDeviceName();
        thisDevice.deviceType = pairingPtr->getDeviceType();
        thisDevice.servicePort = FamilyVault::SERVICE_PORT;
        thisDevice.isOnline = true;

        // Set up callbacks if provided
        // NOTE: Callbacks use allocEventJson() to heap-allocate JSON strings
        // because NativeCallable.listener is async. Dart MUST call fv_free_string().
        if (callback) {
            wrapper->discovery->onDeviceFound([wrapper](const FamilyVault::DeviceInfo& info) {
                if (wrapper->callback) {
                    const char* json = allocEventJson(deviceInfoToJson(info));
                    wrapper->callback(0, json, wrapper->userData);
                }
            });

            wrapper->discovery->onDeviceLost([wrapper](const FamilyVault::DeviceInfo& info) {
                if (wrapper->callback) {
                    const char* json = allocEventJson(deviceInfoToJson(info));
                    wrapper->callback(1, json, wrapper->userData);
                }
            });

            wrapper->discovery->onDeviceUpdated([wrapper](const FamilyVault::DeviceInfo& info) {
                if (wrapper->callback) {
                    const char* json = allocEventJson(deviceInfoToJson(info));
                    wrapper->callback(2, json, wrapper->userData);
                }
            });
        }

        if (!wrapper->discovery->start(thisDevice)) {
            setLastError(FV_ERROR_NETWORK, "Failed to start discovery");
            return FV_ERROR_NETWORK;
        }

        clearLastError();
        return FV_OK;
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return FV_ERROR_INTERNAL;
    }
}

FV_API void fv_discovery_stop(FVNetworkDiscovery discovery) {
    if (!discovery) return;
    
    auto wrapper = reinterpret_cast<DiscoveryWrapper*>(discovery);
    if (wrapper->discovery) {
        wrapper->discovery->stop();
    }
}

FV_API int32_t fv_discovery_is_running(FVNetworkDiscovery discovery) {
    if (!discovery) return 0;
    
    auto wrapper = reinterpret_cast<DiscoveryWrapper*>(discovery);
    return wrapper->discovery ? wrapper->discovery->isRunning() : 0;
}

FV_API char* fv_discovery_get_devices(FVNetworkDiscovery discovery) {
    if (!discovery) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "discovery is null");
        return nullptr;
    }

    try {
        auto wrapper = reinterpret_cast<DiscoveryWrapper*>(discovery);
        auto devices = wrapper->discovery->getDevices();

        json arr = json::array();
        for (const auto& device : devices) {
            arr.push_back(json::parse(deviceInfoToJson(device)));
        }

        clearLastError();
        return allocString(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

FV_API int32_t fv_discovery_get_device_count(FVNetworkDiscovery discovery) {
    if (!discovery) return 0;
    
    auto wrapper = reinterpret_cast<DiscoveryWrapper*>(discovery);
    return static_cast<int32_t>(wrapper->discovery->getDeviceCount());
}

FV_API char* fv_discovery_get_device(FVNetworkDiscovery discovery, const char* device_id) {
    if (!discovery || !device_id) {
        setLastError(FV_ERROR_INVALID_ARGUMENT, "discovery or device_id is null");
        return nullptr;
    }

    try {
        auto wrapper = reinterpret_cast<DiscoveryWrapper*>(discovery);
        auto device = wrapper->discovery->getDevice(device_id);

        if (!device) {
            clearLastError();
            return nullptr;
        }

        clearLastError();
        return allocString(deviceInfoToJson(*device));
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

FV_API char* fv_discovery_get_local_ips(void) {
    try {
        auto ips = FamilyVault::NetworkDiscovery::getLocalIpAddresses();

        json arr = json::array();
        for (const auto& ip : ips) {
            arr.push_back(ip);
        }

        clearLastError();
        return allocString(arr.dump());
    } catch (const std::exception& e) {
        setLastError(FV_ERROR_INTERNAL, e.what());
        return nullptr;
    }
}

