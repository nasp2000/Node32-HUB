#pragma once
#include <stdint.h>

// Minimal host stand-in for the ESP-IDF NVS flash API surface referenced by
// ConfigManager.cpp / NvsStore.cpp (nvs_stats_t diagnostics only).
typedef uint32_t esp_err_t;

#define ESP_OK 0
#define ESP_FAIL 0x101

struct nvs_stats_t {
    uint32_t used_entries;
    uint32_t free_entries;
    uint32_t total_entries;
    uint32_t namespace_count;
};

inline esp_err_t nvs_get_stats(const char* /*partition*/, nvs_stats_t* out) {
    if (out) {
        out->used_entries = 0;
        out->free_entries = 1000;
        out->total_entries = 1000;
        out->namespace_count = 3;
    }
    return ESP_OK;
}