#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace heavy_api_request_guard_internal {

struct HeavyApiSlotState {
    const char* routeId = nullptr;
    bool active = false;
    unsigned long lastMs = 0;
};

inline portMUX_TYPE& heavyApiGuardMux() {
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    return mux;
}

inline HeavyApiSlotState* heavyApiGuardSlots() {
    static HeavyApiSlotState slots[16];
    return slots;
}

inline int findOrCreateHeavyApiSlot(const char* routeId) {
    HeavyApiSlotState* slots = heavyApiGuardSlots();
    int freeIndex = -1;
    for (int i = 0; i < 16; ++i) {
        if (slots[i].routeId == routeId) return i;
        if (freeIndex < 0 && slots[i].routeId == nullptr) freeIndex = i;
    }
    if (freeIndex >= 0) {
        slots[freeIndex].routeId = routeId;
    }
    return freeIndex;
}

} // namespace heavy_api_request_guard_internal

class HeavyApiRequestGuard {
public:
    HeavyApiRequestGuard(AsyncWebServerRequest* request, const char* routeId, unsigned long minIntervalMs = 1000UL)
        : request_(request), routeId_(routeId), minIntervalMs_(minIntervalMs) {}

    ~HeavyApiRequestGuard() {
        if (!entered_ || slotIndex_ < 0) return;
        portMUX_TYPE& mux = heavy_api_request_guard_internal::heavyApiGuardMux();
        portENTER_CRITICAL(&mux);
        heavy_api_request_guard_internal::heavyApiGuardSlots()[slotIndex_].active = false;
        portEXIT_CRITICAL(&mux);
    }

    bool begin() {
        if (evaluated_) return entered_;
        evaluated_ = true;

        portMUX_TYPE& mux = heavy_api_request_guard_internal::heavyApiGuardMux();
        portENTER_CRITICAL(&mux);

        slotIndex_ = heavy_api_request_guard_internal::findOrCreateHeavyApiSlot(routeId_);
        bool ok = false;
        if (slotIndex_ >= 0) {
            auto& slot = heavy_api_request_guard_internal::heavyApiGuardSlots()[slotIndex_];
            unsigned long now = millis();
            unsigned long elapsed = now - slot.lastMs;
            if (now < slot.lastMs) elapsed = now;

            if (!slot.active && elapsed >= minIntervalMs_) {
                slot.active = true;
                slot.lastMs = now;
                ok = true;
            }
        }

        portEXIT_CRITICAL(&mux);

        entered_ = ok;
        if (!entered_ && request_) {
            request_->send(429, "application/json", "{\"error\":\"too_many_requests\"}");
        }
        return entered_;
    }

    bool allowed() {
        return begin();
    }

private:
    AsyncWebServerRequest* request_ = nullptr;
    const char* routeId_ = nullptr;
    unsigned long minIntervalMs_ = 1000UL;
    int slotIndex_ = -1;
    bool evaluated_ = false;
    bool entered_ = false;
};