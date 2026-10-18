#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "arduinojson_fix.h" // 🔧 GCC 12.2.0 fix
#include <ArduinoJson.h> // 📄 Include the library for JSON handling
#include <Preferences.h> // 💾 Include the library for preference storage in flash memory
#include <nvs_flash.h> // 🧪 NVS diagnostics (nvs_stats_t, nvs_get_stats)
#include <esp_task_wdt.h> // Watchdog timer (prevents timeout during heavy NVS operations)
#include <map> // 🗺️ Include the library for maps (key-value associations)
#include <vector> // 🚶 Include the library for vectors (dynamic arrays)
#include <cstring> // ✅ strcmp to map old/new NVS keys

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
#include "touch7_display.h" // touch7ConfigFlushAllowed(), Touch7PanelHold
#endif
#include <cstdint> // Required for uint64_t
#include <sys/stat.h> // 🔍 POSIX stat() for file existence check without creating File objects
#include "json_keys.h" // 🔑 JSON keys in PROGMEM (FPSTR) to reduce heap
#include "screen_hooks.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "psram_helpers.h" // SpiRamJsonDocument - allocates large JSON docs in PSRAM

// Dual-storage architecture: NVS (boot-critical) + LittleFS (everything else)
#include "littlefs_manager.h" 
#include "NvsStore.h"
#include "LittleFsStore.h"
#include "ConfigValidator.h"
#include "config_utils.h"
#include "NtpManager.h"
#if FILESYSTEM_PROVIDER == 1
#include <LittleFS.h>
#endif
// Forward declarations for storage write counters (defined in chatgpt.ino)
void loadStorageStatsFromNVS();
bool saveStorageStatsToNVS();
bool resetStorageStats();
uint64_t nvsGetWriteCount();
uint64_t nvsGetBytesWritten();
void nvsIncrementWriteCount(uint64_t bytes);
// Daily storage stats (defined in sd_tasks_impl.h)
void storNoteSdWrite(uint64_t bytes);
void storNoteLfsWrite(uint64_t bytes);
bool storNoteCfgSave();
void storNoteCfgSaveForced();
void storPersistDaily(bool force);
uint32_t storDaysTracked();
uint32_t storSdTodayW();
uint64_t storSdTodayB();
uint32_t storLfsTodayW();
uint64_t storLfsTodayB();
uint32_t storCfgToday();
uint64_t flashGetWriteCount();
uint64_t flashGetBytesWritten();
void flashIncrementWriteCount(uint64_t bytes);

// 📢 Virtual notification (implemented in chatgpt.ino)
extern void sendVirtualMessage(const String &msg, int msgType, bool alreadyFormatted = false, bool bypassDedup = false);

class ConfigManager { // 👨‍💼 Class to manage device settings
private:
    NvsStore nvs;
    LittleFsStore lfs;
    SpiRamJsonDocument config;          // Core settings (wifi, smtp, rs232, web, telnet, ntp, etc.) — in PSRAM
    SpiRamJsonDocument recipDoc;         // recipients_slots + numeric_rules → /config/recipients.json — in PSRAM
    SpiRamJsonDocument schedDoc;         // sched_cmd + sched_dt + counters  → /config/sched.json — in PSRAM
    SpiRamJsonDocument modbusDoc;        // modbus section (devices array can be large) → /config/modbus.json — in PSRAM
    const unsigned long SAVE_DELAY = 1200; // Wait at least 1.2s after the last change before persisting
    const uint32_t SAVE_LOOP_PROTECTION_THRESHOLD = 100; // Max saves per minute (alarm threshold)
    static const size_t MAX_SAVED_NETWORKS = 5; // network limit
    
    // 🔍 DIAGNOSTICS (internal): Save loop tracking
    unsigned long lastThrottleReport = 0;   // Last time we reported throttle stats
    
    // 🔒 CRITICAL: Recursive mutex to protect ALL NVS/Preferences access across cores
    // Prevents xTaskPriorityDisinherit crash when Core 0 (web) and Core 1 (loop) access ConfigManager simultaneously
    SemaphoreHandle_t configMutex = NULL;
    unsigned long configMutexFailCount = 0; // Diagnostic counter
    TaskHandle_t configMutexOwnerTask = NULL; // 🔒 Track which task owns the mutex
    uint32_t configMutexDepth = 0; // 🔒 Track recursion depth

    // Helper: Try to lock ConfigManager with recursive mutex (safe for same-task reentry)
    inline bool configLock(TickType_t timeout = pdMS_TO_TICKS(5000)) {
        if (configMutex == NULL) {
            configMutex = xSemaphoreCreateRecursiveMutex();
            if (configMutex == NULL) {
                configMutexFailCount = configMutexFailCount + 1;
                return false;
            }
        }
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        if (configMutexOwnerTask == self) {
            // 🔒 Soft recursion: already own the mutex, just increment depth
            configMutexDepth++;
            return true;
        }
        if (xSemaphoreTakeRecursive(configMutex, timeout) != pdTRUE) {
            configMutexFailCount = configMutexFailCount + 1;
            return false;
        }
        configMutexOwnerTask = self;
        configMutexDepth = 1;
        return true;
    }
    
    inline void configUnlock() {
        if (configMutex == NULL) return;
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        // 🔒 CRITICAL: Only unlock if we own the mutex (prevents xTaskPriorityDisinherit crash)
        if (configMutexOwnerTask != self || configMutexDepth == 0) {
            // ⚠️ This task doesn't own the mutex or depth is already 0 - don't unlock!
            return;
        }
        configMutexDepth--;
        if (configMutexDepth == 0) {
            configMutexOwnerTask = NULL;
            xSemaphoreGiveRecursive(configMutex);
        }
    }
    
    // 📦 Hash of last saved JSON (FNV-1a 32-bit) to avoid keeping a large String on heap
    uint32_t lastSavedHash = 0; // 📦 Hash value of the last persisted configuration
    // ♻️ Removed legacy comment scheduled_commands.lastExecuted (mechanism eliminated)
    unsigned long lastSchedPersist = 0;
    // Templates persistence: keep template edits in RAM and persist after a long delay (e.g., 6 hours)
    bool templatesDirty = false;
    unsigned long lastTemplateChangeMillis = 0;
    const unsigned long TEMPLATES_PERSIST_DELAY_MS = 21600000UL; // 6 hours
    
    // CACHE: hot-path values to avoid frequent JSON lookups in the main loop.
    bool _cache_sdcard_enabled = true; // Cache sdcard.enabled (default true)
    String _cache_sdcard_flush_interval = "30m"; // Cache sdcard.flush_interval (default 30m)
    bool _cache_sd_auto_cleanup = true;     // Cache sdcard.auto_cleanup (default true)
    int _cache_sd_cleanup_threshold = 70;   // Cache sdcard.cleanup_threshold (percent, default 70)
    int _cache_sd_keep_days = 0;            // Cache sdcard.keep_days (default 0 = infinity: keep until 70%, then oldest goes)
    bool _cache_telnet_enabled = true; // Cache telnet.enabled (default true)
    bool _cache_wifi_disabled = false; // Cache wifi.runtime_disabled — persisted WiFi off switch
    bool _cache_boot_log_enabled = false; // Cache boot_log.enabled — persisted SD boot log switch
    bool _cache_web_auth_enabled = false; // Cache web.auth_enabled — HTTP Basic Auth master switch (default: off)
    
    // ============================================================================
    // DUAL STORAGE ARCHITECTURE (2025-02-17, updated 2025-02-18)
    // ============================================================================
    // NVS (~20KB): WiFi + SMTP + Email + Saved Networks (boot-critical)
    // LittleFS (1.4MB): Everything else (RS232, recipients, schedulers, etc.)
    // This reduces NVS wear and fragmentation while keeping boot-essential data safe
    // ============================================================================
    
    // NVS keys (ONLY boot-critical data - active storage)
    const char *NVS_KEY_WIFI           = "cfg_wifi";         // WiFi credentials (CRITICAL)
    const char *NVS_KEY_SAVED_NETWORKS = "cfg_saved_net";    // Saved WiFi networks (CRITICAL)
    // Password keys (CRITICAL: LittleFS stores masked "********", NVS holds the real value)
    const char *NVS_KEY_WEB_PWD        = "cfg_web_pwd";      // Web auth password
    const char *NVS_KEY_TELNET_PWD     = "cfg_tlnt_pwd";     // Telnet auth password
    const char *NVS_KEY_AP_PWD         = "cfg_ap_pwd";       // Access Point password
    const char *NVS_KEY_SMTP_PWD       = "cfg_smtp_pwd";     // SMTP password (masked in LittleFS)
    const char *NVS_KEY_FTP_PWD        = "cfg_ftp_pwd";      // FTP password (masked in LittleFS)
    
    // LittleFS paths (slots generated by buildSlotPath(stem, slot) → "/config/<stem>.g<slot>.json")
    const char *LITTLEFS_CONFIG_DIR       = "/config";                 // Config directory
    const char *LITTLEFS_PAGES_REG_PATH   = "/config/pages.reg";      // Config registry listing all pages with max sizes

    // Key compression mapping (delegates to config_utils.h)
    String getShortKey(const String &longKey) { return ::shortKey(longKey); }

    // Key expansion mapping (delegates to config_utils.h)
    String getLongKey(const String &shortKey) { return ::longKey(shortKey); }

    // 🧠 Tenta compactar um objeto JSON (recursivo)
    void compactJsonVariant(JsonVariant source, JsonVariant dest) {
        if (source.is<JsonObject>()) {
            JsonObject srcObj = source.as<JsonObject>();
            JsonObject destObj = dest.to<JsonObject>();
            for (JsonPair p : srcObj) {
                String sk = getShortKey(p.key().c_str());
                if (p.value().is<JsonObject>() || p.value().is<JsonArray>()) {
                    compactJsonVariant(p.value(), destObj.createNestedObject(sk));
                } else {
                    destObj[sk] = p.value();
                }
            }
        } else if (source.is<JsonArray>()) {
            JsonArray srcArr = source.as<JsonArray>();
            JsonArray destArr = dest.to<JsonArray>();
            for (JsonVariant v : srcArr) {
                if (v.is<JsonObject>()) {
                    compactJsonVariant(v, destArr.createNestedObject());
                } else {
                    destArr.add(v);
                }
            }
        } else {
            dest.set(source);
        }
    }

    // 🧠 Tenta expandir um objeto JSON (recursivo)
    void expandJsonVariant(JsonVariant source, JsonVariant dest) {
        if (source.is<JsonObject>()) {
            JsonObject srcObj = source.as<JsonObject>();
            JsonObject destObj = dest.to<JsonObject>();
            for (JsonPair p : srcObj) {
                String lk = getLongKey(p.key().c_str());
                if (p.value().is<JsonObject>() || p.value().is<JsonArray>()) {
                    expandJsonVariant(p.value(), destObj.createNestedObject(lk));
                } else {
                    destObj[lk] = p.value();
                }
            }
        } else if (source.is<JsonArray>()) {
            JsonArray srcArr = source.as<JsonArray>();
            JsonArray destArr = dest.to<JsonArray>();
            for (JsonVariant v : srcArr) {
                if (v.is<JsonObject>()) {
                    expandJsonVariant(v, destArr.createNestedObject());
                } else {
                    destArr.add(v);
                }
            }
        } else {
            dest.set(source);
        }
    }

    // FNV-1a 32-bit hash (delegates to config_utils.h)
    uint32_t calculateHash(const String &s) { return fnv1aHash(s); }

    // 🧠 Write volatile counter (stats) to a separate namespace to avoid spending config sectors
    // Ex: reboot count, runtime total, etc.
    void saveVolatileCounter(const char* key, uint32_t value) {
        Preferences p;
        if (p.begin("nvs_stats", false)) { // Usa namespace "nvs_stats"
            p.putUInt(key, value);
            p.end();
        }
    }

    uint32_t loadVolatileCounter(const char* key, uint32_t defaultValue = 0) {
        Preferences p;
        uint32_t val = defaultValue;
        if (p.begin("nvs_stats", true)) { // Read-only
            val = p.getUInt(key, defaultValue);
            p.end();
        }
        return val;
    }

    // Save non-critical config (everything except WiFi/Email/SavedNetworks) to LittleFS
    bool saveToLittleFS();
    
public:
    // Load non-critical config from LittleFS (called by taskInitFlash after mount)
    // Made public so taskInitFlash can reload config after LittleFS mount completes.
    // Initial loadConfig() in setup() can't read LittleFS because mount happens later.
    bool loadFromLittleFS();
    
    // Migrate existing NVS data to LittleFS (one-time migration)
    void migrateNVSToLittleFS();

    // 🧠 Compute 32-bit FNV-1a hash for a String (light and sufficient for change detection)
    uint32_t computeHash32(const String &s);

    // 🧹 Post-load normalization without aggressive sched_* slot removal (keeps structure for UI)
    void normalizePostLoad();

    // 🧹 Prune empty slots before serializing and persisting to reduce JSON/heap
    void pruneEmptySlotsBeforeSave();

    // Helper de logging condicionado pelo campo config["debug"]["logs_enabled"]
    void logMsg(const String &s) {
        if (config.containsKey("debug") && (config["debug"]["logs_enabled"] | false)) {
            Serial.println(s);
        }
    }
    void logPrint(const String &s) {
        if (config.containsKey("debug") && (config["debug"]["logs_enabled"] | false)) {
            Serial.print(s);
        }
    }

public:
    ConfigManager() :
        config(24576),      // 24KB pool (16KB filled in real use: alerts import then lost slots; PSRAM plentiful)
        recipDoc(8192),     // recipients_slots (user-typed names + emails arrays) + numeric_rules, 2x worst case
        schedDoc(ENABLE_SCHEDULER == 1 ? 5120 : 256),   // 5KB if scheduler active (slots + last_run_day anchors); 256B otherwise
        modbusDoc(ENABLE_MODBUS == 1 ? 4096 : 256)      // 4KB if modbus active; 256B otherwise (scalar fields only)
    { initDefaults(); }

    // Returns a mutable reference to a top-level section of the config document.
    // External modules must use this instead of accessing the private config member directly.
    // Returns a null JsonVariant (isNull()==true) when the key does not exist.
    // "modbus" is routed to modbusDoc to keep the large devices array out of the core pool.
    JsonVariant getSection(const char* key) {
        if (strcmp(key, "modbus") == 0) return modbusDoc["modbus"];
        return config[key];
    }
    
    // PUBLIC DIAGNOSTICS: Read-only access for monitoring (do NOT modify externally!)
    bool isDirty = false;                   // Pending changes flag
    unsigned long lastDirtyMark = 0;        // Timestamp of the latest config change
    unsigned long lastSave = 0;             // Timestamp of last successful save
    uint32_t saveThrottleCount = 0;         // Number of throttled save attempts
    uint32_t successfulSaveCount = 0;       // Completed saves counter

    // Per-stem save model: each page persists to its own config file and only
    // that file is rewritten when the page's settings change. stemHash[..] holds
    // the hash of the last persisted payload; a stem is written only when its
    // hash differs, so one save never touches the other pages' files.
    enum StemIndex {
        STEM_CORE = 0,
        STEM_RS232,
        STEM_RECIPIENTS,
        STEM_SCHEDULE,
        STEM_MODBUS,
        STEM_ACCESS,
        STEM_SYSTEM,
        STEM_USB_SERVER,
        STEM_SMTP_EMAIL,
        STEM_ALERT,
        STEM_TELNET,
        STEM_ETHERNET,
        STEM_SDCARD,
        STEM_DEBUG,
        STEM_ACCESS_POINT,
        STEM_WIFI_HYBRID,
        STEM_DO,
        STEM_FTP,
        STEM_COUNT
    };
    uint32_t stemHash[STEM_COUNT] = {0};
    bool stemHashReady[STEM_COUNT] = {false};

    // Flag set when saveToLittleFS() fails because LittleFS was not mounted yet.
    // taskInitFlash checks this flag: if true, it saves the current in-memory config
    // to LittleFS instead of loading from it, so user changes made before LittleFS
    // was ready are not lost.
    bool lfsWritePending = false;

    // Comma-separated names of the page stems that failed to persist on the last
    // save (populated by saveToLittleFS, read by save() to report the real cause).
    String lfsFailStems;

    void initDefaults();
    
    // (first load/ensureAllFieldsExist implementation stays; remove duplicates below)
    
    bool load();
    
    // 🩺 NVS Health Check - diagnose NVS state and integrity
    void checkNVSHealth();
    
    // 🧪 Post-save verification - read back and verify critical sections
    bool verifySave();
    
    // 🛠️ Ensure all fields exist (for compatibility with old versions) -- SINGLE IMPLEMENTATION
    void ensureAllFieldsExist();

    // Read number of active interval-command rows (clamp via config_utils.h)
    int getSchedCmdActive() const {
        int v = schedDoc["sched_cmd_active"] | 1;
        return clampSchedCmdActive(v);
    }
    // Set number of active interval-command rows (clamp via config_utils.h)
    void setSchedCmdActive(int n){
        schedDoc["sched_cmd_active"] = clampSchedCmdActive(n);
        isDirty = true;
    }
    // 🧠 Ler slot recorrente
    // 🧠 Complete version with type
    bool getSchedCmdSlot(int i, String &cmd, int &interval, String &unit, String &days, String &tpl, bool &enabled, String &type){ // Getter (with type)
        if(i<1||i>10) return false; // Out of range 1..10
        if(!schedDoc.containsKey("sched_cmd")) return false; // Missing
        char key[5]; snprintf(key, sizeof(key), "s%d", i); // Slot name
        if(!schedDoc["sched_cmd"].containsKey(key)) return false; // Slot absent
        JsonObject so = schedDoc["sched_cmd"][key]; // Slot object
        const char* cmdPtr = so[FPSTR(K_COMMAND)].as<const char*>();
        const char* unitPtr = so[FPSTR(K_UNIT)].as<const char*>();
        const char* daysPtr = so[FPSTR(K_DAYS)].as<const char*>();
        const char* tplPtr = so[FPSTR(K_TEMPLATE)].as<const char*>();
        const char* typePtr = so.containsKey("type") ? so["type"].as<const char*>() : "rs232";
        cmd = cmdPtr ? String(cmdPtr) : String(""); // 🧠 cmd
        interval = so[FPSTR(K_INTERVAL)].as<int>(); // ⏱️ intervalo
        unit = unitPtr ? String(unitPtr) : String(""); // ⏱️ unidade
        days = daysPtr ? String(daysPtr) : String(""); // 📅 dias
        tpl = tplPtr ? String(tplPtr) : String(""); // 🧩 template
        enabled = so[FPSTR(K_ENABLED)].as<bool>(); // 🔌 enabled
        type = typePtr ? String(typePtr) : String("rs232"); // 🧠 type (default rs232)
        return true; // ✅
    }
    // ♻️ Compatible version without type (for legacy code)
    bool getSchedCmdSlot(int i, String &cmd, int &interval, String &unit, String &days, String &tpl, bool &enabled){ // 🧠 Getter sem type
        String type; // 🧠 Variável temporária
        return getSchedCmdSlot(i, cmd, interval, unit, days, tpl, enabled, type); // ♻️ Call complete version
    }
    // 🧠 Escrever slot recorrente
    bool setSchedCmdSlot(int i, const String &cmd, int interval, const String &unit, const String &days, const String &tpl, bool enabled, const String &type = "rs232"){ // Setter
        if(i<1||i>10) return false; // Out of range 1..10
        if(!schedDoc.containsKey("sched_cmd")) schedDoc["sched_cmd"] = JsonObject(); // Ensure root object
        char key[5]; snprintf(key, sizeof(key), "s%d", i); // Slot name
        JsonObject so;
        if(!schedDoc["sched_cmd"].containsKey(key)) so = schedDoc["sched_cmd"].createNestedObject(key); else so = schedDoc["sched_cmd"][key]; // Reuse or create
        so[FPSTR(K_COMMAND)] = cmd; // ✍️ Campo
        so[FPSTR(K_INTERVAL)] = interval; // ✍️ Campo
        so[FPSTR(K_UNIT)] = unit; // ✍️ Campo
        so[FPSTR(K_DAYS)] = days; // ✍️ Campo
        so[FPSTR(K_TEMPLATE)] = tpl; // ✍️ Campo
        so[FPSTR(K_ENABLED)] = enabled; // ✍️ Campo
        so["type"] = type; // ✍️ Campo type (rs232/system)
        isDirty = true; // Mark dirty - save() will flush to LittleFS via the main loop
        return true; // ✅
    }
    // ♻️ Legacy scheduled_commands_json migration removed (slots already consolidated)
    void migrateLegacyScheduledCommands(const String &legacyJson){ // 🧠 Migration
        if(!legacyJson.length()) return; // ♻️ Nada
        if(schedDoc.containsKey("sched_cmd_migrated") && schedDoc["sched_cmd_migrated"].as<bool>()) return; // Already migrated
        SpiRamJsonDocument doc(6144); // 📦 Buffer
        DeserializationError err = deserializeJson(doc, legacyJson); // 🧠 Parse
        if(err){ Serial.println(F("[SchedCmd] Falha parse legacy")); return; } // ⚠️ Erro
        if(!doc.is<JsonArray>()){ Serial.println(F("[SchedCmd] Legacy is not an array")); return; } // ⚠️ Tipo
        JsonArray arr = doc.as<JsonArray>(); // 📦 Array
        int idx=1; // 🧠 Next slot
        for(JsonObject o : arr){ // 🔁 Itera
            if(idx>10){ Serial.println(F("[SchedCmd] Truncado >10")); break; } // ⚠️ Limite 10
            String c = o["command"].as<const char*>() ? o["command"].as<const char*>() : ""; // 🧠 Campo
            int inter = o.containsKey("interval") ? o["interval"].as<int>() : 0; // ⏱️ Intervalo
            String u = o["unit"].as<const char*>() ? o["unit"].as<const char*>() : "m"; // ⏱️ Unidade
            String d = o["days"].as<const char*>() ? o["days"].as<const char*>() : ""; // 📅 Dias
            String tpl = o["template"].as<const char*>() ? o["template"].as<const char*>() : ""; // 🧩 Template
            bool en = o.containsKey("enabled") ? o["enabled"].as<bool>() : true; // 🔌 Enabled
            setSchedCmdSlot(idx, c, inter, u, d, tpl, en); // ♻️ Grava
            idx++; // ➕
        }
        schedDoc["sched_cmd_migrated"] = true; // Mark migrated
        isDirty = true; // 🧪 Sujo
    }

    // Get number of active date/time scheduled command rows (clamp via config_utils.h)
    int getSchedDtActive() const {
        int v = schedDoc["sched_dt_active"] | 1;
        return clampSchedDtActive(v);
    }
    // Set number of active date/time scheduled command rows (clamp via config_utils.h)
    void setSchedDtActive(int n) {
        schedDoc["sched_dt_active"] = clampSchedDtActive(n);
        isDirty = true;
    }
    // 🧠 Read slot (returns via reference parameters)
    // 🧠 Complete version with type and daily
    bool getSchedDtSlot(int i, String &cmd, String &date, String &time, String &tpl, bool &enabled, String &type, bool &daily) { // Getter (1..8) with type and daily
        if (i < 1 || i > 8) return false; // Invalid index
        char key[5]; snprintf(key, sizeof(key), "s%d", i); // Slot name
        if (!schedDoc.containsKey("sched_dt") || !schedDoc["sched_dt"].containsKey(key)) return false; // Absent
        JsonObject so = schedDoc["sched_dt"][key]; // Object
        const char* cmdPtr = so[FPSTR(K_COMMAND)].as<const char*>();
        const char* datePtr = so[FPSTR(K_DATE)].as<const char*>();
        const char* timePtr = so[FPSTR(K_TIME)].as<const char*>();
        const char* tplPtr = so[FPSTR(K_TEMPLATE)].as<const char*>();
        const char* typePtr = so.containsKey("type") ? so["type"].as<const char*>() : "rs232";
        cmd = cmdPtr ? String(cmdPtr) : String(""); // 🧠 Extrai command
        date = datePtr ? String(datePtr) : String(""); // 🧠 Extrai date
        time = timePtr ? String(timePtr) : String(""); // 🧠 Extrai time
        tpl = tplPtr ? String(tplPtr) : String(""); // 🧠 Extrai template
        enabled = so[FPSTR(K_ENABLED)].as<bool>(); // 🧠 Extrai enabled
        type = typePtr ? String(typePtr) : String("rs232"); // 🧠 Extrai type (default rs232)
        daily = so.containsKey("daily") ? so["daily"].as<bool>() : false; // 🧠 Extrai daily (default false)
        return true; // ✅ Success
    }
    // ♻️ Compatible version without type and daily (for legacy code)
    bool getSchedDtSlot(int i, String &cmd, String &date, String &time, String &tpl, bool &enabled) { // 🧠 Getter sem type/daily
        String type; bool daily; // 🧠 Variáveis temporárias
        return getSchedDtSlot(i, cmd, date, time, tpl, enabled, type, daily); // ♻️ Call complete version
    }
    // 🧠 Write slot (normalize fields)
    bool setSchedDtSlot(int i, const String &cmd, const String &date, const String &time, const String &tpl, bool enabled, const String &type = "rs232", bool daily = false) { // Setter (1..8)
        if (i < 1 || i > 8) return false; // Out of range
        if (!schedDoc.containsKey("sched_dt")) schedDoc["sched_dt"] = JsonObject(); // Ensure root
        char key[5]; snprintf(key, sizeof(key), "s%d", i); // Slot name
        if (!schedDoc["sched_dt"].containsKey(key)) { // Create if missing
            JsonObject so = schedDoc["sched_dt"].createNestedObject(key); // New slot
            so[FPSTR(K_COMMAND)] = cmd; // Field
            so[FPSTR(K_DATE)] = date; // Field
            so[FPSTR(K_TIME)] = time; // Field
            so[FPSTR(K_TEMPLATE)] = tpl; // Field
            so[FPSTR(K_ENABLED)] = enabled; // Field
            so["type"] = type; // Field type (rs232/system)
            so["daily"] = daily; // Field daily (daily repeat)
        } else { // Update existing
            JsonObject so = schedDoc["sched_dt"][key]; // Slot
            so[FPSTR(K_COMMAND)] = cmd; // Update
            so[FPSTR(K_DATE)] = date; // Update
            so[FPSTR(K_TIME)] = time; // Update
            so[FPSTR(K_TEMPLATE)] = tpl; // Update
            so[FPSTR(K_ENABLED)] = enabled; // Update
            so["type"] = type; // Update type
            so["daily"] = daily; // Update daily
        }
        isDirty = true; // Mark dirty - save() will flush to LittleFS via the main loop
        return true; // Ok
    }
    bool clearSchedDtSlot(int i){ // 🧠 Clear a sched_dt slot after execution (1..8)
        if(i < 1 || i > 8) return false; // ⚠️ Índice inválido
        if(!schedDoc.containsKey("sched_dt")) return false; // Structure absent
        char key[5]; snprintf(key, sizeof(key), "s%d", i); // Slot key
        if(!schedDoc["sched_dt"].containsKey(key)) return false; // Slot absent
        JsonObject so = schedDoc["sched_dt"][key]; // Slot object
        so[FPSTR(K_COMMAND)] = ""; // 🧼 Clear command
        so[FPSTR(K_DATE)] = ""; // 🧼 Clear date
        so[FPSTR(K_TIME)] = ""; // 🧼 Clear time
        so[FPSTR(K_TEMPLATE)] = ""; // 🧼 Clear template
        so[FPSTR(K_ENABLED)] = false; // 🔌 Disable
        isDirty = true; // Mark dirty - save() will flush to LittleFS via the main loop
        return true; // ✅ Success
    }
    // ♻️ Migration from scheduled_commands_json to sched_dt removed (obsolete)
    void migrateLegacyScheduledByDatetime(const String &legacyJson) { // 🧠 Recebe JSON antigo
        if (!legacyJson.length()) return; // ♻️ Nada a migrar
        if (schedDoc.containsKey("sched_dt_migrated") && schedDoc["sched_dt_migrated"].as<bool>()) return; // Already migrated
        SpiRamJsonDocument doc(4096); // 📦 Buffer parse
        DeserializationError err = deserializeJson(doc, legacyJson); // 🧠 Tenta parse
        if (err) { Serial.println(F("[SchedDt] Falha parse legacy JSON")); return; } // ⚠️ Erro parse
        if (!doc.is<JsonArray>()) { Serial.println(F("[SchedDt] Legacy is not an array")); return; } // ⚠️ Tipo errado
        JsonArray arr = doc.as<JsonArray>(); // 📦 Array
        int idx = 1; // 🧠 Next slot
        for (JsonObject o : arr) { // 🧠 Iterate commands (up to 8)
            if (idx > 8) { Serial.println(F("[SchedDt] Migration truncated (>8)")); break; } // ⚠️ Limite
            String c = o["command"].as<const char*>() ? o["command"].as<const char*>() : ""; // 🧠 Campo
            String d = o["date"].as<const char*>() ? o["date"].as<const char*>() : ""; // 🧠 Campo
            String t = o["time"].as<const char*>() ? o["time"].as<const char*>() : ""; // 🧠 Campo
            String tpl = o["template"].as<const char*>() ? o["template"].as<const char*>() : ""; // 🧠 Campo
            bool en = o.containsKey("enabled") ? o["enabled"].as<bool>() : true; // 🧠 Campo
            setSchedDtSlot(idx, c, d, t, tpl, en); // ♻️ Grava
            idx++; // ➕ Incrementa
        }
    schedDoc["sched_dt_active"] = idx - 1; // Adjust active count after migration (max 8)
        schedDoc["sched_dt_migrated"] = true; // Mark migrated
        isDirty = true; // 🧠 Necessário salvar
        Serial.println(F("[SchedDt] Migration complete")); // ✅ Log
    }

    // ========================================================
    // Daily command execution state persistence (survives reboots)
    // ========================================================
    // Save last execution day for a daily scheduled command (prevents duplicate runs after reboot)
    // dayKey format: YYYYMMDD (e.g., 20260225)
    void saveSchedDtLastRunDay(int slotIndex, int dayKey) {
        if (slotIndex < 1 || slotIndex > 16) return; // Valid slots: 1-16
        Preferences pr;
        if (pr.begin("esp32_config", false)) { // Open NVS for writing
            char key[10];
            snprintf(key, sizeof(key), "sdt_lr%d", slotIndex); // Key: sdt_lr1, sdt_lr2, ..., sdt_lr16
            pr.putInt(key, dayKey); // Store YYYYMMDD
            pr.end();
            nvsIncrementWriteCount(4); // Track NVS write (4 bytes)
        }
    }
    
    // Load last execution day for a daily scheduled command
    // Returns: -1 if not found/never executed, or YYYYMMDD (e.g., 20260225)
    int getSchedDtLastRunDay(int slotIndex) {
        if (slotIndex < 1 || slotIndex > 16) return -1; // Invalid slot
        Preferences pr;
        int dayKey = -1;
        if (pr.begin("esp32_config", true)) { // Open NVS for reading
            char key[10];
            snprintf(key, sizeof(key), "sdt_lr%d", slotIndex);
            dayKey = pr.getInt(key, -1); // Default: -1 (not found)
            pr.end();
        }
        return dayKey;
    }
    
    // Clear last execution day for a slot (useful when disabling/clearing a daily command)
    void clearSchedDtLastRunDay(int slotIndex) {
        if (slotIndex < 1 || slotIndex > 16) return;
        Preferences pr;
        if (pr.begin("esp32_config", false)) {
            char key[10];
            snprintf(key, sizeof(key), "sdt_lr%d", slotIndex);
            pr.remove(key);
            pr.end();
        }
    }

    // ================================
    // Debug Task 1 (2026-10-08): exposes use of the `config` doc to isolate lost writes.
    size_t configDocUsage() const { return config.memoryUsage(); }
    const SpiRamJsonDocument& configDoc() const { return config; }
    bool configHasKey(const char* k) const { return k && config.containsKey(k); }
    // ♻️ Daily reboots (2 slots)
    // ================================
    // 🧠 Read daily reboot slot (1..2) — returns time (HH:MM) and enabled
    bool getDailyRebootSlot(int i, String &time, bool &enabled) const {
        if (i < 1 || i > 2) return false; // ⚠️ Invalid index
        // ⛑️ NVS fallback (cfg_drbt, mirror written by set): RAM reads
        // proved unreliable on the board; the reboot engine uses this getter.
        // NVS-first order: loadConfig could write core.json with RAM at
        // default, so NVS is the truth; RAM is the backup.
        {
            Preferences pr;
            if (pr.begin("esp32_config", true)) {
                String raw = pr.getString("cfg_drbt", "");
                pr.end();
                if (raw.length() > 0) {
                    SpiRamJsonDocument tmp(512);
                    if (!deserializeJson(tmp, raw) && tmp.is<JsonObject>()) {
                        String key = String("s") + i;
                        if (tmp.containsKey(key) && tmp[key].is<JsonObject>()) {
                            JsonObjectConst o = tmp[key];
                            if (o.containsKey("time") && o.containsKey("enabled")) {
                                time = String(o["time"].as<const char*>());
                                enabled = o["enabled"].as<bool>();
                                return true;
                            }
                        }
                    }
                }
            }
        }
        if (config.containsKey("daily_reboot") && config["daily_reboot"].is<JsonObjectConst>()) {
            String key = String("s") + i;
            if (config["daily_reboot"].containsKey(key)) {
                JsonObjectConst o = config["daily_reboot"][key];
                if (o.containsKey("time") && o.containsKey("enabled")) {
                    time = String(o["time"].as<const char*>());
                    enabled = o["enabled"].as<bool>();
                    return true;
                }
            }
        }
        return false;         // ❌ Not found
    }
    // 🧠 Write daily reboot slot (1..2)
    bool setDailyRebootSlot(int i, const String &time, bool enabled) {
        if (i < 1 || i > 2) return false; // ⚠️ Limits
        // ArduinoJson v7: to<JsonObject>() CLEARS the existing object! Only convert
        // when not yet an object; then get-or-create the slot without touching
        // siblings. Task 1 2026-10-08 (host proof: to<>() wiped s2).
        if (!config["daily_reboot"].is<JsonObject>()) config["daily_reboot"].to<JsonObject>(); // ♻️ Ensure root
        String key = String("s") + i; // 🔑
        JsonObject dr = config["daily_reboot"].as<JsonObject>();
        JsonObject o = dr.containsKey(key) ? dr[key].as<JsonObject>() : dr.createNestedObject(key); // 📦 Get/create slot
        o["time"] = time; // ✍️ Time
        o["enabled"] = enabled; // ✍️ Active
        if (!o.containsKey("last_run_day")) o["last_run_day"] = 0; // 🧷 Ensure the last_run_day field
        isDirty = true; // Mark dirty - flush in background (loop), outside the request
        // NVS mirror (cfg_drbt): GET already reads it as fallback; RAM reads
        // proved unreliable, NVS is the truth. Only mirror if the write
        // to RAM is confirmed (avoids writing "null" over the good value).
        if (config["daily_reboot"][key].containsKey("time")) {
            String snap; serializeJson(config["daily_reboot"], snap);
            Preferences pr;
            if (pr.begin("esp32_config", false)) { pr.putString("cfg_drbt", snap); pr.end(); }
        }
        return true;
    }

    // ✅ Getter for last_run_day (AAAAMMDD) of the daily reboot slot
    int getDailyRebootLastRunDay(int i) const {
        if (i < 1 || i > 2) return 0;
        if (!config.containsKey("daily_reboot") || !config["daily_reboot"].is<JsonObjectConst>()) return 0;
        String key = String("s") + i;
        if (!config["daily_reboot"].containsKey(key) || !config["daily_reboot"][key].is<JsonObjectConst>()) return 0;
        JsonObjectConst o = config["daily_reboot"][key];
        if (o.containsKey("last_run_day")) return o["last_run_day"].as<int>();
        return 0;
    }

    // Getter for last_run_day of a sched_dt slot (LittleFS truth, schedDoc)
    int getSchedDtLastRunDayJson(int i) const {
        if (i < 1 || i > 16) return -1;
        if (!schedDoc.containsKey("sched_dt") || !schedDoc["sched_dt"].is<JsonObject>()) return -1;
        String key = String("s") + i;
        if (!schedDoc["sched_dt"].containsKey(key) || !schedDoc["sched_dt"][key].is<JsonObject>()) return -1;
        JsonObjectConst o = schedDoc["sched_dt"][key];
        if (o.containsKey("last_run_day")) return o["last_run_day"].as<int>();
        return -1;
    }

    // Setter for last_run_day of a sched_dt slot (LittleFS truth, schedDoc)
    void setSchedDtLastRunDayJson(int i, int dayKey) {
        if (i < 1 || i > 16) return;
        if (!schedDoc.containsKey("sched_dt") || !schedDoc["sched_dt"].is<JsonObject>()) return;
        String key = String("s") + i;
        if (!schedDoc["sched_dt"].containsKey(key) || !schedDoc["sched_dt"][key].is<JsonObject>()) return;
        schedDoc["sched_dt"][key]["last_run_day"] = dayKey;
        isDirty = true;
    }

    // Save daily reboot last-run day directly to NVS (survives crash/reboot before config.save())
    // Must be called immediately before ESP.restart() to prevent double-fire on reboot.
    void saveDailyRebootLastRunDayNVS(int slotIndex, int dayKey) {
        if (slotIndex < 1 || slotIndex > 2) return;
        Preferences pr;
        if (pr.begin("esp32_config", false)) {
            char key[12];
            snprintf(key, sizeof(key), "drb_lr%d", slotIndex); // drb_lr1, drb_lr2
            pr.putInt(key, dayKey); // Store YYYYMMDD
            pr.end();
            nvsIncrementWriteCount(4);
        }
    }

    // Clear daily reboot last-run day from NVS so a reconfigured slot can fire again today.
    void clearDailyRebootLastRunDayNVS(int slotIndex) {
        if (slotIndex < 1 || slotIndex > 2) return;
        Preferences pr;
        if (pr.begin("esp32_config", false)) {
            char key[12];
            snprintf(key, sizeof(key), "drb_lr%d", slotIndex);
            pr.remove(key);
            pr.end();
        }
    }

    // Load daily reboot last-run day from NVS (call once on first tick after boot)
    int getDailyRebootLastRunDayNVS(int slotIndex) {
        if (slotIndex < 1 || slotIndex > 2) return -1;
        Preferences pr;
        int dayKey = -1;
        if (pr.begin("esp32_config", true)) {
            char key[12];
            snprintf(key, sizeof(key), "drb_lr%d", slotIndex);
            dayKey = pr.getInt(key, -1);
            pr.end();
        }
        return dayKey;
    }

    // ✅ Setter for last_run_day with immediate segmented persistence
    bool setDailyRebootLastRunDay(int i, int yyyymmdd) {
        if (i < 1 || i > 2) return false;
        if (!config["daily_reboot"].is<JsonObject>()) config["daily_reboot"].to<JsonObject>();
        String key = String("s") + i;
        JsonObject drl = config["daily_reboot"].as<JsonObject>();
        JsonObject ol = drl.containsKey(key) ? drl[key].as<JsonObject>() : drl.createNestedObject(key);
        ol["last_run_day"] = yyyymmdd;
        isDirty = true;
        return true;
    }

    // ♻️ migrateScheduledCommandsArrayToSlots function removed (all expected installs already migrated)

    // Migration: remove deprecated RTS/CTS flow_control keys from persisted config and templates
    void cleanDeprecatedFlowControl();
    
    // 💾 DUAL-STORAGE SAVE: WiFi+Email to NVS (~2KB), everything else to LittleFS (1.4MB)
    bool save(bool force = false);

    // 💾 Save only one LittleFS section file (stem = "core", "system", "rs232", etc.)
    bool saveSection(const char* stem);

    // Background persistence: request a section write that the loop task
    // performs a few seconds later (hidden flush). Use this from web handlers
    // instead of saveSection() so the request never does the flash write.
    bool scheduleSaveSection(const char* stem) {
        (void)stem;
        markDirtyAndScheduleSave();
        return true;
    }
    
    // 📥 Load configuration from dual-storage (NVS + LittleFS)
    bool loadConfig();
    
    // LEGACY METHOD STUB - Placeholder for removed multipart chunking code
    // Old implementation (200+ lines) used config_parts + config_part_N keys for large configs
    // Dual-storage architecture renders this obsolete: WiFi/Email in NVS, rest in LittleFS
    void _removeLegacyLoadCode_placeholder() {
        // This function is intentionally empty - serves only as documentation marker
        // for where the old monolithic NVS save/load logic was removed
    }
    
    // Mark config as dirty and defer persistence to a short batching window.
    void markDirtyAndScheduleSave() {
        isDirty = true;
        lastDirtyMark = millis();
    }
    
    // Loop: background flush of deferred saves. A few seconds after the last
    // change, write the pending sections to LittleFS — outside any web request.
    // On TOUCH7 the RGB bounce buffer makes ANY flash op while the panel streams
    // panic, so the flush suspends (del) the panel and rebuilds it after.
    void loop() {
        if (!isDirty) return;
        if (millis() - lastDirtyMark < 3000UL) return;  // debounce window
        // Flash wear cap: max STOR_CFG_MAX_DAY routine config saves per day.
        // Over budget → stay dirty, flush tomorrow (forced saves bypass).
        if (!storNoteCfgSave()) return;
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
        // TOUCH7: with the RGB bounce buffer any flash op while the panel
        // streams panics, so we suspend (del) the panel for the write. Do it
        // only when the screen is already off (dim/saver) so the rebuild is
        // invisible; fall back after 60 s in case the screen never dims.
        if (!touch7ConfigFlushAllowed() && millis() - lastDirtyMark < 60000UL) return;
        isDirty = false;
        Touch7PanelHold _t7hold;   // del/rebuild RGB panel around the flash write
        save(true);                // full save (NVS + LittleFS) while safe
#else
        isDirty = false;
        saveToLittleFS();
#endif
    }
    
    // 📤 Typed safe getters (to read configuration values)
    String getWifiSSID() { 
        if (!config.containsKey("wifi") || !config["wifi"].containsKey("ssid")) return String("");
        return config["wifi"]["ssid"].as<String>();
    } // 📶 SSID do Wi-Fi
    String getWifiPassword() { 
        if (!config.containsKey("wifi") || !config["wifi"].containsKey("password")) return String("");
        return config["wifi"]["password"].as<String>();
    } // 🔑 Senha do Wi-Fi
    String getSmtpServer() { 
        if (!config.containsKey("smtp") || !config["smtp"].containsKey("server")) return String("smtp.gmail.com");
        return config["smtp"]["server"].as<String>();
    } // 📧 Servidor SMTP
    int getSmtpPort() { 
        if (!config.containsKey("smtp") || !config["smtp"].containsKey("port")) return 465;
        return config["smtp"]["port"].as<int>();
    } // 🚪 Porta SMTP
    String getSmtpUsername() { 
        if (!config.containsKey("smtp") || !config["smtp"].containsKey("username")) return String("");
        return config["smtp"]["username"].as<String>();
    } // 👤 Usuário SMTP
    String getSmtpPassword() { 
        if (!config.containsKey("smtp") || !config["smtp"].containsKey("password")) return String("");
        return config["smtp"]["password"].as<String>();
    } // 🔑 Senha SMTP
    bool getSmtpTlsInsecure() { 
        if (!config.containsKey("smtp") || !config["smtp"].containsKey("tls_insecure")) return true;
        return config["smtp"]["tls_insecure"].as<bool>();
    } // 🔒 Flag TLS inseguro
    
    // FTP client (SD/PSRAM upload to Windows FTP server). Section "ftp", password in NVS.
    String getFtpHost() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("host")) return String("");
        return config["ftp"]["host"].as<String>();
    }
    void setFtpHost(const String &v) { config["ftp"]["host"] = v; markDirtyAndScheduleSave(); }
    int getFtpPort() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("port")) return 21;
        int p = config["ftp"]["port"].as<int>();
        if (p < 1 || p > 65535) return 21;
        return p;
    }
    void setFtpPort(int p) {
        if (p < 1) p = 1; if (p > 65535) p = 65535;
        config["ftp"]["port"] = p; markDirtyAndScheduleSave();
    }
    String getFtpUser() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("user")) return String("");
        return config["ftp"]["user"].as<String>();
    }
    void setFtpUser(const String &v) { config["ftp"]["user"] = v; markDirtyAndScheduleSave(); }
    String getFtpPassword() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("password")) return String("");
        return config["ftp"]["password"].as<String>();
    }
    void setFtpPassword(const String &v) { config["ftp"]["password"] = v; markDirtyAndScheduleSave(); }
    bool getFtpAnonymous() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("anonymous")) return false;
        return config["ftp"]["anonymous"].as<bool>();
    }
    void setFtpAnonymous(bool v) { config["ftp"]["anonymous"] = v; markDirtyAndScheduleSave(); }
    String getFtpRemotePath() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("remote_path")) return String("/");
        return config["ftp"]["remote_path"].as<String>();
    }
    void setFtpRemotePath(const String &v) { config["ftp"]["remote_path"] = v.length() ? v : String("/"); markDirtyAndScheduleSave(); }
    String getFtpLocalDir() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("local_dir")) return String("/logs");
        return config["ftp"]["local_dir"].as<String>();
    }
    void setFtpLocalDir(const String &v) { config["ftp"]["local_dir"] = v.length() ? v : String("/logs"); markDirtyAndScheduleSave(); }
    bool getFtpEnabled() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("enabled")) return false;
        return config["ftp"]["enabled"].as<bool>();
    }
    void setFtpEnabled(bool v) { config["ftp"]["enabled"] = v; markDirtyAndScheduleSave(); }
    bool getFtpAutoUpload() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("auto_upload")) return false;
        return config["ftp"]["auto_upload"].as<bool>();
    }
    void setFtpAutoUpload(bool v) { config["ftp"]["auto_upload"] = v; markDirtyAndScheduleSave(); }
    int getFtpIntervalMin() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("interval_min")) return 720;
        int m = config["ftp"]["interval_min"].as<int>();
        if (m < 300) return 300; if (m > 2160) return 2160;
        return m;
    }
    void setFtpIntervalMin(int m) {
        if (m < 300) m = 300; if (m > 2160) m = 2160;
        config["ftp"]["interval_min"] = m; markDirtyAndScheduleSave();
    }
    bool getFtpOnlyNew() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("only_new")) return true;
        return config["ftp"]["only_new"].as<bool>();
    }
    void setFtpOnlyNew(bool v) { config["ftp"]["only_new"] = v; markDirtyAndScheduleSave(); }
    bool getFtpDeleteAfter() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("delete_after")) return false;
        return config["ftp"]["delete_after"].as<bool>();
    }
    void setFtpDeleteAfter(bool v) { config["ftp"]["delete_after"] = v; markDirtyAndScheduleSave(); }
    int getFtpMaxFiles() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("max_files")) return 10;
        int m = config["ftp"]["max_files"].as<int>();
        if (m < 1) return 1; if (m > 50) return 50;
        return m;
    }
    void setFtpMaxFiles(int m) {
        if (m < 1) m = 1; if (m > 50) m = 50;
        config["ftp"]["max_files"] = m; markDirtyAndScheduleSave();
    }
    String getFtpFilter() {
        if (!config.containsKey("ftp") || !config["ftp"].containsKey("filter")) return String("*.csv;*.log;*.txt;*.bin;*.wav;*.gcode;*.nc;*.avi;*.jpg;*.mp3;*.flac");
        return config["ftp"]["filter"].as<String>();
    }
    void setFtpFilter(const String &v) { config["ftp"]["filter"] = v.length() ? v : String("*.csv;*.log;*.txt;*.bin;*.wav;*.gcode;*.nc;*.avi;*.jpg;*.mp3;*.flac"); markDirtyAndScheduleSave(); }

    // 🚀 Google Apps Script Email System
    String getGoogleScriptUrl() { 
        if (!config.containsKey("smtp") || !config["smtp"].containsKey("google_script_url")) return String("");
        return config["smtp"]["google_script_url"].as<String>();
    } // 🌐 Google Apps Script URL
    void setGoogleScriptUrl(const String &url) {
        config["smtp"]["google_script_url"] = url;
        markDirtyAndScheduleSave();
    } // 🌐 Set Google Apps Script URL
    
    // 🔄 Email Method Configuration ("smtp" or "google")
    String getEmailMethod() {
        if (!config.containsKey("email") || !config["email"].containsKey("method")) return String("smtp");
        return config["email"]["method"].as<String>();
    } // 🔄 Get email sending method
    void setEmailMethod(const String &method) {
        config["email"]["method"] = method;
        markDirtyAndScheduleSave();
    } // 🔄 Set email sending method (smtp/google only)
    
    String getEmailDestination() { 
        if (!config.containsKey("email") || !config["email"].containsKey("destination")) return String("");
        return config["email"]["destination"].as<String>();
    } // 🎯 Email de destino
    bool getEmailAutoSend() { 
        // 🧠 Return the persisted boolean if present; otherwise use TRUE default (UI-intended behavior)
        //    Reason for the change: previously returned false when the field was missing/corrupt,
        //    causing boot with auto-send off despite initDefaults()/ensureAllFieldsExist() defaults being true.
        if (config.containsKey("email") && config["email"].is<JsonObject>()) { // 🧠 Validate email section exists
            JsonObject e = config["email"].as<JsonObject>(); // 📦 Objeto email
            if (e.containsKey("auto_send")) return e["auto_send"].as<bool>(); // ✅ Field present -> return real value
        }
        return true; // ✅ Fallback consistent with defaults (proactive fail-safe)
    } // Get auto-send status
    int getEmailMaxLines() { 
        if (!config.containsKey("email") || !config["email"].containsKey("max_lines")) return 66;
        return config["email"]["max_lines"].as<int>();
    } // 📏 Max lines per email
    int getEmailMinContentLength() { 
        if (!config.containsKey("email") || !config["email"].containsKey("min_content_length")) return 3;
        return config["email"]["min_content_length"].as<int>();
    } // 📝 Minimum content length
    String getEmailSubject() { 
        if (!config.containsKey("email") || !config["email"].containsKey("subject")) return String("Node32-Hub");
        return config["email"]["subject"].as<String>();
    } // 🧠 Assunto do email
    bool getEmailDontCopySender() { return config["email"]["dont_copy_sender"].as<bool>(); } // 🔁 Avoid copy to sender
    void setEmailDontCopySender(bool value) { config["email"]["dont_copy_sender"] = value; markDirtyAndScheduleSave(); }
    // Attach txt and timestamp are always enabled (options removed from UI)
    bool getEmailAttachTxt() { return true; }
    void setEmailAttachTxt(bool value) { (void)value; } // no-op: always true
    bool getEmailIncludeTimestamp() { return true; }
    void setEmailIncludeTimestamp(bool value) { (void)value; } // no-op: always true
    
    // Inactivity alert configuration — 6 independent slots.
    static constexpr int ALERT_SLOT_COUNT = 6;

    bool getInactivityAlertSensorsResetTimer() {
        if (!config.containsKey("inactivity_alerts")) return false;
        return config["inactivity_alerts"]["sensors_reset_timer"].as<bool>();
    }
    void setInactivityAlertSensorsResetTimer(bool value) {
        config["inactivity_alerts"]["sensors_reset_timer"] = value;
    }

    bool getAlertSlotEnabled(int i) {
        JsonArray slots = _getAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return false;
        return slots[i]["enabled"] | false;
    }
    int getAlertSlotTimeout(int i) {
        JsonArray slots = _getAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return 20;
        return slots[i]["timeout_minutes"] | 20;
    }
    int getAlertSlotMaxPerDay(int i) {
        JsonArray slots = _getAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return 12;
        return slots[i]["max_per_day"] | 12;
    }
    String getAlertSlotEmails(int i) {
        JsonArray slots = _getAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return String();
        return slots[i]["emails"].as<String>();
    }

    // Batch setter — does NOT call markDirtyAndScheduleSave; caller must save explicitly.
    void setAlertSlot(int i, bool enabled, int timeout, int maxPerDay, const String& emails) {
        JsonArray slots = _getAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return;
        slots[i]["enabled"] = enabled;
        slots[i]["timeout_minutes"] = timeout;
        slots[i]["max_per_day"] = maxPerDay;
        slots[i]["emails"] = emails;
    }

    // OEE alert slots (alertas OEE) — same alerts.json file, own "oee_slots" array.
    // Metric: 0 = shift OEE, 1 = day OEE, 2 = availability (shift). Threshold is a
    // percentage (fire when value < threshold); persist_min is how long the value
    // must stay below before firing. All slots ship DISABLED; slot 0 carries the
    // suggested defaults (shift OEE < 60% for 15 min, max 6/day).
    static constexpr int OEE_ALERT_SLOT_COUNT = 3;

    bool getOeeAlertEnabled(int i) {
        JsonArray slots = _getOeeAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return false;
        return slots[i]["enabled"] | false;
    }
    int getOeeAlertMetric(int i) {
        JsonArray slots = _getOeeAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return 0;
        int m = slots[i]["metric"] | 0;
        return (m >= 0 && m <= 2) ? m : 0;
    }
    int getOeeAlertThreshold(int i) {
        JsonArray slots = _getOeeAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return 60;
        int t = slots[i]["threshold"] | 60;
        return (t >= 1 && t <= 100) ? t : 60;
    }
    int getOeeAlertPersistMin(int i) {
        JsonArray slots = _getOeeAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return 15;
        int p = slots[i]["persist_min"] | 15;
        return (p >= 1 && p <= 240) ? p : 15;
    }
    int getOeeAlertMaxPerDay(int i) {
        JsonArray slots = _getOeeAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return 6;
        int m = slots[i]["max_per_day"] | 6;
        return (m >= 1 && m <= 100) ? m : 6;
    }
    String getOeeAlertEmails(int i) {
        JsonArray slots = _getOeeAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return String();
        return slots[i]["emails"].as<String>();
    }

    // Batch setter — does NOT call markDirtyAndScheduleSave; caller must save explicitly.
    void setOeeAlertSlot(int i, bool enabled, int metric, int threshold, int persistMin, int maxPerDay, const String& emails) {
        JsonArray slots = _getOeeAlertSlots();
        if (i < 0 || i >= (int)slots.size()) return;
        slots[i]["enabled"] = enabled;
        slots[i]["metric"] = (metric >= 0 && metric <= 2) ? metric : 0;
        slots[i]["threshold"] = threshold;
        slots[i]["persist_min"] = persistMin;
        slots[i]["max_per_day"] = maxPerDay;
        slots[i]["emails"] = emails;
    }

    // Legacy compat — returns true if ANY slot is enabled (used by home page / status).
    bool getInactivityAlertEnabled() {
        for (int i = 0; i < ALERT_SLOT_COUNT; ++i) {
            if (getAlertSlotEnabled(i)) return true;
        }
        return false;
    }

    // Legacy compat — returns the shortest timeout among enabled slots.
    int getInactivityAlertTimeoutMinutes() {
        int best = 1440;
        for (int i = 0; i < ALERT_SLOT_COUNT; ++i) {
            if (getAlertSlotEnabled(i)) {
                int t = getAlertSlotTimeout(i);
                if (t < best) best = t;
            }
        }
        return best < 1440 ? best : 20;
    }

    // Legacy compat — returns max per day across enabled slots.
    int getInactivityAlertMaxPerDay() {
        int best = 0;
        for (int i = 0; i < ALERT_SLOT_COUNT; ++i) {
            if (getAlertSlotEnabled(i)) {
                int m = getAlertSlotMaxPerDay(i);
                if (m > best) best = m;
            }
        }
        return best > 0 ? best : 12;
    }

    // Legacy compat — comma-joined emails from all enabled slots.
    String getInactivityAlertEmails() {
        String result;
        for (int i = 0; i < ALERT_SLOT_COUNT; ++i) {
            if (!getAlertSlotEnabled(i)) continue;
            String e = getAlertSlotEmails(i);
            e.trim();
            if (e.length() == 0) continue;
            if (result.length() > 0) result += ",";
            result += e;
        }
        return result;
    }

private:
    JsonArray _getAlertSlots() {
        if (!config.containsKey("inactivity_alerts") || !config["inactivity_alerts"].is<JsonObject>()) {
            config["inactivity_alerts"] = JsonObject();
        }
        if (!config["inactivity_alerts"].containsKey("slots") || !config["inactivity_alerts"]["slots"].is<JsonArray>()) {
            config["inactivity_alerts"]["slots"].to<JsonArray>();
            for (int i = 0; i < ALERT_SLOT_COUNT; ++i) {
                JsonObject slot = config["inactivity_alerts"]["slots"].createNestedObject();
                slot["enabled"] = false;
                slot["timeout_minutes"] = 20;
                slot["max_per_day"] = 12;
                slot["emails"] = "";
            }
        }
        return config["inactivity_alerts"]["slots"].as<JsonArray>();
    }

    // OEE alert slots live in the SAME alerts.json file (own "oee_slots" array, so
    // no new stem/file is needed — saveSection("alerts") persists the whole
    // inactivity_alerts object and the loader copies every key back).
    // Defaults: slot 0 = shift OEE < 60% for 15 min (max 6/day), slot 1 = day OEE
    // < 60% for 15 min, slot 2 = availability < 80% for 15 min — ALL disabled.
    JsonArray _getOeeAlertSlots() {
        if (!config.containsKey("inactivity_alerts") || !config["inactivity_alerts"].is<JsonObject>()) {
            config["inactivity_alerts"] = JsonObject();
        }
        if (!config["inactivity_alerts"].containsKey("oee_slots") || !config["inactivity_alerts"]["oee_slots"].is<JsonArray>()) {
            config["inactivity_alerts"]["oee_slots"].to<JsonArray>();
        }
        // Self-heal: an empty/truncated array (e.g. "oee_slots":[] persisted by
        // an older build or a failed allocation) must grow back to the 3
        // default slots — otherwise every setOeeAlertSlot silently no-ops and
        // every getter returns hardcoded defaults, so the OEE tab validates +
        // answers ok but never persists (.29 board, 2026-10-12).
        JsonArray slots = config["inactivity_alerts"]["oee_slots"].as<JsonArray>();
        if ((int)slots.size() < OEE_ALERT_SLOT_COUNT) {
            const int defMetric[OEE_ALERT_SLOT_COUNT] = {0, 1, 2};
            const int defThreshold[OEE_ALERT_SLOT_COUNT] = {60, 60, 80};
            for (int i = (int)slots.size(); i < OEE_ALERT_SLOT_COUNT; ++i) {
                JsonObject slot = slots.createNestedObject();
                if (slot.isNull()) break; // pool full — keep what healed, retry next call
                slot["enabled"] = false;
                slot["metric"] = defMetric[i];
                slot["threshold"] = defThreshold[i];
                slot["persist_min"] = 15;
                slot["max_per_day"] = 6;
                slot["emails"] = "";
            }
            slots = config["inactivity_alerts"]["oee_slots"].as<JsonArray>();
        }
        return slots;
    }

public:
    
    // Heartbeat configuration getters/setters
    bool getHeartbeatEnabled() {
        if (!config.containsKey("system") || !config["system"].containsKey("heartbeat_enabled")) return false;
        return config["system"]["heartbeat_enabled"].as<bool>();
    }
    void setHeartbeatEnabled(bool enabled) {
        config["system"]["heartbeat_enabled"] = enabled;
        markDirtyAndScheduleSave();
    }
    bool getChartsPageEnabled() {
        if (!config.containsKey("system") || !config["system"].containsKey("charts_page_enabled")) return true;
        return config["system"]["charts_page_enabled"].as<bool>();
    }
    void setChartsPageEnabled(bool enabled) {
        config["system"]["charts_page_enabled"] = enabled;
        markDirtyAndScheduleSave();
    }
    // When true, chart sessions never expire by hard timeout — users are never
    // kicked from /sensor-charts. The page still counts as a concurrent session.
    bool getChartsExemptSessions() {
        if (!config.containsKey("system") || !config["system"].containsKey("charts_exempt_sessions")) return false;
        return config["system"]["charts_exempt_sessions"].as<bool>();
    }
    void setChartsExemptSessions(bool value) {
        config["system"]["charts_exempt_sessions"] = value;
        markDirtyAndScheduleSave();
    }
    // When true, the Serial Monitor button is shown on the home page.
    bool getSerialMonitorEnabled() {
        if (!config.containsKey("system") || !config["system"].containsKey("serial_monitor_enabled")) return false;
        return config["system"]["serial_monitor_enabled"].as<bool>();
    }
    void setSerialMonitorEnabled(bool enabled) {
        config["system"]["serial_monitor_enabled"] = enabled;
        markDirtyAndScheduleSave();
    }
    uint32_t getHeartbeatInterval() {
        if (!config.containsKey("system") || !config["system"].containsKey("heartbeat_interval")) return 10000;
        return config["system"]["heartbeat_interval"].as<uint32_t>();
    }
    void setHeartbeatInterval(uint32_t intervalMs) {
        config["system"]["heartbeat_interval"] = intervalMs;
        markDirtyAndScheduleSave();
    }
    
    bool getRs232Enabled() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("enabled")) return false;
        return config["rs232"]["enabled"].as<bool>();
    } // RS232 status
    int getRs232BaudrateIndex() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("baudrate_index")) return 4;
        return config["rs232"]["baudrate_index"].as<int>();
    } // Baudrate index
    bool getRs232InvertPins() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("invert_pins")) return false;
        return config["rs232"]["invert_pins"].as<bool>();
    } // Pins inversion
    // New RS232 getters
    int getRs232DataBits() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("data_bits")) return 8;
        return config["rs232"]["data_bits"].as<int>();
    } // Data bits
    String getRs232Parity() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("parity")) return String("none");
        return config["rs232"]["parity"].as<String>();
    } // Parity
    int getRs232StopBits() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("stop_bits")) return 1;
        return config["rs232"]["stop_bits"].as<int>();
    } // Stop bits
    int getRs232ReadTimeoutMs() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("read_timeout_ms")) return 1000;
        return config["rs232"]["read_timeout_ms"].as<int>();
    } // Read timeout
    bool getRs232UseXonXoff() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("use_xon_xoff")) return false;
        return config["rs232"]["use_xon_xoff"].as<bool>();
    } // XON/XOFF flow control
    bool getRs232MirrorToUsb() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("mirror_to_usb")) return true;
        return config["rs232"]["mirror_to_usb"].as<bool>();
    } // Mirror to USB
    int getPrinterEmulationMode() { 
        if (!config.containsKey("printer") || !config["printer"].containsKey("emulation_mode")) return 0;
        return config["printer"]["emulation_mode"].as<int>();
    } // Emulation mode
    int getPrinterCharsetMode() { 
        if (!config.containsKey("printer") || !config["printer"].containsKey("charset_mode")) return 0;
        return config["printer"]["charset_mode"].as<int>();
    } // 🔤 Modo de charset
    String getTelnetUsername() { 
        if (!config.containsKey("telnet") || !config["telnet"].containsKey("username")) return String("root");
        return config["telnet"]["username"].as<String>();
    } // 👤 Usuário Telnet
    String getTelnetPassword() { 
        if (!config.containsKey("telnet") || !config["telnet"].containsKey("password")) return String("root");
        return config["telnet"]["password"].as<String>();
    } // 🔑 Senha Telnet
    int getTelnetPort() { 
        if (!config.containsKey("telnet") || !config["telnet"].containsKey("port")) return 23;
        return config["telnet"]["port"].as<int>();
    } // 🚪 Porta Telnet
    bool getTelnetEnabled() { 
        return _cache_telnet_enabled; // ⚡ Retorna cache (seguro/rápido)
    } // ✅ Status Telnet
    void setTelnetEnabled(bool value) { 
        config["telnet"]["enabled"] = value; 
        _cache_telnet_enabled = value; // ⚡ Atualiza cache
        markDirtyAndScheduleSave(); 
    }

    // WiFi runtime disable switch (persisted; survives reboot).
    // Disabling: immediate via WiFi.mode(WIFI_OFF) + this flag saved.
    // Re-enabling: flag cleared + reboot required for clean driver init.
    bool getWifiDisabled() {
        return _cache_wifi_disabled;
    }
    void setWifiDisabled(bool value) {
        config["wifi"]["runtime_disabled"] = value;
        _cache_wifi_disabled = value;
        markDirtyAndScheduleSave();
    }

    // Boot log to SD card switch (persisted; survives reboot).
    // When enabled, boot messages (ETH_BOOT etc.) are written to the SD daily log.
    // Default: false — no extra log files written unless explicitly enabled.
    bool getBootLogEnabled() {
        return _cache_boot_log_enabled;
    }
    void setBootLogEnabled(bool value) {
        config["boot_log"]["enabled"] = value;
        _cache_boot_log_enabled = value;
        markDirtyAndScheduleSave();
    }

#if ENABLE_GRBL == 1
    // GRBL job trace log switch (persisted; survives reboot).
    // When enabled, every stream writes a trace log to /GRBL_JOBS/ on SD.
    // Default: false — no trace logs unless explicitly enabled.
    bool getGrblLogEnabled() {
        if (config.containsKey("grbl") && config["grbl"].containsKey("log_enabled"))
            return config["grbl"]["log_enabled"].as<bool>();
        return false;
    }
    void setGrblLogEnabled(bool value) {
        config["grbl"]["log_enabled"] = value;
        save(true);  // Immediate save to survive reboots.
    }
#endif

    // System Name getters/setters
    String getSystemName() {
        if (!config.containsKey("system_name")) return String("Node32-HUB");
        return config["system_name"].as<String>();
    }
    void setSystemName(const String& value) {
        config["system_name"] = value;
        markDirtyAndScheduleSave();
    }

    // mDNS / Bonjour advertisement (find the device by http://<name>.local)
    bool getMdnsEnabled() {
        if (!config.containsKey("mdns_enabled")) return true; // default ON
        return config["mdns_enabled"].as<bool>();
    }
    void setMdnsEnabled(bool on) {
        config["mdns_enabled"] = on;
        markDirtyAndScheduleSave();
    }

    // System Language getters/setters ("pt" or "en")
    String getSystemLanguage() {
        if (!config.containsKey("system_language")) return String("en");
        return config["system_language"].as<String>();
    }
    void setSystemLanguage(const String& value) {
        config["system_language"] = value;
        markDirtyAndScheduleSave();
    }
    
    // 💾 SD Card Logging getters/setters
    bool getSdcardEnabled() { 
        return _cache_sdcard_enabled; // ⚡ Retorna cache (seguro/rápido)
    }
    void setSdcardEnabled(bool value) { 
        config["sdcard"]["enabled"] = value; 
        _cache_sdcard_enabled = value; // ⚡ Atualiza cache
        markDirtyAndScheduleSave(); 
    }

    String getSdcardFlushInterval() {
        return _cache_sdcard_flush_interval.isEmpty() ? "30m" : _cache_sdcard_flush_interval;
    }
    // Minimum buffer-write interval is 10m (flash/SD wear protection).
    // Only 10m/30m/60m are accepted; legacy sub-10m values are coerced to 10m.
    static String clampFlushInterval(const String& value) {
        if (value == "10m" || value == "30m" || value == "60m") return value;
        return "10m";
    }
    void setSdcardFlushInterval(const String& value) {
        String v = clampFlushInterval(value);
        if (v != _cache_sdcard_flush_interval) {
            config["sdcard"]["flush_interval"] = v;
            _cache_sdcard_flush_interval = v;
            markDirtyAndScheduleSave();
        }
    }
    
    String getSdcardName() { 
        // Use system name for SD card folder naming (unified identity)
        // Note: sdcard.name field is deprecated and removed - always uses system name
        return getSystemName();
    }
    
    String getSdcardFormat() { 
        // Return with safe fallback ("csv" by default)
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("format")) { 
            return String("csv"); 
        }
        return config["sdcard"]["format"].as<String>();
    } // 📝 Formato: txt, csv, bin
    void setSdcardFormat(const String &value) { config["sdcard"]["format"] = value; markDirtyAndScheduleSave(); }
    
    bool getSdcardStructured() { 
        // 🔐 Validate field exists and return with safe fallback (true by default - with structures)
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("structured")) { 
            return true; 
        }
        return config["sdcard"]["structured"].as<bool>();
    }
    void setSdcardStructured(bool value) { config["sdcard"]["structured"] = value; markDirtyAndScheduleSave(); }
    
    bool getSdcardEmailSend() { 
        // 🔐 Validate field exists and return with safe fallback (true by default - also send by email)
        //     For new installs, also sending by email is enabled by default.
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("email_send")) { 
            return true; 
        }
        return config["sdcard"]["email_send"].as<bool>();
    }
    void setSdcardEmailSend(bool value) { config["sdcard"]["email_send"] = value; markDirtyAndScheduleSave(); }
    
    bool getSdcardEmailFromSd() { 
        // 🔐 Validate field exists and return with safe fallback (true by default - always attach)
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("email_from_sd")) { 
            return true; 
        }
        return config["sdcard"]["email_from_sd"].as<bool>();
    }
    void setSdcardEmailFromSd(bool value) { config["sdcard"]["email_from_sd"] = value; markDirtyAndScheduleSave(); }
    
    int getSdcardMaxSize() { 
        // Total max for all files to send via email (~4MB = 4 x 1024KB files)
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("max_size_mb")) { 
            return 4; 
        }
        return config["sdcard"]["max_size_mb"].as<int>();
    }
    void setSdcardMaxSize(int value) { config["sdcard"]["max_size_mb"] = value; markDirtyAndScheduleSave(); }
    
    String getEmaildayExtraEmails() { 
        // 📧 Return extra email list for EMAILDAY command (comma-separated)
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("emailday_extra_emails")) { 
            return String(""); // 🔒 No direct log (avoids deadlocks)
        }
        String val = config["sdcard"]["emailday_extra_emails"].as<String>();
        return val; // 🔒 Sem log direto (evita bloqueios)
    }
    void setEmaildayExtraEmails(const String &value) { 
        if (!config.containsKey("sdcard")) { 
            Serial.println("Criar seccao sdcard");
            config["sdcard"] = JsonObject(); 
        }
        config["sdcard"]["emailday_extra_emails"] = value;
        markDirtyAndScheduleSave();
    }

    String getSensordayExtraEmails() {
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("sensorday_extra_emails")) return String("");
        return config["sdcard"]["sensorday_extra_emails"].as<String>();
    }
    void setSensordayExtraEmails(const String &value) {
        if (!config.containsKey("sdcard")) config["sdcard"] = JsonObject();
        config["sdcard"]["sensorday_extra_emails"] = value;
        markDirtyAndScheduleSave();
    }

    String getLogdayExtraEmails() {
        if (!config.containsKey("sdcard") || !config["sdcard"].containsKey("logday_extra_emails")) return String("");
        return config["sdcard"]["logday_extra_emails"].as<String>();
    }
    void setLogdayExtraEmails(const String &value) {
        if (!config.containsKey("sdcard")) config["sdcard"] = JsonObject();
        config["sdcard"]["logday_extra_emails"] = value;
        markDirtyAndScheduleSave();
    }

    bool getSdAutoCleanup() { return _cache_sd_auto_cleanup; }
    void setSdAutoCleanup(bool value) {
        config["sdcard"]["auto_cleanup"] = value;
        _cache_sd_auto_cleanup = value;
        markDirtyAndScheduleSave();
    }

    int getSdCleanupThreshold() {
        int v = _cache_sd_cleanup_threshold;
        return (v >= 10 && v <= 99) ? v : 80;
    }
    void setSdCleanupThreshold(int value) {
        if (value < 10) value = 10;
        if (value > 99) value = 99;
        config["sdcard"]["cleanup_threshold"] = value;
        _cache_sd_cleanup_threshold = value;
        markDirtyAndScheduleSave();
    }

    int getSdKeepDays() { return _cache_sd_keep_days >= 0 ? _cache_sd_keep_days : 0; }
    void setSdKeepDays(int value) {
        if (value < 0) value = 0; // 0 = infinity (keep until the 70% threshold)
        config["sdcard"]["keep_days"] = value;
        _cache_sd_keep_days = value;
        markDirtyAndScheduleSave();
    }

    bool getWebServerEnabled() { 
        if (!config.containsKey("web") || !config["web"].containsKey("enabled")) return true;
        return config["web"]["enabled"].as<bool>();
    } // Status do webserver
    // Web UI credentials
    String getWebUsername() { 
        if (!config.containsKey("web") || !config["web"].containsKey("username")) return String("");
        return config["web"]["username"].as<String>();
    } // 👤 Usuário Web
    String getWebPassword() { 
        if (!config.containsKey("web") || !config["web"].containsKey("password")) return String("");
        return config["web"]["password"].as<String>();
    } // 🔑 Senha Web
    bool getWebAuthEnabled() { 
        return _cache_web_auth_enabled; // ⚡ Retorna cache (seguro/rápido) — HTTP Basic Auth master switch
    } // 🔒 HTTP auth active
    void setWebAuthEnabled(bool value) { 
        config["web"]["auth_enabled"] = value; 
        _cache_web_auth_enabled = value; // ⚡ Atualiza cache
        markDirtyAndScheduleSave(); 
    }
    bool getWebSingleClientOnly() { 
        if (!config.containsKey("web") || !config["web"].containsKey("single_client_only")) return false;
        return config["web"]["single_client_only"].as<bool>();
    } // Single-client mode
    int getWebMaxSessions() {
        if (!config.containsKey("web") || !config["web"].containsKey("max_sessions")) return 3;
        return config["web"]["max_sessions"].as<int>();
    } // Max concurrent HTML sessions
    int getWebIdleTimeoutMin() {
        if (!config.containsKey("web") || !config["web"].containsKey("idle_timeout_min")) return 10;
        return config["web"]["idle_timeout_min"].as<int>();
    } // Session idle kick timeout in minutes
    int getWebAbsTimeoutMin() {
        if (!config.containsKey("web") || !config["web"].containsKey("abs_timeout_min")) return 20;
        return config["web"]["abs_timeout_min"].as<int>();
    } // Session absolute timeout in minutes
    bool getWebViewerEnabled() {
        if (!config.containsKey("web") || !config["web"].containsKey("viewer_enabled")) return false;
        return config["web"]["viewer_enabled"].as<bool>();
    } // Read-only viewer user enabled
    String getWebViewerUsername() {
        if (!config.containsKey("web") || !config["web"].containsKey("viewer_username")) return String("viewer");
        return config["web"]["viewer_username"].as<String>();
    } // Viewer username
    String getWebViewerPassword() {
        if (!config.containsKey("web") || !config["web"].containsKey("viewer_password")) return String("");
        return config["web"]["viewer_password"].as<String>();
    } // Viewer password
    // 🌐 Ethernet getters
    String getEthernetMode() { 
        if (!config.containsKey("ethernet") || !config["ethernet"].containsKey("mode")) return String("dhcp");
        return config["ethernet"]["mode"].as<String>();
    } // 📡 Modo Ethernet
    String getEthernetIP() { 
        if (!config.containsKey("ethernet") || !config["ethernet"].containsKey("ip")) return String("");
        return config["ethernet"]["ip"].as<String>();
    } // 📍 IP Ethernet
    String getEthernetGW() { 
        if (!config.containsKey("ethernet") || !config["ethernet"].containsKey("gw")) return String("");
        return config["ethernet"]["gw"].as<String>();
    } // 🚪 Gateway Ethernet
    String getEthernetDNS() { 
        if (!config.containsKey("ethernet") || !config["ethernet"].containsKey("dns")) return String("");
        return config["ethernet"]["dns"].as<String>();
    } // 🔍 DNS Ethernet
    String getEthernetMacOverride() { 
        if (!config.containsKey("ethernet") || !config["ethernet"].containsKey("mac_override")) return String("");
        return config["ethernet"]["mac_override"].as<String>();
    } // 🆔 MAC Override
    int getTimeoutAutoEmailEnabled() { 
        if (!config.containsKey("timeouts") || !config["timeouts"].containsKey("auto_email_enabled")) return 10000;
        return config["timeouts"]["auto_email_enabled"].as<int>();
    } // ⏱️ Timeout email (ativado)
    int getTimeoutAutoEmailDisabled() { 
        if (!config.containsKey("timeouts") || !config["timeouts"].containsKey("auto_email_disabled")) return 300000;
        return config["timeouts"]["auto_email_disabled"].as<int>();
    } // ⏱️ Timeout email (desativado)
    int getTimeoutTelnetInactivity() { 
        if (!config.containsKey("timeouts") || !config["timeouts"].containsKey("telnet_inactivity")) return 60000;
        return config["timeouts"]["telnet_inactivity"].as<int>();
    } // ⏱️ Timeout Telnet
    String getNtpServer() { return ntpManager.getServer(); }
    String getNtpTimezone() { return ntpManager.getTimezone(); }
    int getNtpSyncIntervalMinutes() { return ntpManager.getSyncIntervalMinutes(); }
    bool getDebugLogsEnabled() { 
        if (!config.containsKey("debug") || !config["debug"].containsKey("logs_enabled")) return false;
        return config["debug"]["logs_enabled"].as<bool>();
    } // 🐛 Status logs debug
    // 🧩 Estruturas predefinidas – Amada ISQ20 MTF1
    bool getAmadaISQ20MTF1Enabled() { // 🧠 Read enable flag
        if (!config.containsKey("structures") || !config["structures"].is<JsonObject>()) return false; // ⚠️ No section
        JsonObject s = config["structures"].as<JsonObject>(); // 📦 Raiz
        if (!s.containsKey("amada_isq20_mtf1") || !s["amada_isq20_mtf1"].is<JsonObject>()) return false; // ⚠️ Sem equipamento
        JsonObject a = s["amada_isq20_mtf1"].as<JsonObject>(); // 📦 Equipamento
        return a.containsKey("enabled") ? a["enabled"].as<bool>() : false; // ✅ Valor ou false
    }

    // 🧩 Estruturas predefinidas – Amada ISQ20 AWSE
    bool getAmadaISQ20AWSEEnabled() { // 🧠 Read enable flag
        if (!config.containsKey("structures") || !config["structures"].is<JsonObject>()) return false; // ⚠️ No section
        JsonObject s = config["structures"].as<JsonObject>(); // 📦 Raiz
        if (!s.containsKey("amada_isq20_awse") || !s["amada_isq20_awse"].is<JsonObject>()) return false; // ⚠️ Sem equipamento
        JsonObject a = s["amada_isq20_awse"].as<JsonObject>(); // 📦 Equipamento
        return a.containsKey("enabled") ? a["enabled"].as<bool>() : false; // ✅ Valor ou false
    }
    
    // 🧩 Auto-detection – global flag to try all structures
    bool getStructuresAutoDetectEnabled() { // 🧠 Read auto_detect flag
        if (!config.containsKey("structures") || !config["structures"].is<JsonObject>()) return true; // ⚠️ No section — active by default
        JsonObject s = config["structures"].as<JsonObject>(); // 📦 Raiz
        return s.containsKey("auto_detect") ? s["auto_detect"].as<bool>() : true; // ✅ Valor ou por defeito true
    }
    
    // Header repeat interval — how many data lines between each header re-insertion (0 = no repeat)
    int getAmadaHeaderEvery() {
        if (!config.containsKey("structures") || !config["structures"].is<JsonObject>()) return 20;
        JsonObject s = config["structures"].as<JsonObject>();
        return s.containsKey("amada_header_every") ? s["amada_header_every"].as<int>() : 20;
    }
    void setAmadaHeaderEvery(int value) {
        config["structures"]["amada_header_every"] = value;
        markDirtyAndScheduleSave();
    }

    //  Filter Amada headers (discard received header lines)
    bool getAmadaFilterHeaders() { // 🧠 Read filter headers flag
        if (!config.containsKey("structures") || !config["structures"].is<JsonObject>()) return true; // ⚠️ No section — ACTIVE by default (filter for safety)
        JsonObject s = config["structures"].as<JsonObject>(); // 📦 Raiz
        return s.containsKey("amada_filter_headers") ? s["amada_filter_headers"].as<bool>() : true; // ✅ Valor ou true (ativo por defeito)
    }
    
    // 📥 Typed safe setters (to define configuration values)
    void setWifiSSID(const String& value) { config["wifi"]["ssid"] = value; markDirtyAndScheduleSave(); } // Sets the WiFi SSID
    void setWifiPassword(const String& value) { config["wifi"]["password"] = value; markDirtyAndScheduleSave(); } // 🔑 Define a senha do Wi-Fi
    void setSmtpServer(const String& value) { config["smtp"]["server"] = value; markDirtyAndScheduleSave(); } // Sets the SMTP server
    void setSmtpPort(int value) { config["smtp"]["port"] = value; markDirtyAndScheduleSave(); } // 🚪 Define a porta SMTP
    void setSmtpUsername(const String& value) { config["smtp"]["username"] = value; markDirtyAndScheduleSave(); } // 👤 Define o usuário SMTP
    void setSmtpPassword(const String& value) { config["smtp"]["password"] = value; markDirtyAndScheduleSave(); } // 🔑 Define a senha SMTP
    void setSmtpTlsInsecure(bool value) { config["smtp"]["tls_insecure"] = value; markDirtyAndScheduleSave(); } // 🔒 Define TLS inseguro
    void setEmailDestination(const String& value) { config["email"]["destination"] = value; markDirtyAndScheduleSave(); } // 🎯 Set the destination email (now schedules immediate write to persist)
    void setEmailAutoSend(bool value) { 
        config["email"]["auto_send"] = value; 
        markDirtyAndScheduleSave(); 
        // If firmware runtime variable exists, update it immediately so behavior follows saved config
        extern bool autoSendEmail; // declared in chatgpt.ino
        extern void clearPrinterDataBufferAndNotifyUI(); // best-effort forward decl - may be defined in firmware
        bool oldVal = false;
        // Attempt to read old value safely (wrapped in try-like pattern)
        oldVal = autoSendEmail;
        autoSendEmail = value;
        // If value changed, trigger buffer clear/notification as firmware does elsewhere
        if (oldVal != value) {
            // call best-effort function; if not linked it will be resolved at link-time if present
            clearPrinterDataBufferAndNotifyUI();
        }
    } // 自動 Define o envio automático de e-mail
    void setEmailMaxLines(int value) { 
        // 🧠 Atualiza config persistente
        config["email"]["max_lines"] = value; // 📦 Store in the JSON object
        // 🧠 Immediately propagate to runtime global variable to reflect in /api/configs
        extern int currentMaxEmailLines; // 📦 Declared in chatgpt.ino
        currentMaxEmailLines = value; // 🔄 Sync runtime
        markDirtyAndScheduleSave(); // 💾 Schedule write
    } // 📏 Set max lines per email (with runtime sync)
    // ♻️ Removed recipients_active_lines getters/setters (legacy field eliminated)
    void setEmailMinContentLength(int value) {
        // ✅ Defensive clamp (0..100) to avoid invalid values from frontend/JSON
        if (value < 0) value = 0; else if (value > 100) value = 100; // ✅ Reasonable limits
        config["email"]["min_content_length"] = value; // 💾 Update in-memory config
        markDirtyAndScheduleSave(); // ♻️ Schedule write
        // 🧪 Light log (not too verbose) for eventual diagnosis
    Serial.print(F("CFG: email.min_len=")); Serial.println(String(value));
    } // 📝 Set the minimum content length
    void setEmailSubject(const String& value) { config["email"]["subject"] = value; markDirtyAndScheduleSave(); } // Set the email subject
    // Web UI setters
    void setWebUsername(const String& value) { config["web"]["username"] = value; markDirtyAndScheduleSave(); }
    void setWebPassword(const String& value) { config["web"]["password"] = value; markDirtyAndScheduleSave(); }
    void setWebSingleClientOnly(bool value) { config["web"]["single_client_only"] = value; markDirtyAndScheduleSave(); }
    void setWebMaxSessions(int value) { config["web"]["max_sessions"] = value; markDirtyAndScheduleSave(); }
    void setWebIdleTimeoutMin(int value) { config["web"]["idle_timeout_min"] = value; markDirtyAndScheduleSave(); }
    void setWebAbsTimeoutMin(int value) { config["web"]["abs_timeout_min"] = value; markDirtyAndScheduleSave(); }
    void setWebViewerEnabled(bool value) { config["web"]["viewer_enabled"] = value; markDirtyAndScheduleSave(); }
    void setWebViewerUsername(const String& value) { config["web"]["viewer_username"] = value; markDirtyAndScheduleSave(); }
    void setWebViewerPassword(const String& value) { config["web"]["viewer_password"] = value; markDirtyAndScheduleSave(); }
    void setRs232Enabled(bool value) { config["rs232"]["enabled"] = value; markDirtyAndScheduleSave(); } // Sets RS232 enabled flag
    void setRs232BaudrateIndex(int value) { config["rs232"]["baudrate_index"] = value; markDirtyAndScheduleSave(); } // 🔢 Set the baudrate index
    void setRs232InvertPins(bool value) { config["rs232"]["invert_pins"] = value; markDirtyAndScheduleSave(); } // 🔄 Set pin inversion
    // Novos setters RS232
    void setRs232DataBits(int value) { config["rs232"]["data_bits"] = value; markDirtyAndScheduleSave(); }
    void setRs232Parity(const String& value) { config["rs232"]["parity"] = value; markDirtyAndScheduleSave(); }
    void setRs232StopBits(int value) { config["rs232"]["stop_bits"] = value; markDirtyAndScheduleSave(); }
    void setRs232ReadTimeoutMs(int value) { config["rs232"]["read_timeout_ms"] = value; markDirtyAndScheduleSave(); }
    // 🌐 Ethernet setters (marcam dirty e agenda save)
    void setEthernetMode(const String &value){ config["ethernet"]["mode"] = value; markDirtyAndScheduleSave(); }
    void setEthernetIP(const String &value){ config["ethernet"]["ip"] = value; markDirtyAndScheduleSave(); }
    void setEthernetGW(const String &value){ config["ethernet"]["gw"] = value; markDirtyAndScheduleSave(); }
    void setEthernetDNS(const String &value){ config["ethernet"]["dns"] = value; markDirtyAndScheduleSave(); }
    void setEthernetMacOverride(const String &value){ config["ethernet"]["mac_override"] = value; markDirtyAndScheduleSave(); }
    void setRs232UseXonXoff(bool value) { config["rs232"]["use_xon_xoff"] = value; markDirtyAndScheduleSave(); }
    void setRs232MirrorToUsb(bool value) { config["rs232"]["mirror_to_usb"] = value; markDirtyAndScheduleSave(); }
    // Persist the name of the currently active RS232 template (optional - helps "last used" behavior)
    String getRs232CurrentTemplate() { 
        if (!config.containsKey("rs232") || !config["rs232"].containsKey("current_template")) return String("");
        return config["rs232"]["current_template"].as<String>();
    } // 📋 Template atual
    void setRs232CurrentTemplate(const String& name) { config["rs232"]["current_template"] = name; markDirtyAndScheduleSave(); }
    // Volatile setter: update current_template in RAM only, do not mark dirty or persist to NVS.
    void setRs232CurrentTemplateVolatile(const String& name) { 
        if (!config.containsKey("rs232")) config["rs232"] = JsonObject();
        config["rs232"]["current_template"] = name;
        // intentionally do not call markDirtyAndScheduleSave()
    }
    // -------------------------
    // RS232 Templates (persisted under config["rs232"]["templates"])
    // -------------------------
    JsonArray getRs232Templates() {
        if (!config.containsKey("rs232") || !config["rs232"].is<JsonObject>()) config["rs232"] = JsonObject();
        if (!config["rs232"].containsKey("templates") || !config["rs232"]["templates"].is<JsonArray>()) {
            // create if missing
            if (config["rs232"].containsKey("templates")) config["rs232"].remove("templates");
            config["rs232"].createNestedArray("templates");
        }
        return config["rs232"]["templates"].as<JsonArray>();
    }

    // Add or update a template by name. Fields are simple typed values stored under each template object.
    // Now accepts an optional explicit numeric baudrate (baudrate_numeric). If >=0, it will be stored as `baudrate` in the template object.
    void addOrUpdateRs232Template(const String& name, int baudrate_index, int data_bits, const String& parity, int stop_bits, bool invert_pins, int read_timeout_ms, bool enabled, int baudrate_numeric = -1, bool use_xon_xoff=false) {
        if (name.length() == 0) return;
        Serial.print(F("[TPL] Adding/updating template: ")); Serial.println(name);
        JsonArray arr = getRs232Templates();
        
        // Check if template already exists (update mode)
        int foundIndex = -1;
        for (size_t i = 0; i < arr.size(); ++i) {
            JsonObject o = arr[i];
            if (o.containsKey("name") && String(o["name"].as<String>()) == name) { foundIndex = (int)i; break; }
        }
        
        if (foundIndex >= 0) {
            // Update existing template (no limit enforcement)
            JsonObject o = arr[foundIndex];
            o["baudrate_index"] = baudrate_index;
            if (baudrate_numeric >= 0) o["baudrate"] = baudrate_numeric;
            o["data_bits"] = data_bits;
            o["parity"] = parity;
            o["stop_bits"] = stop_bits;
            o["invert_pins"] = invert_pins;
            // RTS/CTS removed: not stored per template (hardware unsupported)
            o["read_timeout_ms"] = read_timeout_ms;
            o["enabled"] = enabled;
            o["use_xon_xoff"] = use_xon_xoff;
        } else {
            // Add new template: enforce limit of 6 (circular overwrite)
            const int MAX_RS232_TEMPLATES = 6; // Maximum templates allowed
            if (arr.size() >= MAX_RS232_TEMPLATES) {
                // Remove oldest (first) template to make space
                arr.remove(0);
            }
            
            JsonObject o = arr.createNestedObject();
            o["name"] = name;
            o["baudrate_index"] = baudrate_index;
            if (baudrate_numeric >= 0) o["baudrate"] = baudrate_numeric;
            o["data_bits"] = data_bits;
            o["parity"] = parity;
            o["stop_bits"] = stop_bits;
            o["invert_pins"] = invert_pins;
            // RTS/CTS removed: not stored per template (hardware unsupported)
            o["read_timeout_ms"] = read_timeout_ms;
            o["enabled"] = enabled;
            o["use_xon_xoff"] = use_xon_xoff; // 🧠 Fix parameter name (previously used nonexistent identifier use_xon)
        }
    // Mark templates changed; defer heavy NVS save
    templatesDirty = true;
    lastTemplateChangeMillis = millis();
    // keep general dirty flag so getFullConfig reflects changes immediately to web UI
    isDirty = true;
        // Templates are saved to LittleFS by save(true) called from the web handler
    }

    // Remove template by name
    void removeRs232Template(const String& name) {
        if (name.length() == 0) return;
        JsonArray arr = getRs232Templates();
        for (int i = arr.size() - 1; i >= 0; --i) {
            JsonObject o = arr[i];
            if (o.containsKey("name") && String(o["name"].as<String>()) == name) {
                arr.remove(i);
                templatesDirty = true;
                lastTemplateChangeMillis = millis();
                isDirty = true;
                // Templates are saved to LittleFS by save(true) called from the web handler
                break;
            }
        }
    }
    void setPrinterEmulationMode(int value) { config["printer"]["emulation_mode"] = value; markDirtyAndScheduleSave(); } // 🖨️ Set the printer emulation mode
    void setPrinterCharsetMode(int value) { config["printer"]["charset_mode"] = value; markDirtyAndScheduleSave(); } // 🔤 Define o modo de charset da impressora
    void setTelnetUsername(const String& value) { config["telnet"]["username"] = value; markDirtyAndScheduleSave(); } // 👤 Define o usuário Telnet
    void setTelnetPassword(const String& value) { config["telnet"]["password"] = value; markDirtyAndScheduleSave(); } // 🔑 Define a senha Telnet
    void setTelnetPort(int value) { config["telnet"]["port"] = value; markDirtyAndScheduleSave(); } // 🚪 Define a porta Telnet
    void setWebServerEnabled(bool value) { config["web"]["enabled"] = value; markDirtyAndScheduleSave(); } // 🌐 Define se webserver está ativo
    void setTimeoutAutoEmailEnabled(int value) { config["timeouts"]["auto_email_enabled"] = value; markDirtyAndScheduleSave(); } // ⏱️ Define o timeout de e-mail (habilitado)
    void setTimeoutAutoEmailDisabled(int value) { config["timeouts"]["auto_email_disabled"] = value; markDirtyAndScheduleSave(); } // ⏱️ Define o timeout de e-mail (desabilitado)
    void setTimeoutTelnetInactivity(int value) { config["timeouts"]["telnet_inactivity"] = value; markDirtyAndScheduleSave(); } // ⏱️ Define o timeout de inatividade Telnet
    void setNtpServer(const String& value) { ntpManager.setServer(value); }
    void setNtpTimezone(const String& value) { ntpManager.setTimezone(value); }
    void setNtpSyncIntervalMinutes(int value) { ntpManager.setSyncIntervalMinutes(value); }
    void setDebugLogsEnabled(bool value) { config["debug"]["logs_enabled"] = value; markDirtyAndScheduleSave(); } // 🐛 Define o status dos logs de debug
    // 🧩 Estruturas predefinidas – setter Amada ISQ20 MTF1
    void setAmadaISQ20MTF1Enabled(bool value) { // 🧠 Atualiza flag e agenda save
        if (!config.containsKey("structures")) config["structures"] = JsonObject(); // ♻️ Garante raiz
        if (!config["structures"].containsKey("amada_isq20_mtf1")) config["structures"]["amada_isq20_mtf1"] = JsonObject(); // ♻️ Garante objeto
        config["structures"]["amada_isq20_mtf1"]["enabled"] = value; // ✍️ Grava
        markDirtyAndScheduleSave(); // 💾 Agenda
    }

    // 🧩 Estruturas predefinidas – setter Amada ISQ20 AWSE
    void setAmadaISQ20AWSEEnabled(bool value) { // 🧠 Atualiza flag e agenda save
        if (!config.containsKey("structures")) config["structures"] = JsonObject(); // ♻️ Garante raiz
        if (!config["structures"].containsKey("amada_isq20_awse")) config["structures"]["amada_isq20_awse"] = JsonObject(); // ♻️ Garante objeto
        config["structures"]["amada_isq20_awse"]["enabled"] = value; // ✍️ Grava
        markDirtyAndScheduleSave(); // 💾 Agenda
    }
    
    // 🧩 Auto-detection – global setter
    void setStructuresAutoDetectEnabled(bool value) { // 🧠 Update auto_detect flag
        if (!config.containsKey("structures")) config["structures"] = JsonObject(); // ♻️ Ensure root
        config["structures"]["auto_detect"] = value; // ✍️ Write simple flag
        markDirtyAndScheduleSave(); // 💾 Schedule persistence
    }
    
    //  Filter Amada headers – setter
    void setAmadaFilterHeaders(bool value) { // 🧠 Update filter headers flag
        if (!config.containsKey("structures")) config["structures"] = JsonObject(); // ♻️ Ensure root
        config["structures"]["amada_filter_headers"] = value; // ✍️ Write simple flag
        markDirtyAndScheduleSave(); // 💾 Schedule persistence
    }
    
    // 📥 Setters for Access Point settings (LEGACY - for compatibility)
    void setAccessPointSSID(const String& value) { config["access_point"]["ssid"] = value; markDirtyAndScheduleSave(); } // 📶 Define o SSID do AP
    void setAccessPointPassword(const String& value) { config["access_point"]["password"] = value; markDirtyAndScheduleSave(); } // 🔑 Define a senha do AP
    void setAccessPointRetryDelay(unsigned long value) { config["access_point"]["retry_delay"] = value; markDirtyAndScheduleSave(); } // ⏳ Define o delay de retentativa do AP
    void setAccessPointMaxFailures(int value) { config["access_point"]["max_failures"] = value; markDirtyAndScheduleSave(); } // ❌ Set the maximum de falhas do AP
    void setAccessPointFailurePause(unsigned long value) { config["access_point"]["failure_pause"] = value; markDirtyAndScheduleSave(); } // ⏸️ Define a pausa por falha do AP
    void setAccessPointActivationDelay(unsigned long value) { config["access_point"]["activation_delay"] = value; markDirtyAndScheduleSave(); } // ⏱️ Set the AP activation delay
    // 📤 Access Point getters (needed for existing calls in other units)
    String getAccessPointSSID() { // 🧠 Return configured SSID (or empty if undefined)
        return config.containsKey("access_point") && config["access_point"].is<JsonObject>() && config["access_point"].containsKey("ssid") ? String(config["access_point"]["ssid"].as<const char*>()) : String(""); // 🧩 Acesso seguro
    }
    String getAccessPointPassword() { // 🧠 Return AP password (may be empty)
        return config.containsKey("access_point") && config["access_point"].is<JsonObject>() && config["access_point"].containsKey("password") ? String(config["access_point"]["password"].as<const char*>()) : String(""); // 🧩 Acesso seguro
    }
    unsigned long getAccessPointRetryDelay() { // 🧠 Delay between AP attempts
        return config.containsKey("access_point") && config["access_point"].is<JsonObject>() && config["access_point"].containsKey("retry_delay") ? (unsigned long) config["access_point"]["retry_delay"].as<unsigned long>() : 0UL; // 🧩 Fallback 0
    }
    int getAccessPointMaxFailures() { // 🧠 Max failures before pause
        return config.containsKey("access_point") && config["access_point"].is<JsonObject>() && config["access_point"].containsKey("max_failures") ? (int) config["access_point"]["max_failures"].as<int>() : 0; // 🧩 Fallback 0
    }
    unsigned long getAccessPointFailurePause() { // 🧠 Pause after exceeding failures
        return config.containsKey("access_point") && config["access_point"].is<JsonObject>() && config["access_point"].containsKey("failure_pause") ? (unsigned long) config["access_point"]["failure_pause"].as<unsigned long>() : 0UL; // 🧩 Fallback 0
    }
    unsigned long getAccessPointActivationDelay() { // 🧠 Initial AP activation delay
        return config.containsKey("access_point") && config["access_point"].is<JsonObject>() && config["access_point"].containsKey("activation_delay") ? (unsigned long) config["access_point"]["activation_delay"].as<unsigned long>() : 0UL; // 🧩 Fallback 0
    }
    
    // 📡 NEW: Setters for hybrid WiFi system settings
    void setWifiHybridEnabled(bool value) { config["wifi_hybrid"]["enabled"] = value; markDirtyAndScheduleSave(); } // ✅ Set hybrid mode status
    void setWifiHybridBootTimeout(unsigned long value) { config["wifi_hybrid"]["boot_timeout"] = value; markDirtyAndScheduleSave(); } // ⏱️ Set hybrid mode boot timeout
    void setWifiHybridStaStableTime(unsigned long value) { config["wifi_hybrid"]["sta_stable_time"] = value; markDirtyAndScheduleSave(); } // ⏳ Set hybrid mode stabilization time
    void setWifiHybridStaRecoveryTimeout(unsigned long value) { config["wifi_hybrid"]["sta_recovery_timeout"] = value; markDirtyAndScheduleSave(); } // 🔄 Set hybrid mode recovery timeout
    // 📤 Hybrid WiFi getters
    unsigned long getWifiHybridBootTimeout() { // 🧠 Hybrid boot timeout
        return config.containsKey("wifi_hybrid") && config["wifi_hybrid"].is<JsonObject>() && config["wifi_hybrid"].containsKey("boot_timeout") ? (unsigned long) config["wifi_hybrid"]["boot_timeout"].as<unsigned long>() : 0UL; // 🧩 Fallback 0
    }
    unsigned long getWifiHybridStaStableTime() { // 🧠 STA stabilization time
        return config.containsKey("wifi_hybrid") && config["wifi_hybrid"].is<JsonObject>() && config["wifi_hybrid"].containsKey("sta_stable_time") ? (unsigned long) config["wifi_hybrid"]["sta_stable_time"].as<unsigned long>() : 0UL; // 🧩 Fallback 0
    }
    unsigned long getWifiHybridStaRecoveryTimeout() { // 🧠 STA recovery timeout
        return config.containsKey("wifi_hybrid") && config["wifi_hybrid"].is<JsonObject>() && config["wifi_hybrid"].containsKey("sta_recovery_timeout") ? (unsigned long) config["wifi_hybrid"]["sta_recovery_timeout"].as<unsigned long>() : 0UL; // 🧩 Fallback 0
    }
    
    // 👥 Recipient management
    void addRecipient(const String& name, const String& email) { // ➕ Adiciona um novo destinatário
        String lowerName = name; // 📜 Copia o nome
        lowerName.toLowerCase(); // 🔡 Convert to lowercase
        config["recipients"][lowerName] = email; // Add email to recipient
        markDirtyAndScheduleSave(); // Schedule save
    }
    
    void removeRecipient(const String& name) { // ➖ Remove um destinatário
        String lowerName = name; // 📜 Copia o nome
        lowerName.toLowerCase(); // 🔡 Convert to lowercase
        config["recipients"].remove(lowerName); // 🗑️ Remove o destinatário
        markDirtyAndScheduleSave(); // 💾 Mark for save
    }
    
    void clearAllRecipients() { // 🧹 Clear all recipients
        config["recipients"] = JsonObject(); // 🗑️ Create a new empty JSON object for recipients
        markDirtyAndScheduleSave(); // 💾 Mark for save
    }

    // Slot-based recipient management (recipients_slots) for row-level editing with low heap impact
    void ensureRecipientSlotsArray(){ // Ensure array exists
        if(!recipDoc.containsKey("recipients_slots") || !recipDoc["recipients_slots"].is<JsonArray>()){ // Missing or wrong type
            if(recipDoc.containsKey("recipients_slots")) recipDoc.remove("recipients_slots"); // Remove invalid
            recipDoc.createNestedArray("recipients_slots"); // Create empty array
        }
    }
    JsonArray getRecipientSlots(){ // Access the slots array
        ensureRecipientSlotsArray(); // Ensure array exists
        JsonArray arr = recipDoc["recipients_slots"].as<JsonArray>(); // Current array
        if(arr.size()==0){ // Empty - try migrating legacy data
            // Migrate legacy recipients (I) entries
            if(config.containsKey("recipients") && config["recipients"].is<JsonObject>()){ // Has recipients object
                JsonObject recObj = config["recipients"].as<JsonObject>(); // Object
                for(JsonPair kv : recObj){ // Each pair
                    JsonObject slot = arr.createNestedObject(); // Slot
                    slot["name"] = String(kv.key().c_str()); // Name
                    slot["op"] = "I"; // Equal
                    slot["emails"] = kv.value().as<String>(); // Emails
                }
            }
            // Migrate legacy numeric_rules entries
            if(recipDoc.containsKey("numeric_rules") && recipDoc["numeric_rules"].is<JsonArray>()){ // Has numeric_rules
                JsonArray nr = recipDoc["numeric_rules"].as<JsonArray>(); // Array
                for(JsonObject r : nr){ // Each rule
                    String nm = r.containsKey("name") ? r["name"].as<String>() : (r.containsKey("keyword") ? r["keyword"].as<String>() : String()); // Name
                    int opi = r.containsKey("op") ? r["op"].as<int>() : 2; // Code
                    String em = r.containsKey("emails") ? r["emails"].as<String>() : String(); // Emails
                    if(nm.length()==0 || em.length()==0) continue; // Skip empty
                    JsonObject slot = arr.createNestedObject(); // Slot
                    slot["name"] = nm; // Name
                    // Map integer code to symbol
                    char sop = 'D'; // Default different
                    if(opi==0) sop='-'; else if(opi==1) sop='+'; else if(opi==2) sop='D'; // Translate
                    slot["op"] = String(sop); // Operator
                    slot["emails"] = em; // Emails
                }
            }
        }
        return arr; // Return (migrated if applicable)
    }
    bool getRecipientSlot(int index, String &name, String &op, String &emails){ // 🔍 Get slot (1-based)
        if(index<1 || index>50) return false; // ⚠️ Fora de limites
        JsonArray arr = getRecipientSlots(); // 📦 Array
        if(index > (int)arr.size()) return false; // ⚠️ Does not exist
        JsonObject slot = arr[index-1]; // 📌 Slot
        name = slot["name"].as<String>(); // 📝 Nome
        op = slot["op"].as<String>(); // 📝 Operador
        emails = slot["emails"].as<String>(); // 📝 Emails
        return true; // ✅ Success
    }
    bool getRecipientSlotWithFreq(int index, String &name, String &op, String &emails, int &freq){ // 🔍 Get slot with frequency
        if(index<1 || index>50) return false; // ⚠️ Out of bounds
        JsonArray arr = getRecipientSlots(); // 📦 Array
        if(index > (int)arr.size()) return false; // ⚠️ Does not exist
        JsonObject slot = arr[index-1]; // 📌 Slot
        name = slot["name"].as<String>(); // 📝 Name
        op = slot["op"].as<String>(); // 📝 Operator
        emails = slot["emails"].as<String>(); // 📝 Emails
        freq = slot["freq"] | 0; // 📝 Frequency (default 0 = always)
        return true; // ✅ Success
    }
    void resizeRecipientSlots(int minSize){ // Ensure minimum array size
        ensureRecipientSlotsArray(); // Ensure array
        JsonArray arr = recipDoc["recipients_slots"].as<JsonArray>(); // Array
        while((int)arr.size() < minSize){ // 🧮 Enquanto menor que necessário
            JsonObject slot = arr.createNestedObject(); // ➕ Novo slot
            slot["name"] = ""; // 📦 Inicial nome vazio
            slot["op"] = "I"; // 📦 Operador default
            slot["emails"] = ""; // 📦 Emails vazios
            slot["enabled"] = true; // Slot enabled by default
        }
    }
    bool updateRecipientSlot(int index, const String &nameRaw, const String &opRaw, const String &emailsRaw){ // ✏️ Update specific slot (1-based)
        if(index < 1 || index > 50) return false; // ⚠️ Index limit
        resizeRecipientSlots(index); // 🧠 Garante slot existente
        JsonArray arr = getRecipientSlots(); // 📦 Array
        JsonObject slot = arr[index-1]; // 🔍 Slot pretendido
        String name = nameRaw; // 🧪 Copia
        name.trim(); // 🧼 Trim edge spaces
        if(name.length() > 50) name = name.substring(0,50); // Limit to 50 chars
        // Normalize op (delegates to config_utils.h)
        String op = ::normalizeOp(opRaw);
        String emails = emailsRaw; emails.trim(); // Strip emails
        if(emails.length() > 256) emails = emails.substring(0,256); // Limit emails
        if(name.length()==0 && emails.length()==0){ // Empty line → reset slot
            slot["name"] = ""; //
            slot["op"] = "I"; // Reset op
            slot["emails"] = ""; //
        } else {
            slot["name"] = name; // Store name
            slot["op"] = op; // Store op (includes multi-char codes like DF, Cw, Hy)
            slot["emails"] = emails; // Store emails
        }
        rebuildRecipientsDerivedFromSlots(); // Rebuild derived structures
        markDirtyAndScheduleSave(); // Schedule persistence - save() writes to LittleFS
        return true; // ✅ Success
    }
    bool updateRecipientSlotWithFreq(int index, const String &nameRaw, const String &opRaw, const String &emailsRaw, int freqRaw, bool enabled = true){ // ✏️ Update specific slot with frequency (1-based)
        if(index < 1 || index > 50) return false; // ⚠️ Index limit
        resizeRecipientSlots(index); // 🧠 Ensure slot exists
        JsonArray arr = getRecipientSlots(); // 📦 Array
        JsonObject slot = arr[index-1]; // 🔍 Target slot
        String name = nameRaw; // 🧪 Copy
        name.trim(); // 🧼 Strip whitespace
        if(name.length() > 50) name = name.substring(0,50); // Limit to 50 chars
        // Normalize op (delegates to config_utils.h)
        String op = ::normalizeOp(opRaw);
        String emails = emailsRaw; emails.trim(); // Strip emails
        if(emails.length() > 256) emails = emails.substring(0,256); // Limit emails
        int freq = freqRaw; // Copy frequency
        if(freq < 0) freq = 0; // Sanitize (no negative)
        if(freq > 86400) freq = 86400; // Max 24h (86400 seconds)
        if(name.length()==0 && emails.length()==0){ // Empty line → reset slot
            slot["name"] = ""; //
            slot["op"] = "I"; // Reset op
            slot["emails"] = ""; //
            slot["freq"] = 0; // Reset freq
            slot["enabled"] = true; // Reset to enabled on clear
        } else {
            slot["name"] = name; // Store name
            slot["op"] = op; // Store op (includes multi-char codes like DF, Cw, Hy)
            slot["emails"] = emails; // Store emails
            slot["freq"] = freq; // Store frequency
            slot["enabled"] = enabled; // Store enabled flag
        }
        rebuildRecipientsDerivedFromSlots(); // Rebuild derived structures
        markDirtyAndScheduleSave(); // Schedule persistence - save() writes to LittleFS
        return true; // Success
    }
    void rebuildRecipientsDerivedFromSlots(){ // 🔄 Rebuild recipients + numeric_rules for compatibility
        ensureRecipientSlotsArray(); // 🧠 Garante array
        // 🧹 Clear previous
        if(config.containsKey("recipients")) config.remove("recipients"); // Remove legacy object
        config["recipients"] = JsonObject(); // New object
        if(recipDoc.containsKey("numeric_rules")) recipDoc.remove("numeric_rules"); // Remove old rules
        JsonArray numArr = recipDoc.createNestedArray("numeric_rules"); // New numeric_rules array
        JsonArray slots = getRecipientSlots(); // 📦 Slots
        for(JsonObject s : slots){ // 🔁 Iterate each slot
            String n = s["name"] | String(""); // 🔍 Nome
            String op = s["op"] | String("I"); // 🔍 Operador
            String em = s["emails"] | String(""); // 🔍 Emails
            if(!n.length() || !em.length()) continue; // ⏭️ Ignora incompletos
            if(!(s["enabled"] | true)) continue; // Skip disabled slots
            if(op == "I"){ // 🧠 Mapa direto
                String lower = n; lower.toLowerCase(); // 🔡 Normalize
                config["recipients"][lower] = em; // Associate
            } else { // 🧠 Numeric rules based on embedded name+number
                JsonObject r = numArr.createNestedObject(); // 🆕 Rule object
                r["name"] = n; // 📝 Original name (contains number)
                r["keyword"] = n; // 📝 same keyword (legacy)
                // Mapping: symbol → integer code (0=less,1=greater,2=different)
                // 🧠 Expanded: +=greater, -=less, D=different, G=greater or equal, L=less or equal, R=range, DF/DR=out of range
                if(op == "+") r["op"] = 1; // 🧠 Maior que
                else if(op == "-") r["op"] = 0; // 🧠 Menor que
                else if(op == "D") r["op"] = 2; // 🧠 Diferente de
                else if(op == "G") r["op"] = 4; // 🧠 Maior ou igual
                else if(op == "L") r["op"] = 5; // 🧠 Menor ou igual
                else if(op == "R") r["op"] = 6; // 🧠 Intervalo (a..b)
                else if(op == "DF" || op == "DR") r["op"] = 7; // 🧠 Out of range (outside a..b)
                else if(op == "V") r["op"] = 8; // V: delta - variacao relativa ao valor anterior
                else if(op == "C") r["op"] = 9; // C: count - contagem de ocorrencias acumuladas
                else if(op == "P") r["op"] = 10; // P: presence watchdog - ausencia por timeout
                else if(op == "Cw") r["op"] = 11; // Cw: count within time window (N occurrences in W seconds)
                else if(op == "Hy") r["op"] = 12; // Hy: hysteresis (fire at >= high, re-arm when < low)
                else r["op"] = 2; // Unknown operator - default to 'different'
                r["emails"] = em; // 📝 Emails
            }
        }
    }
    
    int findRecipientSlotIndexByName(const String &nameLower) { // Find slot index (1-50) by recipient name (expects lowercase)
        ensureRecipientSlotsArray(); // Ensure array exists
        JsonArray slots = getRecipientSlots(); // Slots
        for (size_t i = 0; i < slots.size(); ++i) { // Iterate all slots
            JsonObject slot = slots[i]; // Current slot
            String n = slot["name"] | String(""); // Name
            if (!n.length()) continue; // Skip empty
            String nLower = n; nLower.toLowerCase(); // Normalize
            if (nLower == nameLower) {
                return i + 1; // Found (1-based index)
            }
        }
        return -1; // Not found
    }
    
    int getRecipientSlotFrequency(int index) { // Get throttling frequency for slot (returns 0 if always)
        if (index < 1 || index > 50) return 0; // Invalid index
        ensureRecipientSlotsArray(); // Ensure array
        JsonArray arr = getRecipientSlots(); // Array
        if (index > (int)arr.size()) return 0; // Does not exist
        JsonObject slot = arr[index - 1]; // Slot
        return slot["freq"] | 0; // Frequency (default 0 = always)
    }

    size_t countRecipientSlotsNonEmpty(){ // Count all filled slots (includes I,+,-,D)
        ensureRecipientSlotsArray(); // Ensure array exists
        JsonArray arr = recipDoc["recipients_slots"].as<JsonArray>(); // Get array
        size_t count = 0; // Accumulator
        for(JsonObject s : arr){ // Iterate slots
            String n = s["name"] | String(""); // Name
            String em = s["emails"] | String(""); // Emails
            if(n.length() && em.length()) count++; // Count if both present
        }
        return count; // Return total
    }
    // ♻️ Removed setRecipientsArray function (legacy recipients_array structure eliminated)

    // Numeric rules management (numeric_rules) - each item: {name, keyword, op, emails}
    void setNumericRules(JsonArray arr) {
        // Replace existing array with new one
        if (recipDoc.containsKey("numeric_rules")) recipDoc.remove("numeric_rules");
        recipDoc.createNestedArray("numeric_rules");
        JsonArray na = recipDoc["numeric_rules"];
        for (JsonObject o : arr) {
            JsonObject nn = na.createNestedObject();
            if (o.containsKey("name")) nn["name"] = o["name"].as<String>();
            if (o.containsKey("keyword")) nn["keyword"] = o["keyword"].as<String>();
            if (o.containsKey("op")) nn["op"] = o["op"].as<int>();
            if (o.containsKey("emails")) nn["emails"] = o["emails"].as<String>();
        }
        markDirtyAndScheduleSave();
    }

    // Return numeric_rules as JsonArray (caller can iterate)
    JsonArray getNumericRulesJson() {
        if (!recipDoc.containsKey("numeric_rules") || !recipDoc["numeric_rules"].is<JsonArray>()) {
            recipDoc.createNestedArray("numeric_rules");
        }
        return recipDoc["numeric_rules"].as<JsonArray>();
    }
    
    // 📶 Multiple WiFi network management (with limit)
    void addWifiNetwork(const String& ssid, const String& password) {
        if (!config.containsKey("saved_networks")) { // 🤔 If the network list does not exist
            config["saved_networks"] = JsonArray(); // ➕ Cria a lista
        }
        
        JsonArray networks = config["saved_networks"]; // 📶 Get the network list
        for (JsonObject network : networks) {
            if (network["ssid"].as<String>() == ssid) {
                network["password"] = password;
                network["lastConnected"] = millis();
                network["connectionCount"] = network["connectionCount"].as<int>() + 1;
                markDirtyAndScheduleSave();
                return;
            }
        }
        JsonObject newNetwork = networks.createNestedObject();
        newNetwork["ssid"] = ssid;
        newNetwork["password"] = password;
        newNetwork["rssi"] = -100;
        newNetwork["lastConnected"] = millis();
        newNetwork["connectionCount"] = 1;
        // prune: remove least-used/oldest until it fits
        while (networks.size() > MAX_SAVED_NETWORKS) {
            int removeIndex = -1; int minConn = 0x7FFFFFFF; unsigned long oldest = 0xFFFFFFFF;
            for (size_t i = 0; i < networks.size(); ++i) {
                JsonObject n = networks[i];
                int cc = n["connectionCount"].as<int>(); unsigned long lc = n["lastConnected"].as<unsigned long>();
                if (cc < minConn || (cc == minConn && lc < oldest)) { minConn = cc; oldest = lc; removeIndex = (int)i; }
            }
            if (removeIndex >= 0) networks.remove(removeIndex); else break;
        }
        markDirtyAndScheduleSave();
    }
    
    void removeWifiNetwork(const String& ssid) { // ➖ Remove uma rede Wi-Fi
        if (!config.containsKey("saved_networks")) return; // 🤔 If the network list does not exist, do nothing
        
        JsonArray networks = config["saved_networks"]; // 📶 Get the network list
        for (int i = networks.size() - 1; i >= 0; i--) { // 🔄 Iterate networks back to front
            if (networks[i]["ssid"].as<String>() == ssid) { // ✅ Se encontrar a rede
                networks.remove(i); // 🗑️ Remove a rede
                markDirtyAndScheduleSave(); // 💾 Mark for save
                break; // 🛑 Para o loop
            }
        }
    }
    
    JsonArray getSavedNetworks() { // 📤 Get the saved network list
        if (!config.containsKey("saved_networks")) { // 🤔 If the network list does not exist
            config["saved_networks"] = JsonArray(); // ➕ Cria a lista
        }
        return config["saved_networks"]; // 📤 Retorna a lista de redes
    }
    
    void clearSavedNetworks() { // 🧹 Clear all saved networks
        config["saved_networks"] = JsonArray(); // 🗑️ Cria uma nova lista vazia
        markDirtyAndScheduleSave(); // 💾 Mark for save
    }
    
    // ♻️ Legacy scheduled_commands API removed earlier – no legacy add/remove functions kept
    
    // Export to JSON (with passwords masked for UI)
    SpiRamJsonDocument getFullConfig(bool maskPasswords = true) {
        // Merge all four split documents into a single result for callers
        size_t needed = measureJson(config) + measureJson(recipDoc) + measureJson(schedDoc) + measureJson(modbusDoc) + 512;
        if (needed < 1024) needed = 1024;
        SpiRamJsonDocument result(needed);
        // Core settings
        for (JsonPair kv : config.as<JsonObject>()) result[kv.key()] = kv.value();
        // Recipients + numeric_rules
        for (JsonPair kv : recipDoc.as<JsonObject>()) result[kv.key()] = kv.value();
        // Scheduler slots + counters
        for (JsonPair kv : schedDoc.as<JsonObject>()) result[kv.key()] = kv.value();
        // Modbus section (devices array)
        for (JsonPair kv : modbusDoc.as<JsonObject>()) result[kv.key()] = kv.value();
        if (maskPasswords) {
            if (result["wifi"]["password"].as<String>().length() > 0) result["wifi"]["password"] = "********";
            if (result["smtp"]["password"].as<String>().length() > 0) result["smtp"]["password"] = "********";
            if (result["telnet"]["password"].as<String>().length() > 0) result["telnet"]["password"] = "********";
            if (result["web"]["password"].as<String>().length() > 0) result["web"]["password"] = "********";
            if (result["access_point"]["password"].as<String>().length() > 0) result["access_point"]["password"] = "********";
            if (result["ftp"]["password"].as<String>().length() > 0) result["ftp"]["password"] = "********";
        }
        return result;
    }
    
    // 📥 Importar de JSON (usado pelos endpoints web)
    bool updateFromJson(const String& jsonStr) {
        SpiRamJsonDocument newConfig(4096);
        DeserializationError error = deserializeJson(newConfig, jsonStr);
        if (error) {
            Serial.println(F("CFG: JSON parse error"));
            Serial.println("Error: " + String(error.c_str()));
            return false;
        }
        bool updated = false;
        // 🔄 Update only fields present in the JSON
        if (newConfig.containsKey("wifi")) { // 📶 If there is a Wi-Fi section
            if (newConfig["wifi"].containsKey("ssid")) { // 📶 Se houver SSID
                config["wifi"]["ssid"] = newConfig["wifi"]["ssid"]; // 🔄 Atualiza o SSID
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["wifi"].containsKey("password") && 
                newConfig["wifi"]["password"].as<String>() != "********") { // 🔑 If a password exists and is not masked
                config["wifi"]["password"] = newConfig["wifi"]["password"]; // 🔄 Atualiza a senha
                updated = true; // 🧼 Mark as updated
            }
        }
        
        if (newConfig.containsKey("smtp")) { // 📧 If there is an SMTP section
            if (newConfig["smtp"].containsKey("server")) { // 📧 Se houver servidor
                config["smtp"]["server"] = newConfig["smtp"]["server"]; // 🔄 Atualiza o servidor
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["smtp"].containsKey("port")) { // 🚪 Se houver porta
                config["smtp"]["port"] = newConfig["smtp"]["port"]; // 🔄 Atualiza a porta
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["smtp"].containsKey("username")) { // 👤 Se houver usuário
                config["smtp"]["username"] = newConfig["smtp"]["username"]; // 🔄 Atualiza o usuário
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["smtp"].containsKey("password") && 
                newConfig["smtp"]["password"].as<String>() != "********") { // 🔑 If a password exists and is not masked
                config["smtp"]["password"] = newConfig["smtp"]["password"]; // 🔄 Atualiza a senha
                updated = true; // 🧼 Mark as updated
            }
        }
        
        if (newConfig.containsKey("email")) { // 📧 If there is an Email section
            if (newConfig["email"].containsKey("destination")) { // 🎯 Se houver destino
                config["email"]["destination"] = newConfig["email"]["destination"]; // 🔄 Atualiza o destino
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["email"].containsKey("auto_send")) { // 自動 Se houver envio automático
                config["email"]["auto_send"] = newConfig["email"]["auto_send"]; // 🔄 Atualiza o envio automático
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["email"].containsKey("max_lines")) { // 📏 If max lines present
                config["email"]["max_lines"] = newConfig["email"]["max_lines"]; // 🔄 Update the max lines
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["email"].containsKey("min_content_length")) { // 📝 If minimum length present
                config["email"]["min_content_length"] = newConfig["email"]["min_content_length"]; // 🔄 Update the minimum length
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["email"].containsKey("subject")) { // ርዕሰ Se houver assunto
                config["email"]["subject"] = newConfig["email"]["subject"]; // 🔄 Atualiza o assunto
                updated = true; // 🧼 Mark as updated
            }
            // 🧠 New: accept nested email.attach_txt field
            if (newConfig["email"].containsKey("attach_txt")) { // 🧠 Se vier attach_txt aninhado
                config["email"]["attach_txt"] = newConfig["email"]["attach_txt"]; // 🧠 Atualiza flag attach
                updated = true; // 🧼 Mark update
            }
            // 🧠 New: accept nested email.include_timestamp field
            if (newConfig["email"].containsKey("include_timestamp")) { // 🧠 Se vier include_timestamp aninhado
                config["email"]["include_timestamp"] = newConfig["email"]["include_timestamp"]; // 🧠 Atualiza flag timestamp
                updated = true; // 🧼 Mark update
            }
            // Support flattened boolean key for backwards compatibility
            if (newConfig.containsKey("email_dont_copy_sender")) {
                config["email"]["dont_copy_sender"] = newConfig["email_dont_copy_sender"].as<bool>();
                updated = true;
            }
            // 🧠 New: support flattened email_attach_txt key (UI default)
            if (newConfig.containsKey("email_attach_txt")) { // 🧠 Se vier forma achatada
                config["email"]["attach_txt"] = newConfig["email_attach_txt"].as<bool>(); // 🧠 Atualiza flag attach
                updated = true; // 🧼 Mark update
            }
            // 🧠 New: support flattened email_include_timestamp key (UI default)
            if (newConfig.containsKey("email_include_timestamp")) { // 🧠 Se vier forma achatada
                config["email"]["include_timestamp"] = newConfig["email_include_timestamp"].as<bool>(); // 🧠 Atualiza flag timestamp
                updated = true; // 🧼 Mark update
            }
        }
        
        if (newConfig.containsKey("rs232")) { // 🔌 If there is an RS232 section
            if (newConfig["rs232"].containsKey("enabled")) { // 🔌 Se houver habilitado
                config["rs232"]["enabled"] = newConfig["rs232"]["enabled"]; // 🔄 Atualiza o status
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["rs232"].containsKey("baudrate_index")) { // 🔢 Se houver baudrate
                config["rs232"]["baudrate_index"] = newConfig["rs232"]["baudrate_index"]; // 🔄 Atualiza o baudrate
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["rs232"].containsKey("invert_pins")) { // 🔄 If pin inversion present
                config["rs232"]["invert_pins"] = newConfig["rs232"]["invert_pins"]; // 🔄 Update pin inversion
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["rs232"].containsKey("data_bits")) { config["rs232"]["data_bits"] = newConfig["rs232"]["data_bits"]; updated = true; }
            if (newConfig["rs232"].containsKey("parity")) { config["rs232"]["parity"] = newConfig["rs232"]["parity"]; updated = true; }
            if (newConfig["rs232"].containsKey("stop_bits")) { config["rs232"]["stop_bits"] = newConfig["rs232"]["stop_bits"]; updated = true; }
            // incoming flow_control ignored - RTS/CTS unsupported
            if (newConfig["rs232"].containsKey("read_timeout_ms")) { config["rs232"]["read_timeout_ms"] = newConfig["rs232"]["read_timeout_ms"]; updated = true; }
        }

        // Import templates array if provided: accept array of template objects
        if (newConfig.containsKey("rs232") && newConfig["rs232"].containsKey("templates") && newConfig["rs232"]["templates"].is<JsonArray>()) {
            JsonArray incoming = newConfig["rs232"]["templates"].as<JsonArray>();
            for (JsonObject t : incoming) {
                String name = t["name"] | String("");
                if (name.length() == 0) continue;
                int baud_idx = t["baudrate_index"] | (config["rs232"]["baudrate_index"].as<int>());
                int baud_numeric = -1;
                if (t.containsKey("baudrate")) baud_numeric = t["baudrate"].as<int>();
                int data_bits = t["data_bits"] | 8;
                String parity = t["parity"] | String("none");
                int stop_bits = t["stop_bits"] | 1;
                bool invert_pins = t["invert_pins"] | false;
                // incoming flow_control ignored - RTS/CTS removed
                int read_timeout = t["read_timeout_ms"] | 1000;
                bool enabled = t["enabled"] | true;
                bool use_xon = t["use_xon_xoff"] | false;
                // Add or update template in memory; do not force immediate NVS write
                addOrUpdateRs232Template(name, baud_idx, data_bits, parity, stop_bits, invert_pins, read_timeout, enabled, baud_numeric, use_xon);
                // mark templates dirty for deferred persistence
                templatesDirty = true;
                lastTemplateChangeMillis = millis();
                isDirty = true;
                updated = true;
            }
        }
        
        if (newConfig.containsKey("printer")) { // 🖨️ If there is a Printer section
            if (newConfig["printer"].containsKey("emulation_mode")) { // 🖨️ If emulation mode present
                config["printer"]["emulation_mode"] = newConfig["printer"]["emulation_mode"]; // 🔄 Update emulation mode
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["printer"].containsKey("charset_mode")) { // 🔤 Se houver modo de charset
                config["printer"]["charset_mode"] = newConfig["printer"]["charset_mode"]; // 🔄 Atualiza o modo de charset
                updated = true; // 🧼 Mark as updated
            }
        }
        
        if (newConfig.containsKey("telnet")) { // 💻 If there is a Telnet section
            if (newConfig["telnet"].containsKey("username")) { // 👤 Se houver usuário
                config["telnet"]["username"] = newConfig["telnet"]["username"]; // 🔄 Atualiza o usuário
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["telnet"].containsKey("password") && 
                newConfig["telnet"]["password"].as<String>() != "********") { // 🔑 If a password exists and is not masked
                config["telnet"]["password"] = newConfig["telnet"]["password"]; // 🔄 Atualiza a senha
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["telnet"].containsKey("port")) { // 🚪 Se houver porta
                config["telnet"]["port"] = newConfig["telnet"]["port"]; // 🔄 Atualiza a porta
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["telnet"].containsKey("enabled")) { // ✅ Se houver flag de enable/disable
                config["telnet"]["enabled"] = newConfig["telnet"]["enabled"]; // 🔄 Atualiza flag
                updated = true;
            }
        }
        
        if (newConfig.containsKey("timeouts")) { // ⏱️ If there is a Timeouts section
            if (newConfig["timeouts"].containsKey("auto_email_enabled")) { // ⏱️ Se houver timeout de e-mail (habilitado)
                config["timeouts"]["auto_email_enabled"] = newConfig["timeouts"]["auto_email_enabled"]; // 🔄 Atualiza o timeout
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["timeouts"].containsKey("auto_email_disabled")) { // ⏱️ Se houver timeout de e-mail (desabilitado)
                config["timeouts"]["auto_email_disabled"] = newConfig["timeouts"]["auto_email_disabled"]; // 🔄 Atualiza o timeout
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["timeouts"].containsKey("telnet_inactivity")) { // ⏱️ Se houver timeout de inatividade Telnet
                config["timeouts"]["telnet_inactivity"] = newConfig["timeouts"]["telnet_inactivity"]; // 🔄 Atualiza o timeout
                updated = true; // 🧼 Mark as updated
            }
        }
        
        if (newConfig.containsKey("ntp")) { // 🕒 If there is an NTP section
            if (newConfig["ntp"].containsKey("server")) { // 🕒 Se houver servidor
                config["ntp"]["server"] = newConfig["ntp"]["server"]; // 🔄 Atualiza o servidor
                updated = true; // 🧼 Mark as updated
            }
            if (newConfig["ntp"].containsKey("timezone")) { // 🌍 Se houver fuso horário
                config["ntp"]["timezone"] = newConfig["ntp"]["timezone"]; // 🔄 Atualiza o fuso horário
                updated = true; // 🧼 Mark as updated
            }
        }
        
        if (newConfig.containsKey("debug")) { // 🐛 If there is a Debug section
            if (newConfig["debug"].containsKey("logs_enabled")) { // 🐛 If log enable present
                config["debug"]["logs_enabled"] = newConfig["debug"]["logs_enabled"]; // 🔄 Update log enable
                updated = true; // 🧼 Mark as updated
            }
        }
        
        if (newConfig.containsKey("recipients")) { // 👥 If there is a Recipients section
            config["recipients"] = newConfig["recipients"]; // 🔄 Atualiza os destinatários
            updated = true; // 🧼 Mark as updated
        }
        
        // ♻️ Ignorado campo scheduled_commands legacy se presente no JSON importado
        
        if (newConfig.containsKey("access_point")) {
            JsonObject apNew = newConfig["access_point"]; JsonObject ap = config["access_point"]; if (!ap.isNull()) {
                for (JsonPair kv : apNew) { ap[kv.key().c_str()] = kv.value(); updated = true; }
            }
        }
        if (newConfig.containsKey("wifi_hybrid")) {
            JsonObject whNew = newConfig["wifi_hybrid"]; JsonObject wh = config["wifi_hybrid"]; if (!wh.isNull()) {
                for (JsonPair kv : whNew) { wh[kv.key().c_str()] = kv.value(); updated = true; }
            }
        }
        if (updated) {
            markDirtyAndScheduleSave();
            Serial.println(F("CFG: config atualizada"));
        }
        return updated;
    }
    
    // 🔄 Update multiple fields at once
    bool updateMultipleFields(const std::map<String, String>& updates) {
        bool updated = false; // 🧼 Flag to indicate whether an update happened
        
        for (auto const& pair : updates) { // 🔄 Iterate over update pairs
            String key = pair.first; // 🔑 Get the key
            String value = pair.second; // 📜 Get the value
            
            // 🔪 Processar chaves no formato "secao.campo"
            int dotIndex = key.indexOf('.'); // 🔎 Encontra o ponto
            if (dotIndex > 0) { // 🤔 Se for uma chave composta
                String section = key.substring(0, dotIndex); // 🔪 Extract the section
                String field = key.substring(dotIndex + 1); // 🔪 Extrai o campo
                
                if (!config.containsKey(section)) { // 🤔 If the section does not exist
                    config[section] = JsonObject(); // ➕ Create the section
                }
                
                // 🔄 Convert values as needed
                if (value == "true" || value == "false") { // 🤔 Se for um booleano
                    config[section][field] = (value == "true"); // 🔄 Convert to boolean
                } else if (value.toInt() != 0 || value == "0") { // 🤔 If it is a number
                    config[section][field] = value.toInt(); // 🔄 Convert to integer
                } else { // 🤔 Se for uma string
                    config[section][field] = value; // 🔄 Assign as string
                }
                
                updated = true; // 🧼 Mark as updated
            }
        }
        
        if (updated) { // ✅ Se algo foi atualizado
            markDirtyAndScheduleSave(); // 💾 Mark for save
        }
        
        return updated; // 👍 Return whether an update happened
    }
    
    // 🤔 Check whether configuration is valid
    bool isValidConfig() {
        // ✅ Basic checks
        if (getWifiSSID().length() == 0) return false; // 📶 Check whether the Wi-Fi SSID is set
        if (getSmtpServer().length() == 0) return false; // 📧 Check whether the SMTP server is set
        if (getEmailDestination().length() == 0) return false; // 🎯 Check whether the destination email is set
        
        return true; // 👍 Return true if the configuration is valid
    }
    
    // 📊 Get configuration statistics
    void printStats() {
    Serial.println(F("CFG Stats:")); // Statistics header
    Serial.print(F("Config: ")); Serial.println(String(measureJson(config)));
    Serial.print(F("WiFi: ")); Serial.println(getWifiSSID());
    Serial.print(F("SMTP: ")); Serial.println(getSmtpServer());
    Serial.print(F("Email: ")); Serial.println(getEmailDestination());
    Serial.print(F("Destinatarios: ")); Serial.println(String(countRecipientSlotsNonEmpty()));
    // 🧠 Detailed scheduling statistics
    int schedCmdActive = getSchedCmdActive(); // 🧮 Active interval
    int schedDtActive = getSchedDtActive(); // 🧮 Active datetime
    Serial.print(F("SchedInt ativos: ")); Serial.println(String(schedCmdActive));
    Serial.print(F("SchedDT ativos: ")); Serial.println(String(schedDtActive));
    Serial.print(F("Dirty: ")); Serial.println(isDirty ? "Yes" : "No");
    Serial.println(F("--")); // End of statistics
    }
};

#endif
