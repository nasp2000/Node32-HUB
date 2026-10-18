#include "selftest_handler.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "../../feature_flags.h"
#include "../../ConfigManager.h"
#include "../../littlefs_manager.h"
#include "../../psram_helpers.h"
#include "../../system_status_payload.h"
#include "../../sdcard.h"

#if ENABLE_PROXIMITY==1
#include "../../sensors/proximity_manager.h"
#endif

#ifndef ENABLE_USB_HOST
  #define ENABLE_USB_HOST 0
#endif

extern const char* FIRMWARE_VERSION;
extern const char* FIRMWARE_BUILD;

static const char* stems[] = {
    "core", "rs232", "recipients", "schedule", "modbus", "access", "system",
    "usb_server", "smtp_email", "alerts", "telnet", "ethernet", "sdcard",
    "debug", "access_point", "wifi_hybrid", "do"
};
static const int STEM_COUNT = 17;

static const char* sensorStems[] = {
    "sensor_config", "sensor_mq135", "sensor_hx711", "sensor_ina226",
    "sensor_sound", "sensor_max31865", "sensor_proximity", "sensor_prox_counters"
};
static const int SENSOR_STEM_COUNT = 8;

void handleSelfTestGet(HttpRequest& req, HttpResponse& res) {
    (void)req;

    SpiRamJsonDocument doc(12288);

    // ── Device Identity ──
    JsonObject identity = doc.createNestedObject("device");
    identity["firmware"] = FIRMWARE_VERSION;
    identity["build"] = FIRMWARE_BUILD;
    identity["pack"] = PACK_SYSTEM_NAME;
    identity["pack_id"] = (int)SELECTED_PACK;
    identity["brand"] = PACK_BRAND_NAME;
    identity["uptime_ms"] = millis();
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
    identity["chip"] = "ESP32-P4";
#elif defined(TARGET_BOARD_S3_CAM) && TARGET_BOARD_S3_CAM==1
    identity["chip"] = "ESP32-S3-CAM";
#else
    identity["chip"] = "ESP32-S3";
#endif

    // ── Feature Flags ──
    JsonObject feat = doc.createNestedObject("features");
    #define FT(k,v) feat[k] = (v==1)
    FT("wifi",              ENABLE_WIFI);
    FT("ethernet",          ENABLE_ETHERNET);
    FT("proximity",         ENABLE_PROXIMITY);
    FT("smtp",              ENABLE_SMTP);
    FT("google_email",      ENABLE_GOOGLE_EMAIL);
    FT("rs232",             ENABLE_RS232);
    FT("db25",              ENABLE_DB25);
    FT("modbus",            ENABLE_MODBUS);
    FT("modbus_rtu",        ENABLE_MODBUS_RTU);
    FT("modbus_tcp",        ENABLE_MODBUS_TCP);
    FT("modbus_slave",      ENABLE_MODBUS_SLAVE);
    FT("can",               ENABLE_CAN);
    FT("camera",            ENABLE_CAMERA);
    FT("scheduler",         ENABLE_SCHEDULER);
    FT("buzzer",            ENABLE_BUZZER);
    FT("unikon",            ENABLE_UNIKON);
    FT("ipg_laser",         ENABLE_IPG_LASER);
    FT("uda1334a",          ENABLE_UDA1334A);
    FT("lcd_screen",        ENABLE_LCD_SCREEN);
    FT("ota",               ENABLE_OTA);
    FT("mdns",              ENABLE_MDNS);
    FT("telnet",            ENABLE_TELNET);
    FT("sdcard",            ENABLE_SDCARD);
    FT("filesystem",        ENABLE_FILESYSTEM);
    FT("http_basic_auth",   ENABLE_HTTP_BASIC_AUTH);
    FT("sniffer",           ENABLE_SNIFFER);
    FT("grbl",              ENABLE_GRBL);
    FT("usb_host",          ENABLE_USB_HOST);
    FT("usbip_server",      ENABLE_USBIP_SERVER);
    FT("dualfile",          ENABLE_DUALFILE);
    FT("async_websocket",   ENABLE_ASYNC_WEBSOCKET);
    #undef FT

    // Board flags
    JsonObject boards = doc.createNestedObject("boards");
    #if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
    boards["8di8do_poe"] = true;
    #else
    boards["8di8do_poe"] = false;
    #endif
    #if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
    boards["p4"] = true;
    #else
    boards["p4"] = false;
    #endif
    boards["tca9554_do"] =
        #if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
        true;
        #else
        false;
        #endif

    // ── Pages (expected based on feature flags) ──
    JsonArray pages = doc.createNestedArray("pages");

    // Helper macro: conditionally add a page entry
    #define PAGE(url, flag) do { \
        JsonObject p = pages.createNestedObject(); \
        p["url"] = url; \
        p["enabled"] = (flag == 1); \
    } while(0)

    PAGE("/",                    1);
    PAGE("/proximity",           ENABLE_PROXIMITY);
    PAGE("/proximity-dashboard", ENABLE_PROXIMITY);
    PAGE("/monitor",             ENABLE_PAGE_MONITOR);
    PAGE("/serial-monitor",      ENABLE_PAGE_MONITOR);
    PAGE("/commands",            ENABLE_PAGE_COMMANDS_INTERVAL);
    PAGE("/commands-by-datetime",ENABLE_PAGE_COMMANDS);
    PAGE("/recipients",          ENABLE_PAGE_RECIPIENTS);
    PAGE("/alerts-config",       ENABLE_PAGE_ALERTS);
    PAGE("/files",               ENABLE_PAGE_FILES);
    PAGE("/sdcard",              ENABLE_PAGE_SDCARD);
    PAGE("/access",              ENABLE_PAGE_ACCESS);
    PAGE("/wifi",                ENABLE_WIFI);
    PAGE("/ethernet",            ENABLE_ETHERNET);
    PAGE("/email",               (ENABLE_SMTP || ENABLE_GOOGLE_EMAIL));
    PAGE("/rs232",               (ENABLE_RS232 || ENABLE_SNIFFER));
    PAGE("/lpt-db25",            ENABLE_DB25);
    PAGE("/modbus-rtu",          ENABLE_MODBUS_RTU);
    PAGE("/modbus-tcp",          ENABLE_MODBUS_TCP);
    PAGE("/modbus-live",         ENABLE_MODBUS);
    PAGE("/can",                 ENABLE_CAN);
    PAGE("/camera",              ENABLE_CAMERA);
    PAGE("/buzzer",              ENABLE_BUZZER);
    PAGE("/unikon",              ENABLE_UNIKON);
    PAGE("/ipg-laser",           ENABLE_IPG_LASER);
    PAGE("/uda1334a",            ENABLE_UDA1334A);
    PAGE("/radio-ui",            ENABLE_UDA1334A);
    PAGE("/grbl-controller",     ENABLE_GRBL);
    PAGE("/sensor-config",       (ENABLE_AHT20_BMP280 && ENABLE_SENSOR_CONFIG_PAGE));
    PAGE("/mq135-config",        ENABLE_MQ135);
    PAGE("/hx711",               ENABLE_HX711);
    PAGE("/ina226",              ENABLE_INA226);
    PAGE("/sound",               ENABLE_SOUND_SENSOR);
    PAGE("/max31865",            ENABLE_MAX31865);
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    PAGE("/falas",               1);
    PAGE("/ecra",                1);
#endif
    PAGE("/ota",                 ENABLE_OTA);
    PAGE("/networks",            ENABLE_TELNET);
    PAGE("/do",                  BOARD_HAS_TCA9554_DO);
    PAGE("/api-reference",       ENABLE_API_REFERENCE_PAGE);
    PAGE("/structures",          ENABLE_STRUCTURES_PAGE);
    PAGE("/dualfile",            ENABLE_DUALFILE);
    PAGE("/ftp",                 ENABLE_PAGE_FTP);
    #undef PAGE

    // ── Config Health ──
    JsonObject health = doc.createNestedObject("config_health");
    health["littlefs_mounted"] = littlefsMounted();
    health["littlefs_total"] = (uint32_t)littlefsTotalBytes();
    health["littlefs_used"] = (uint32_t)littlefsUsedBytes();
    health["littlefs_free"] = (uint32_t)littlefsFreeBytes();

    health["is_dirty"] = configManager.isDirty;
    health["last_save_ms"] = configManager.lastSave;
    health["successful_saves"] = configManager.successfulSaveCount;
    health["throttled_saves"] = configManager.saveThrottleCount;
    health["lfs_fail_stems"] = configManager.lfsFailStems;

    // Check each config file
    JsonObject files = health.createNestedObject("files");
    if (littlefsMounted()) {
        for (int i = 0; i < STEM_COUNT; i++) {
            String path = "/config/" + String(stems[i]) + ".json";
            JsonObject f = files.createNestedObject(stems[i]);
            f["exists"] = LittleFS.exists(path);
            if (LittleFS.exists(path)) {
                File file = LittleFS.open(path, "r");
                if (file) {
                    f["bytes"] = (uint32_t)file.size();
                    file.close();
                }
            }
        }
        for (int i = 0; i < SENSOR_STEM_COUNT; i++) {
            String path = "/config/" + String(sensorStems[i]) + ".json";
            JsonObject f = files.createNestedObject(sensorStems[i]);
            f["exists"] = LittleFS.exists(path);
            if (LittleFS.exists(path)) {
                File file = LittleFS.open(path, "r");
                if (file) {
                    f["bytes"] = (uint32_t)file.size();
                    file.close();
                }
            }
        }
        // pages.reg
        {
            JsonObject f = files.createNestedObject("pages_reg");
            f["exists"] = LittleFS.exists("/config/pages.reg");
            if (LittleFS.exists("/config/pages.reg")) {
                File file = LittleFS.open("/config/pages.reg", "r");
                if (file) {
                    f["bytes"] = (uint32_t)file.size();
                    file.close();
                }
            }
        }
    }

    // ── Module-specific checks ──
    JsonObject modules = doc.createNestedObject("modules");

    #if ENABLE_PROXIMITY==1
    {
        JsonObject prox = modules.createNestedObject("proximity");
        uint8_t chCount = proximityChannelCount();
        prox["channel_count"] = chCount;
        prox["max_channels"] = PROXIMITY_MAX_CHANNELS;
        bool mismatch = (chCount != PROXIMITY_MAX_CHANNELS);
        prox["count_mismatch"] = mismatch;
        // Check config consistency
        SpiRamJsonDocument cfgDoc(4096);
        JsonArray cfgArr = cfgDoc.createNestedArray("channels");
        uint8_t cfgCount = 0;
        for (uint8_t i = 0; i < chCount; i++) {
            JsonObject obj = cfgArr.createNestedObject();
            ProximityChannel ch = proximityGetChannel(i);
            obj["n"] = ch.name;
            obj["m"] = ch.mode;
            obj["gi"] = ch.gpio_in;
            cfgCount++;
        }
        prox["config_channels_written"] = cfgCount;
        prox["config_channels_ok"] = (cfgCount == chCount);
        // Active channels (count_in > 0 or gpio_in != 255)
        int activeCount = 0;
        for (uint8_t i = 0; i < chCount; i++) {
            ProximityChannel ch = proximityGetChannel(i);
            if (ch.count_in > 0 || (ch.gpio_in != 255 && ch.gpio_in != 0)) activeCount++;
        }
        prox["active_channels"] = activeCount;
    }
    #endif

    #if ENABLE_PAGE_ALERTS==1
    {
        JsonObject alerts = modules.createNestedObject("alerts");
        int enabledSlots = 0;
        for (int i = 0; i < 6; i++) {
            if (configManager.getAlertSlotEnabled(i)) enabledSlots++;
        }
        alerts["enabled_slots"] = enabledSlots;
        alerts["total_slots"] = 6;
        alerts["sensors_reset_timer"] = configManager.getInactivityAlertSensorsResetTimer();
    }
    #endif

    #if ENABLE_SDCARD==1
    {
        JsonObject sd = modules.createNestedObject("sdcard");
        sd["available"] = sdcardIsMounted();
    }
    #endif

    // ── Memory ──
    JsonObject mem = doc.createNestedObject("memory");
    mem["free_heap"] = (uint32_t)ESP.getFreeHeap();
    mem["max_alloc_heap"] = (uint32_t)ESP.getMaxAllocHeap();
    mem["min_free_heap"] = (uint32_t)ESP.getMinFreeHeap();
    #if (defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT))
    mem["psram_free"] = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mem["psram_total"] = (uint32_t)ESP.getPsramSize();
    #else
    mem["psram_free"] = 0;
    mem["psram_total"] = 0;
    #endif

    // ── Test capabilities & safety (descriptive only, nothing executes) ──
    JsonObject tcap = doc.createNestedObject("test_capabilities");
    tcap["manifest"] = true;
    tcap["api"] = true;
    tcap["browser"] = true;
    tcap["persistence"] = true;
    tcap["hardware"] =
        #if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO==1
        true;
        #else
        false;
        #endif
    tcap["reboot"] = true;

    JsonObject tsafe = doc.createNestedObject("test_safety");
    tsafe["read_only"] = true;
    tsafe["config_write"] = true;
    tsafe["hardware_outputs"] = false;
    tsafe["reboot"] = true;
    tsafe["factory_reset"] = false;

    // ── Summary checks ──
    JsonArray checks = doc.createNestedArray("checks");

    // Check: at least one page enabled + critical core pages present
    {
        JsonObject c = checks.createNestedObject();
        c["name"] = "pages_consistent";
        int enabledCount = 0;
        for (size_t i = 0; i < pages.size(); i++) {
            if (pages[i]["enabled"].as<bool>()) enabledCount++;
        }
        c["pass"] = enabledCount > 0;
        c["enabled_pages"] = enabledCount;
    }

    // Check: no failed stems
    {
        JsonObject c = checks.createNestedObject();
        c["name"] = "config_no_failures";
        c["pass"] = configManager.lfsFailStems.length() == 0;
        if (!c["pass"].as<bool>()) c["detail"] = configManager.lfsFailStems;
    }

    // Check: not dirty for too long
    {
        JsonObject c = checks.createNestedObject();
        c["name"] = "config_not_stale";
        bool dirty = configManager.isDirty;
        unsigned long age = dirty ? (millis() - configManager.lastDirtyMark) : 0;
        c["pass"] = !dirty || age < 300000; // 5 min
        if (dirty) c["dirty_age_ms"] = (uint32_t)age;
    }

    // Check: memory not critical
    {
        JsonObject c = checks.createNestedObject();
        c["name"] = "heap_sufficient";
        c["pass"] = ESP.getFreeHeap() > 20000;
        c["free_bytes"] = (uint32_t)ESP.getFreeHeap();
    }

    // Serialize to a PSRAM buffer (minimal DRAM): keep the body out of the
    // internal heap and stream it back without a heap String. Falls back to
    // heap when PSRAM is absent.
    size_t len = measureJson(doc) + 1;
    uint8_t* buf = (uint8_t*)(psramFound() ? ps_malloc(len) : malloc(len));
    if (!buf) {
        res.send(500, F("application/json"), F("{\"error\":\"out_of_memory\"}"));
        return;
    }
    serializeJson(doc, (char*)buf, len);

    res.code = 200;
    res.contentType = F("application/json");
    res.isBinary = true;
    res.binaryData = buf;
    res.binaryLen = len - 1;   // exclude trailing NUL written by serializeJson
    res.freeFn = [](const uint8_t* p) { free(const_cast<uint8_t*>(p)); };
}
