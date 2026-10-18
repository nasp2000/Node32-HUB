#include "../../languages/lang_active.h"
#include "commands_handler.h"
#include "../../feature_flags.h"

#if ENABLE_SCHEDULER == 1 || ENABLE_DAILY_REBOOT == 1

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "../../ConfigManager.h"
#include "../../observability.h"
#include "../../runtime_scheduler.h"

extern ConfigManager configManager;

namespace {

String getParam(const String& src, const char* key) {
    String search = String(key) + "=";
    int idx = src.indexOf(search);
    if (idx < 0) return String();
    int start = idx + search.length();
    int end = src.indexOf('&', start);
    return end < 0 ? src.substring(start) : src.substring(start, end);
}

int getParamInt(const String& src, const char* key, int fallback) {
    String v = getParam(src, key);
    return v.length() ? v.toInt() : fallback;
}

String escapeJsonStr(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        switch (c) {
            case '\\': out += F("\\\\"); break;
            case '"': out += F("\\\""); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default: out += c; break;
        }
    }
    return out;
}

} // namespace

#if ENABLE_SCHEDULER == 1
void buildScheduledDatetimeSlotsJson(HttpResponse& res) {
    String json = F("{\"status\":\"ok\",\"slots\":[");
    for (int i = 1; i <= 8; ++i) {
        String command, date, timeValue, templ, type;
        bool enabled = false, daily = false;
        bool ok = configManager.getSchedDtSlot(i, command, date, timeValue, templ, enabled, type, daily);
        if (i > 1) json += ',';
        json += F("{\"i\":"); json += String(i);
        json += F(",\"command\":\""); json += escapeJsonStr(ok ? command : String(""));
        json += F("\",\"date\":\""); json += escapeJsonStr(ok ? date : String(""));
        json += F("\",\"time\":\""); json += escapeJsonStr(ok ? timeValue : String(""));
        json += F("\",\"template\":\""); json += escapeJsonStr(ok ? templ : String(""));
        json += F("\",\"enabled\":"); json += (ok && enabled) ? F("true") : F("false");
        json += '}';
    }
    json += F("]}");
    res.sendJson(json);
}

void handleScheduledDatetimeSlotsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    buildScheduledDatetimeSlotsJson(res);
}

void handleScheduledDatetimeSlotGet(HttpRequest& req, HttpResponse& res) {
    int slotIndex = getParamInt(req.query, "i", -1);
    if (slotIndex < 1 || slotIndex > 16) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"indice invalido\"}"));
        return;
    }

    String command, date, timeValue, templ, type;
    bool enabled = false, daily = false;
    bool ok = configManager.getSchedDtSlot(slotIndex, command, date, timeValue, templ, enabled, type, daily);

    if (!ok) {
        command = ""; date = ""; timeValue = ""; templ = "";
        type = "rs232"; enabled = false; daily = false;
    }
    if (!type.length()) type = "rs232";

    String out = F("{\"status\":\"ok\",\"command\":\"");
    out += escapeJsonStr(command);
    out += F("\",\"date\":\""); out += escapeJsonStr(date);
    out += F("\",\"time\":\""); out += escapeJsonStr(timeValue);
    out += F("\",\"template\":\""); out += escapeJsonStr(templ);
    out += F("\",\"type\":\""); out += escapeJsonStr(type);
    out += F("\",\"daily\":"); out += daily ? F("true") : F("false");
    out += F(",\"enabled\":"); out += enabled ? F("true") : F("false");
    out += '}';
    res.sendJson(out);
}

void handleScheduledDatetimeSlotPost(HttpRequest& req, HttpResponse& res) {
    const String& src = req.body.length() ? req.body : req.query;

    int slotIndex = getParamInt(src, "i", -1);
    if (slotIndex < 1) slotIndex = getParamInt(src, "sd_i", -1);
    if (slotIndex < 1 || slotIndex > 16) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"indice\"}"));
        return;
    }

    String command = getParam(src, "command");
    if (!command.length()) command = getParam(src, "sd_cmd");
    String date = getParam(src, "date");
    if (!date.length()) date = getParam(src, "sd_date");
    String timeValue = getParam(src, "time");
    if (!timeValue.length()) timeValue = getParam(src, "sd_time");
    String templ = getParam(src, "template");
    if (!templ.length()) templ = getParam(src, "sd_tpl");
    String type = getParam(src, "type");
    if (!type.length()) type = getParam(src, "sd_type");
    String dailyValue = getParam(src, "daily");
    if (!dailyValue.length()) dailyValue = getParam(src, "sd_daily");
    String enabledValue = getParam(src, "enabled");
    if (!enabledValue.length()) enabledValue = getParam(src, "sd_en");

    bool enabled = (enabledValue == "1" || enabledValue == "true" || enabledValue == "on");
    bool daily = (dailyValue == "1" || dailyValue == "true" || dailyValue == "on");
    if (!type.length()) type = "rs232";

    if (enabled) {
        if (!command.length()) {
            res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"command vazio\"}"));
            return;
        }
        if (!(date.length() == 10 && date.charAt(4) == '-' && date.charAt(7) == '-')) {
            res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"data invalida\"}"));
            return;
        }
        if (!(timeValue.length() == 5 && timeValue.charAt(2) == ':')) {
            res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"hora invalida\"}"));
            return;
        }
    }

    if (!configManager.setSchedDtSlot(slotIndex, command, date, timeValue, templ, enabled, type, daily)) {
        res.send(500, "application/json", F("{\"status\":\"err\",\"msg\":\"gravar\"}"));
        return;
    }

    resetSchedDtSlotTimer(slotIndex);
    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("schedule");
    char cfgMsg[192];
    snprintf(cfgMsg, sizeof(cfgMsg), "slot=%d type=%s daily=%s cmd=%.96s", slotIndex, type.c_str(), daily ? "yes" : "no", command.c_str());
    observabilityLogConfigChange("/api/scheduled-datetime-slot", cfgMsg, savedOk);
    if (!savedOk) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveSched) + F(" por data\",\"status\":\"err\",\"msg\":\"save\"}"));
        return;
    }
    res.sendJson(F("{\"status\":\"ok\"}"));
}
#endif // ENABLE_SCHEDULER

void handleDailyRebootGet(HttpRequest& req, HttpResponse& res) {
    int slotIndex = getParamInt(req.query, "i", -1);
    if (slotIndex < 1 || slotIndex > 2) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"indice\"}"));
        return;
    }

    String timeValue;
    bool enabled = false;
    bool ok = configManager.getDailyRebootSlot(slotIndex, timeValue, enabled);
    if (!ok) {
        Preferences prefs;
        if (prefs.begin("esp32_config", true)) {
            String raw;
            if (prefs.isKey("cfg_drbt")) raw = prefs.getString("cfg_drbt", "");
            prefs.end();
            if (raw.length() > 0) {
                SpiRamJsonDocument tmp(512);
                DeserializationError err = deserializeJson(tmp, raw);
                if (!err && tmp.is<JsonObject>()) {
                    String key = String("s") + slotIndex;
                    if (tmp.containsKey(key) && tmp[key].is<JsonObject>()) {
                        JsonObject obj = tmp[key];
                        if (obj.containsKey("time")) timeValue = String(obj["time"].as<const char*>());
                        if (obj.containsKey("enabled")) enabled = obj["enabled"].as<bool>();
                        ok = true;
                    }
                }
            }
        }
        if (!ok) {
            timeValue = (slotIndex == 1) ? String("00:00") : String("");
            enabled = (slotIndex == 1);
        }
    }

    // Slot 1 follows the OEE shift layout when shifts are configured: report
    // the effective fire time so the page shows (and tracks) it automatically.
    // storedTime keeps the persisted value for no-fire diagnostics.
    String storedTime = timeValue;
    bool followsShifts = (slotIndex == 1) && dailyRebootFollowsShifts();
    char effStr[8] = {0};
    if (followsShifts) {
        int effH = 0, effM = 0;
        dailyRebootEffectiveHM(&effH, &effM);
        snprintf(effStr, sizeof(effStr), "%02d:%02d", effH, effM);
        timeValue = String(effStr);
    }

    String out = F("{\"status\":\"ok\",\"i\":"); out += String(slotIndex);
    out += F(",\"time\":\""); out += escapeJsonStr(timeValue);
    out += F("\",\"stored_time\":\""); out += escapeJsonStr(storedTime);
    out += F("\",\"enabled\":"); out += enabled ? F("true") : F("false");
    if (followsShifts) {
        out += F(",\"follows_shifts\":true,\"effective_time\":\"");
        out += effStr;
        out += F("\"");
    }
    out += F(",\"cfg_mem\":"); out += String((unsigned long)configManager.configDocUsage());
    out += F(",\"has_dr\":"); out += configManager.configHasKey("daily_reboot") ? F("true") : F("false");
    { const auto& _cd = configManager.configDoc();
      bool _isObj = _cd["daily_reboot"].is<JsonObject>();
      out += F(",\"dr_isobj\":"); out += _isObj ? F("true") : F("false");
      bool _hasS1 = _isObj && _cd["daily_reboot"].as<JsonObjectConst>().containsKey("s1");
      out += F(",\"has_s1\":"); out += _hasS1 ? F("true") : F("false");
      bool _s1ok = false;
      if (_hasS1) { JsonObjectConst _o = _cd["daily_reboot"]["s1"].as<JsonObjectConst>();
        _s1ok = _o.containsKey("time") && _o.containsKey("enabled"); }
      out += F(",\"s1_ok\":"); out += _s1ok ? F("true") : F("false"); }
    { String drd; serializeJson(configManager.configDoc()["daily_reboot"], drd);
      out += F(",\"dr_dump\":\""); out += escapeJsonStr(drd.substring(0, 200)); out += F("\""); }
    { Preferences _pr; String _raw;
      if (_pr.begin("esp32_config", true)) { _raw = _pr.getString("cfg_drbt", ""); _pr.end(); }
      out += F(",\"nvs_len\":"); out += String(_raw.length());
      out += F(",\"nvs_dump\":\""); out += escapeJsonStr(_raw.substring(0, 200)); out += F("\""); }
    out += '}';
    res.sendJson(out);
}

void handleDailyRebootPost(HttpRequest& req, HttpResponse& res) {
    const String& src = req.body.length() ? req.body : req.query;

    int slotIndex = getParamInt(src, "i", -1);
    if (slotIndex < 1 || slotIndex > 2) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"indice\"}"));
        return;
    }

    String timeValue = getParam(src, "time");
    timeValue.trim();
    String enabledValue = getParam(src, "enabled");
    bool enabled = (enabledValue == "1" || enabledValue == "true" || enabledValue == "on" || enabledValue == "Sim" || enabledValue == "sim" || enabledValue == "YES" || enabledValue == "Yes" || enabledValue == "y" || enabledValue == "Y");

    if (enabled) {
        bool okFmt = (timeValue.length() == 5 && timeValue.charAt(2) == ':' &&
                      isDigit((char)timeValue[0]) && isDigit((char)timeValue[1]) &&
                      isDigit((char)timeValue[3]) && isDigit((char)timeValue[4]));
        if (!okFmt) {
            res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"hora\"}"));
            return;
        }
        int hh = (timeValue[0] - '0') * 10 + (timeValue[1] - '0');
        int mm = (timeValue[3] - '0') * 10 + (timeValue[4] - '0');
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59) {
            res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"hora\"}"));
            return;
        }
    }

    if (!configManager.setDailyRebootSlot(slotIndex, timeValue, enabled)) {
        res.send(500, "application/json", F("{\"status\":\"err\",\"msg\":\"gravar\"}"));
        return;
    }

    configManager.markDirtyAndScheduleSave();
    // daily_reboot vive no doc `config` (core.json); o flush em background (loop)
    // grava o core completo — sem isso o boot seguinte recarregava core.json velho
    // por cima da NVS (Tarefa 1 2026-10-08: slot perdia-se no reboot).
    bool savedOk = true;
    resetDailyRebootSlotTimer(slotIndex);
    char cfgMsg[96];
    snprintf(cfgMsg, sizeof(cfgMsg), "slot=%d time=%s enabled=%s", slotIndex, timeValue.c_str(), enabled ? "yes" : "no");
    observabilityLogConfigChange("/api/daily-reboot", cfgMsg, savedOk);
    if (!savedOk) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveReboot) + F("\",\"status\":\"err\",\"msg\":\"save\"}"));
        return;
    }

    String storedTime = timeValue;
    bool storedEnabled = enabled;
    Preferences prefs;
    if (prefs.begin("esp32_config", true)) {
        String raw;
        if (prefs.isKey("cfg_drbt")) raw = prefs.getString("cfg_drbt", "");
        prefs.end();
        if (raw.length() > 0) {
            SpiRamJsonDocument tmp(512);
            DeserializationError err = deserializeJson(tmp, raw);
            if (!err && tmp.is<JsonObject>()) {
                String key = String("s") + slotIndex;
                if (tmp.containsKey(key) && tmp[key].is<JsonObject>()) {
                    JsonObject obj = tmp[key];
                    if (obj.containsKey("time")) storedTime = String(obj["time"].as<const char*>());
                    if (obj.containsKey("enabled")) storedEnabled = obj["enabled"].as<bool>();
                }
            }
        }
    }

    String out = F("{\"status\":\"ok\",\"i\":"); out += String(slotIndex);
    out += F(",\"time\":\""); out += escapeJsonStr(storedTime);
    out += F("\",\"enabled\":"); out += storedEnabled ? F("true") : F("false");
    out += '}';
    res.sendJson(out);
}

#if ENABLE_SCHEDULER == 1
void handleScheduledCommandSlotsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String json = F("{\"status\":\"ok\",\"slots\":[");
    for (int i = 1; i <= 30; ++i) {
        String command, unit, days, templ, type;
        int interval = 0;
        bool enabled = false;
        bool ok = configManager.getSchedCmdSlot(i, command, interval, unit, days, templ, enabled, type);
        if (i > 1) json += ',';
        json += F("{\"i\":"); json += String(i);
        json += F(",\"command\":\""); json += escapeJsonStr(ok ? command : String(""));
        json += F("\",\"interval\":"); json += String(ok ? interval : 0);
        json += F(",\"unit\":\""); json += escapeJsonStr(ok ? unit : String("m"));
        json += F("\",\"days\":\""); json += escapeJsonStr(ok ? days : String(""));
        json += F("\",\"template\":\""); json += escapeJsonStr(ok ? templ : String(""));
        json += F("\",\"enabled\":"); json += (ok && enabled) ? F("true") : F("false");
        json += '}';
    }
    json += F("]}");
    res.sendJson(json);
}

void handleScheduledCommandSlotGet(HttpRequest& req, HttpResponse& res) {
    int slotIndex = getParamInt(req.query, "i", -1);
    if (slotIndex < 1 || slotIndex > 30) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"idx\"}"));
        return;
    }

    String command, unit, days, templ, type;
    int interval = 0;
    bool enabled = false;
    if (!configManager.getSchedCmdSlot(slotIndex, command, interval, unit, days, templ, enabled, type)) {
        res.send(404, "application/json", F("{\"status\":\"err\",\"msg\":\"slot\"}"));
        return;
    }

    if (type.length() == 0) type = "rs232";
    String out = F("{\"status\":\"ok\",\"command\":\"");
    out += escapeJsonStr(command);
    out += F("\",\"interval\":"); out += String(interval);
    out += F(",\"unit\":\""); out += escapeJsonStr(unit);
    out += F("\",\"days\":\""); out += escapeJsonStr(days);
    out += F("\",\"template\":\""); out += escapeJsonStr(templ);
    out += F("\",\"type\":\""); out += escapeJsonStr(type);
    out += F("\",\"enabled\":"); out += enabled ? F("true") : F("false");
    out += '}';
    res.sendJson(out);
}

void handleScheduledCommandSlotPost(HttpRequest& req, HttpResponse& res) {
    const String& src = req.body.length() ? req.body : req.query;

    int slotIndex = getParamInt(src, "i", -1);
    if (slotIndex < 1 || slotIndex > 30) {
        res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"idx\"}"));
        return;
    }

    String command = getParam(src, "command");
    int interval = getParamInt(src, "interval", 0);
    String unit = getParam(src, "unit");
    if (unit == "s" || unit == F("seconds")) unit = F("seconds");
    else if (unit == "h" || unit == F("hours")) unit = F("hours");
    else unit = F("minutes");

    String days = getParam(src, "days");
    String templ = getParam(src, "template");
    String type = getParam(src, "type");
    if (type.length() == 0) type = "rs232";

    String enabledValue = getParam(src, "enabled");
    bool enabled = (enabledValue == "1" || enabledValue == "true");
    if (enabled) {
        if (command.length() == 0) {
            res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"cmd\"}"));
            return;
        }
        if (interval <= 0) {
            res.send(400, "application/json", F("{\"status\":\"err\",\"msg\":\"interval\"}"));
            return;
        }
    }

    if (!configManager.setSchedCmdSlot(slotIndex, command, interval, unit, days, templ, enabled, type)) {
        res.send(500, "application/json", F("{\"status\":\"err\",\"msg\":\"gravar\"}"));
        return;
    }

    resetSchedCmdSlotTimer(slotIndex);
    configManager.markDirtyAndScheduleSave();
    bool savedOk = configManager.saveSection("schedule");
    char cfgMsg[176];
    snprintf(cfgMsg, sizeof(cfgMsg), "slot=%d type=%s interval=%d cmd=%.96s", slotIndex, type.c_str(), interval, command.c_str());
    observabilityLogConfigChange("/api/scheduled-command-slot", cfgMsg, savedOk);
    if (!savedOk) {
        res.send(500, "application/json", String(F("{\"success\":false,\"message\":\"")) + L(activeLang->apiErrSaveSched) + F("\",\"status\":\"err\",\"msg\":\"save\"}"));
        return;
    }
    res.sendJson(F("{\"status\":\"ok\"}"));
}
#endif // ENABLE_SCHEDULER

#else
void handleScheduledDatetimeSlotsGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
void handleScheduledDatetimeSlotGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
void handleScheduledDatetimeSlotPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
void handleDailyRebootGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
void handleDailyRebootPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
void handleScheduledCommandSlotsGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
void handleScheduledCommandSlotGet(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
void handleScheduledCommandSlotPost(HttpRequest& req, HttpResponse& res) { (void)req; res.send(404, "application/json", F("{\"error\":\"Scheduler not enabled\"}")); }
#endif
