#include "observability_access.h"
#include "observability.h"

ModuleHealthState gModuleHealth;

static const char* MODULE_NAMES[HEALTH_ID_MAX] = {
    "heartbeat", "sensors", "network", "config", "scheduler",
    "email", "rs232", "proximity", "grbl", "usbip", "camera", "can"
};

uint32_t healthGetLastAliveMs(uint8_t id) {
    if (id >= HEALTH_ID_MAX) return 0;
    return gModuleHealth.lastAliveMs[id];
}

uint32_t healthGetTimeoutMs(uint8_t id) {
    if (id >= HEALTH_ID_MAX) return 0;
    return gModuleHealth.timeoutMs[id];
}

bool healthGetEssential(uint8_t id) {
    if (id >= HEALTH_ID_MAX) return false;
    return gModuleHealth.essential[id];
}

uint8_t healthGetCount() {
    return HEALTH_ID_COUNT;
}

const char* healthGetName(uint8_t id) {
    if (id >= HEALTH_ID_MAX || id >= (sizeof(MODULE_NAMES) / sizeof(MODULE_NAMES[0]))) return "?";
    return MODULE_NAMES[id];
}
