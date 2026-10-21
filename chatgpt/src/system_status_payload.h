#pragma once
#include "heap_trace.h"   // measured boot checkpoints (heap_trace.cpp)

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_chip_info.h>
#include <esp_partition.h>
#include <esp_system.h>
// esp_wifi.h: not available on P4 (WiFi via C6 SDIO); use WiFi.h API instead.
#include "feature_flags.h"
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
#include <esp_wifi.h>
#endif
#include <math.h>
#include <nvs_flash.h>

#include "config_manager_runtime.h"
#include "feature_flags.h"
#include "littlefs_manager.h"
#include "memory_pressure_guard.h"
#include "network_status_payload.h"
#include "network_runtime_state.h"
#include "network_hybrid_manager.h"
#include "psram_helpers.h"
#include "psram_vfs.h"
#include "sd_diagnostics.h"
#include "sdcard.h"
#include "structures.h"

#if ENABLE_UNIKON==1
#include "unikon_protocol.h"
#endif

#if ENABLE_ETHERNET==1
#include <Ethernet.h>
#endif

#if (defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)) && defined(__has_include)
  #if __has_include(<esp_heap_caps.h>) && __has_include(<esp_spiram.h>)
    #include <esp_heap_caps.h>
    #include <esp_spiram.h>
    #define SYSTEM_STATUS_HAVE_PSRAM_API 1
  #else
    #define SYSTEM_STATUS_HAVE_PSRAM_API 0
  #endif
#else
  #define SYSTEM_STATUS_HAVE_PSRAM_API 0
#endif

extern unsigned long wifiReconnectCount;
extern unsigned long bootTime;
extern volatile int activeHtmlSessions;
extern PsramCircularBuffer<uint8_t> bufferDados;
extern volatile unsigned long bufferDadosSeq;
extern volatile bool bufferDadosBusy;
extern portMUX_TYPE bufferDadosSpinlock;
extern volatile unsigned long bufferDadosMutexFailCount;
extern volatile unsigned long bufferDadosPushMutexFailCount;
extern volatile StorageMode currentStorageMode;
extern const char* getStorageModeString();
extern volatile unsigned long overflowCount;
#if ENABLE_RS232==1
extern int rs232_rx_pin;
extern int rs232_tx_pin;
extern String lastAppliedRs232TemplateName;
extern int rs232_baudrate;
extern int rs232_data_bits;
extern String rs232_parity;
extern int rs232_stop_bits;
extern const int RS232_BAUDRATES[];
extern const int RS232_BAUDRATES_COUNT;
#endif
extern unsigned long lastDataReceivedForInactivityAlert;
extern bool inactivityAlertSent;
extern int inactivityAlertsTodaySlot[];
extern bool inactivityAlertSentSlot[];
extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD;
extern uint64_t nvsGetWriteCount();
extern uint64_t nvsGetBytesWritten();
extern int getInactivityAlertsToday();
extern void fillRecentUniqueIPs(JsonArray& arr, int maxCount);
extern void fillRecentUniqueIPs(JsonArray& arr, int maxCount);

struct SystemStatusPayloadState {
    unsigned long lastRxSeq = 0;
    unsigned long lastRxMs = 0;
};

inline void fillSystemStatusJson(JsonDocument& out, SystemStatusPayloadState& state) {
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool networkConnected = hasNetworkConnectivity();
    out["wifi_connected"] = wifiConnected;
    out["network_connected"] = networkConnected;
    out["ip_address"] = networkConnected ? getCurrentNetworkIPString() : String("");
    out["wifi_ip"] = WiFi.localIP().toString();  // Always the WiFi STA IP (0.0.0.0 if disconnected)
    out["wifi_ssid"] = wifiConnected ? WiFi.SSID() : configManager.getWifiSSID();
    if (wifiConnected) {
        out["wifi_rssi"] = WiFi.RSSI();
    }
    out["wifi_reconnect_attempts"] = (uint32_t)wifiReconnectCount;

    out["charset"] = configManager.getPrinterCharsetMode();
    out["ntp_server"] = configManager.getNtpServer();
    out["timezone"] = configManager.getNtpTimezone();

#if ENABLE_ETHERNET==1
    {
        IPAddress eip = ethernetLocalIP();
        bool hasIp = (eip[0] || eip[1] || eip[2] || eip[3]);
        if (hasIp) {
            out["ethernet_ip"] = eip.toString();
            byte mac[6];
            Ethernet.MACAddress(mac);
            char macBuffer[18];
            snprintf(macBuffer, sizeof(macBuffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            out["ethernet_mac"] = String(macBuffer);
            out["ethernet_link"] = true;
        } else {
            out["ethernet_ip"] = String("");
            out["ethernet_mac"] = String("");
            out["ethernet_link"] = false;
        }
        out["ethernet_mode"] = configManager.getEthernetMode();
    }
#else
    out["ethernet_mode"] = "desativado";
    out["ethernet_link"] = false;
#endif

    memoryPressureRefreshFromSystem(0);
    out["free_heap"] = ESP.getFreeHeap();
    out["heap_size"] = ESP.getHeapSize();
    out["max_alloc_heap"] = ESP.getMaxAllocHeap();
    out["min_free_heap"] = ESP.getMinFreeHeap();
    out["memory_pressure_level"] = memoryPressureLevelName(memoryPressureCurrentLevel());
    out["memory_pressure_code"] = (int)memoryPressureCurrentLevel();
    out["memory_pressure_retry_ms"] = (uint32_t)memoryPressureSuggestedRetryMs();
    out["memory_pressure_since_ms"] = (uint32_t)gMemoryPressureState.levelSinceMs;
    out["flash_size"] = ESP.getFlashChipSize();

#if SYSTEM_STATUS_HAVE_PSRAM_API
    out["psram_total"] = (uint32_t)ESP.getPsramSize();
    out["psram_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out["psram_used"] = ((uint32_t)ESP.getPsramSize() > (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM))
        ? ((uint32_t)ESP.getPsramSize() - (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM))
        : 0;
#else
    {
        uint32_t psramTotal = (uint32_t)ESP.getPsramSize();
        uint32_t psramFree = (psramTotal > 0) ? (uint32_t)ESP.getFreePsram() : 0;
        uint32_t psramUsed = (psramTotal > psramFree) ? (psramTotal - psramFree) : 0;
        out["psram_total"] = psramTotal;
        out["psram_free"] = psramFree;
        out["psram_used"] = psramUsed;
    }
#endif

    if (ESP.getFreeHeap() > 0) {
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t maxAlloc = ESP.getMaxAllocHeap();
        if (maxAlloc > freeHeap) {
            maxAlloc = freeHeap;
        }
        double frag = 1.0 - ((double)maxAlloc / (double)freeHeap);
        if (frag < 0) frag = 0;
        if (frag > 1) frag = 1;
        out["heap_frag_pct"] = (int)(frag * 100.0 + 0.5);
    }

    // Raw fragmentation view: the largest contiguous block per memory region.
    // Read-only diagnostics (no allocation) used to decide whether heap tuning
    // is worth it and to measure the effect after a change.
    out["largest_block_internal"] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    out["free_internal"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out["largest_block_psram"] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    out["free_psram"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    // DMA-capable internal pool: the one WiFi/SDMMC/SPI actually need. It can be
    // exhausted while ordinary DRAM still looks free, so surface it explicitly.
    out["dma_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DMA);
    out["dma_largest"] = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    // Boot-time heap checkpoints (MEASURED at each init step, heap_trace.h).
    // heap_trace[] is the raw sequence; heap_breakdown[] below pairs each module
    // estimate with the measured delta of its checkpoint.
    {
        JsonArray tr = out.createNestedArray("heap_trace");
        for (int i = 0; i < heapTraceCount(); i++) {
            const HeapTracePoint* p = heapTraceAt(i);
            if (!p) continue;
            JsonObject e = tr.createNestedObject();
            e["label"]     = p->label;
            e["free_kb"]   = (int)(p->heapFree / 1024UL);
            e["dma_kb"]    = (int)(p->dmaFree / 1024UL);
            e["largest_kb"]= (int)(p->largestInt / 1024UL);
            e["psram_kb"]  = (int)(p->psramFree / 1024UL);
            e["delta_kb"]  = (int)((heapTraceDeltaAt(i) + 512) / 1024);
        }
    }

    out["cpu_mhz"] = (int)getCpuFrequencyMhz();
    out["wifi_channel"] = wifiConnected ? WiFi.channel() : 0;

#ifdef ARDUINO_ARCH_ESP32
    {
        esp_chip_info_t chipInfo;
        esp_chip_info(&chipInfo);

        const char* chipModel = "ESP32";
        #if CONFIG_IDF_TARGET_ESP32S2
            chipModel = "ESP32-S2";
        #elif CONFIG_IDF_TARGET_ESP32S3
            chipModel = "ESP32-S3";
        #elif CONFIG_IDF_TARGET_ESP32C3
            chipModel = "ESP32-C3";
        #elif CONFIG_IDF_TARGET_ESP32C6
            chipModel = "ESP32-C6";
        #elif CONFIG_IDF_TARGET_ESP32H2
            chipModel = "ESP32-H2";
        #elif defined(CONFIG_IDF_TARGET_ESP32P4)
            chipModel = "ESP32-P4";
        #endif
        out["chip_model"] = chipModel;

        char revision[16];
        snprintf(revision, sizeof(revision), "%d.%d", chipInfo.revision / 100, chipInfo.revision % 100);
        out["chip_revision"] = String(revision);
        out["cpu_cores"] = chipInfo.cores;
        // On P4, WiFi is provided by the C6 companion via SDIO — no integrated WiFi/BT flags.
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
        out["chip_features"] = "WiFi(C6-SDIO)+RMII-ETH";
#else
        out["chip_features"] = (chipInfo.features & CHIP_FEATURE_BT) ? "WiFi+BT"
                                : ((chipInfo.features & CHIP_FEATURE_BLE) ? "WiFi+BLE" : "WiFi");
#endif
    }

    {
        uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
        esp_wifi_get_mac(WIFI_IF_STA, mac);
#else
        // On P4 WiFi MAC is owned by the C6 companion; read via WiFi.h instead.
        WiFi.macAddress(mac);
#endif
        char macString[18];
        snprintf(macString, sizeof(macString), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        out["wifi_mac"] = String(macString);
    }

    {
        esp_reset_reason_t reason = esp_reset_reason();
        const char* reasonName = "UNKNOWN";
        switch (reason) {
            case ESP_RST_POWERON: reasonName = "POWERON"; break;
            case ESP_RST_EXT: reasonName = "EXT"; break;
            case ESP_RST_SW: reasonName = "SW"; break;
            case ESP_RST_PANIC: reasonName = "PANIC"; break;
            case ESP_RST_INT_WDT: reasonName = "INT_WDT"; break;
            case ESP_RST_TASK_WDT: reasonName = "TASK_WDT"; break;
            case ESP_RST_WDT: reasonName = "WDT"; break;
            case ESP_RST_DEEPSLEEP: reasonName = "DEEPSLEEP"; break;
            case ESP_RST_BROWNOUT: reasonName = "BROWNOUT"; break;
            case ESP_RST_SDIO: reasonName = "SDIO"; break;
            default: reasonName = "OTHER"; break;
        }
        out["reset_reason"] = reasonName;
    }

    {
        float tempC = temperatureRead();
        if (!isnan(tempC) && tempC >= -40.0f && tempC <= 125.0f) {
            out["chip_temp_c"] = tempC;
        }
    }
#endif

    bool ntpSynced = false;
    {
        struct tm timeInfo;
        memset(&timeInfo, 0, sizeof(timeInfo));
        // Short timeout: if NTP is not yet synced, do not block the HTTP task.
        if (getLocalTime(&timeInfo, 10)) {
            char timeBuffer[40];
            strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M %Z", &timeInfo);
            out["local_time"] = String(timeBuffer);
            ntpSynced = true;
        }
    }
    out["ntp_sync"] = ntpSynced;

    int preferencesFree = 50;
    int nvsFreeKb = -1;
    int nvsTotalKb = -1;
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // TOUCH7: use the boot-cached NVS stats (nvs_get_stats/PARTITION scan read
    // flash, which panics while the RGB panel streams with the bounce buffer).
    {
        extern int touch7StatNvsTotalKb, touch7StatNvsFreeKb;
        nvsTotalKb = touch7StatNvsTotalKb;
        nvsFreeKb = touch7StatNvsFreeKb;
        if (nvsTotalKb > 0 && nvsFreeKb >= 0) {
            preferencesFree = (int)(100.0f * (float)nvsFreeKb / (float)nvsTotalKb);
        }
    }
#else
    try {
        nvs_stats_t nvsStats;
        esp_err_t err = nvs_get_stats(NULL, &nvsStats);
        if (err == ESP_OK && nvsStats.total_entries > 0) {
            float usagePct = (float)nvsStats.used_entries / (float)nvsStats.total_entries * 100.0f;
            float freePct = 100.0f - usagePct;
            if (freePct < 0) freePct = 0;
            if (freePct > 100) freePct = 100;
            preferencesFree = (int)(freePct + 0.5f);
            const esp_partition_t* nvsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
            if (nvsPart) {
                nvsTotalKb = (int)(nvsPart->size / 1024UL);
                uint32_t freeEntries = nvsStats.total_entries - nvsStats.used_entries;
                if (freeEntries > nvsStats.total_entries) {
                    freeEntries = 0;
                }
                double ratio = (double)freeEntries / (double)nvsStats.total_entries;
                uint64_t freeBytes = (uint64_t)(ratio * (double)nvsPart->size);
                nvsFreeKb = (int)(freeBytes / 1024ULL);
            }
        }
    } catch (...) {
        preferencesFree = 50;
    }
#endif
    out["preferences_free"] = preferencesFree;
    if (nvsFreeKb >= 0) out["nvs_free_kb"] = nvsFreeKb;
    if (nvsTotalKb >= 0) out["nvs_total_kb"] = nvsTotalKb;
    if (nvsFreeKb >= 0 && nvsTotalKb >= 0) {
        int nvsUsedKb = nvsTotalKb - nvsFreeKb;
        if (nvsUsedKb < 0) nvsUsedKb = 0;
        out["nvs_used_kb"] = nvsUsedKb;
        out["nvs_usage_pct"] = (nvsTotalKb > 0) ? (nvsUsedKb * 100 / nvsTotalKb) : 0;
    }

#if ENABLE_FILESYSTEM==1
    out["flash_fs_enabled"] = true;
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // TOUCH7: boot-cached LittleFS stats (littlefsTotalBytes/UsedBytes read
    // flash -> panic while the RGB panel streams with the bounce buffer).
    {
        extern bool touch7StatFlashMounted;
        extern uint64_t touch7StatFlashTotal, touch7StatFlashUsed, touch7StatFlashFree;
        bool flashMounted = touch7StatFlashMounted;
        out["flash_fs_mounted"] = flashMounted;
        if (flashMounted) {
            uint64_t flashTotal = touch7StatFlashTotal;
            uint64_t flashUsed = touch7StatFlashUsed;
            uint64_t flashFree = touch7StatFlashFree;
            uint64_t flashWriteCount = flashGetWriteCount();
            uint64_t flashBytesWritten = flashGetBytesWritten();
            if (flashTotal > 0) {
                out["flash_total_mb"] = (int)(flashTotal / (1024ULL * 1024ULL));
                out["flash_used_mb"] = (int)(flashUsed / (1024ULL * 1024ULL));
                out["flash_free_mb"] = (int)(flashFree / (1024ULL * 1024ULL));
                out["flash_usage_pct"] = (int)((flashUsed * 100ULL) / flashTotal);
            }
            out["flash_write_count"] = (unsigned long)flashWriteCount;
            out["flash_bytes_written_kb"] = (int)(flashBytesWritten / 1024ULL);
            out["flash_today_writes"] = storLfsTodayW();
            out["flash_today_bytes"] = (unsigned long)storLfsTodayB();
            out["flash_avg_writes_day"] = (unsigned long)(flashWriteCount / (uint64_t)storDaysTracked());
            out["cfg_saves_today"] = storCfgToday();
        }
    }
#else
    // Ensure status reflects real runtime state, even when the FS was not touched yet.
    // littlefsMounted() alone can be false before first littlefsBegin() adoption/mount.
    bool flashMounted = littlefsBegin() && littlefsMounted();
    out["flash_fs_mounted"] = flashMounted;
    if (flashMounted) {
        uint64_t flashTotal = littlefsTotalBytes();
        uint64_t flashUsed = littlefsUsedBytes();
        uint64_t flashFree = littlefsFreeBytes();
        uint64_t flashWriteCount = flashGetWriteCount();
        uint64_t flashBytesWritten = flashGetBytesWritten();
        if (flashTotal > 0) {
            out["flash_total_mb"] = (int)(flashTotal / (1024ULL * 1024ULL));
            out["flash_used_mb"] = (int)(flashUsed / (1024ULL * 1024ULL));
            out["flash_free_mb"] = (int)(flashFree / (1024ULL * 1024ULL));
            out["flash_usage_pct"] = (int)((flashUsed * 100ULL) / flashTotal);
        }
        out["flash_write_count"] = (unsigned long)flashWriteCount;
        out["flash_bytes_written_kb"] = (int)(flashBytesWritten / 1024ULL);
        out["flash_today_writes"] = storLfsTodayW();
        out["flash_today_bytes"] = (unsigned long)storLfsTodayB();
        out["flash_avg_writes_day"] = (unsigned long)(flashWriteCount / (uint64_t)storDaysTracked());
        out["cfg_saves_today"] = storCfgToday();
    }
#endif
#else
    out["flash_fs_enabled"] = false;
    out["flash_fs_mounted"] = false;
#endif

    uint64_t nvsWriteCount = nvsGetWriteCount();
    uint64_t nvsBytesWritten = nvsGetBytesWritten();
    out["nvs_write_count"] = (unsigned long)nvsWriteCount;
    out["nvs_bytes_written_kb"] = (int)(nvsBytesWritten / 1024ULL);

#if ENABLE_SDCARD==1
    out["sd_enabled"] = true;
    bool sdMounted = sdcardIsMounted();
    out["sd_mounted"] = sdMounted;
    out["sd_fallback_mode"] = sdcardInFallbackMode();
    out["sd_detected"] = sdMounted || sdcardDetect();
    if (sdMounted) {
        uint64_t sdTotal = sdcardTotalBytes();
        uint64_t sdUsed = sdcardUsedBytes();
        unsigned long writeCount = sdcardGetWriteCount();
        unsigned long writeCountTotal = sdcardGetWriteCountTotal();
        uint64_t bytesWritten = sdcardGetBytesWritten();
        uint64_t bytesWrittenTotal = sdcardGetBytesWrittenTotal();
        if (sdTotal > 0) {
            out["sd_total_mb"] = (int)(sdTotal / (1024ULL * 1024ULL));
            out["sd_used_mb"] = (int)(sdUsed / (1024ULL * 1024ULL));
            out["sd_free_mb"] = (int)((sdTotal - sdUsed) / (1024ULL * 1024ULL));
            double pct = ((double)sdUsed * 100.0) / (double)sdTotal;
            out["sd_usage_pct"] = (int)(pct + 0.5);
        }
        out["sd_write_count_total"] = writeCountTotal;
        out["sd_bytes_written_total_mb"] = (int)(bytesWrittenTotal / (1024ULL * 1024ULL));
        out["sd_today_writes"] = storSdTodayW();
        out["sd_today_bytes"] = (unsigned long)storSdTodayB();
        out["sd_avg_writes_day"] = (unsigned long)(writeCountTotal / (uint64_t)storDaysTracked());
        out["sd_avg_bytes_day"] = (unsigned long)((bytesWrittenTotal / (uint64_t)storDaysTracked()));

        // Cache SD folder/file counts — directory scan is expensive (10ms per entry).
        // Rebuild at most once every 2 minutes to avoid blocking the HTTP task.
        static int s_sdFolderCount = 0;
        static int s_sdFileCount = 0;
        static unsigned long s_sdCountCacheMs = 0;
        if (millis() - s_sdCountCacheMs > 120000UL) {
            sdcardCountFoldersAndFiles(s_sdFolderCount, s_sdFileCount);
            s_sdCountCacheMs = millis();
        }
        out["sd_folder_count"] = s_sdFolderCount;
        out["sd_file_count"] = s_sdFileCount;

        out["sd_push_mutex_fail_count_rs232"] = getSdcardPushMutexFailCountRs232();
        out["sd_push_pointer_null_count_rs232"] = getSdcardPushPointerNullCountRs232();

        if (bytesWrittenTotal > 0 && sdTotal > 0) {
            const unsigned long conservativeCycles = 25000;
            uint64_t lifetimeBytes = sdTotal * conservativeCycles;
            int lifeUsedPct = (int)((bytesWrittenTotal * 100ULL) / lifetimeBytes);
            if (lifeUsedPct > 100) lifeUsedPct = 100;
            out["sd_life_used_pct"] = lifeUsedPct;
        }
    }
#else
    out["sd_enabled"] = false;
    out["sd_mounted"] = false;
    out["sd_fallback_mode"] = true;
    out["sd_detected"] = false;
#endif

    out["sd_logging_enabled"] = configManager.getSdcardEnabled();
    out["sd_email_send"] = configManager.getSdcardEmailSend();
    out["sd_email_from_sd"] = configManager.getSdcardEmailFromSd();
    out["sd_emailday_extra"] = configManager.getEmaildayExtraEmails();
    out["sd_structured"] = configManager.getSdcardStructured();
    out["sd_format"] = configManager.getSdcardFormat();
    out["sd_max_size_mb"] = configManager.getSdcardMaxSize();

    out["storage_mode"] = getStorageModeString();
    out["storage_volatile"] = (currentStorageMode == STORAGE_MODE_PSRAM);
    if (currentStorageMode == STORAGE_MODE_PSRAM) {
        out["psram_vfs_enabled"] = psramVFSIsEnabled();
        if (psramVFSIsEnabled()) {
            size_t usedBytes = psramVFSGetUsedBytes();
            size_t freeBytes = psramVFSGetFreeBytes();
            size_t totalBytes = usedBytes + freeBytes;
            out["psram_vfs_used_kb"] = (int)(usedBytes / 1024);
            out["psram_vfs_free_kb"] = (int)(freeBytes / 1024);
            out["psram_vfs_total_kb"] = (int)(totalBytes / 1024);
            out["psram_vfs_folders"] = (int)psramVFSGetFolderCount();
            out["psram_vfs_files"] = (int)psramVFSGetTotalFileCount();
            if (totalBytes > 0) {
                out["psram_vfs_usage_pct"] = (int)((usedBytes * 100ULL) / totalBytes);
            }
        }
    }

    JsonObject emailObj = out.createNestedObject("email");
    emailObj["auto_send"] = configManager.getEmailAutoSend();
    emailObj["smtp_server"] = configManager.getSmtpServer();
    emailObj["smtp_port"] = configManager.getSmtpPort();
    emailObj["smtp_username"] = configManager.getSmtpUsername();
    emailObj["smtp_tls_insecure"] = configManager.getSmtpTlsInsecure();
    emailObj["google_script_url"] = configManager.getGoogleScriptUrl();
    emailObj["method"] = configManager.getEmailMethod();
    emailObj["destination"] = configManager.getEmailDestination();
    emailObj["subject"] = configManager.getEmailSubject();
    emailObj["max_lines"] = configManager.getEmailMaxLines();
    emailObj["min_content_length"] = configManager.getEmailMinContentLength();
    emailObj["dont_copy_sender"] = configManager.getEmailDontCopySender();
    emailObj["attach_txt"] = configManager.getEmailAttachTxt();
    emailObj["include_timestamp"] = configManager.getEmailIncludeTimestamp();

#if ENABLE_UNIKON==1
    {
        UnikonConfig uc = unikonGetConfig();
        UnikonStatus us = unikonGetStatus();
        out["unikon_enabled"] = uc.enabled;
        out["unikon_interval_sec"] = uc.pollIntervalSec;
        out["unikon_passive_listen"] = uc.passiveListen;
        out["unikon_connected"] = us.connected;
        out["unikon_last_poll_ms"] = us.lastPollMs;
        out["unikon_total_known"] = us.totalKnown;
        out["unikon_last_new_count"] = us.lastNewCount;
        out["unikon_registered_count"] = us.registeredCount;
        out["unikon_contest_count"] = us.contestCount;
        out["unikon_fancier_name"] = String(us.fancierName);
        out["unikon_race_name"] = String(us.raceName);
        out["unikon_race_date"] = String(us.raceDate);
        out["unikon_club_name"] = String(us.clubName);
    }
#endif

#if ENABLE_RS232==1
    int idx = configManager.getRs232BaudrateIndex();
    int baud = (idx >= 0 && idx < RS232_BAUDRATES_COUNT) ? RS232_BAUDRATES[idx] : 9600;
    out["rs232_enabled"] = configManager.getRs232Enabled();
    out["rs232_mirror_to_usb"] = configManager.getRs232MirrorToUsb();
    out["rs232_baudrate"] = baud;
    out["rs232_baudrate_index"] = idx;
    out["rs232_invert_pins"] = configManager.getRs232InvertPins();
    out["invert_pins"] = out["rs232_invert_pins"];
    out["rs232_data_bits"] = configManager.getRs232DataBits();
    out["rs232_parity"] = configManager.getRs232Parity();
    out["rs232_stop_bits"] = configManager.getRs232StopBits();
    out["rs232_read_timeout_ms"] = configManager.getRs232ReadTimeoutMs();
    out["rs232_use_xon_xoff"] = configManager.getRs232UseXonXoff();
    out["use_xon_xoff"] = out["rs232_use_xon_xoff"];

    out["rs232_rx_pin"] = rs232_rx_pin;
    out["rs232_tx_pin"] = rs232_tx_pin;
    out["rs232_active_template"] = lastAppliedRs232TemplateName;
    out["rs232_system_baudrate"] = rs232_baudrate;
    out["rs232_system_parity"] = rs232_parity;
    out["rs232_system_data_bits"] = rs232_data_bits;
    out["rs232_system_stop_bits"] = rs232_stop_bits;

    {
        String currentTemplate = configManager.getRs232CurrentTemplate();
        out["rs232_current_template"] = currentTemplate;
    }
#else
    out["rs232_enabled"] = false;
#endif // ENABLE_RS232
    out["printer_mode"] = configManager.getPrinterEmulationMode();
    out["charset_mode"] = configManager.getPrinterCharsetMode();

    int bufSizeSnapshot = 0;
    int bufCapSnapshot = 0;
    if (!bufferDadosBusy) {
        bufSizeSnapshot = (int)bufferDados.size();
        bufCapSnapshot = (int)bufferDados.capacity();
    } else {
        bufCapSnapshot = (int)bufferDados.capacity();
    }
    out["buffer_size"] = bufSizeSnapshot;
    out["max_buffer_size"] = bufCapSnapshot;
    out["buffer_usage_pct"] = (bufCapSnapshot > 0)
        ? (int)(((double)bufSizeSnapshot / (double)bufCapSnapshot) * 100.0 + 0.5)
        : 0;
    out["buffer_dados_mutex_fail_count"] = (int)bufferDadosMutexFailCount;
    out["buffer_dados_push_mutex_fail_count"] = (int)bufferDadosPushMutexFailCount;
    out["active_html_sessions"] = activeHtmlSessions;
    out["rs232_overflows"] = (uint32_t)overflowCount;
    out["eth_wifi_state"] = 0;

    {
        unsigned long nowMs = millis();
        unsigned long currentSeq = bufferDadosSeq;
        unsigned long deltaSeq = (currentSeq >= state.lastRxSeq)
            ? (currentSeq - state.lastRxSeq)
            : ((0xFFFFFFFFUL - state.lastRxSeq) + currentSeq + 1UL);
        unsigned long deltaMs = (state.lastRxMs == 0) ? 0 : (nowMs - state.lastRxMs);
        int rxBps = -1;
        if (deltaMs >= 300) {
            double bps = ((double)deltaSeq * 1000.0) / (double)deltaMs;
            if (bps < 0) bps = 0;
            if (bps > 2147483647.0) bps = 2147483647.0;
            rxBps = (int)(bps + 0.5);
        }
        out["rx_bps"] = rxBps;
        out["rx_bytes_total"] = (uint32_t)currentSeq;
        state.lastRxSeq = currentSeq;
        state.lastRxMs = nowMs;
    }

    out["hybrid_wifi_state"] = getCurrentHybridStateValue();
    out["ap_active"] = apActive;
    out["ap_lock"] = apLockedUntilReboot;
    out["telnet_active"] = wifiConnected && configManager.getTelnetEnabled();

    out["firmware_version"] = FIRMWARE_VERSION;
    out["firmware_build"] = FIRMWARE_BUILD;

    out["feature_rs232"] = (ENABLE_RS232 == 1);
    out["feature_ethernet"] = (ENABLE_ETHERNET == 1);
    out["feature_sdcard"] = (ENABLE_SDCARD == 1);
    out["feature_smtp"] = (ENABLE_SMTP == 1);
    out["feature_telnet"] = (ENABLE_TELNET == 1);
    out["feature_ntp"] = (ENABLE_NTP == 1);
    out["feature_scheduler"] = (ENABLE_SCHEDULER == 1);
    out["feature_wifi"] = (ENABLE_WIFI == 1);
    out["feature_ota"] = (ENABLE_OTA == 1);
    out["feature_db25"] = (ENABLE_DB25 == 1);
    out["feature_modbus"] = (ENABLE_MODBUS == 1);
    out["feature_modbus_rtu"] = (ENABLE_MODBUS_RTU == 1);
    out["feature_modbus_tcp"] = (ENABLE_MODBUS_TCP == 1);
    out["feature_modbus_slave"] = (ENABLE_MODBUS_SLAVE == 1);
    out["feature_unikon"] = (ENABLE_UNIKON == 1);
    out["feature_can"] = (ENABLE_CAN == 1);
    out["feature_camera"] = (ENABLE_CAMERA == 1);
    out["feature_proximity"] = (ENABLE_PROXIMITY == 1);
    out["feature_buzzer"] = (ENABLE_BUZZER == 1);
    out["feature_uda1334a"] = (ENABLE_UDA1334A == 1);
    out["feature_sensor_aht20_bmp280"] = (ENABLE_AHT20_BMP280 == 1);
    out["feature_sensor_mq135"] = (ENABLE_MQ135 == 1);
    out["feature_ipg_laser"] = (ENABLE_IPG_LASER == 1);
#if defined(ENABLE_USB_HOST)
    out["feature_usb_host"] = (ENABLE_USB_HOST == 1);
#else
    out["feature_usb_host"] = false;
#endif
#if defined(ENABLE_GRBL)
    out["feature_grbl"] = (ENABLE_GRBL == 1);
#else
    out["feature_grbl"] = false;
#endif
    out["feature_psram"] = (ESP.getPsramSize() > 0);

    // Heap breakdown: static estimates per compiled module.
    // Approximate values based on ESP-IDF 5.x / arduino-esp32 3.x typical measurements.
    // These are compile-time estimates, not runtime measurements.
    {
        JsonArray hb = out.createNestedArray("heap_breakdown");
        // trace_key: checkpoint label whose MEASURED delta is this module's cost
        // ("" = no checkpoint -> measured unknown, est stays the only figure).
        auto addHb = [&](const char* name, int estKb, const char* color,
                         const char* traceKey = "") {
            JsonObject e = hb.createNestedObject();
            e["name"] = name;
            e["est_kb"] = estKb;
            e["color"] = color;
            if (traceKey && traceKey[0]) {
                const HeapTracePoint* tp = heapTraceFind(traceKey);
                if (tp) {
                    e["meas_kb"] = (int)((heapTraceDeltaFor(traceKey) + 512) / 1024);
                    e["trace_key"] = traceKey;
                }
            }
        };
        addHb("FreeRTOS + Base ESP-IDF", 28, "#6366f1");   // baseline: used before our modules
#if ENABLE_WIFI == 1
  #if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
        addHb("WiFi C6-SDIO (esp-hosted)", 32, "#3b82f6", "wifi");
  #else
        addHb("WiFi stack (lwIP+NVS)", 55, "#3b82f6", "wifi");
  #endif
  #if ENABLE_WIFI_WEB_SERVER == 1
        addHb("AsyncWebServer", 14, "#0ea5e9", "webserver");
  #endif
#endif // ENABLE_WIFI
#if ENABLE_ETHERNET == 1
        addHb("Ethernet W5500", 3, "#06b6d4", "ethernet");
#endif
#if ENABLE_OTA == 1
        addHb("OTA", 10, "#8b5cf6");
#endif
#if ENABLE_SMTP == 1
        addHb("SMTP/TLS", 25, "#f59e0b", "email");
#endif
#if ENABLE_SDCARD == 1
        addHb("SD card (SD_MMC)", 7, "#10b981", "sd");
#endif
#if ENABLE_FILESYSTEM == 1
        addHb("LittleFS", 5, "#34d399", "littlefs");
#endif
#if ENABLE_TELNET == 1
        addHb("Telnet", 4, "#a78bfa");
#endif
#if ENABLE_RS232 == 1
        addHb("RS232 UART", 4, "#fb923c");
#endif
#if ENABLE_DB25 == 1
        addHb("DB25 bridge", 2, "#fbbf24");
#endif
#if ENABLE_MODBUS == 1
        addHb("Modbus", 8, "#f97316");
#endif
#if ENABLE_AHT20_BMP280 == 1
        addHb("Sensor AHT20+BMP280", 2, "#4ade80");
#endif
#if ENABLE_MQ135 == 1
        addHb("Sensor MQ135", 1, "#86efac");
#endif
#if ENABLE_PROXIMITY == 1
        addHb("Sensor Proximidade", 2, "#bbf7d0", "proximity");
#endif
#if !(defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT))
        // Main circular buffer on internal heap when no PSRAM is available.
        {
            int bufKb = (int)((bufferDados.capacity() + 1023) / 1024);
            if (bufKb > 0) addHb("Buffer dados (sem PSRAM)", bufKb, "#ef4444");
        }
#endif
    }

    out["timeout_active"] = configManager.getTimeoutAutoEmailEnabled();
    out["timeout_inactive"] = configManager.getTimeoutAutoEmailDisabled();
    out["telnet_timeout"] = configManager.getTimeoutTelnetInactivity();

    out["structures_auto_detect"] = configManager.getStructuresAutoDetectEnabled();
    out["amada_isq20_mtf1_enabled"] = configManager.getAmadaISQ20MTF1Enabled();
    out["amada_isq20_awse_enabled"] = configManager.getAmadaISQ20AWSEEnabled();
    out["amada_filter_headers"] = configManager.getAmadaFilterHeaders();
    out["amada_header_every"] = configManager.getAmadaHeaderEvery();

    unsigned long totalSecs = (millis() - bootTime) / 1000UL;
    unsigned long days = totalSecs / 86400UL;
    unsigned long rem = totalSecs % 86400UL;
    unsigned long hours = rem / 3600UL;
    rem %= 3600UL;
    unsigned long mins = rem / 60UL;
    unsigned long secs = rem % 60UL;
    char uptime[48];
    if (days > 0) {
        snprintf(uptime, sizeof(uptime), "%lud %02lu:%02lu:%02lu", days, hours, mins, secs);
    } else {
        snprintf(uptime, sizeof(uptime), "%02lu:%02lu:%02lu", hours, mins, secs);
    }
    out["uptime"] = String(uptime);

    JsonArray recipientsArray = out.createNestedArray("recipients_active");
    JsonArray recipientSlots = configManager.getRecipientSlots();
    for (size_t i = 0; i < recipientSlots.size() && i < 10; i++) {
        JsonObject slot = recipientSlots[i];
        String name = slot["name"] | "";
        String op = slot["op"] | "I";
        String emails = slot["emails"] | "";
        int freq = slot["freq"] | 0;
        if (name.length() > 0 && emails.length() > 0) {
            JsonObject recipient = recipientsArray.createNestedObject();
            recipient["name"] = name;
            recipient["op"] = op;
            recipient["emails"] = emails;
            recipient["freq"] = freq;
        }
    }

    JsonArray schedCmdArray = out.createNestedArray("sched_cmd_active");
    for (int i = 1; i <= 10; i++) {
        String cmd, unit, daysMask, tpl, type;
        int interval = 0;
        bool enabled = false;
        if (configManager.getSchedCmdSlot(i, cmd, interval, unit, daysMask, tpl, enabled, type)) {
            if (enabled && cmd.length() > 0 && interval > 0) {
                JsonObject entry = schedCmdArray.createNestedObject();
                entry["i"] = i;
                entry["cmd"] = cmd;
                entry["interval"] = interval;
                entry["unit"] = unit;
                entry["days"] = daysMask;
                entry["type"] = type;
            }
        }
    }

    JsonArray schedDtArray = out.createNestedArray("sched_dt_active");
    for (int i = 1; i <= 8; i++) {
        String cmd, date, time, tpl, type;
        bool enabled = false;
        bool daily = false;
        if (configManager.getSchedDtSlot(i, cmd, date, time, tpl, enabled, type, daily)) {
            if (enabled && cmd.length() > 0 && (date.length() > 0 || time.length() > 0)) {
                JsonObject entry = schedDtArray.createNestedObject();
                entry["i"] = i;
                entry["cmd"] = cmd;
                entry["date"] = date;
                entry["time"] = time;
                entry["type"] = type;
                if (daily) entry["daily"] = true;
            }
        }
    }

    JsonArray dailyRebootArray = out.createNestedArray("daily_reboot_active");
    for (int i = 1; i <= 2; i++) {
        String time;
        bool enabled = false;
        if (configManager.getDailyRebootSlot(i, time, enabled)) {
            if (enabled && time.length() > 0) {
                JsonObject entry = dailyRebootArray.createNestedObject();
                entry["i"] = i;
                entry["time"] = time;
            }
        }
    }

    out["scheduled_cmd_count"] = schedCmdArray.size();
    out["scheduled_dt_count"] = schedDtArray.size();
    out["email_destination"] = configManager.getEmailDestination();
    out["emailday_extra_emails"] = configManager.getEmaildayExtraEmails();

    {
        JsonObject inactivity = out.createNestedObject("inactivity_alerts");
        inactivity["sensors_reset_timer"] = configManager.getInactivityAlertSensorsResetTimer();

        JsonArray slotsArr = inactivity.createNestedArray("slots");
        for (int i = 0; i < ConfigManager::ALERT_SLOT_COUNT; ++i) {
            JsonObject slot = slotsArr.createNestedObject();
            slot["enabled"] = configManager.getAlertSlotEnabled(i);
            slot["timeout_minutes"] = configManager.getAlertSlotTimeout(i);
            slot["max_per_day"] = configManager.getAlertSlotMaxPerDay(i);
            slot["emails"] = configManager.getAlertSlotEmails(i);
            slot["alerts_sent_today"] = inactivityAlertsTodaySlot[i];
            slot["alert_sent_this_period"] = inactivityAlertSentSlot[i];
        }

        inactivity["alerts_sent_today"] = getInactivityAlertsToday();

        if (lastDataReceivedForInactivityAlert > 0) {
            unsigned long timeSinceData = millis() - lastDataReceivedForInactivityAlert;
            inactivity["seconds_since_last_data"] = (uint32_t)(timeSinceData / 1000);
            // Use shortest enabled timeout for the global pct display
            unsigned long timeoutMs = (unsigned long)configManager.getInactivityAlertTimeoutMinutes() * 60UL * 1000UL;
            if (timeoutMs > 0) {
                int pct = (int)((timeSinceData * 100) / timeoutMs);
                if (pct > 100) pct = 100;
                inactivity["timeout_elapsed_pct"] = pct;
            } else {
                inactivity["timeout_elapsed_pct"] = 0;
            }
        } else {
            inactivity["seconds_since_last_data"] = 0;
            inactivity["timeout_elapsed_pct"] = 0;
            inactivity["waiting_for_first_data"] = true;
        }
    }

    {
        JsonArray recentIPsArr = out.createNestedArray("recent_ips");
        fillRecentUniqueIPs(recentIPsArr, 4);
    }
}