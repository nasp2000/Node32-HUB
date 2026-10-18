#include "core_admin_handler.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_partition.h>
#include "../../feature_flags.h"
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
#include <esp_wifi.h>
#endif
#include "../../network_mdns.h"
#include <exception>
#include "../../ConfigManager.h"
#include "../../coordinator_hooks.h"
#include "../../commands.h"
#include "../../direct_log.h"
#include "../../web_shared_logic.h"
#include "../../psram_helpers.h"
#include "../../system_status_payload.h"
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../../touch7_display.h" // boot-cached NVS metrics; no request-time flash reads
#endif

extern void syncGlobalVariables();

namespace {
    volatile bool statusBusy = false;
    unsigned long lastStatusMs = 0;
    SystemStatusPayloadState statusPayloadState;
}

void handleStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    unsigned long now = millis();
    if (statusBusy || (now - lastStatusMs < 500UL)) {
        res.send(429, F("application/json"), F("{\"error\":\"too_many_requests\"}"));
        return;
    }
    statusBusy = true;
    lastStatusMs = now;

    SpiRamJsonDocument doc(12288);
    fillSystemStatusJson(doc, statusPayloadState);

    String json;
    // Pre-size the output String from the JSON doc usage (pretty-print adds
    // whitespace, hence the margin) so the serializer does not realloc it
    // repeatedly — avoids transient internal-heap churn on every poll.
    json.reserve((size_t)doc.memoryUsage() * 2 + 64);
    serializeJsonPretty(doc, json);
    res.sendJson(json);
    statusBusy = false;
}

void handleNvsStatsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String json = F("{\"nvs_total_bytes\":");
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    // The RGB bounce-buffer ISR cannot run alongside request-time NVS reads.
    // touch7SystemBegin() caches both values before the panel starts.
    const uint32_t nvsTotalBytes = (touch7StatNvsTotalKb > 0)
        ? (uint32_t)touch7StatNvsTotalKb * 1024UL : 0;
    const size_t dataLength = touch7NvsAllConfigsBytes;
#else
    const esp_partition_t* nvsPart = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, nullptr);
    const uint32_t nvsTotalBytes = nvsPart ? (uint32_t)nvsPart->size : 0;
    size_t dataLength = 0;
    try {
        Preferences p;
        p.begin("all_configs", true);
        String raw = p.isKey("data") ? p.getString("data", "") : String("");
        p.end();
        dataLength = raw.length();
    } catch (...) {
        dataLength = 0;
    }
#endif
    json += String(nvsTotalBytes);
    json += F(",\"data_length\":");
    json += String(dataLength);
    json += '}';
    res.send(200, F("application/json"), json);
}

void handleRestartPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    directLogPrintln(F("Restart requested via API"));
    sendVirtualMessage(F("Restarting..."), INFO);
    res.sendJson(F("{\"status\":\"ok\",\"message\":\"Reiniciando Node32-Hub...\"}"));
}

void performRestartActions() {
    delay(500);
#if ENABLE_SDCARD==1
    debugWriterFlush();
    delay(100);
    flushSdcardNowBlocking("WEB_REBOOT", 5000);
#endif
    safeReboot("API Request");
}

void handleTestPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    directLogPrintln(F("POST /api/test - Test route"));
    res.sendJson(F("{\"success\":true,\"message\":\"Teste bem-sucedido\"}"));
}

void handleDb25InjectGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_DB25==0
    res.send(501, F("application/json"), F("{\"status\":\"disabled\",\"feature\":\"DB25\"}"));
    return;
#endif
    String line = req.query;
    int eq = line.indexOf('=');
    if (eq >= 0) line = line.substring(eq + 1);
    int amp = line.indexOf('&');
    if (amp >= 0) line = line.substring(0, amp);

    if (line.length() == 0) {
        res.send(400, F("application/json"), F("{\"success\":false,\"error\":\"missing line param\"}"));
        return;
    }

    unsigned long spinStart = millis();
    while (bufferDadosBusy) {
        if (millis() - spinStart > 100) {
            bufferDadosMutexFailCount = bufferDadosMutexFailCount + 1;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    bufferDadosBusy = true;
    for (size_t i = 0; i < line.length(); ++i) {
        bufferDados.push((uint8_t)line.charAt(i));
        bufferDadosSeq++;
    }
    bufferDados.push('\n');
    bufferDadosSeq++;
    bufferDadosBusy = false;

    SpiRamJsonDocument resp(256);
    resp["success"] = true;
    resp["injected_bytes"] = (unsigned long)line.length() + 1UL;
    String json;
    serializeJson(resp, json);
    res.send(200, F("application/json"), json);
}

void handleFactoryResetPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    directLogPrintln(F("Factory reset requested via API"));
    sendVirtualMessage(F("Factory reset..."), WARN);
    res.sendJson(F("{\"status\":\"ok\",\"message\":\"Defaults repostos. Reiniciando...\"}"));
}

void performFactoryResetActions() {
    Preferences p;
    p.begin("esp32_config",    false); p.clear(); p.end();
    p.begin("all_configs",     false); p.clear(); p.end();
    p.begin("printer_configs", false); p.clear(); p.end();
    p.begin("wifi_networks",   false); p.clear(); p.end();

    configManager.initDefaults();
    configManager.save(true);
    triggerScreenConfigSaved();
    syncGlobalVariables();

    WiFi.persistent(true);
    WiFi.disconnect(true, true);
    delay(200);
    stopMdns();
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
    esp_wifi_restore();
#endif
    WiFi.mode(WIFI_OFF);
    delay(300);

#if ENABLE_SDCARD==1
    debugWriterFlush();
    delay(100);
    flushSdcardNowBlocking("FACTORY_RESET", 5000);
#endif

    safeReboot("Factory Reset");
}

void handleHeapSnapshotGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    SpiRamJsonDocument doc(2048);
    doc["timestamp"] = millis();
    doc["free_heap_bytes"] = (uint32_t)ESP.getFreeHeap();
    doc["min_free_heap_bytes"] = (uint32_t)ESP.getMinFreeHeap();
    doc["max_alloc_bytes"] = (uint32_t)ESP.getMaxAllocHeap();
    doc["pressure_level"] = memoryPressureLevelName(memoryPressureCurrentLevel());

#if (defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT))
    doc["psram_free_bytes"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
    doc["psram_free_bytes"] = 0;
#endif

    JsonArray tasksArr = doc.createNestedArray("tasks");
    unsigned long taskCount = uxTaskGetNumberOfTasks();
    if (taskCount > 0 && taskCount < 64) {
        TaskStatus_t* taskStatusArray = (TaskStatus_t*)heap_caps_malloc(
            taskCount * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
        if (taskStatusArray) {
            unsigned long numTasks = uxTaskGetSystemState(taskStatusArray, taskCount, nullptr);
            for (unsigned long i = 0; i < numTasks; i++) {
                JsonObject taskObj = tasksArr.createNestedObject();
                taskObj["name"] = (const char*)taskStatusArray[i].pcTaskName;
                taskObj["core"] = (int)taskStatusArray[i].xCoreID;
                taskObj["priority"] = (int)taskStatusArray[i].uxCurrentPriority;
                taskObj["stack_watermark_bytes"] = (int)uxTaskGetStackHighWaterMark(
                    taskStatusArray[i].xHandle) * sizeof(StackType_t);
                taskObj["state"] = (int)taskStatusArray[i].eCurrentState;
            }
            heap_caps_free(taskStatusArray);
        }
    }

    String json;
    serializeJsonPretty(doc, json);
    res.send(200, F("application/json"), json);
}
