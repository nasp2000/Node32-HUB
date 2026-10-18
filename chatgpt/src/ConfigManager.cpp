#include "ConfigManager.h"
#include "nvs_write_guard.h"
using namespace ConfigValidator;

// LogLevel::INFO = 0 (from commands.h LogLevel enum)
static constexpr int kLogInfo = 0;

// Only these keys are allowed inside config["web"] (the access page). Anything
// else (e.g. junk/bloat copied by an old save) is ignored on load and on save,
// so the access.json file can never grow beyond a few hundred bytes.
static bool isKnownWebAccessKey(const char* k) {
    static const char* const keys[] = {
        "enabled", "username", "password", "auth_enabled", "single_client_only",
        "max_sessions", "idle_timeout_min", "abs_timeout_min",
        "viewer_enabled", "viewer_username", "viewer_password"
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (strcmp(k, keys[i]) == 0) return true;
    }
    return false;
}

// Strips every byte < 0x20 except \t \n \r (the only control codes ArduinoJson
// serializes as valid JSON escapes). Any OTHER raw control byte makes the
// re-serialized file unparseable on the next boot — the source of the recurring
// alerts.json corruption. Mirror of stripControlChars() in web_shared_logic.cpp,
// kept here so the config layer does not depend on the web module.
static String cleanControlCharsString(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];
        if ((uint8_t)c >= 0x20 || c == '\n' || c == '\r' || c == '\t') out += c;
    }
    return out;
}

// Recursively copy a JSON subtree into dst, stripping control bytes from every
// string value. Called on free-text sections (inactivity_alerts) right before
// serialize so the file on disk can never be made invalid by a pasted email.
//
// v1.093 fix: the previous implementation passed the child destination as a
// JsonVariant BY VALUE (copyJsonCleanControlChars(kv.value(), out[kv.key()])).
// With the ArduinoJson version in this environment, writes through a copied
// JsonVariant are silently dropped depending on which overload the compiler
// picks (the JsonArray overload could not even be selected until it was
// forward-declared below). The recursion now writes only through
// JsonObject/JsonArray handles created with createNestedObject/().
static void copyJsonCleanControlChars(JsonVariant src, JsonArray dstArr);

static void copyJsonCleanControlChars(JsonVariant src, JsonObject dstRoot) {
    JsonObject srcObj = src.as<JsonObject>();
    for (JsonPair kv : srcObj) {
        JsonVariant val = kv.value();
        if (val.is<JsonObject>()) {
            JsonObject child = dstRoot.createNestedObject(kv.key());
            copyJsonCleanControlChars(val, child);
        } else if (val.is<JsonArray>()) {
            JsonArray child = dstRoot.createNestedArray(kv.key());
            copyJsonCleanControlChars(val, child);
        } else if (val.is<const char*>()) {
            dstRoot[kv.key()].set(cleanControlCharsString(val.as<String>()));
        } else {
            dstRoot[kv.key()].set(val);
        }
    }
}

static void copyJsonCleanControlChars(JsonVariant src, JsonArray dstArr) {
    JsonArray srcArr = src.as<JsonArray>();
    for (JsonVariant item : srcArr) {
        if (item.is<JsonObject>()) {
            JsonObject o = dstArr.createNestedObject();
            copyJsonCleanControlChars(item, o);
        } else if (item.is<JsonArray>()) {
            JsonArray a = dstArr.createNestedArray();
            copyJsonCleanControlChars(item, a);
        } else if (item.is<const char*>()) {
            dstArr.add(cleanControlCharsString(item.as<String>()));
        } else {
            dstArr.add(item);
        }
    }
}

void ConfigManager::initDefaults() {
    config.clear();
    config["wifi"]["ssid"] = "";
    config["wifi"]["password"] = "";
    config["smtp"]["server"] = "smtp.gmail.com";
    config["smtp"]["port"] = 465;
    config["smtp"]["username"] = "";
    config["smtp"]["password"] = "";
    config["smtp"]["tls_insecure"] = true;
    config["email"]["destination"] = "";
    config["email"]["auto_send"] = true;
    config["email"]["max_lines"] = 66;
    config["email"]["min_content_length"] = 3;
config["email"]["subject"] = PACK_EMAIL_SUBJECT;
    config["email"]["dont_copy_sender"] = false;
    config["email"]["attach_txt"] = true;
    config["email"]["include_timestamp"] = true;
    config["rs232"]["enabled"] = true;
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)
    config["rs232"]["baudrate_index"] = 3;
#else
    config["rs232"]["baudrate_index"] = 4;
#endif
    config["rs232"]["invert_pins"] = false;
    config["rs232"]["data_bits"] = 8;
    config["rs232"]["parity"] = "none";
    config["rs232"]["stop_bits"] = 1;
    config["rs232"]["read_timeout_ms"] = 1000;
    config["rs232"]["use_xon_xoff"] = false;
    config["rs232"]["mirror_to_usb"] = true;
    if (!config["rs232"].containsKey("templates") || !config["rs232"]["templates"].is<JsonArray>()) {
        JsonArray tarr = config["rs232"].createNestedArray("templates");
        JsonObject def = tarr.createNestedObject();
        def["name"] = "Default";
        def["built_in"] = true;
    #if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)
        def["baudrate_index"] = 3;
        def["baudrate"] = 9600;
    #else
        def["baudrate_index"] = 4;
        def["baudrate"] = 19200;
    #endif
        def["data_bits"] = 8;
        def["parity"] = "none";
        def["stop_bits"] = 1;
        def["invert_pins"] = false;
        def["read_timeout_ms"] = 1000;
        def["enabled"] = true;
    }
    if (!config["rs232"].containsKey("current_template") || String(config["rs232"]["current_template"].as<const char*>()).length() == 0) {
        config["rs232"]["current_template"] = String("Default");
    }
    config["printer"]["emulation_mode"] = 0;
    config["printer"]["charset_mode"] = 0;
    config["telnet"]["username"] = "root";
    config["telnet"]["password"] = "root";
    config["telnet"]["port"] = 23;
    config["telnet"]["enabled"] = false;
    _cache_telnet_enabled = false;
    config["wifi"]["runtime_disabled"] = false;
    _cache_wifi_disabled = false;
    config["boot_log"]["enabled"] = false;
    _cache_boot_log_enabled = false;
    config["web"]["username"] = "";
    config["web"]["password"] = "";
    config["web"]["auth_enabled"] = false;
    _cache_web_auth_enabled = false;
    config["web"]["single_client_only"] = false;
    config["web"]["max_sessions"] = 3;
    config["web"]["idle_timeout_min"] = 10;
    config["web"]["abs_timeout_min"] = 20;
    config["web"]["viewer_enabled"] = false;
    config["web"]["viewer_username"] = "viewer";
    config["web"]["viewer_password"] = "";
    config["timeouts"]["auto_email_enabled"] = 10000;
    config["timeouts"]["auto_email_disabled"] = 300000;
    config["timeouts"]["telnet_inactivity"] = 60000;
    config["debug"]["logs_enabled"] = false;
#include "feature_flags.h"
#if ENABLE_GRBL == 1
    config["grbl"]["log_enabled"] = false;
#endif
    config["recipients"] = JsonObject();
    config["saved_networks"] = JsonArray();
    config["access_point"]["ssid"] = "NODE32-HUB";
    config["access_point"]["password"] = "12345678";
    config["access_point"]["retry_delay"] = 10000;
    config["access_point"]["max_failures"] = 3;
    config["access_point"]["failure_pause"] = 60000;
    config["access_point"]["activation_delay"] = 5000;
    config["wifi_hybrid"]["boot_timeout"] = 30000;
    config["wifi_hybrid"]["sta_stable_time"] = 0;
    config["wifi_hybrid"]["sta_recovery_timeout"] = 10000;
    config["wifi_hybrid"]["enabled"] = true;
    config["structures"]["amada_isq20_mtf1"]["enabled"] = false;
    config["structures"]["amada_isq20_awse"]["enabled"] = false;
    config["structures"]["auto_detect"] = true;
    config["structures"]["amada_header_every"] = 20;
    config["sdcard"]["enabled"] = true;
    _cache_sdcard_enabled = true;
    config["sdcard"]["flush_interval"] = "30m";
    _cache_sdcard_flush_interval = "30m";
    config["sdcard"]["format"] = "csv";
    config["sdcard"]["structured"] = true;
    config["sdcard"]["email_send"] = true;
    config["sdcard"]["email_from_sd"] = true;
    config["sdcard"]["emailday_extra_emails"] = "";
    config["sdcard"]["sensorday_extra_emails"] = "";
    config["sdcard"]["logday_extra_emails"] = "";
    config["sdcard"]["auto_cleanup"] = true;
    _cache_sd_auto_cleanup = true;
    config["sdcard"]["cleanup_threshold"] = 70;
    _cache_sd_cleanup_threshold = 70;
    config["sdcard"]["keep_days"] = 0;
    _cache_sd_keep_days = 0;
    config["ethernet"]["mode"] = "dhcp";
    config["ethernet"]["ip"] = "";
    config["ethernet"]["gw"] = "";
    config["ethernet"]["dns"] = "";
    config["ethernet"]["mac_override"] = "";
    config["do"]["invert"] = 0;
    {
        JsonArray doNames = config["do"].createNestedArray("names");
        for (int i = 0; i < 8; i++) doNames.add("");
    }
    config["usb_server"]["enabled"] = true;
    config["usb_server"]["port"] = 3240;
    config["usb_server"]["recv_timeout_ms"] = 1200;
    config["usb_server"]["in_wait_timeout_ms"] = 220;
    config["ftp"]["enabled"] = false;
    config["ftp"]["host"] = "";
    config["ftp"]["port"] = 21;
    config["ftp"]["user"] = "";
    config["ftp"]["password"] = "";
    config["ftp"]["anonymous"] = false;
    config["ftp"]["remote_path"] = "/";
    config["ftp"]["local_dir"] = "/logs";
    config["ftp"]["auto_upload"] = false;
    config["ftp"]["interval_min"] = 720;
    config["ftp"]["only_new"] = true;
    config["ftp"]["delete_after"] = false;
    config["ftp"]["max_files"] = 10;
    config["ftp"]["filter"] = "*.csv;*.log;*.txt;*.bin;*.wav;*.gcode;*.nc;*.avi;*.jpg;*.mp3;*.flac";
    config["system"]["heartbeat_enabled"] = false;
    config["system"]["heartbeat_interval"] = 10000;
    config["system"]["charts_page_enabled"] = true;
    config["system"]["charts_exempt_sessions"] = true;
    config["system"]["serial_monitor_enabled"] = false;
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
    config["system_name"] = "Node32-8DI8DO";
#else
    config["system_name"] = PACK_SYSTEM_NAME;
#endif
    config["system_language"] = "en";
    // Modbus defaults — all disabled until user enables via web or user_config.h
#if ENABLE_MODBUS==1
    modbusDoc["modbus"]["enabled"]      = false;
    modbusDoc["modbus"]["rtu_enabled"]  = false;
    modbusDoc["modbus"]["tcp_enabled"]  = false;
    modbusDoc["modbus"]["slave_enabled"]= false;
    modbusDoc["modbus"]["rtu_baud"]     = 9600;
    modbusDoc["modbus"]["rtu_data_bits"]= 8;
    modbusDoc["modbus"]["rtu_parity"]   = "N";
    modbusDoc["modbus"]["rtu_stop_bits"]= 1;
    modbusDoc["modbus"]["rtu_de_pin"]   = -1; // -1 = auto-direction TTL module
    modbusDoc["modbus"]["slave_id"]     = 1;
    modbusDoc["modbus"]["devices"].to<JsonArray>(); // empty array; populated via web UI
#endif // ENABLE_MODBUS==1

}

void ConfigManager::ensureAllFieldsExist() {
    if (!config.containsKey("wifi")) config["wifi"] = JsonObject();
    if (!config["wifi"].containsKey("ssid")) config["wifi"]["ssid"] = "";
    if (!config["wifi"].containsKey("password")) config["wifi"]["password"] = "";

    if (!config.containsKey("smtp")) config["smtp"] = JsonObject();
    if (!config["smtp"].containsKey("server")) config["smtp"]["server"] = "smtp.gmail.com";
    if (!config["smtp"].containsKey("port")) config["smtp"]["port"] = 465;
    if (!config["smtp"].containsKey("username")) config["smtp"]["username"] = "";
    if (!config["smtp"].containsKey("password")) config["smtp"]["password"] = "";
    if (!config["smtp"].containsKey("tls_insecure")) config["smtp"]["tls_insecure"] = true;
    if (!config["smtp"].containsKey("google_script_url")) config["smtp"]["google_script_url"] = "";

    if (!config.containsKey("email")) config["email"] = JsonObject();
    if (!config["email"].containsKey("destination")) config["email"]["destination"] = "";
    if (!config["email"].containsKey("auto_send")) config["email"]["auto_send"] = true;
    if (!config["email"].containsKey("max_lines")) config["email"]["max_lines"] = 66;
    if (!config["email"].containsKey("min_content_length")) config["email"]["min_content_length"] = 3;
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_P4_CNC)
    if (!config["email"].containsKey("subject")) config["email"]["subject"] = "GcodeSender-P4";
#elif defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    if (!config["email"].containsKey("subject")) config["email"]["subject"] = "GcodeSender-S3";
#else
    if (!config["email"].containsKey("subject")) config["email"]["subject"] = "Node32-Hub";
#endif
    if (!config["email"].containsKey("dont_copy_sender")) config["email"]["dont_copy_sender"] = false;
    if (!config["email"].containsKey("attach_txt")) {
        if (config.containsKey("sdcard") && config["sdcard"].containsKey("email_attach_txt")) config["email"]["attach_txt"] = config["sdcard"]["email_attach_txt"].as<bool>();
        else config["email"]["attach_txt"] = true;
    }
    if (!config["email"].containsKey("include_timestamp")) {
        if (config.containsKey("sdcard") && config["sdcard"].containsKey("email_include_timestamp")) config["email"]["include_timestamp"] = config["sdcard"]["email_include_timestamp"].as<bool>();
        else config["email"]["include_timestamp"] = true;
    }
    if (!config["email"].containsKey("method")) config["email"]["method"] = "smtp";

    if (!config.containsKey("ftp")) config["ftp"] = JsonObject();
    if (!config["ftp"].containsKey("enabled")) config["ftp"]["enabled"] = false;
    if (!config["ftp"].containsKey("host")) config["ftp"]["host"] = "";
    if (!config["ftp"].containsKey("port")) config["ftp"]["port"] = 21;
    if (!config["ftp"].containsKey("user")) config["ftp"]["user"] = "";
    if (!config["ftp"].containsKey("password")) config["ftp"]["password"] = "";
    if (!config["ftp"].containsKey("anonymous")) config["ftp"]["anonymous"] = false;
    if (!config["ftp"].containsKey("remote_path")) config["ftp"]["remote_path"] = "/";
    if (!config["ftp"].containsKey("local_dir")) config["ftp"]["local_dir"] = "/logs";
    else {
        // Legacy log root /LOGs was renamed to /logs (see log.cpp boot migration).
        // A persisted local_dir pointing at the old root (any case, with or without
        // the "SD:"/"PSRAM:"/"LFS:" prefix) would make FTP scan a folder that no
        // longer exists -> zero files uploaded. Normalize it once, here on load.
        String ld = config["ftp"]["local_dir"].as<String>();
        String prefix, pathPart = ld;
        int colon = ld.indexOf(':');
        if (colon > 0) { prefix = ld.substring(0, colon + 1); pathPart = ld.substring(colon + 1); }
        String cmp = pathPart;
        if (cmp.length() > 1 && cmp.endsWith("/")) cmp = cmp.substring(0, cmp.length() - 1);
        cmp.toLowerCase();
        if (cmp == "/logs" && pathPart != "/logs") {
            config["ftp"]["local_dir"] = prefix + "/logs";
            String msg = String(F("📁 FTP: local_dir migrated ")) + ld + String(F(" -> ")) + prefix + String(F("/logs"));
            sendVirtualMessage(msg, kLogInfo);
        }
    }
    if (!config["ftp"].containsKey("auto_upload")) config["ftp"]["auto_upload"] = false;
    if (!config["ftp"].containsKey("interval_min")) config["ftp"]["interval_min"] = 720;
    if (!config["ftp"].containsKey("only_new")) config["ftp"]["only_new"] = true;
    if (!config["ftp"].containsKey("delete_after")) config["ftp"]["delete_after"] = false;
    if (!config["ftp"].containsKey("max_files")) config["ftp"]["max_files"] = 10;
    if (!config["ftp"].containsKey("filter")) config["ftp"]["filter"] = "*.csv;*.log;*.txt;*.bin;*.wav;*.gcode;*.nc;*.avi;*.jpg;*.mp3;*.flac";

    if (!config.containsKey("rs232")) config["rs232"] = JsonObject();
    if (!config["rs232"].containsKey("enabled")) config["rs232"]["enabled"] = true;
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_RAMBOX)
    if (!config["rs232"].containsKey("baudrate_index")) config["rs232"]["baudrate_index"] = 3;
#else
    if (!config["rs232"].containsKey("baudrate_index")) config["rs232"]["baudrate_index"] = 4;
#endif
    if (!config["rs232"].containsKey("invert_pins")) config["rs232"]["invert_pins"] = false;
    if (!config["rs232"].containsKey("data_bits")) config["rs232"]["data_bits"] = 8;
    if (!config["rs232"].containsKey("parity")) config["rs232"]["parity"] = "none";
    if (!config["rs232"].containsKey("stop_bits")) config["rs232"]["stop_bits"] = 1;
    if (!config["rs232"].containsKey("read_timeout_ms")) config["rs232"]["read_timeout_ms"] = 1000;
    if (!config["rs232"].containsKey("mirror_to_usb")) config["rs232"]["mirror_to_usb"] = true;

    if (!config.containsKey("printer")) config["printer"] = JsonObject();
    if (!config["printer"].containsKey("emulation_mode")) config["printer"]["emulation_mode"] = 0;
    if (!config["printer"].containsKey("charset_mode")) config["printer"]["charset_mode"] = 0;

    if (!config.containsKey("telnet")) config["telnet"] = JsonObject();
    if (!config["telnet"].containsKey("username")) config["telnet"]["username"] = "root";
    if (!config["telnet"].containsKey("password")) config["telnet"]["password"] = "root";
    if (!config["telnet"].containsKey("port")) config["telnet"]["port"] = 23;
    if (!config["telnet"].containsKey("enabled")) config["telnet"]["enabled"] = false;
    if (!config.containsKey("wifi")) config["wifi"] = JsonObject();
    if (!config["wifi"].containsKey("runtime_disabled")) config["wifi"]["runtime_disabled"] = false;
    if (!config.containsKey("boot_log")) config["boot_log"] = JsonObject();
    if (!config["boot_log"].containsKey("enabled")) config["boot_log"]["enabled"] = false;
    if (!config.containsKey("web")) config["web"] = JsonObject();
    if (!config["web"].containsKey("enabled")) config["web"]["enabled"] = true;
    if (!config.containsKey("web")) config["web"] = JsonObject();
    if (!config["web"].containsKey("username")) config["web"]["username"] = "";
    if (!config["web"].containsKey("password")) config["web"]["password"] = "";
    if (!config["web"].containsKey("auth_enabled")) config["web"]["auth_enabled"] = false;
    if (!config["web"].containsKey("single_client_only")) config["web"]["single_client_only"] = false;
    if (!config["web"].containsKey("max_sessions")) config["web"]["max_sessions"] = 3;
    if (!config["web"].containsKey("idle_timeout_min")) config["web"]["idle_timeout_min"] = 10;
    if (!config["web"].containsKey("abs_timeout_min")) config["web"]["abs_timeout_min"] = 20;
    if (!config["web"].containsKey("viewer_enabled")) config["web"]["viewer_enabled"] = false;
    if (!config["web"].containsKey("viewer_username")) config["web"]["viewer_username"] = "viewer";
    if (!config["web"].containsKey("viewer_password")) config["web"]["viewer_password"] = "";

    if (!config.containsKey("timeouts")) config["timeouts"] = JsonObject();
    if (!config["timeouts"].containsKey("auto_email_enabled")) config["timeouts"]["auto_email_enabled"] = 10000;
    if (!config["timeouts"].containsKey("auto_email_disabled")) config["timeouts"]["auto_email_disabled"] = 300000;
    if (!config["timeouts"].containsKey("telnet_inactivity")) config["timeouts"]["telnet_inactivity"] = 60000;

    if (!config.containsKey("daily_reboot") || !config["daily_reboot"].is<JsonObject>()) {
        config["daily_reboot"].to<JsonObject>();
    }
    for (int i = 1; i <= 2; ++i) {
        String key = String("s") + i;
        if (!config["daily_reboot"].containsKey(key)) {
            JsonObject o = config["daily_reboot"].createNestedObject(key);
            o["time"] = (i == 1) ? "00:00" : "";
            o["enabled"] = (i == 1);
            o["last_run_day"] = 0;
        } else {
            JsonObject o = config["daily_reboot"][key];
            if (!o.containsKey("time")) o["time"] = (i == 1) ? "00:00" : "";
            if (!o.containsKey("enabled")) o["enabled"] = (i == 1);
            if (!o.containsKey("last_run_day")) o["last_run_day"] = 0;
        }
    }

    if (!config.containsKey("debug")) config["debug"] = JsonObject();
    if (!config["debug"].containsKey("logs_enabled")) config["debug"]["logs_enabled"] = false;

#if ENABLE_GRBL == 1
    if (!config.containsKey("grbl")) config["grbl"] = JsonObject();
    if (!config["grbl"].containsKey("log_enabled")) config["grbl"]["log_enabled"] = false;
#endif

    if (!config.containsKey("recipients")) config["recipients"] = JsonObject();
    if (recipDoc.containsKey("recipients_slots") && recipDoc["recipients_slots"].is<JsonArray>()) {
        JsonArray _rslots = recipDoc["recipients_slots"].as<JsonArray>();
        for (JsonObject _rs : _rslots) {
            String _rsName = _rs["name"] | String("");
            String _rsEmails = _rs["emails"] | String("");
            if (_rsName.length() > 0 && _rsEmails.length() > 0 && !_rs.containsKey("freq")) {
                _rs["freq"] = 600;
            }
        }
    }

#if ENABLE_SCHEDULER==1
    if (!schedDoc.containsKey("sched_dt")) schedDoc["sched_dt"] = JsonObject();
    if (!schedDoc.containsKey("sched_dt_active")) schedDoc["sched_dt_active"] = 1;
    for (int i = 1; i <= 8; ++i) {
        char key[5]; snprintf(key, sizeof(key), "s%d", i);
        if (!schedDoc["sched_dt"].containsKey(key)) {
            JsonObject so = schedDoc["sched_dt"].createNestedObject(key);
            so[FPSTR(K_COMMAND)] = "";
            so[FPSTR(K_DATE)] = "";
            so[FPSTR(K_TIME)] = "";
            so[FPSTR(K_TEMPLATE)] = "";
            so[FPSTR(K_ENABLED)] = false;
        } else {
            JsonObject so = schedDoc["sched_dt"][key];
            if (!so.containsKey(FPSTR(K_COMMAND))) so[FPSTR(K_COMMAND)] = "";
            if (!so.containsKey(FPSTR(K_DATE))) so[FPSTR(K_DATE)] = "";
            if (!so.containsKey(FPSTR(K_TIME))) so[FPSTR(K_TIME)] = "";
            if (!so.containsKey(FPSTR(K_TEMPLATE))) so[FPSTR(K_TEMPLATE)] = "";
            if (!so.containsKey(FPSTR(K_ENABLED))) so[FPSTR(K_ENABLED)] = false;
        }
    }

    if (!schedDoc.containsKey("sched_cmd")) schedDoc["sched_cmd"] = JsonObject();
    if (!schedDoc.containsKey("sched_cmd_active")) schedDoc["sched_cmd_active"] = 1;
    for (int i = 1; i <= 10; ++i) {
        char key[5]; snprintf(key, sizeof(key), "s%d", i);
        if (!schedDoc["sched_cmd"].containsKey(key)) {
            JsonObject so = schedDoc["sched_cmd"].createNestedObject(key);
            so[FPSTR(K_COMMAND)] = "";
            so[FPSTR(K_INTERVAL)] = 0;
            so[FPSTR(K_UNIT)] = "m";
            so[FPSTR(K_DAYS)] = "";
            so[FPSTR(K_TEMPLATE)] = "";
            so[FPSTR(K_ENABLED)] = false;
        } else {
            JsonObject so = schedDoc["sched_cmd"][key];
            if (!so.containsKey(FPSTR(K_COMMAND))) so[FPSTR(K_COMMAND)] = "";
            if (!so.containsKey(FPSTR(K_INTERVAL))) so[FPSTR(K_INTERVAL)] = 0;
            if (!so.containsKey(FPSTR(K_UNIT))) so[FPSTR(K_UNIT)] = "m";
            if (!so.containsKey(FPSTR(K_DAYS))) so[FPSTR(K_DAYS)] = "";
            if (!so.containsKey(FPSTR(K_TEMPLATE))) so[FPSTR(K_TEMPLATE)] = "";
            if (!so.containsKey(FPSTR(K_ENABLED))) so[FPSTR(K_ENABLED)] = false;
        }
    }
#endif // ENABLE_SCHEDULER==1
    if (!config.containsKey("saved_networks")) config["saved_networks"] = JsonArray();
    if (!config.containsKey("access_point")) config["access_point"] = JsonObject();
    if (!config["access_point"].containsKey("ssid")) config["access_point"]["ssid"] = "NODE32-HUB";
    if (!config["access_point"].containsKey("password")) config["access_point"]["password"] = "12345678";
    if (!config["access_point"].containsKey("retry_delay")) config["access_point"]["retry_delay"] = 10000;
    if (!config["access_point"].containsKey("max_failures")) config["access_point"]["max_failures"] = 3;
    if (!config["access_point"].containsKey("failure_pause")) config["access_point"]["failure_pause"] = 60000;
    if (!config["access_point"].containsKey("activation_delay")) config["access_point"]["activation_delay"] = 5000;
    if (!config.containsKey("wifi_hybrid")) config["wifi_hybrid"] = JsonObject();
    if (!config["wifi_hybrid"].containsKey("boot_timeout")) config["wifi_hybrid"]["boot_timeout"] = 30000;
    if (!config["wifi_hybrid"].containsKey("sta_stable_time")) config["wifi_hybrid"]["sta_stable_time"] = 5000;
    if (!config["wifi_hybrid"].containsKey("sta_recovery_timeout")) config["wifi_hybrid"]["sta_recovery_timeout"] = 10000;
    if (!config["wifi_hybrid"].containsKey("enabled")) config["wifi_hybrid"]["enabled"] = true;
    if (!config.containsKey("structures")) config["structures"] = JsonObject();
    if (!config["structures"].containsKey("amada_isq20_mtf1")) config["structures"]["amada_isq20_mtf1"] = JsonObject();
    if (!config["structures"]["amada_isq20_mtf1"].containsKey("enabled")) config["structures"]["amada_isq20_mtf1"]["enabled"] = false;
    if (!config["structures"].containsKey("amada_isq20_awse")) config["structures"]["amada_isq20_awse"] = JsonObject();
    if (!config["structures"]["amada_isq20_awse"].containsKey("enabled")) config["structures"]["amada_isq20_awse"]["enabled"] = false;
    if (!config["structures"].containsKey("auto_detect")) config["structures"]["auto_detect"] = true;
    if (!config["structures"].containsKey("amada_header_every")) config["structures"]["amada_header_every"] = 20;
    if (!config.containsKey("ethernet")) config["ethernet"] = JsonObject();
    if (!config["ethernet"].containsKey("mode")) config["ethernet"]["mode"] = "dhcp";
    if (!config["ethernet"].containsKey("ip")) config["ethernet"]["ip"] = "";
    if (!config["ethernet"].containsKey("gw")) config["ethernet"]["gw"] = "";
    if (!config["ethernet"].containsKey("dns")) config["ethernet"]["dns"] = "";
    if (!config["ethernet"].containsKey("mac_override")) config["ethernet"]["mac_override"] = "";
    if (!config.containsKey("usb_server")) config["usb_server"] = JsonObject();
    if (!config["usb_server"].containsKey("enabled")) config["usb_server"]["enabled"] = true;
    if (!config["usb_server"].containsKey("port")) config["usb_server"]["port"] = 3240;
    if (!config["usb_server"].containsKey("recv_timeout_ms")) config["usb_server"]["recv_timeout_ms"] = 1200;
    if (!config["usb_server"].containsKey("in_wait_timeout_ms")) config["usb_server"]["in_wait_timeout_ms"] = 220;

    if (!config.containsKey("sdcard")) {
        config["sdcard"] = JsonObject();
        Serial.println(F("CFG: ensureAllFieldsExist() created sdcard section"));
    }
    if (!config["sdcard"].containsKey("enabled")) {
        config["sdcard"]["enabled"] = true;
        Serial.println(F("CFG: ensureAllFieldsExist() set default enabled=true"));
    }
    if (!config["sdcard"].containsKey("flush_interval")) {
        config["sdcard"]["flush_interval"] = "30m";
        Serial.println(F("CFG: ensureAllFieldsExist() set default flush_interval=30m"));
    } else {
        // Enforce 10m minimum: migrate legacy sub-10m values once.
        String fi = config["sdcard"]["flush_interval"] | String("30m");
        String clamped = ConfigManager::clampFlushInterval(fi);
        if (clamped != fi) {
            config["sdcard"]["flush_interval"] = clamped;
            Serial.println(F("CFG: ensureAllFieldsExist() migrated flush_interval to 10m minimum"));
        }
    }
    if (!config["sdcard"].containsKey("format")) config["sdcard"]["format"] = "csv";
    if (!config["sdcard"].containsKey("structured")) config["sdcard"]["structured"] = true;
    if (!config["sdcard"].containsKey("email_send")) config["sdcard"]["email_send"] = true;
    if (!config["sdcard"].containsKey("email_from_sd")) config["sdcard"]["email_from_sd"] = true;
    if (!config["sdcard"].containsKey("max_size_mb")) config["sdcard"]["max_size_mb"] = 4;
    if (!config["sdcard"].containsKey("emailday_extra_emails")) config["sdcard"]["emailday_extra_emails"] = "";
    if (!config["sdcard"].containsKey("sensorday_extra_emails")) config["sdcard"]["sensorday_extra_emails"] = "";
    if (!config["sdcard"].containsKey("logday_extra_emails")) config["sdcard"]["logday_extra_emails"] = "";
    if (!config["sdcard"].containsKey("auto_cleanup")) config["sdcard"]["auto_cleanup"] = true;
    if (!config["sdcard"].containsKey("cleanup_threshold")) config["sdcard"]["cleanup_threshold"] = 70;
    if (!config["sdcard"].containsKey("keep_days")) config["sdcard"]["keep_days"] = 0;

    // Inactivity alerts — 6 independent slots + global sensors_reset_timer.
    if (!config.containsKey("inactivity_alerts") || !config["inactivity_alerts"].is<JsonObject>())
        config["inactivity_alerts"] = JsonObject();
    if (!config["inactivity_alerts"].containsKey("sensors_reset_timer"))
        config["inactivity_alerts"]["sensors_reset_timer"] = false;
    if (!config["inactivity_alerts"].containsKey("slots") || !config["inactivity_alerts"]["slots"].is<JsonArray>()) {
        JsonArray slots = config["inactivity_alerts"]["slots"].to<JsonArray>();
        for (int i = 0; i < 6; ++i) {
            JsonObject slot = slots.createNestedObject();
            slot["enabled"] = false;
            slot["timeout_minutes"] = 20;
            slot["max_per_day"] = 12;
            slot["emails"] = "";
        }
    }
    // Ensure exactly 6 slots (pad if file had fewer).
    {
        JsonArray slots = config["inactivity_alerts"]["slots"].as<JsonArray>();
        while ((int)slots.size() < 6) {
            JsonObject slot = slots.createNestedObject();
            slot["enabled"] = false;
            slot["timeout_minutes"] = 20;
            slot["max_per_day"] = 12;
            slot["emails"] = "";
        }
    }

    if (!config.containsKey("do")) config["do"] = JsonObject();
    if (!config["do"].containsKey("invert")) config["do"]["invert"] = 0;
    if (!config["do"].containsKey("names") || !config["do"]["names"].is<JsonArray>()) {
        config["do"]["names"].to<JsonArray>();
    }
    while (config["do"]["names"].as<JsonArray>().size() < 8) config["do"]["names"].add("");

    if (!config.containsKey("system")) config["system"] = JsonObject();
    if (!config["system"].containsKey("heartbeat_enabled")) config["system"]["heartbeat_enabled"] = false;
    if (!config["system"].containsKey("heartbeat_interval")) config["system"]["heartbeat_interval"] = 10000;
    if (!config["system"].containsKey("charts_page_enabled")) config["system"]["charts_page_enabled"] = true;
    if (!config["system"].containsKey("charts_exempt_sessions")) config["system"]["charts_exempt_sessions"] = true;
    if (!config["system"].containsKey("serial_monitor_enabled")) config["system"]["serial_monitor_enabled"] = false;
#if defined(TARGET_BOARD_8DI8DO_POE) && TARGET_BOARD_8DI8DO_POE==1
    if (!config.containsKey("system_name")) config["system_name"] = "Node32-8DI8DO";
#else
    if (!config.containsKey("system_name")) config["system_name"] = PACK_SYSTEM_NAME;
#endif
    if (!config.containsKey("system_language")) config["system_language"] = "en";

#if ENABLE_MODBUS==1
    if (!modbusDoc.containsKey("modbus")) modbusDoc["modbus"] = JsonObject();
    if (!modbusDoc["modbus"].containsKey("enabled"))       modbusDoc["modbus"]["enabled"]       = false;
    if (!modbusDoc["modbus"].containsKey("rtu_enabled"))   modbusDoc["modbus"]["rtu_enabled"]   = false;
    if (!modbusDoc["modbus"].containsKey("tcp_enabled"))   modbusDoc["modbus"]["tcp_enabled"]   = false;
    if (!modbusDoc["modbus"].containsKey("slave_enabled")) modbusDoc["modbus"]["slave_enabled"] = false;
    if (!modbusDoc["modbus"].containsKey("rtu_baud"))      modbusDoc["modbus"]["rtu_baud"]      = 9600;
    if (!modbusDoc["modbus"].containsKey("rtu_data_bits")) modbusDoc["modbus"]["rtu_data_bits"] = 8;
    if (!modbusDoc["modbus"].containsKey("rtu_parity"))    modbusDoc["modbus"]["rtu_parity"]    = "N";
    if (!modbusDoc["modbus"].containsKey("rtu_stop_bits")) modbusDoc["modbus"]["rtu_stop_bits"] = 1;
    if (!modbusDoc["modbus"].containsKey("rtu_de_pin"))    modbusDoc["modbus"]["rtu_de_pin"]    = -1;
    if (!modbusDoc["modbus"].containsKey("slave_id"))      modbusDoc["modbus"]["slave_id"]      = 1;
    if (!modbusDoc["modbus"].containsKey("devices"))       modbusDoc["modbus"]["devices"].to<JsonArray>();
#endif // ENABLE_MODBUS==1

}

bool ConfigManager::saveToLittleFS() {
#if FILESYSTEM_PROVIDER == 1
    // littlefsBegin() mounts synchronously if not yet mounted (thread-safe, handles double-mount).
    // This removes the dependency on taskInitFlash mount timing.
    if (!littlefsBegin()) {
        Serial.println(F("CFG: LittleFS mount failed - flagging pending save for taskInitFlash"));
        lfsWritePending = true;
        return false;
    }

    struct stat st;
    bool dirExists = (stat("/littlefs/config", &st) == 0 && S_ISDIR(st.st_mode));
    if (!dirExists) {
        LittleFS.mkdir(LITTLEFS_CONFIG_DIR);
        delay(10);
    }

    auto stemPath = [](const char* stem) -> String {
        return String("/config/") + stem + ".json";
    };

    uint8_t lfsFailures = 0;
    lfsFailStems = "";
    String failReason = "";

    auto writeJsonAtomic = [&](const String& finalPath, JsonVariantConst payload, const char* label, size_t maxBytes = 0) -> bool {
        if (maxBytes > 0) {
            String sizeCheck;
            serializeJson(payload, sizeCheck);
            if (sizeCheck.length() > maxBytes) {
                Serial.printf("CFG: %s too large (%u > %u bytes) - skipping\n", label, (unsigned)sizeCheck.length(), (unsigned)maxBytes);
                failReason = String(F("too large (")) + String((unsigned)sizeCheck.length()) + F(" > ") + String((unsigned)maxBytes) + F(" bytes)");
                return false;
            }
        }

        String tmpPath = finalPath + ".tmp";
        // Clear any stale tmp left by a crashed write before retrying.
        LittleFS.remove(tmpPath);

        File f = LittleFS.open(tmpPath, "w");
        if (!f) {
            Serial.printf("CFG: Failed to open %s.tmp for write\n", label);
            failReason = String(F("open tmp failed (heap free ")) + String(ESP.getFreeHeap()) + F(")");
            return false;
        }

        size_t bytes = serializeJsonPretty(payload, f);
        f.close();
        delay(50);
        if (bytes == 0) {
            Serial.printf("CFG: %s.tmp wrote 0 bytes - aborting\n", label);
            LittleFS.remove(tmpPath);
            failReason = String(F("wrote 0 bytes (heap free ")) + String(ESP.getFreeHeap()) + F(")");
            return false;
        }

        // Verify the tmp file is REAL JSON before it can replace anything. A
        // truncated write (bytes > 0 but incomplete document) used to be renamed
        // over a valid file and corrupt the section on the next load. On failure
        // the original final file is left untouched and the tmp is dropped.
        {
            DynamicJsonDocument probe(maxBytes > 0 ? (maxBytes * 2 + 256) : 16384);
            File vf = LittleFS.open(tmpPath, "r");
            bool parseOk = false;
            if (vf) {
                parseOk = (deserializeJson(probe, vf) == DeserializationError::Ok);
                vf.close();
            }
            if (!parseOk) {
                Serial.printf("CFG: %s.tmp failed JSON verification - aborting, keeping original\n", label);
                LittleFS.remove(tmpPath);
                failReason = String(F("tmp failed JSON verification (heap free ")) + String(ESP.getFreeHeap()) + F(")");
                return false;
            }
        }

        flashIncrementWriteCount(bytes);

        // Atomic replace: littlefs rename will not overwrite an existing target,
        // so the final file is removed first (before the rename loop) and the
        // rename retried once if the first attempt still fails. If it still
        // fails the tmp file is KEPT (never delete the only good copy) so a
        // retry can recover instead of leaving the stem missing entirely.
        if (LittleFS.exists(finalPath)) {
            LittleFS.remove(finalPath);
            delay(5);
        }
        bool renamed = false;
        for (int r = 0; r < 2 && !renamed; r++) {
            if (LittleFS.rename(tmpPath, finalPath)) {
                renamed = true;
            } else {
                LittleFS.remove(finalPath);
                delay(10);
            }
        }
        if (!renamed) {
            Serial.printf("CFG: Failed to rename %s.tmp (tmp kept for retry)\n", label);
            failReason = String(F("rename failed (heap free ")) + String(ESP.getFreeHeap()) + F(")");
            return false;
        }

        Serial.printf("CFG: Saved %s (%u bytes)\n", label, (unsigned)bytes);
        return true;
    };

    // Each page/stem is saved to its OWN file, one at a time, and only when that
    // page actually changed (per-stem hash). A failure in one stem must not abort
    // the others; a failed stem is retried immediately and re-queued otherwise.
    auto saveStem = [&](StemIndex idx, const String& path, JsonVariantConst payload,
                        const char* label, size_t maxBytes) {
        if (!stemHashReady[idx]) {
            // First save: seed the baseline from what is already on disk so an
            // unchanged page is never rewritten (one save = one page).
            File rf = LittleFS.open(path, "r");
            if (rf) {
                String content;
                content.reserve((size_t)rf.size() + 1);  // known size: avoid realloc churn while reading
                while (rf.available()) content += (char)rf.read();
                rf.close();
                stemHash[idx] = computeHash32(content);
            }
            stemHashReady[idx] = true;
        }
        String prettyStr;
        serializeJsonPretty(payload, prettyStr);   // matches the on-disk format
        uint32_t h = computeHash32(prettyStr);
        if (h == stemHash[idx]) return;            // page unchanged -> skip its file
        bool ok = false;
        String lastReason;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            failReason = "";
            ok = writeJsonAtomic(path, payload, label, maxBytes);
            lastReason = failReason;
            if (!ok) delay(30);                    // try again right away
        }
        if (ok) {
            stemHash[idx] = h;
        } else {
            lfsFailures++;
            if (lfsFailStems.length()) lfsFailStems += ", ";
            lfsFailStems += label;
            stemHashReady[idx] = false;            // re-seed from disk next time
            String wmsg = F("⚠️ LittleFS save failed: ");
            wmsg += label;
            wmsg += F(" — ");
            wmsg += lastReason.length() ? lastReason : String(F("unknown"));
            sendVirtualMessage(wmsg, 0, true, true);
        }
    };

    // --- Save core.json (core settings only, excluding dedicated large-page files) ---
    {
        DynamicJsonDocument doc(10240); // Core subset copy (excludes dedicated page files); sized 2x worst case
        JsonObject root = doc.to<JsonObject>();
        for (JsonPair kv : config.as<JsonObject>()) {
            const char* key = kv.key().c_str();
            if (!isSafeConfigJsonKey(key)) continue;
            if (strcmp(key, "wifi") != 0 &&
                strcmp(key, "saved_networks") != 0 &&
                strcmp(key, "smtp") != 0 &&
                strcmp(key, "email") != 0 &&
                strcmp(key, "rs232") != 0 &&
                strcmp(key, "modbus") != 0 &&
                strcmp(key, "web") != 0 &&
                strcmp(key, "printer") != 0 &&
                strcmp(key, "ntp") != 0 &&
                strcmp(key, "system_name") != 0 &&
                strcmp(key, "system_language") != 0 &&
                strcmp(key, "inactivity_alerts") != 0 &&
                strcmp(key, "telnet") != 0 &&
                strcmp(key, "ethernet") != 0 &&
                strcmp(key, "sdcard") != 0 &&
                strcmp(key, "debug") != 0 &&
                strcmp(key, "access_point") != 0 &&
                strcmp(key, "wifi_hybrid") != 0 &&
                strcmp(key, "recipients_slots") != 0 &&
                strcmp(key, "numeric_rules") != 0 &&
                strcmp(key, "sched_cmd") != 0 &&
                strcmp(key, "sched_dt") != 0 &&
                strcmp(key, "sched_cmd_active") != 0 &&
                strcmp(key, "sched_dt_active") != 0 &&
                strcmp(key, "sched_cmd_migrated") != 0 &&
                strcmp(key, "sched_dt_migrated") != 0) {
                JsonVariant out = root[key];
                copyJsonSanitized(kv.value(), out);
            }
        }
        // Mask passwords in stored file
        if (root.containsKey("telnet") && root["telnet"].containsKey("password")) {
            if (root["telnet"]["password"].as<String>().length() > 0) root["telnet"]["password"] = "********";
        }
        if (root.containsKey("web") && root["web"].containsKey("password")) {
            if (root["web"]["password"].as<String>().length() > 0) root["web"]["password"] = "********";
        }
        if (root.containsKey("access_point") && root["access_point"].containsKey("password")) {
            if (root["access_point"]["password"].as<String>().length() > 0) root["access_point"]["password"] = "********";
        }
        String mainPath = stemPath("core");
        saveStem(STEM_CORE, mainPath, doc.as<JsonVariantConst>(), "core", 10240);
    }

#if ENABLE_RS232 == 1
    // --- Save rs232 section with templates ---
    if (config.containsKey("rs232") && config["rs232"].is<JsonObject>()) {
        String rs232Path = stemPath("rs232");
        saveStem(STEM_RS232, rs232Path, config["rs232"].as<JsonVariantConst>(), "rs232", 8192);
    }
#endif

#if ENABLE_PAGE_RECIPIENTS == 1
    // --- Save recipients.json (recipients_slots + numeric_rules) ---
    {
        String recipientsPath = stemPath("recipients");
        saveStem(STEM_RECIPIENTS, recipientsPath, recipDoc.as<JsonVariantConst>(), "recipients", 8192);
    }
#endif

#if ENABLE_SCHEDULER == 1
    // --- Save schedule.json (sched_cmd + sched_dt + counters) ---
    {
        String schedPath = stemPath("schedule");
        saveStem(STEM_SCHEDULE, schedPath, schedDoc.as<JsonVariantConst>(), "schedule", 8192);
    }
#endif

#if ENABLE_MODBUS == 1
    // --- Save modbus.json (modbus section with devices array) ---
    {
        String modbusPath = stemPath("modbus");
        saveStem(STEM_MODBUS, modbusPath, modbusDoc.as<JsonVariantConst>(), "modbus", 4096);
    }
#endif

    // --- Save access.json (Access page: web + related system toggles) ---
    {
        DynamicJsonDocument accessDoc(8192);
        JsonObject root = accessDoc.to<JsonObject>();

        if (config.containsKey("web") && config["web"].is<JsonObject>()) {
            JsonObject webOut = root.createNestedObject("web");
            for (JsonPair kv : config["web"].as<JsonObject>()) {
                if (!isKnownWebAccessKey(kv.key().c_str())) continue;   // never persist bloat
                webOut[kv.key()] = kv.value();
            }
            if (webOut.containsKey("password") && webOut["password"].as<String>().length() > 0) {
                webOut["password"] = "********";
            }
        }

        JsonObject sys = root.createNestedObject("system");
        sys["charts_page_enabled"] = config["system"]["charts_page_enabled"] | false;
        sys["charts_exempt_sessions"] = config["system"]["charts_exempt_sessions"] | false;
        sys["serial_monitor_enabled"] = config["system"]["serial_monitor_enabled"] | false;

        String accessPath = stemPath("access");
        saveStem(STEM_ACCESS, accessPath, accessDoc.as<JsonVariantConst>(), "access", 8192);
    }

    // --- Save system.json (System Settings page payload) ---
    {
        DynamicJsonDocument sysDoc(8192);
        JsonObject root = sysDoc.to<JsonObject>();

        if (config.containsKey("printer") && config["printer"].is<JsonObject>()) root["printer"] = config["printer"].as<JsonObject>();
        if (config.containsKey("system_name")) root["system_name"] = config["system_name"];
        if (config.containsKey("system_language")) root["system_language"] = config["system_language"];
        if (config.containsKey("mdns_enabled")) root["mdns_enabled"] = config["mdns_enabled"];
        if (config.containsKey("system") && config["system"].is<JsonObject>()) {
            JsonObject out = root.createNestedObject("system");
            for (JsonPair kv : config["system"].as<JsonObject>()) {
                const char* k = kv.key().c_str();
                if (!isSafeConfigJsonKey(k)) continue;
                out[k] = kv.value();
            }
        }

        String systemPagePath = stemPath("system");
        saveStem(STEM_SYSTEM, systemPagePath, sysDoc.as<JsonVariantConst>(), "system", 8192);
    }

#if ENABLE_USBIP_SERVER == 1
    // --- Save usb_server.json (USB Server page payload) ---
    {
        DynamicJsonDocument usbServerDoc(4096);
        JsonObject root = usbServerDoc.to<JsonObject>();

        if (config.containsKey("usb_server") && config["usb_server"].is<JsonObject>()) {
            root["usb_server"] = config["usb_server"].as<JsonObject>();
        }

        String usbServerPath = stemPath("usb_server");
        saveStem(STEM_USB_SERVER, usbServerPath, usbServerDoc.as<JsonVariantConst>(), "usb_server", 4096);
    }
#endif

#if ENABLE_SMTP == 1
    // --- Save smtp_email config (SMTP + Email settings) ---
    {
        DynamicJsonDocument smtpEmailDoc(4096);
        JsonObject root = smtpEmailDoc.to<JsonObject>();

        if (config.containsKey("smtp") && config["smtp"].is<JsonObject>()) {
            JsonObject smtpOut = root.createNestedObject("smtp");
            for (JsonPair kv : config["smtp"].as<JsonObject>()) {
                if (strcmp(kv.key().c_str(), "password") == 0) {
                    String pwd = kv.value().as<String>();
                    smtpOut["password"] = (pwd.length() > 0) ? "********" : "";
                } else {
                    smtpOut[kv.key()] = kv.value();
                }
            }
        }

        if (config.containsKey("email") && config["email"].is<JsonObject>()) {
            root["email"] = config["email"].as<JsonObject>();
        }

        String smtpEmailPath = stemPath("smtp_email");
        saveStem(STEM_SMTP_EMAIL, smtpEmailPath, smtpEmailDoc.as<JsonVariantConst>(), "smtp_email", 4096);
    }
#endif

    // --- Save alerts.json (inactivity alert — its own file, independent of recipients) ---
    if (config.containsKey("inactivity_alerts") && config["inactivity_alerts"].is<JsonObject>()) {
        DynamicJsonDocument alertDoc(8192);
        JsonObject alertRoot = alertDoc.to<JsonObject>();
        copyJsonCleanControlChars(config["inactivity_alerts"], alertRoot);
        String alertPath = stemPath("alerts");
        saveStem(STEM_ALERT, alertPath, alertRoot, "alerts", 8192);
    }

    // --- telnet.json (own file) ---
    if (config.containsKey("telnet") && config["telnet"].is<JsonObject>()) {
        DynamicJsonDocument t(4096);
        JsonObject to = t.to<JsonObject>();
        for (JsonPair kv : config["telnet"].as<JsonObject>()) {
            if (strcmp(kv.key().c_str(), "password") == 0 && kv.value().as<String>().length() > 0) to["password"] = "********";
            else to[kv.key()] = kv.value();
        }
        saveStem(STEM_TELNET, stemPath("telnet"), t.as<JsonVariantConst>(), "telnet", 4096);
    }
    // --- ethernet.json (own file) ---
    if (config.containsKey("ethernet") && config["ethernet"].is<JsonObject>())
        saveStem(STEM_ETHERNET, stemPath("ethernet"), config["ethernet"].as<JsonVariantConst>(), "ethernet", 4096);
    // --- sdcard.json (own file) ---
    if (config.containsKey("sdcard") && config["sdcard"].is<JsonObject>())
        saveStem(STEM_SDCARD, stemPath("sdcard"), config["sdcard"].as<JsonVariantConst>(), "sdcard", 4096);
    // --- debug.json (own file) ---
    if (config.containsKey("debug") && config["debug"].is<JsonObject>())
        saveStem(STEM_DEBUG, stemPath("debug"), config["debug"].as<JsonVariantConst>(), "debug", 4096);
    // --- access_point.json (own file) ---
    if (config.containsKey("access_point") && config["access_point"].is<JsonObject>()) {
        DynamicJsonDocument ap(4096);
        JsonObject apo = ap.to<JsonObject>();
        for (JsonPair kv : config["access_point"].as<JsonObject>()) {
            if (strcmp(kv.key().c_str(), "password") == 0 && kv.value().as<String>().length() > 0) apo["password"] = "********";
            else apo[kv.key()] = kv.value();
        }
        saveStem(STEM_ACCESS_POINT, stemPath("access_point"), ap.as<JsonVariantConst>(), "access_point", 4096);
    }
    // --- wifi_hybrid.json (own file) ---
    if (config.containsKey("wifi_hybrid") && config["wifi_hybrid"].is<JsonObject>())
        saveStem(STEM_WIFI_HYBRID, stemPath("wifi_hybrid"), config["wifi_hybrid"].as<JsonVariantConst>(), "wifi_hybrid", 4096);
    // --- do.json (own file) ---
    if (config.containsKey("do") && config["do"].is<JsonObject>())
        saveStem(STEM_DO, stemPath("do"), config["do"].as<JsonVariantConst>(), "do", 4096);
    // --- ftp.json (own file, password masked, real value in NVS cfg_ftp_pwd) ---
    if (config.containsKey("ftp") && config["ftp"].is<JsonObject>()) {
        DynamicJsonDocument f(4096);
        JsonObject fo = f.to<JsonObject>();
        for (JsonPair kv : config["ftp"].as<JsonObject>()) {
            if (strcmp(kv.key().c_str(), "password") == 0 && kv.value().as<String>().length() > 0) fo["password"] = "********";
            else fo[kv.key()] = kv.value();
        }
        saveStem(STEM_FTP, stemPath("ftp"), f.as<JsonVariantConst>(), "ftp", 4096);
    }

    // Write config registry listing all pages with their max sizes
    {
        DynamicJsonDocument regDoc(4096);
        JsonArray pages = regDoc.createNestedArray("pages");
        auto addPage = [&](const char* stem, size_t maxBytes) {
            JsonObject p = pages.createNestedObject();
            p["stem"] = stem;
            p["max_bytes"] = (uint32_t)maxBytes;
        };
        addPage("core",     10240);
        addPage("rs232",          8192);
        addPage("recipients",  4096);
        addPage("schedule",         4096);
        addPage("modbus",         4096);
        addPage("access",        8192);
        addPage("system",        4096);
        addPage("usb_server",   4096);
        addPage("smtp_email",     4096);
        addPage("alerts",          4096);
        addPage("telnet",         4096);
        addPage("ethernet",       4096);
        addPage("sdcard",        4096);
        addPage("debug",          4096);
        addPage("access_point",   4096);
        addPage("wifi_hybrid",    4096);
        addPage("do",             4096);
        addPage("ftp",            4096);
        // Sensor config files (managed independently outside ConfigManager)
        addPage("sensor_config",     4096);
        addPage("sensor_mq135",     4096);
        addPage("sensor_hx711",     4096);
        addPage("sensor_ina226",    4096);
        addPage("sensor_sound",     4096);
        addPage("sensor_max31865",  4096);
        addPage("sensor_proximity", 4096);
        addPage("sensor_prox_counters", 4096);
        addPage("sensor_bh1750", 4096);
        addPage("touch7", 4096);
        String regTmp = String(LITTLEFS_PAGES_REG_PATH) + ".tmp";
        static uint32_t s_regHash = 0;
        static bool s_regSeeded = false;
        if (!s_regSeeded) {
            File rf = LittleFS.open(LITTLEFS_PAGES_REG_PATH, "r");
            if (rf) {
                String content;
                content.reserve((size_t)rf.size() + 1);  // known size: avoid realloc churn while reading
                while (rf.available()) content += (char)rf.read();
                rf.close();
                s_regHash = computeHash32(content);
            }
            s_regSeeded = true;
        }
        String regStr;
        serializeJsonPretty(regDoc, regStr);
        uint32_t regHash = computeHash32(regStr);
        if (regHash != s_regHash) {
            File f = LittleFS.open(regTmp, "w");
            if (f) {
                size_t regBytes = serializeJsonPretty(regDoc, f);
                f.close();
                storNoteLfsWrite(regBytes);
                if (regBytes > 0) {
                    LittleFS.remove(LITTLEFS_PAGES_REG_PATH);
                    LittleFS.rename(regTmp, LITTLEFS_PAGES_REG_PATH);
                    s_regHash = regHash;
                } else {
                    LittleFS.remove(regTmp);
                }
            }
        }
    }

    if (lfsFailures > 0) {
        Serial.printf("CFG: ⚠️ %u LittleFS stem(s) failed to write (other pages saved OK)\n", (unsigned)lfsFailures);
        return false;
    }
    Serial.println(F("CFG: All config files saved to LittleFS"));
    return true;
#else
    return false;
#endif
}

bool ConfigManager::loadFromLittleFS() {
#if FILESYSTEM_PROVIDER == 1
    // littlefsBegin() mounts synchronously if not yet mounted — eliminates async timing dependency.
    // This is safe to call from setup() or from taskInitFlash (handles double-mount internally).
    if (!littlefsBegin()) {
        Serial.println(F("CFG: LittleFS mount failed - cannot load config"));
        return false;
    }

    auto stemPath = [](const char* stem) -> String {
        return String("/config/") + stem + ".json";
    };

    // Recovery must not promote a corrupt .tmp: the file has to parse as JSON
    // before it is renamed over the (missing) final. Only alerts.json is checked
    // here (the file that kept coming back corrupted); every other stem keeps the
    // cheap promote path.
    auto recoverOrRemoveTmpPath = [](const String& finalPath) {
        String tmpPath = finalPath + ".tmp";
        if (!LittleFS.exists(finalPath) && LittleFS.exists(tmpPath)) {
            bool ok = true;
            if (finalPath.endsWith("alerts.json")) {
                File vf = LittleFS.open(tmpPath, "r");
                if (vf) {
                    DynamicJsonDocument probe(16384);
                    ok = (deserializeJson(probe, vf) == DeserializationError::Ok);
                    vf.close();
                } else {
                    ok = false;
                }
            }
            if (ok) {
                Serial.printf("CFG: recovering %s from .tmp (interrupted save)\n", finalPath.c_str());
                LittleFS.rename(tmpPath, finalPath);
            } else {
                Serial.printf("CFG: discarding unparseable .tmp for %s (would corrupt the file)\n", finalPath.c_str());
                LittleFS.remove(tmpPath);
            }
        } else {
            LittleFS.remove(tmpPath);
        }
    };

    // Rewrite an unparseable own-file immediately with the current in-memory
    // section (defaults or the validated values) so a corrupt alerts.json heals
    // in the same boot instead of persisting until the next page save.
    auto repairOwnFileJson = [&](const String& finalPath, JsonVariantConst payload) {
        String tmpPath = finalPath + ".tmp";
        LittleFS.remove(tmpPath);
        File wf = LittleFS.open(tmpPath, "w");
        if (!wf) return;
        size_t wb = serializeJsonPretty(payload, wf);
        wf.close();
        storNoteLfsWrite(wb);
        delay(20);
        if (wb == 0) { LittleFS.remove(tmpPath); return; }
        LittleFS.remove(finalPath);
        LittleFS.rename(tmpPath, finalPath);
        Serial.printf("CFG: rewritten clean %s\n", finalPath.c_str());
        sendVirtualMessage(F("⚠️ config alerts.json corrupt — rewritten clean"), 1, true, true); // level 1 = LogLevel::WARN
    };

    const char* stems[] = {
        "core", "rs232", "recipients", "schedule", "modbus", "access", "system", "usb_server", "smtp_email",
        "alerts", "telnet", "ethernet", "sdcard", "debug", "access_point", "wifi_hybrid", "do", "ftp"
    };
    for (size_t i = 0; i < (sizeof(stems) / sizeof(stems[0])); ++i) {
        recoverOrRemoveTmpPath(stemPath(stems[i]));
    }

    auto parseJsonFile = [&](const String& path, DynamicJsonDocument& outDoc, bool required, const char* label, bool* wasCorrupt = nullptr) -> bool {
        outDoc.clear();
        if (wasCorrupt) *wasCorrupt = false;
        if (!LittleFS.exists(path)) {
            if (required) {
                Serial.printf("CFG: Missing required %s file: %s\n", label, path.c_str());
                return false;
            }
            return true;
        }

        File f = LittleFS.open(path, "r");
        if (!f) {
            Serial.printf("CFG: Failed to open %s: %s\n", label, path.c_str());
            return false;
        }
        if (f.size() == 0) {
            f.close();
            Serial.printf("CFG: Empty %s file: %s\n", label, path.c_str());
            return false;
        }

        DeserializationError err = deserializeJson(outDoc, f);
        f.close();
        if (err) {
            Serial.printf("CFG: %s parse error (%s): %s\n", label, path.c_str(), err.c_str());
            if (required) return false;      // core is mandatory — abort
            if (wasCorrupt) *wasCorrupt = true;
            outDoc.clear();                  // non-core: skip this section, keep defaults
            return true;                     // do NOT abort the whole load
        }

        return true;
    };

    auto tryLoadSet = [&]() -> bool {
        String mainPath = stemPath("core");
        String rs232Path = stemPath("rs232");
        String recipientsPath = stemPath("recipients");
        String schedPath = stemPath("schedule");
        String modbusPath = stemPath("modbus");
        String accessPath = stemPath("access");
        String systemPagePath = stemPath("system");
        String usbServerPath = stemPath("usb_server");

        DynamicJsonDocument mainDoc(16384);
        DynamicJsonDocument rs232Doc(8192);
        DynamicJsonDocument recipientsDoc(4096);
        DynamicJsonDocument schedFileDoc(5120);
        DynamicJsonDocument modbusFileDoc(4096);
        DynamicJsonDocument accessDoc(8192);
        DynamicJsonDocument systemPageDoc(8192);
        DynamicJsonDocument usbServerDoc(4096);

        if (!parseJsonFile(mainPath, mainDoc, false, "core")) { /* handled non-fatally */ }
        if (!parseJsonFile(rs232Path, rs232Doc, false, "rs232")) return false;
        if (!parseJsonFile(recipientsPath, recipientsDoc, false, "recipients")) return false;
        if (!parseJsonFile(schedPath, schedFileDoc, false, "schedule")) return false;
        if (!parseJsonFile(modbusPath, modbusFileDoc, false, "modbus")) return false;
        if (!parseJsonFile(accessPath, accessDoc, false, "access")) return false;
        if (!parseJsonFile(systemPagePath, systemPageDoc, false, "system")) return false;
        if (!parseJsonFile(usbServerPath, usbServerDoc, false, "usb_server")) return false;

        // Load smtp_email section (replaces NVS-only storage)
        {
            String smtpEmailPath = stemPath("smtp_email");
DynamicJsonDocument smtpEmailDoc(4096);
            if (parseJsonFile(smtpEmailPath, smtpEmailDoc, false, "smtp_email")) {
                if (smtpEmailDoc.containsKey("smtp") && smtpEmailDoc["smtp"].is<JsonObject>()) {
                    // Overwrite individual keys to preserve any defaults from initDefaults()
                    for (JsonPair kv : smtpEmailDoc["smtp"].as<JsonObject>()) {
                        config["smtp"][kv.key()] = kv.value();
                    }
                }
                if (smtpEmailDoc.containsKey("email") && smtpEmailDoc["email"].is<JsonObject>()) {
                    for (JsonPair kv : smtpEmailDoc["email"].as<JsonObject>()) {
                        config["email"][kv.key()] = kv.value();
                    }
                }
            }
        }

        // Own-file sections: telnet / ethernet / sdcard / debug / access_point / wifi_hybrid / do / ftp
        {
            const char* secs[] = {"telnet", "ethernet", "sdcard", "debug", "access_point", "wifi_hybrid", "do", "ftp"};
            for (int si = 0; si < 8; si++) {
                const char* sec = secs[si];
                DynamicJsonDocument d(4096);
                if (parseJsonFile(stemPath(sec), d, false, sec) && d.is<JsonObject>() && d.as<JsonObject>().size() > 0) {
                    if (!config.containsKey(sec) || !config[sec].is<JsonObject>()) config.createNestedObject(sec);
                    for (JsonPair kv : d.as<JsonObject>()) {
                        const char* k = kv.key().c_str();
                        if (!isSafeConfigJsonKey(k)) continue;
                        if (strcmp(k, "password") == 0 && kv.value().as<String>() == "********") continue;
                        // kv.key() (JsonString) stores BY COPY; const char* k would LINK-dangle after d dies.
                        config[sec][kv.key()] = kv.value();
                    }
                }
            }
        }

        for (JsonPair kv : mainDoc.as<JsonObject>()) {
            const char* key = kv.key().c_str();
            JsonVariant value = kv.value();
            if (!isSafeConfigJsonKey(key)) continue;
            if (strcmp(key, "wifi") == 0) continue;
            if (strcmp(key, "smtp") == 0) continue;
            if (strcmp(key, "email") == 0) continue;
            if (strcmp(key, "saved_networks") == 0) continue;
            if (strcmp(key, "rs232") == 0) continue;
            if (strcmp(key, "modbus") == 0) continue;
            if (strcmp(key, "inactivity_alerts") == 0) continue;   // own file alerts.json
            if (strcmp(key, "telnet") == 0) continue;             // own file telnet.json
            if (strcmp(key, "ethernet") == 0) continue;           // own file ethernet.json
            if (strcmp(key, "sdcard") == 0) continue;             // own file sdcard.json
            if (strcmp(key, "debug") == 0) continue;              // own file debug.json
            if (strcmp(key, "access_point") == 0) continue;       // own file access_point.json
            if (strcmp(key, "wifi_hybrid") == 0) continue;        // own file wifi_hybrid.json
            if (strcmp(key, "do") == 0) continue;                 // own file do.json
            if (strcmp(key, "recipients_slots") == 0) { recipDoc["recipients_slots"] = value; continue; }
            if (strcmp(key, "numeric_rules") == 0) { recipDoc["numeric_rules"] = value; continue; }
            if (strcmp(key, "sched_cmd") == 0) { schedDoc["sched_cmd"] = value; continue; }
            if (strcmp(key, "sched_dt") == 0) { schedDoc["sched_dt"] = value; continue; }
            if (strcmp(key, "sched_cmd_active") == 0) { schedDoc["sched_cmd_active"] = value; continue; }
            if (strcmp(key, "sched_dt_active") == 0) { schedDoc["sched_dt_active"] = value; continue; }
            if (strcmp(key, "sched_cmd_migrated") == 0) { schedDoc["sched_cmd_migrated"] = value; continue; }
            if (strcmp(key, "sched_dt_migrated") == 0) { schedDoc["sched_dt_migrated"] = value; continue; }

            if (value.is<JsonObject>()) {
                JsonObject obj = value.as<JsonObject>();
                if (!config.containsKey(key) || !config[key].is<JsonObject>()) {
                    // kv.key() copies; const char* key would LINK-dangle after mainDoc dies.
                    config.createNestedObject(kv.key());
                }
                for (JsonPair innerKv : obj) {
                    const char* innerKey = innerKv.key().c_str();
                    if (!isSafeConfigJsonKey(innerKey)) continue;
                    if (strcmp(innerKey, "password") == 0 && innerKv.value().as<String>() == "********") {
                        continue;
                    }
                    // kv.key()/innerKv.key() copy; const char* would LINK-dangle after mainDoc dies.
                    JsonVariant out = config[kv.key()][innerKv.key()];
                    copyJsonSanitized(innerKv.value(), out);
                }
            } else {
                // kv.key() copies; const char* key would LINK-dangle after mainDoc dies.
                JsonVariant out = config[kv.key()];
                copyJsonSanitized(value, out);
            }
        }

        if (rs232Doc.is<JsonObject>() && rs232Doc.as<JsonObject>().size() > 0) {
            config["rs232"] = rs232Doc.as<JsonObject>();
        }
        if (recipientsDoc.is<JsonObject>() && recipientsDoc.as<JsonObject>().size() > 0) {
            for (JsonPair kv : recipientsDoc.as<JsonObject>()) {
                recipDoc[kv.key()] = kv.value();
            }
        }
        if (schedFileDoc.is<JsonObject>() && schedFileDoc.as<JsonObject>().size() > 0) {
            for (JsonPair kv : schedFileDoc.as<JsonObject>()) {
                schedDoc[kv.key()] = kv.value();
            }
        }
        if (modbusFileDoc.is<JsonObject>() && modbusFileDoc.as<JsonObject>().size() > 0) {
            for (JsonPair kv : modbusFileDoc.as<JsonObject>()) {
                modbusDoc[kv.key()] = kv.value();
            }
        }

        if (accessDoc.is<JsonObject>() && accessDoc.as<JsonObject>().size() > 0) {
        if (accessDoc.containsKey("web") && accessDoc["web"].is<JsonObject>()) {
            JsonObject src = accessDoc["web"].as<JsonObject>();
            if (!config.containsKey("web") || !config["web"].is<JsonObject>()) config.createNestedObject("web");
            for (JsonPair kv : src) {
                const char* k = kv.key().c_str();
                if (!isKnownWebAccessKey(k)) continue;            // drop junk/bloat
                if (strcmp(k, "password") == 0 && kv.value().as<String>() == "********") continue;
                // kv.key() copies; const char* k would LINK-dangle after accessDoc dies.
                config["web"][kv.key()] = kv.value();
            }
        }
            if (accessDoc.containsKey("system") && accessDoc["system"].is<JsonObject>()) {
                JsonObject sys = accessDoc["system"].as<JsonObject>();
                if (!config.containsKey("system") || !config["system"].is<JsonObject>()) config.createNestedObject("system");
                if (sys.containsKey("charts_page_enabled")) config["system"]["charts_page_enabled"] = sys["charts_page_enabled"];
                if (sys.containsKey("charts_exempt_sessions")) config["system"]["charts_exempt_sessions"] = sys["charts_exempt_sessions"];
                if (sys.containsKey("serial_monitor_enabled")) {
                    config["system"]["serial_monitor_enabled"] = sys["serial_monitor_enabled"];
                    sendVirtualMessage(String(F("[cfg:boot] serial_monitor_enabled loaded from access=")) + String(sys["serial_monitor_enabled"].as<bool>() ? 1 : 0), kLogInfo);
                }
            }
        }

        if (usbServerDoc.is<JsonObject>() && usbServerDoc.as<JsonObject>().size() > 0) {
            if (usbServerDoc.containsKey("usb_server") && usbServerDoc["usb_server"].is<JsonObject>()) {
                config["usb_server"] = usbServerDoc["usb_server"].as<JsonObject>();
            }
        }

        // Restore the system-settings section (its own file). Kept next to the
        // alerts import in the final config-import zone: with the ArduinoJson
        // version in this env, imports copied into `config` before the bulk of
        // the load can be invalidated by later config writes (pool relocation).
        // Restores mdns_enabled, the system_name/language/printer set and the
        // full config["system"] subtree (serial_monitor, heartbeat, charts).
        if (systemPageDoc.is<JsonObject>() && systemPageDoc.as<JsonObject>().size() > 0) {
            if (systemPageDoc.containsKey("printer") && systemPageDoc["printer"].is<JsonObject>()) config["printer"] = systemPageDoc["printer"].as<JsonObject>();
            if (systemPageDoc.containsKey("system_name")) config["system_name"] = systemPageDoc["system_name"];
            if (systemPageDoc.containsKey("system_language")) config["system_language"] = systemPageDoc["system_language"];
            if (systemPageDoc.containsKey("mdns_enabled")) config["mdns_enabled"] = systemPageDoc["mdns_enabled"];
            if (systemPageDoc.containsKey("system") && systemPageDoc["system"].is<JsonObject>()) {
                if (!config.containsKey("system") || !config["system"].is<JsonObject>()) config.createNestedObject("system");
                for (JsonPair kv : systemPageDoc["system"].as<JsonObject>()) {
                    const char* k = kv.key().c_str();
                    if (!isSafeConfigJsonKey(k)) continue;
                    // kv.key() copies; const char* k would LINK-dangle after systemPageDoc dies.
                    config["system"][kv.key()] = kv.value();
                }
            }
        }

        // Reload the alerts section LAST. With the ArduinoJson version in this
        // env, writing any other section into `config` after the alerts import
        // can invalidate the freshly-copied nested tree (pool relocation), so
        // this must remain the final config mutation inside loadFromLittleFS.
        // Load alerts.json (inactivity alert — its own file)
        {
            String alertPath = stemPath("alerts");
            DynamicJsonDocument alertDoc(8192);
            bool alertsCorrupt = false;
            if (parseJsonFile(alertPath, alertDoc, false, "alerts", &alertsCorrupt)) {
                if (alertsCorrupt) {
                    // The file on disk is broken — rewrite it RIGHT NOW from the
                    // current (defaults + validated) in-memory section. Without
                    // this the corruption would survive until the next page save
                    // and keep producing the "comes back every time" bug.
                    DynamicJsonDocument healDoc(8192);
                    JsonObject healRoot = healDoc.to<JsonObject>();
                    copyJsonCleanControlChars(config["inactivity_alerts"], healRoot);
                    repairOwnFileJson(alertPath, healRoot);
                }
                if (alertDoc.is<JsonObject>() && alertDoc.as<JsonObject>().size() > 0) {
                    if (!config.containsKey("inactivity_alerts") || !config["inactivity_alerts"].is<JsonObject>())
                        config.createNestedObject("inactivity_alerts");
                    for (JsonPair kv : alertDoc.as<JsonObject>()) {
                        const char* k = kv.key().c_str();
                        if (!isSafeConfigJsonKey(k)) continue;
                        // kv.key() (JsonString) stores BY COPY. const char* k would LINK-dangle
                        // after alertDoc dies: keys then read freed PSRAM (garbage keys in alerts.json
                        // + lost slots on every reboot — the recurring alerts.json corruption).
                        config["inactivity_alerts"][kv.key()] = kv.value();
                    }
                    // Migrate old single-alert format: if "enabled" key exists at top level, convert to slot[0].
                    if (config["inactivity_alerts"].containsKey("enabled") && !config["inactivity_alerts"].containsKey("slots")) {
                        JsonArray slots;
                        if (!config["inactivity_alerts"]["slots"].is<JsonArray>())
                            slots = config["inactivity_alerts"]["slots"].to<JsonArray>();
                        else
                            slots = config["inactivity_alerts"]["slots"].as<JsonArray>();
                        if ((int)slots.size() == 0) {
                            JsonObject slot = slots.createNestedObject();
                            slot["enabled"] = config["inactivity_alerts"]["enabled"] | false;
                            slot["timeout_minutes"] = config["inactivity_alerts"]["timeout_minutes"] | 20;
                            slot["max_per_day"] = config["inactivity_alerts"]["max_per_day"] | 12;
                            slot["emails"] = config["inactivity_alerts"]["emails"] | "";
                        }
                        config["inactivity_alerts"].remove("enabled");
                        config["inactivity_alerts"].remove("timeout_minutes");
                        config["inactivity_alerts"].remove("max_per_day");
                        config["inactivity_alerts"].remove("emails");
                    }
                    // Ensure 6 slots.
                    if (config["inactivity_alerts"].containsKey("slots") && config["inactivity_alerts"]["slots"].is<JsonArray>()) {
                        JsonArray slots = config["inactivity_alerts"]["slots"].as<JsonArray>();
                        while ((int)slots.size() < 6) {
                            JsonObject slot = slots.createNestedObject();
                            slot["enabled"] = false;
                            slot["timeout_minutes"] = 20;
                            slot["max_per_day"] = 12;
                            slot["emails"] = "";
                        }
                    }
                }
            }
        }

        Serial.println(F("CFG: Loaded config set"));
        return true;
    };

    bool loaded = tryLoadSet();

    if (!loaded) {
        Serial.println(F("CFG: No LittleFS config found (first boot)"));
        delay(50);
        return false;
    }
    // Restore passwords from NVS (LittleFS stores masked "********"; NVS holds the real value).
    // If NVS key is missing, seed it with the current config default (not masked by LittleFS anymore).
    {
        Preferences pp;
        if (pp.begin("esp32_config", true)) {
            auto loadPwd = [&](const char* nvsKey, const char* section, const char* field) {
                if (pp.isKey(nvsKey)) {
                    String pwd = pp.getString(nvsKey, "");
                    if (pwd.length() > 0) config[section][field] = pwd;
                } else {
                    String seed = config[section][field] | "";
                    if (seed.length() > 0 && seed != "********") {
                        Preferences ppw;
                        if (ppw.begin("esp32_config", false)) {
                            ppw.putString(nvsKey, seed);
                            ppw.end();
                        }
                    }
                }
            };
            loadPwd(NVS_KEY_WEB_PWD,    "web",          "password");
            loadPwd(NVS_KEY_TELNET_PWD, "telnet",       "password");
            loadPwd(NVS_KEY_AP_PWD,     "access_point", "password");
            loadPwd(NVS_KEY_SMTP_PWD,   "smtp",         "password");
            loadPwd(NVS_KEY_FTP_PWD,    "ftp",          "password");
            pp.end();
        }
    }

    return true;
#else
    return false;
#endif
}

void ConfigManager::migrateNVSToLittleFS() {
#if FILESYSTEM_PROVIDER == 1
    Preferences p;
    if (!p.begin("esp32_config", true)) return;

    bool migrationNeeded = p.getBool("littlefs_migr", false) == false;
    p.end();

    if (!migrationNeeded) {
        Serial.println(F("CFG: ✅ LittleFS migration already completed"));
        return;
    }

    Serial.println(F("CFG: 🔄 Starting NVS → LittleFS migration..."));
    if (saveToLittleFS()) {
        Serial.println(F("CFG: ✅ Migration: Non-critical data moved to LittleFS"));
        if (p.begin("esp32_config", false)) {
            p.putBool("littlefs_migr", true);
            p.end();
        }
        sendVirtualMessage(F("✅ Config migrated: NVS → LittleFS"), 0, true, false);
    } else {
        Serial.println(F("CFG: ❌ Migration failed - keeping NVS data as fallback"));
    }
#endif
}

uint32_t ConfigManager::computeHash32(const String &s) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < (size_t)s.length(); ++i) {
        hash ^= (uint8_t)s[i];
        hash *= 16777619u;
    }
    return hash;
}

void ConfigManager::normalizePostLoad() {
    if (recipDoc.containsKey("recipients_slots") && recipDoc["recipients_slots"].is<JsonArray>()) {
        JsonArray arr = recipDoc["recipients_slots"].as<JsonArray>();
        for (int i = (int)arr.size() - 1; i >= 0; --i) {
            JsonObject slot = arr[i];
            String n = slot["name"] | String("");
            String em = slot["emails"] | String("");
            if (n.length() == 0 && em.length() == 0) arr.remove(i);
        }
    }
    if (config.containsKey("saved_networks") && config["saved_networks"].is<JsonArray>()) {
        JsonArray nets = config["saved_networks"].as<JsonArray>();
        for (int i = (int)nets.size() - 1; i >= 0; --i) {
            JsonObject n = nets[i];
            if (!n.containsKey("ssid")) { nets.remove(i); continue; }
            String ssid = n["ssid"].as<String>();
            if (ssid.length() == 0) { nets.remove(i); continue; }
        }
    }
    if (schedDoc.containsKey("sched_cmd") && schedDoc["sched_cmd"].is<JsonObject>()) {
        JsonObject sc = schedDoc["sched_cmd"].as<JsonObject>();
        int lastIdx = 0;
        for (int i = 1; i <= 10; ++i) {
            char key[5]; snprintf(key, sizeof(key), "s%d", i);
            if (!sc.containsKey(key)) continue;
            JsonObject so = sc[key];
            String cmd = so.containsKey(FPSTR(K_COMMAND)) ? String(so[FPSTR(K_COMMAND)].as<const char*>()) : String("");
            int inter = so.containsKey(FPSTR(K_INTERVAL)) ? so[FPSTR(K_INTERVAL)].as<int>() : 0;
            String days = so.containsKey(FPSTR(K_DAYS)) ? String(so[FPSTR(K_DAYS)].as<const char*>()) : String("");
            String tpl = so.containsKey(FPSTR(K_TEMPLATE)) ? String(so[FPSTR(K_TEMPLATE)].as<const char*>()) : String("");
            bool en = so.containsKey(FPSTR(K_ENABLED)) ? so[FPSTR(K_ENABLED)].as<bool>() : false;
            if (en || cmd.length() || inter > 0 || days.length() || tpl.length()) lastIdx = i;
        }
        if (lastIdx == 0) lastIdx = 1;
        schedDoc["sched_cmd_active"] = lastIdx;
    }
    if (schedDoc.containsKey("sched_dt") && schedDoc["sched_dt"].is<JsonObject>()) {
        JsonObject sd = schedDoc["sched_dt"].as<JsonObject>();
        int lastIdx = 0;
        for (int i = 1; i <= 8; ++i) {
            char key[5]; snprintf(key, sizeof(key), "s%d", i);
            if (!sd.containsKey(key)) continue;
            JsonObject so = sd[key];
            String cmd = so.containsKey(FPSTR(K_COMMAND)) ? String(so[FPSTR(K_COMMAND)].as<const char*>()) : String("");
            String dt = so.containsKey(FPSTR(K_DATE)) ? String(so[FPSTR(K_DATE)].as<const char*>()) : String("");
            String tm = so.containsKey(FPSTR(K_TIME)) ? String(so[FPSTR(K_TIME)].as<const char*>()) : String("");
            String tpl = so.containsKey(FPSTR(K_TEMPLATE)) ? String(so[FPSTR(K_TEMPLATE)].as<const char*>()) : String("");
            bool en = so.containsKey(FPSTR(K_ENABLED)) ? so[FPSTR(K_ENABLED)].as<bool>() : false;
            if (en || cmd.length() || dt.length() || tm.length() || tpl.length()) lastIdx = i;
        }
        if (lastIdx == 0) lastIdx = 1;
        schedDoc["sched_dt_active"] = lastIdx;
    }
}

void ConfigManager::pruneEmptySlotsBeforeSave() {
    if (recipDoc.containsKey("recipients_slots") && recipDoc["recipients_slots"].is<JsonArray>()) {
        JsonArray arr = recipDoc["recipients_slots"].as<JsonArray>();
        for (int i = (int)arr.size() - 1; i >= 0; --i) {
            JsonObject slot = arr[i];
            String n = slot["name"] | String("");
            String em = slot["emails"] | String("");
            if (n.length() == 0 && em.length() == 0) {
                arr.remove(i);
            }
        }
    }
    if (schedDoc.containsKey("sched_cmd") && schedDoc["sched_cmd"].is<JsonObject>()) {
        JsonObject sc = schedDoc["sched_cmd"].as<JsonObject>();
        for (int i = 1; i <= 10; ++i) {
            char key[5]; snprintf(key, sizeof(key), "s%d", i);
            if (!sc.containsKey(key)) continue;
            JsonObject so = sc[key];
            String cmd = so.containsKey(FPSTR(K_COMMAND)) ? String(so[FPSTR(K_COMMAND)].as<const char*>()) : String("");
            int inter = so.containsKey(FPSTR(K_INTERVAL)) ? so[FPSTR(K_INTERVAL)].as<int>() : 0;
            String days = so.containsKey(FPSTR(K_DAYS)) ? String(so[FPSTR(K_DAYS)].as<const char*>()) : String("");
            String tpl = so.containsKey(FPSTR(K_TEMPLATE)) ? String(so[FPSTR(K_TEMPLATE)].as<const char*>()) : String("");
            bool en = so.containsKey(FPSTR(K_ENABLED)) ? so[FPSTR(K_ENABLED)].as<bool>() : false;
            if (!en && cmd.length() == 0 && inter == 0 && days.length() == 0 && tpl.length() == 0) {
                sc.remove(key);
            }
        }
    }
    if (schedDoc.containsKey("sched_dt") && schedDoc["sched_dt"].is<JsonObject>()) {
        JsonObject sd = schedDoc["sched_dt"].as<JsonObject>();
        for (int i = 1; i <= 8; ++i) {
            char key[5]; snprintf(key, sizeof(key), "s%d", i);
            if (!sd.containsKey(key)) continue;
            JsonObject so = sd[key];
            String cmd = so.containsKey(FPSTR(K_COMMAND)) ? String(so[FPSTR(K_COMMAND)].as<const char*>()) : String("");
            String dt = so.containsKey(FPSTR(K_DATE)) ? String(so[FPSTR(K_DATE)].as<const char*>()) : String("");
            String tm = so.containsKey(FPSTR(K_TIME)) ? String(so[FPSTR(K_TIME)].as<const char*>()) : String("");
            String tpl = so.containsKey(FPSTR(K_TEMPLATE)) ? String(so[FPSTR(K_TEMPLATE)].as<const char*>()) : String("");
            bool en = so.containsKey(FPSTR(K_ENABLED)) ? so[FPSTR(K_ENABLED)].as<bool>() : false;
            if (!en && cmd.length() == 0 && dt.length() == 0 && tm.length() == 0 && tpl.length() == 0) {
                sd.remove(key);
            }
        }
    }
    if (config.containsKey("saved_networks") && config["saved_networks"].is<JsonArray>()) {
        JsonArray nets = config["saved_networks"].as<JsonArray>();
        for (int i = (int)nets.size() - 1; i >= 0; --i) {
            JsonObject n = nets[i];
            if (!n.containsKey("ssid")) {
                nets.remove(i);
                continue;
            }
            String ssid = n["ssid"].as<String>();
            if (ssid.length() == 0) {
                nets.remove(i);
                continue;
            }
        }
    }
    if (schedDoc.containsKey("sched_cmd") && schedDoc["sched_cmd"].is<JsonObject>()) {
        JsonObject sc = schedDoc["sched_cmd"].as<JsonObject>();
        int lastIdx = 0;
        for (int i = 1; i <= 10; ++i) {
            String key = String("s") + i;
            if (!sc.containsKey(key)) continue;
            JsonObject so = sc[key];
            String cmd = so.containsKey("command") ? String(so["command"].as<const char*>()) : String("");
            int inter = so.containsKey("interval") ? so["interval"].as<int>() : 0;
            String days = so.containsKey("days") ? String(so["days"].as<const char*>()) : String("");
            String tpl = so.containsKey("template") ? String(so["template"].as<const char*>()) : String("");
            bool en = so.containsKey("enabled") ? so["enabled"].as<bool>() : false;
            if (en || cmd.length() || inter > 0 || days.length() || tpl.length()) {
                lastIdx = i;
            }
        }
        if (lastIdx == 0) lastIdx = 1;
        schedDoc["sched_cmd_active"] = lastIdx;
    }
    if (schedDoc.containsKey("sched_dt") && schedDoc["sched_dt"].is<JsonObject>()) {
        JsonObject sd = schedDoc["sched_dt"].as<JsonObject>();
        int lastIdx = 0;
        for (int i = 1; i <= 10; ++i) {
            String key = String("s") + i;
            if (!sd.containsKey(key)) continue;
            JsonObject so = sd[key];
            String cmd = so.containsKey("command") ? String(so["command"].as<const char*>()) : String("");
            String dt = so.containsKey("date") ? String(so["date"].as<const char*>()) : String("");
            String tm = so.containsKey("time") ? String(so["time"].as<const char*>()) : String("");
            String tpl = so.containsKey("template") ? String(so["template"].as<const char*>()) : String("");
            bool en = so.containsKey("enabled") ? so["enabled"].as<bool>() : false;
            if (en || cmd.length() || dt.length() || tm.length() || tpl.length()) lastIdx = i;
        }
        if (lastIdx == 0) lastIdx = 1;
        schedDoc["sched_dt_active"] = lastIdx;
    }
}

void ConfigManager::checkNVSHealth() {
    Serial.println(F("========================================="));
    Serial.println(F("NVS Health Check"));
    Serial.println(F("========================================="));

    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK) {
        Serial.print(F("  Used entries: ")); Serial.println(stats.used_entries);
        Serial.print(F("  Free entries: ")); Serial.println(stats.free_entries);
        Serial.print(F("  Total entries: ")); Serial.println(stats.total_entries);
        Serial.print(F("  Namespace count: ")); Serial.println(stats.namespace_count);

        float usage = (float)stats.used_entries / (float)stats.total_entries * 100.0f;
        Serial.print(F("  Usage: ")); Serial.print(usage, 1); Serial.println(F("%"));

        if (usage > 90.0f) {
            Serial.println(F("  ⚠️ WARNING: NVS >90% full - config loss risk!"));
            triggerScreenNVSWarning((int)usage);
        } else if (usage > 75.0f) {
            Serial.println(F("  ⚠️ CAUTION: NVS >75% full - monitor closely"));
            triggerScreenNVSWarning((int)usage);
        } else {
            Serial.println(F("  ✅ NVS health OK"));
        }
    } else {
        Serial.print(F("  ❌ Failed to get NVS stats: 0x"));
        Serial.println(err, HEX);
    }

    Serial.println(F("  Checking segmented keys..."));
    nvs.begin("esp32_config", true);
    const char* keys[] = {
        "cfg_wifi", "cfg_smtp_pwd", "cfg_eth", "cfg_saved_net", "cfg_rs232",
        "cfg_tpl", "cfg_rcp", "cfg_nr", "cfg_sched_cmd", "cfg_sched_dt",
        "cfg_drbt", "cfg_sdcard"
    };
    int keysFound = 0;
    for (int i = 0; i < 12; i++) {
        if (nvs.isKey(keys[i])) {
            String val = nvs.getString(keys[i], "");
            Serial.print(F("    ")); Serial.print(keys[i]);
            Serial.print(F(": ")); Serial.print(val.length());
            Serial.println(F(" bytes"));
            keysFound++;
        }
    }
    nvs.end();
    Serial.print(F("  Found ")); Serial.print(keysFound);
    Serial.println(F("/12 segmented keys"));
    Serial.println(F("========================================="));
}

bool ConfigManager::verifySave() {
    Serial.println(F("CFG: Post-save verification..."));

    nvs.begin("esp32_config", true);

    String wifiCheck = nvs.getString(NVS_KEY_WIFI, "");
    if (config.containsKey("wifi") && wifiCheck.length() == 0) {
        Serial.println(F("CFG: ❌ VERIFY FAILED: WiFi not readable"));
        nvs.end();
        return false;
    }

    nvs.end();
    Serial.println(F("CFG: ✅ Post-save verification passed"));
    return true;
}

bool ConfigManager::loadConfig() {
    Serial.println(F("CFG: Loading from dual-storage (NVS + LittleFS)..."));
    initDefaults();
    uint8_t nvsSectionsLoaded = 0;

    nvs.begin("esp32_config", true);

    String wifiJson = nvs.isKey(NVS_KEY_WIFI) ? nvs.getString(NVS_KEY_WIFI, "") : String("");
    if (wifiJson.length() > 0) {
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, wifiJson) == DeserializationError::Ok) {
            config["wifi"] = doc.as<JsonObject>();
            nvsSectionsLoaded++;
        }
    }

    String netsJson = nvs.isKey(NVS_KEY_SAVED_NETWORKS) ? nvs.getString(NVS_KEY_SAVED_NETWORKS, "") : String("");
    if (netsJson.length() > 0) {
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, netsJson) == DeserializationError::Ok) {
            config["saved_networks"] = doc.as<JsonArray>();
            nvsSectionsLoaded++;
        }
    }

    nvs.end();

    // Snapshot initDefaults passwords BEFORE loadFromLittleFS overwrites them with "********".
    String defaultWebPwd    = config["web"]["password"]          | "";
    String defaultTelnetPwd = config["telnet"]["password"]       | "";
    String defaultApPwd     = config["access_point"]["password"] | "";
    String defaultSmtpPwd   = config["smtp"]["password"]         | "";

    bool littlefsLoaded = loadFromLittleFS();

    // Restore passwords from NVS (LittleFS only has masked "********" for these fields).
    // If NVS key is missing, seed it with the initDefaults snapshot.
    {
        Preferences pp;
        if (pp.begin("esp32_config", true)) {
            auto loadPwd = [&](const char* nvsKey, const char* section, const char* field, const String& defaultPwd) {
                if (pp.isKey(nvsKey)) {
                    String pwd = pp.getString(nvsKey, "");
                    if (pwd.length() > 0) config[section][field] = pwd;
                } else if (defaultPwd.length() > 0 && defaultPwd != "********") {
                    Preferences ppw;
                    if (ppw.begin("esp32_config", false)) {
                        ppw.putString(nvsKey, defaultPwd);
                        ppw.end();
                    }
                    config[section][field] = defaultPwd;
                }
            };
            loadPwd(NVS_KEY_WEB_PWD,    "web",          "password",    defaultWebPwd);
            loadPwd(NVS_KEY_TELNET_PWD, "telnet",       "password",    defaultTelnetPwd);
            loadPwd(NVS_KEY_AP_PWD,     "access_point", "password",    defaultApPwd);
            loadPwd(NVS_KEY_SMTP_PWD,   "smtp",         "password",    defaultSmtpPwd);
            loadPwd(NVS_KEY_FTP_PWD,    "ftp",          "password",    String(""));
            pp.end();
        }
    }

    ensureAllFieldsExist();
    normalizePostLoad();
    migrateNVSToLittleFS();

    sendVirtualMessage(String(F("[cfg:boot] serial_monitor_enabled after load=")) + String(getSerialMonitorEnabled() ? 1 : 0), kLogInfo);

    _cache_sdcard_enabled = config["sdcard"]["enabled"] | true;
    _cache_sdcard_flush_interval = clampFlushInterval(config["sdcard"]["flush_interval"] | String("30m"));
    _cache_sd_auto_cleanup = config["sdcard"]["auto_cleanup"] | true;
    _cache_sd_cleanup_threshold = config["sdcard"]["cleanup_threshold"] | 70;
    _cache_sd_keep_days = config["sdcard"]["keep_days"] | 0;
    _cache_telnet_enabled = config["telnet"]["enabled"] | false;
    _cache_wifi_disabled = config["wifi"]["runtime_disabled"] | false;
    _cache_boot_log_enabled = config["boot_log"]["enabled"] | false;
    _cache_web_auth_enabled = config["web"]["auth_enabled"] | false;

    isDirty = false;
    lastDirtyMark = 0;
    lastSave = millis();

    char cfgMsg[80];
    snprintf(cfgMsg, sizeof(cfgMsg), "CFG: Load completed (NVS sections=%u, LFS=%s)",
             (unsigned)nvsSectionsLoaded,
             littlefsLoaded ? "ok" : "defaults");
    Serial.println(cfgMsg);
    return true;
}

void ConfigManager::cleanDeprecatedFlowControl() {
    bool changed = false;
    if (config.containsKey("rs232")) {
        if (config["rs232"].containsKey("flow_control")) {
            config["rs232"].remove("flow_control");
            changed = true;
        }
        if (config["rs232"].containsKey("templates") && config["rs232"]["templates"].is<JsonArray>()) {
            for (JsonObject t : config["rs232"]["templates"].as<JsonArray>()) {
                if (t.containsKey("flow_control")) {
                    t.remove("flow_control");
                    changed = true;
                }
            }
        }
    }
    if (changed) {
        save(true);
        logMsg("ConfigManager: Removed deprecated flow_control keys");
    }
}

bool ConfigManager::load() {
    // Legacy API kept for compatibility with old callers.
    // The active path is loadConfig() with dual-storage + transactional LittleFS.
    return loadConfig();
}

bool ConfigManager::save(bool force) {
    if (!configLock(pdMS_TO_TICKS(5000))) {
        Serial.println(F("CFG: ⚠️ Save blocked (mutex timeout - concurrent access?)"));
        return false;
    }

    if (!isDirty && !force) {
        configUnlock();
        return true;
    }

    unsigned long now = millis();
    if (!force) {
        // Some legacy setters still set isDirty directly without calling markDirtyAndScheduleSave().
        if (lastDirtyMark == 0) {
            lastDirtyMark = now;
        }

        // Save only after the short batching window elapsed since the last change.
        if (now - lastDirtyMark < SAVE_DELAY) {
            configUnlock();
            return true;
        }

        // Keep a minimum spacing between consecutive flash writes.
        if (now - lastSave < SAVE_DELAY) {
        saveThrottleCount++;
        if (now - lastThrottleReport >= 10000 && saveThrottleCount > 20) {
            Serial.printf("⚠️ CFG SAVE LOOP: %u throttled attempts in last 10s\n", saveThrottleCount);
            saveThrottleCount = 0;
            lastThrottleReport = now;
        }
        configUnlock();
        return true;
        }
    }

    Serial.println("CFG: Starting dual-storage save...");
    pruneEmptySlotsBeforeSave();

    String jsonStr;
    serializeJson(config, jsonStr);
    uint32_t curHash = computeHash32(jsonStr);

    if (!force && lastSavedHash != 0 && curHash == lastSavedHash) {
        isDirty = false;
        lastDirtyMark = 0;
        lastSave = now;
        Serial.println(F("CFG: No changes (hash match)"));
        configUnlock();
        return true;
    }

    bool nvsSuccess = true;
    bool lfsSuccess = true;

    nvs.begin("esp32_config", false);

    if (config.containsKey("wifi")) {
        String wifiJson;
        serializeJson(config["wifi"], wifiJson);
        // Write-only-on-change: the stored value is authoritative; a redundant
        // putString costs heap churn + flash wear for zero benefit.
        if (nvsStringNeedsWrite(nvs.getString(NVS_KEY_WIFI, "").c_str(), wifiJson.c_str())) {
        size_t written = nvs.putString(NVS_KEY_WIFI, wifiJson);
        if (written == 0) {
            Serial.println(F("CFG: ❌ Failed to save WiFi to NVS"));
            nvsSuccess = false;
        } else {
            nvs.end();
            delay(10);
            nvs.begin("esp32_config", true);
            String verify = nvs.getString(NVS_KEY_WIFI, "");
            nvs.end();
            nvs.begin("esp32_config", false);

            if (verify != wifiJson) {
                Serial.println(F("CFG: 🚨 CRITICAL: WiFi verification FAILED - NVS corrupt!"));
                nvsSuccess = false;
            } else {
                nvsIncrementWriteCount(wifiJson.length());
                Serial.println(F("CFG: ✅ WiFi saved & verified"));
            }
        }
        }
    }

    if (config.containsKey("saved_networks")) {
        String networksJson;
        serializeJson(config["saved_networks"], networksJson);
        // Write-only-on-change (see wifi block above).
        if (nvsStringNeedsWrite(nvs.getString(NVS_KEY_SAVED_NETWORKS, "").c_str(), networksJson.c_str())) {
        size_t written = nvs.putString(NVS_KEY_SAVED_NETWORKS, networksJson);
        if (written == 0) {
            Serial.println(F("CFG: ❌ CRITICAL: Saved Networks NOT saved - WiFi fallback disabled!"));
            nvsSuccess = false;
        } else {
            nvs.end();
            delay(10);
            nvs.begin("esp32_config", true);
            String verify = nvs.getString(NVS_KEY_SAVED_NETWORKS, "");
            nvs.end();
            nvs.begin("esp32_config", false);

            if (verify != networksJson) {
                Serial.println(F("CFG: 🚨 CRITICAL: SavedNetworks verification FAILED!"));
                nvsSuccess = false;
            } else {
                nvsIncrementWriteCount(networksJson.length());
                Serial.println(F("CFG: ✅ Saved Networks saved & verified"));
            }
        }
        }
    }

    // SMTP/Email now persist only to LittleFS (smtp_email.g<slot>.json) in saveToLittleFS().
    // Password is saved separately below.

    // RS232 now persists only to LittleFS (/config/rs232.json) in saveToLittleFS().

    // Save passwords to NVS (LittleFS stores masked "********" for these fields).
    // Only store the real value; skip if empty or still carrying the mask sentinel.
    auto savePasswordNVS = [&](const char* nvsKey, const char* section, const char* field) {
        if (!config.containsKey(section)) return;
        JsonVariant v = config[section][field];
        if (v.isNull()) return;
        String pwd = v.as<String>();
        if (pwd.length() == 0 || pwd == "********") return;
        // Write-only-on-change: skip the putString when the stored secret matches.
        if (!nvsStringNeedsWrite(nvs.getString(nvsKey, "").c_str(), pwd.c_str())) return;
        bool ok = nvs.putString(nvsKey, pwd);
        if (ok) {
            nvsIncrementWriteCount(pwd.length());
        } else {
            Serial.printf("CFG: ❌ Failed to save %s to NVS\n", nvsKey);
            nvsSuccess = false;
        }
    };
    savePasswordNVS(NVS_KEY_WEB_PWD,    "web",          "password");
    savePasswordNVS(NVS_KEY_TELNET_PWD, "telnet",       "password");
    savePasswordNVS(NVS_KEY_AP_PWD,     "access_point", "password");
    savePasswordNVS(NVS_KEY_SMTP_PWD,   "smtp",         "password");
    savePasswordNVS(NVS_KEY_FTP_PWD,    "ftp",          "password");

    if (nvs.isKey("config")) nvs.remove("config");
    String oldPartsStr = nvs.isKey("config_parts") ? nvs.getString("config_parts", "") : String("");
    if (oldPartsStr.length() > 0) {
        int oldParts = oldPartsStr.toInt();
        for (int j = 0; j < oldParts; j++) {
            String key = String("config_part_") + String(j);
            if (nvs.isKey(key.c_str())) nvs.remove(key.c_str());
        }
        nvs.remove("config_parts");
        Serial.printf("CFG: Cleaned %d old NVS chunks\n", oldParts);
    }

    nvs.end();
    delay(50);

    lfsSuccess = saveToLittleFS();
    if (!lfsSuccess) {
        Serial.println(F("CFG: ⚠️ LittleFS save failed (non-critical data may be lost)"));
    }

    bool finalSuccess = false;
    if (!nvsSuccess && !lfsSuccess) {
        Serial.println(F("CFG: 🚨 CRITICAL: Total save failure (NVS + LittleFS)"));
        sendVirtualMessage(F("🚨 CRITICAL: Config save FAILED"), 1, true, true);
        triggerScreenNVSWarning(100);
        finalSuccess = false;
    } else if (!nvsSuccess) {
        Serial.println(F("CFG: 🚨 CRITICAL: NVS save failed - boot config NOT persisted!"));
        Serial.println(F("CFG: ⚠️ Device may not reconnect after reboot"));
        sendVirtualMessage(F("🚨 CRITICAL: WiFi config NOT saved"), 1, true, true);
        triggerScreenNVSWarning(100);
        finalSuccess = false;
    } else if (!lfsSuccess) {
        Serial.println(F("CFG: ⚠️ WARNING: LittleFS save failed (non-critical data lost)"));
        Serial.println(F("CFG: ℹ️ WiFi safe in NVS - device will boot normally"));
        if (lfsFailStems.length()) {
            Serial.printf("CFG: ❌ Failed stem(s): %s\n", lfsFailStems.c_str());
            String wmsg = F("⚠️ Save failed: ");
            wmsg += lfsFailStems;
            wmsg += F(" (LittleFS)");
            sendVirtualMessage(wmsg, 0, true, true);   // bypassDedup: repeated failures still show on the web monitor
        } else {
            sendVirtualMessage(F("⚠️ Config partially saved (LittleFS)"), 0, true, true);
        }
        finalSuccess = true;
    } else {
        Serial.println(F("CFG: ✅ Dual-storage save completed & verified"));
        finalSuccess = true;
    }

    if (finalSuccess) {
        isDirty = false;
        lastDirtyMark = 0;
        lastSave = now;
        lastSavedHash = curHash;
        successfulSaveCount++;

        _cache_sdcard_enabled = config["sdcard"]["enabled"] | true;
        _cache_sdcard_flush_interval = clampFlushInterval(config["sdcard"]["flush_interval"] | String("30m"));
        _cache_sd_auto_cleanup = config["sdcard"]["auto_cleanup"] | true;
        _cache_sd_cleanup_threshold = config["sdcard"]["cleanup_threshold"] | 70;
        _cache_sd_keep_days = config["sdcard"]["keep_days"] | 0;
        _cache_telnet_enabled = config["telnet"]["enabled"] | false;
        _cache_wifi_disabled = config["wifi"]["runtime_disabled"] | false;
        _cache_boot_log_enabled = config["boot_log"]["enabled"] | false;
        _cache_web_auth_enabled = config["web"]["auth_enabled"] | false;
    }

    configUnlock();
    return finalSuccess;
}

bool ConfigManager::saveSection(const char* stem) {
#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // TOUCH7: never write flash inside a request (the RGB bounce ISR would panic
    // on the cache suspend). The data is already in RAM; defer to the hidden
    // background flush (ConfigManager::loop) which runs when the screen is off.
    (void)stem;
    markDirtyAndScheduleSave();
    return true;
#else
#if FILESYSTEM_PROVIDER != 1
    return false;
#else
    if (!configLock(pdMS_TO_TICKS(5000))) {
        Serial.printf("CFG: saveSection(%s) blocked (mutex timeout)\n", stem);
        return false;
    }
    if (!littlefsBegin()) {
        configUnlock();
        return false;
    }
    struct stat st;
    if (!(stat("/littlefs/config", &st) == 0 && S_ISDIR(st.st_mode))) {
        LittleFS.mkdir(LITTLEFS_CONFIG_DIR);
        delay(10);
    }
    auto stemPath = [](const char* s) -> String {
        return String("/config/") + s + ".json";
    };
    auto writeJsonAtomic = [&](const String& finalPath, JsonVariantConst payload, const char* label, size_t maxBytes) -> bool {
        if (maxBytes > 0) {
            String sizeCheck; serializeJson(payload, sizeCheck);
            if (sizeCheck.length() > maxBytes) {
                Serial.printf("CFG: %s too large (%u > %u bytes) — skipping\n", label, (unsigned)sizeCheck.length(), (unsigned)maxBytes);
                return false;
            }
        }
        String tmpPath = finalPath + ".tmp";
        LittleFS.remove(tmpPath);
        File f = LittleFS.open(tmpPath, "w");
        if (!f) { Serial.printf("CFG: Failed to open %s.tmp\n", label); return false; }
        size_t bytes = serializeJsonPretty(payload, f); f.close(); delay(50);
        if (bytes == 0) { LittleFS.remove(tmpPath); return false; }
        // Verify the tmp actually parses as JSON before it can replace anything.
        // A truncated write (bytes > 0 but an incomplete document) used to be
        // renamed over the previous file and corrupt the section on next load —
        // the origin of the recurring alerts.json corruption. On failure the
        // original final file is kept and the tmp is dropped.
        {
            DynamicJsonDocument probe(maxBytes > 0 ? (maxBytes * 2 + 256) : 16384);
            File vf = LittleFS.open(tmpPath, "r");
            bool parseOk = false;
            if (vf) {
                parseOk = (deserializeJson(probe, vf) == DeserializationError::Ok);
                vf.close();
            }
            if (!parseOk) {
                Serial.printf("CFG: %s.tmp failed JSON verification - aborting, keeping original\n", label);
                LittleFS.remove(tmpPath);
                return false;
            }
        }
        flashIncrementWriteCount(bytes);
        // Atomic replace: littlefs rename will not overwrite an existing target,
        // so the final file is removed first (before the rename loop), guaranteed
        // before the first rename attempt.
        if (LittleFS.exists(finalPath)) {
            LittleFS.remove(finalPath);
            delay(5);
        }
        bool renamed = false;
        for (int r = 0; r < 2 && !renamed; r++) {
            if (LittleFS.rename(tmpPath, finalPath)) { renamed = true; }
            else { LittleFS.remove(finalPath); delay(10); }
        }
        if (!renamed) { Serial.printf("CFG: Failed to rename %s.tmp (tmp kept)\n", label); return false; }
        Serial.printf("CFG: Saved %s (%u bytes)\n", label, (unsigned)bytes);
        return true;
    };
    auto writePagesReg = [&]() {
        DynamicJsonDocument regDoc(4096);
        JsonArray pages = regDoc.createNestedArray("pages");
        auto addPage = [&](const char* s, size_t mb) { JsonObject p = pages.createNestedObject(); p["stem"] = s; p["max_bytes"] = (uint32_t)mb; };
        addPage("core", 10240); addPage("rs232", 8192); addPage("recipients", 4096);
        addPage("schedule", 4096); addPage("modbus", 4096); addPage("access", 8192);
        addPage("system", 4096); addPage("usb_server", 4096); addPage("smtp_email", 4096); addPage("alerts", 4096);
        addPage("telnet", 4096); addPage("ethernet", 4096); addPage("sdcard", 4096);
        addPage("debug", 4096); addPage("access_point", 4096); addPage("wifi_hybrid", 4096); addPage("do", 4096);
        addPage("ftp", 4096);
        addPage("sensor_config", 4096); addPage("sensor_mq135", 4096); addPage("sensor_hx711", 4096);
        addPage("sensor_ina226", 4096); addPage("sensor_sound", 4096); addPage("sensor_max31865", 4096);
        addPage("sensor_proximity", 4096); addPage("sensor_prox_counters", 4096);
        addPage("sensor_bh1750", 4096); addPage("touch7", 4096);
        String regTmp = String(LITTLEFS_PAGES_REG_PATH) + ".tmp";
        File f = LittleFS.open(regTmp, "w");
        if (f) { size_t rb = serializeJsonPretty(regDoc, f); f.close();
            if (rb > 0) { LittleFS.remove(LITTLEFS_PAGES_REG_PATH); LittleFS.rename(regTmp, LITTLEFS_PAGES_REG_PATH); }
            else { LittleFS.remove(regTmp); } }
    };
    bool ok = true;
    // Keep the per-stem hash in sync after a direct section save, so the next
    // full save does not rewrite a file that is already up to date.
    auto refreshStemHash = [&](const char* s) {
        StemIndex idx = STEM_CORE;
        if      (strcmp(s, "core") == 0)        idx = STEM_CORE;
        else if (strcmp(s, "system") == 0)      idx = STEM_SYSTEM;
        else if (strcmp(s, "smtp_email") == 0)  idx = STEM_SMTP_EMAIL;
        else if (strcmp(s, "rs232") == 0)       idx = STEM_RS232;
        else if (strcmp(s, "access") == 0)      idx = STEM_ACCESS;
        else if (strcmp(s, "alerts") == 0)      idx = STEM_ALERT;
        else if (strcmp(s, "telnet") == 0)      idx = STEM_TELNET;
        else if (strcmp(s, "ethernet") == 0)    idx = STEM_ETHERNET;
        else if (strcmp(s, "sdcard") == 0)      idx = STEM_SDCARD;
        else if (strcmp(s, "debug") == 0)       idx = STEM_DEBUG;
        else if (strcmp(s, "access_point") == 0) idx = STEM_ACCESS_POINT;
        else if (strcmp(s, "wifi_hybrid") == 0) idx = STEM_WIFI_HYBRID;
        else if (strcmp(s, "do") == 0)          idx = STEM_DO;
        else if (strcmp(s, "ftp") == 0)         idx = STEM_FTP;
        else if (strcmp(s, "recipients") == 0)  idx = STEM_RECIPIENTS;
        else if (strcmp(s, "schedule") == 0)    idx = STEM_SCHEDULE;
        else if (strcmp(s, "modbus") == 0)      idx = STEM_MODBUS;
        else if (strcmp(s, "usb_server") == 0)  idx = STEM_USB_SERVER;
        else return;
        File rf = LittleFS.open(stemPath(s), "r");
        if (rf) {
            String content;
            content.reserve((size_t)rf.size() + 1);  // known size: avoid realloc churn while reading
            while (rf.available()) content += (char)rf.read();
            rf.close();
            stemHash[idx] = computeHash32(content);
        }
        stemHashReady[idx] = true;
    };
    if (strcmp(stem, "wifi") == 0) {
        // WiFi + saved_networks vivem no NVS (boot-critical)
        bool nvsOk = true;
        Preferences p;
        if (p.begin("esp32_config", false)) {
            if (config.containsKey("wifi")) {
                String wifiJson; serializeJson(config["wifi"], wifiJson);
                if (!p.putString(NVS_KEY_WIFI, wifiJson)) nvsOk = false;
            }
            if (config.containsKey("saved_networks")) {
                String netsJson; serializeJson(config["saved_networks"], netsJson);
                if (!p.putString(NVS_KEY_SAVED_NETWORKS, netsJson)) nvsOk = false;
            }
            // Password in dedicated NVS key
            if (config.containsKey("web")) {
                String pwd = config["web"]["password"] | "";
                if (pwd.length() && pwd != "********") p.putString(NVS_KEY_WEB_PWD, pwd);
            }
            p.end();
        } else { nvsOk = false; }
        ok = nvsOk;
    } else if (strcmp(stem, "core") == 0) {
        DynamicJsonDocument doc(10240);
        JsonObject root = doc.to<JsonObject>();
        for (JsonPair kv : config.as<JsonObject>()) {
            const char* key = kv.key().c_str();
            if (!isSafeConfigJsonKey(key)) continue;
            if (strcmp(key, "wifi") != 0 && strcmp(key, "saved_networks") != 0 &&
                strcmp(key, "smtp") != 0 && strcmp(key, "email") != 0 &&
                strcmp(key, "rs232") != 0 && strcmp(key, "modbus") != 0 &&
                strcmp(key, "web") != 0 && strcmp(key, "printer") != 0 &&
                strcmp(key, "ntp") != 0 &&
                strcmp(key, "system_name") != 0 && strcmp(key, "system_language") != 0 &&
                strcmp(key, "inactivity_alerts") != 0 &&
                strcmp(key, "telnet") != 0 && strcmp(key, "ethernet") != 0 &&
                strcmp(key, "sdcard") != 0 && strcmp(key, "debug") != 0 &&
                strcmp(key, "access_point") != 0 && strcmp(key, "wifi_hybrid") != 0 &&
                strcmp(key, "do") != 0 && strcmp(key, "ftp") != 0 &&
                strcmp(key, "recipients_slots") != 0 && strcmp(key, "numeric_rules") != 0 &&
                strcmp(key, "sched_cmd") != 0 && strcmp(key, "sched_dt") != 0 &&
                strcmp(key, "sched_cmd_active") != 0 && strcmp(key, "sched_dt_active") != 0 &&
                strcmp(key, "sched_cmd_migrated") != 0 && strcmp(key, "sched_dt_migrated") != 0) {
                JsonVariant out = root[key]; copyJsonSanitized(kv.value(), out);
            }
        }
        if (root.containsKey("telnet") && root["telnet"].containsKey("password") && root["telnet"]["password"].as<String>().length() > 0) root["telnet"]["password"] = "********";
        if (root.containsKey("web") && root["web"].containsKey("password") && root["web"]["password"].as<String>().length() > 0) root["web"]["password"] = "********";
        if (root.containsKey("access_point") && root["access_point"].containsKey("password") && root["access_point"]["password"].as<String>().length() > 0) root["access_point"]["password"] = "********";
        ok = writeJsonAtomic(stemPath("core"), doc.as<JsonVariantConst>(), "core", 10240);
    } else if (strcmp(stem, "system") == 0) {
        DynamicJsonDocument sysDoc(8192);
        JsonObject root = sysDoc.to<JsonObject>();
        if (config.containsKey("printer") && config["printer"].is<JsonObject>()) root["printer"] = config["printer"].as<JsonObject>();
        if (config.containsKey("system_name")) root["system_name"] = config["system_name"];
        if (config.containsKey("system_language")) root["system_language"] = config["system_language"];
        if (config.containsKey("mdns_enabled")) root["mdns_enabled"] = config["mdns_enabled"];
        if (config.containsKey("system") && config["system"].is<JsonObject>()) {
            JsonObject out = root.createNestedObject("system");
            for (JsonPair kv : config["system"].as<JsonObject>()) {
                const char* k = kv.key().c_str();
                if (!isSafeConfigJsonKey(k)) continue;
                out[k] = kv.value();
            }
        }
        ok = writeJsonAtomic(stemPath("system"), sysDoc.as<JsonVariantConst>(), "system", 8192);
    } else if (strcmp(stem, "smtp_email") == 0) {
        DynamicJsonDocument smtpDoc(4096);
        JsonObject root = smtpDoc.to<JsonObject>();
        if (config.containsKey("smtp") && config["smtp"].is<JsonObject>()) {
            JsonObject out = root.createNestedObject("smtp");
            for (JsonPair kv : config["smtp"].as<JsonObject>()) {
                if (strcmp(kv.key().c_str(), "password") == 0) {
                    String pwd = kv.value().as<String>();
                    out["password"] = (pwd.length() > 0) ? "********" : "";
                } else { out[kv.key()] = kv.value(); }
            }
        }
        if (config.containsKey("email") && config["email"].is<JsonObject>()) root["email"] = config["email"].as<JsonObject>();
        ok = writeJsonAtomic(stemPath("smtp_email"), smtpDoc.as<JsonVariantConst>(), "smtp_email", 4096);
    } else if (strcmp(stem, "rs232") == 0) {
        if (config.containsKey("rs232") && config["rs232"].is<JsonObject>())
            ok = writeJsonAtomic(stemPath("rs232"), config["rs232"].as<JsonVariantConst>(), "rs232", 8192);
    } else if (strcmp(stem, "access") == 0) {
        DynamicJsonDocument accessDoc(8192);
        JsonObject root = accessDoc.to<JsonObject>();
        if (config.containsKey("web") && config["web"].is<JsonObject>()) {
            JsonObject webOut = root.createNestedObject("web");
            for (JsonPair kv : config["web"].as<JsonObject>()) {
                if (!isKnownWebAccessKey(kv.key().c_str())) continue;   // never persist bloat
                webOut[kv.key()] = kv.value();
            }
            if (webOut.containsKey("password") && webOut["password"].as<String>().length() > 0) webOut["password"] = "********";
        }
        JsonObject sys = root.createNestedObject("system");
        sys["charts_page_enabled"] = config["system"]["charts_page_enabled"] | false;
        sys["charts_exempt_sessions"] = config["system"]["charts_exempt_sessions"] | false;
        sys["serial_monitor_enabled"] = config["system"]["serial_monitor_enabled"] | false;
        ok = writeJsonAtomic(stemPath("access"), accessDoc.as<JsonVariantConst>(), "access", 8192);
    } else if (strcmp(stem, "alerts") == 0) {
        // Inactivity alert — own file, independent of recipients/core.
        if (config.containsKey("inactivity_alerts") && config["inactivity_alerts"].is<JsonObject>()) {
            // Sanitize the WHOLE section before serialize (all string fields in
            // every slot), not just the emails the POST handler already strips.
            // A raw control byte in any field is enough to make the re-serialized
            // file unparseable — this is the origin of the recurring corruption.
            DynamicJsonDocument alertDoc(8192);
            JsonObject root = alertDoc.to<JsonObject>();
            copyJsonCleanControlChars(config["inactivity_alerts"], root);
            ok = writeJsonAtomic(stemPath("alerts"), root, "alerts", 8192);
        }
        else ok = true;   // nothing to save — treat as success
    } else if (strcmp(stem, "telnet") == 0 || strcmp(stem, "access_point") == 0) {
        // Sections with a password — mask before writing to its own file.
        DynamicJsonDocument d(4096);
        JsonObject o = d.to<JsonObject>();
        if (config.containsKey(stem) && config[stem].is<JsonObject>()) {
            for (JsonPair kv : config[stem].as<JsonObject>()) {
                if (strcmp(kv.key().c_str(), "password") == 0 && kv.value().as<String>().length() > 0) o["password"] = "********";
                else o[kv.key()] = kv.value();
            }
        }
        ok = writeJsonAtomic(stemPath(stem), d.as<JsonVariantConst>(), stem, 4096);
    } else if (strcmp(stem, "ethernet") == 0 || strcmp(stem, "debug") == 0 ||
               strcmp(stem, "sdcard") == 0 || strcmp(stem, "wifi_hybrid") == 0) {
        ok = (config.containsKey(stem) && config[stem].is<JsonObject>())
            ? writeJsonAtomic(stemPath(stem), config[stem].as<JsonVariantConst>(), stem, 4096)
            : true;
    } else if (strcmp(stem, "do") == 0) {
        ok = (config.containsKey("do") && config["do"].is<JsonObject>())
            ? writeJsonAtomic(stemPath("do"), config["do"].as<JsonVariantConst>(), "do", 4096)
            : true;
    } else if (strcmp(stem, "ftp") == 0) {
        DynamicJsonDocument fd(4096);
        JsonObject fo = fd.to<JsonObject>();
        if (config.containsKey("ftp") && config["ftp"].is<JsonObject>()) {
            for (JsonPair kv : config["ftp"].as<JsonObject>()) {
                if (strcmp(kv.key().c_str(), "password") == 0 && kv.value().as<String>().length() > 0) fo["password"] = "********";
                else fo[kv.key()] = kv.value();
            }
        }
        ok = writeJsonAtomic(stemPath("ftp"), fd.as<JsonVariantConst>(), "ftp", 4096);
    } else if (strcmp(stem, "recipients") == 0) {
        ok = writeJsonAtomic(stemPath("recipients"), recipDoc.as<JsonVariantConst>(), "recipients", 8192);
    } else if (strcmp(stem, "schedule") == 0) {
        ok = writeJsonAtomic(stemPath("schedule"), schedDoc.as<JsonVariantConst>(), "schedule", 8192);
    } else if (strcmp(stem, "modbus") == 0) {
        ok = writeJsonAtomic(stemPath("modbus"), modbusDoc.as<JsonVariantConst>(), "modbus", 4096);
    } else if (strcmp(stem, "usb_server") == 0) {
        DynamicJsonDocument usbDoc(4096);
        JsonObject usbRoot = usbDoc.to<JsonObject>();
        if (config.containsKey("usb_server") && config["usb_server"].is<JsonObject>())
            usbRoot["usb_server"] = config["usb_server"].as<JsonObject>();
        ok = writeJsonAtomic(stemPath("usb_server"), usbDoc.as<JsonVariantConst>(), "usb_server", 4096);
    } else {
        ok = false;
    }
    if (ok) { refreshStemHash(stem); writePagesReg(); }
    configUnlock();
    return ok;
#endif // FILESYSTEM_PROVIDER
#endif // TARGET_BOARD_S3_TOUCH7
}
