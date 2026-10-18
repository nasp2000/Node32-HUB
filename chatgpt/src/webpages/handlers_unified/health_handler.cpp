#include "health_handler.h"
#include "../../feature_flags.h"
#include "../../memory_pressure_guard.h"
#include "../../observability.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_chip_info.h>
#include <nvs_flash.h>

#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
#include <esp_heap_caps.h>
#endif

extern unsigned long bootTime;
extern uint64_t nvsGetWriteCount();
extern uint64_t nvsGetBytesWritten();
extern unsigned long wifiReconnectCount;

void handleHealthGet(HttpRequest& req, HttpResponse& res) {
    (void)req;

    size_t freeHeap = ESP.getFreeHeap();
    size_t minFreeHeap = ESP.getMinFreeHeap();
    size_t maxAllocHeap = ESP.getMaxAllocHeap();
    size_t freePsram = 0;
    size_t totalPsram = 0;
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
    if (psramFound()) {
        freePsram = ESP.getFreePsram();
        totalPsram = ESP.getPsramSize();
    }
#endif

    unsigned long uptimeMs = millis();
    uint32_t uptimeSec = uptimeMs / 1000;
    uint32_t uptimeMin = uptimeSec / 60;
    uint32_t uptimeHour = uptimeMin / 60;
    uint32_t uptimeDay = uptimeHour / 24;

    esp_reset_reason_t resetReason = esp_reset_reason();
    const char* resetStr = "UNKNOWN";
    switch (resetReason) {
        case ESP_RST_POWERON:    resetStr = "POWER_ON"; break;
        case ESP_RST_SW:         resetStr = "SOFTWARE"; break;
        case ESP_RST_PANIC:      resetStr = "PANIC"; break;
        case ESP_RST_INT_WDT:    resetStr = "INT_WDT"; break;
        case ESP_RST_TASK_WDT:   resetStr = "TASK_WDT"; break;
        case ESP_RST_WDT:        resetStr = "WDT"; break;
        case ESP_RST_BROWNOUT:   resetStr = "BROWNOUT"; break;
        case ESP_RST_DEEPSLEEP:  resetStr = "DEEP_SLEEP"; break;
        default: break;
    }

    int cpuTemp = 0;
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4 != 1
    cpuTemp = (int)temperatureRead();
#endif

    TaskHandle_t idleHandles[2];
    uint32_t idleHwm[2] = {0, 0};
    idleHandles[0] = xTaskGetIdleTaskHandleForCore(0);
    idleHandles[1] = xTaskGetIdleTaskHandleForCore(1);
    for (int i = 0; i < 2; i++) {
        if (idleHandles[i]) {
            idleHwm[i] = (uint32_t)uxTaskGetStackHighWaterMark(idleHandles[i]);
        }
    }

    char buf[2048];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "{\"status\":\"ok\",\"uptime_s\":%lu,\"uptime_str\":\"%lud %luh %lum %lus\""
        ",\"reset_reason\":\"%s\""
        ",\"heap\":{\"free\":%lu,\"min_free\":%lu,\"max_alloc\":%lu}"
        ",\"psram\":{\"free\":%lu,\"total\":%lu}"
        ",\"cpu_temp_c\":%d"
        ",\"pressure\":\"%s\""
        ",\"nvs_writes\":%llu,\"nvs_bytes\":%llu"
        ",\"wifi_reconnects\":%lu"
        ",\"idle_hwm\":[%lu,%lu]"
        ",\"modules\":[",
        (unsigned long)uptimeSec,
        (unsigned long)uptimeDay, (unsigned long)(uptimeHour % 24),
        (unsigned long)(uptimeMin % 60), (unsigned long)(uptimeSec % 60),
        resetStr,
        (unsigned long)freeHeap, (unsigned long)minFreeHeap, (unsigned long)maxAllocHeap,
        (unsigned long)freePsram, (unsigned long)totalPsram,
        cpuTemp,
        memoryPressureLevelName(memoryPressureCurrentLevel()),
        (unsigned long long)nvsGetWriteCount(), (unsigned long long)nvsGetBytesWritten(),
        (unsigned long)wifiReconnectCount,
        (unsigned long)idleHwm[0], (unsigned long)idleHwm[1]);

    unsigned long nowMs = millis();
    const char* names[] = {"heartbeat","sensors","network","config","scheduler",
                           "email","rs232","proximity","grbl","usbip","camera","can"};
    for (uint8_t i = 0; i < HEALTH_ID_COUNT; i++) {
        uint32_t age = (uint32_t)(nowMs - gModuleHealth.lastAliveMs[i]);
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s{\"id\":%u,\"name\":\"%s\",\"age_ms\":%lu,\"timeout_ms\":%lu,\"essential\":%s}",
            (i > 0 ? "," : ""),
            (unsigned)i,
            (i < sizeof(names)/sizeof(names[0]) ? names[i] : "?"),
            (unsigned long)age,
            (unsigned long)gModuleHealth.timeoutMs[i],
            gModuleHealth.essential[i] ? "true" : "false");
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    res.sendJson(String(buf));
}
