#include "../../languages/lang_active.h"
#include "sensor_handler.h"

#include <Arduino.h>

#include "../../config_manager_runtime.h"
#include "../../coordinator_hooks.h"
#include "../../sensors/sensor_config.h"
#include "../../sensor_history_merge.h"

#include "feature_flags.h"
#if ENABLE_MQ135==1
#include "../../sensors/sensor_mq135.h"
#include "../../sensors/sensor_mq135_config.h"
#include "feature_flags.h"
#endif
#if ENABLE_HX711==1
#include "../../sensors/sensor_hx711_config.h"
#endif
#if ENABLE_INA226==1
#include "../../sensors/sensor_ina226_config.h"
#endif
#if ENABLE_MAX31865==1
#include "../../sensors/sensor_max31865_config.h"
#endif
#if ENABLE_SOUND_SENSOR==1
#include "../../sensors/sensor_sound_config.h"
#endif

// ---- Print adapter: appends to a String (heap-friendly for bounded responses) -----
struct _StringPrint : public Print {
    String* s;
    _StringPrint(String& str) : s(&str) {}
    size_t write(uint8_t c) override { *s += (char)c; return 1; }
    size_t write(const uint8_t* buf, size_t len) override {
        for (size_t i = 0; i < len; i++) *s += (char)buf[i];
        return len;
    }
};

// ===== Chart Session State ==========================================================
#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
namespace {
struct ChartSession {
    String sessionId;
    unsigned long lastActivity;
    unsigned long registeredAt;
    bool isPriority;
};
ChartSession _chartSessions[2] = {{"", 0, 0, false}, {"", 0, 0, false}};
} // namespace

static const unsigned long _kPrioritySessionMs = 15UL * 60UL * 1000UL;
static const unsigned long _kSecondarySessionMs = 5UL * 60UL * 1000UL;

static void _cleanupChartSessions() {
    unsigned long now = millis();
    bool noKick = configManager.getChartsExemptSessions();
    for (int i = 0; i < 2; ++i) {
        if (_chartSessions[i].sessionId.length() == 0) continue;
        // "Local processing pages never expire": with the option on, chart
        // sessions are never cleaned up by timeout (previously they still
        // expired after _kPrioritySessionMs of inactivity).
        if (noKick) continue;
        bool expired = _chartSessions[i].isPriority
            ? ((now - _chartSessions[i].lastActivity) > _kPrioritySessionMs)
            : ((now - _chartSessions[i].registeredAt) > _kSecondarySessionMs);
        if (expired) {
            _chartSessions[i] = {"", 0, 0, false};
        }
    }
}
#endif

// ===== /api/sensor-flags =============================================================
void handleSensorFlagsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    String json = "{";
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    json += "\"aht20_bmp280\":";
    json += sensorConfigManager.getConfig().sensor_enabled ? "true" : "false";
#else
    json += "\"aht20_bmp280\":false";
#endif
    json += ",\"mq135\":";
#if ENABLE_MQ135==1
    json += mq135ConfigManager.isEnabled() ? "true" : "false";
#else
    json += "false";
#endif
    json += "}";
    res.sendJson(json);
}

// ===== AHT20 / BMP280 (ambient sensor) ===============================================
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
void handleSensorConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(sensorConfigManager.toJsonString());
}

void handleSensorConfigPost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    // TouchMeteo-7: save() erases/writes LittleFS (SPI1) — hold rendering
    // so no PSRAM traffic races the RGB DMA (cache panic, 14-09-2026).
    extern void touch7FlashHold(bool hold);
    touch7FlashHold(true);
#endif
    bool success = sensorConfigManager.fromJsonString(req.body) && sensorConfigManager.save();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    touch7FlashHold(false);
#endif
    if (success) {
#if ENABLE_BUZZER == 1
        buzzerOnSave();
#endif
        res.sendJson("{\"success\":true}");
    } else {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"Failed to save config\"}");
    }
}

void handleSensorMeasurePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool success = sensorConfigManager.performReading();
    if (success) {
        res.sendJson(sensorConfigManager.toJsonString());
    } else {
        res.send(400, "application/json", "{\"success\":false,\"error\":\"Failed to read sensor\"}");
    }
}

void handleSensorResetStatsPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    sensorConfigManager.resetStats();
    res.sendJson("{\"success\":true}");
}

void handleSensorSlotsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    const SensorConfig& cfg = sensorConfigManager.getConfig();
    String json = "[";
    for (uint8_t slot = 0; slot < EXTRA_SENSOR_SLOTS; ++slot) {
        if (slot > 0) json += ",";
        json += "{\"slot\":" + String(slot);
        json += ",\"enabled\":" + String(cfg.extra_slots[slot].enabled ? "true" : "false");
        json += ",\"name\":\"" + String(cfg.extra_slots[slot].name) + "\"";
        json += ",\"label\":\"" + String(cfg.extra_slots[slot].label) + "\"";
        json += ",\"unit\":\"" + String(cfg.extra_slots[slot].unit) + "\"";
        json += ",\"scale\":" + String(cfg.extra_slots[slot].scale, 1);
        json += "}";
    }
    json += "]";
    res.sendJson(json);
}

void handleSensorFlushPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool ok = sensorConfigManager.getHistory().periodicFlushToSD(sensorConfigManager.getConfig());
    String json = ok ? "{\"ok\":true,\"msg\":\"Dados guardados no SD\"}"
                     : "{\"ok\":false,\"msg\":\"Sem dados novos ou SD n\\u00e3o dispon\\u00edvel\"}";
    res.sendJson(json);
}
#endif

// ===== Chart capabilities =============================================================
void handleChartCapsGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    bool hasAht20     = (ENABLE_AHT20_BMP280 == 1 && ENABLE_SENSOR_CONFIG_PAGE == 1);
    bool hasGas       = (ENABLE_MQ135 == 1);
    bool hasSensors   = hasAht20 || hasGas;
    bool hasRS232     = (ENABLE_RS232_CHARTS == 1);
    bool hasModbus    = (ENABLE_MODBUS == 1 && ENABLE_RS232_CHARTS == 1 && ENABLE_SDCARD == 1);
    bool hasProximity = (ENABLE_PROXIMITY == 1 && ENABLE_PROXIMITY_CHARTS == 1);
    String json = "{\"sensors\":";
    json += hasSensors ? "true" : "false";
    json += ",\"aht20\":";
    json += hasAht20 ? "true" : "false";
    json += ",\"gas\":";
    json += hasGas ? "true" : "false";
    json += ",\"rs232\":";
    json += hasRS232 ? "true" : "false";
    json += ",\"modbus\":";
    json += hasModbus ? "true" : "false";
    json += ",\"proximity\":";
    json += hasProximity ? "true" : "false";
    json += "}";
    res.sendJson(json);
}

// ===== Chart sessions ================================================================
#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_RS232_CHARTS==1 || ENABLE_MQ135==1
void handleSensorChartsSessionPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    _cleanupChartSessions();
    int slot = -1;
    for (int i = 0; i < 2; ++i) {
        if (_chartSessions[i].sessionId.length() == 0) { slot = i; break; }
    }
    if (slot < 0) {
        res.send(503, "application/json", "{\"error\":\"Max 2 concurrent chart viewers\",\"success\":false}");
        return;
    }
    bool isPriority = true;
    for (int i = 0; i < 2; ++i) {
        if (_chartSessions[i].sessionId.length() > 0 && _chartSessions[i].isPriority) {
            isPriority = false;
            break;
        }
    }
    unsigned long now = millis();
    String sid = String(now) + '-' + String(random(10000, 99999));
    _chartSessions[slot] = {sid, now, now, isPriority};
    unsigned long timeoutSec = isPriority ? (_kPrioritySessionMs / 1000UL) : (_kSecondarySessionMs / 1000UL);
    String json = "{\"success\":true,\"sessionId\":\"" + sid +
                  "\",\"priority\":" + (isPriority ? "true" : "false") +
                  ",\"timeoutSec\":" + String(timeoutSec) + "}";
    res.sendJson(json);
}

void handleSensorChartsHeartbeatPost(HttpRequest& req, HttpResponse& res) {
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    String sid;
    int eq = req.query.indexOf('=');
    int am = req.query.indexOf('&');
    if (eq > 0) {
        int end = (am > eq) ? am : (int)req.query.length();
        sid = req.query.substring(eq + 1, end);
    }
    if (sid.length() == 0) {
        res.send(400, "application/json", "{\"error\":\"Missing sid\"}");
        return;
    }
    unsigned long now = millis();
    for (int i = 0; i < 2; ++i) {
        if (_chartSessions[i].sessionId != sid) continue;
        if (!_chartSessions[i].isPriority) {
            if (!configManager.getChartsExemptSessions()) {
                unsigned long elapsed = now - _chartSessions[i].registeredAt;
                if (elapsed >= _kSecondarySessionMs) {
                    _chartSessions[i] = {"", 0, 0, false};
                    res.sendJson("{\"success\":false,\"kicked\":true,\"remainingSec\":0}");
                    return;
                }
                _chartSessions[i].lastActivity = now;
                String json = "{\"success\":true,\"priority\":false,\"remainingSec\":" +
                              String((_kSecondarySessionMs - elapsed) / 1000UL) + "}";
                res.sendJson(json);
                return;
            }
            _chartSessions[i].lastActivity = now;
            res.sendJson("{\"success\":true,\"priority\":false,\"remainingSec\":900}");
            return;
        }
        _chartSessions[i].lastActivity = now;
        res.sendJson("{\"success\":true,\"priority\":true,\"remainingSec\":900}");
        return;
    }
    res.send(404, "application/json", "{\"error\":\"Session not found\"}");
}

void handleSensorChartsSessionDelete(HttpRequest& req, HttpResponse& res) {
    String sid;
    int eq = req.query.indexOf('=');
    int am = req.query.indexOf('&');
    if (eq > 0) {
        int end = (am > eq) ? am : (int)req.query.length();
        sid = req.query.substring(eq + 1, end);
    }
    if (sid.length() == 0) {
        res.send(400, "application/json", "{\"error\":\"Missing sid\"}");
        return;
    }
    for (int i = 0; i < 2; ++i) {
        if (_chartSessions[i].sessionId == sid) {
            _chartSessions[i] = {"", 0, 0, false};
            res.sendJson("{\"success\":true}");
            return;
        }
    }
    res.send(404, "application/json", "{\"error\":\"Session not found\"}");
}

void handleSensorChartsSessionEndPost(HttpRequest& req, HttpResponse& res) {
    String sid;
    int eq = req.query.indexOf('=');
    int am = req.query.indexOf('&');
    if (eq > 0) {
        int end = (am > eq) ? am : (int)req.query.length();
        sid = req.query.substring(eq + 1, end);
    }
    if (sid.length() > 0) {
        for (int i = 0; i < 2; ++i) {
            if (_chartSessions[i].sessionId == sid) {
                _chartSessions[i] = {"", 0, 0, false};
                break;
            }
        }
    }
    res.send(204, "text/plain", "");
}
#endif

// ===== History streaming =============================================================
#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
void handleSensorHistoryGet(HttpRequest& req, HttpResponse& res) {
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    String period;
    uint32_t fromTs = 0;
    int start = 0;
    while (start < (int)req.query.length()) {
        int amp = req.query.indexOf('&', start);
        String seg = (amp < 0) ? req.query.substring(start) : req.query.substring(start, amp);
        int eq = seg.indexOf('=');
        if (eq > 0) {
            String k = seg.substring(0, eq);
            String v = seg.substring(eq + 1);
            if (k == "period") period = v;
            if (k == "from") fromTs = (uint32_t)strtoul(v.c_str(), nullptr, 10);
        }
        if (amp < 0) break;
        start = amp + 1;
    }
    uint16_t maxPoints = 288;
    if (fromTs == 0) {
        if (period == "1h") maxPoints = 72;
        else if (period == "12h" || period.length() == 0) maxPoints = 144;
        else maxPoints = 288;
    } else {
        time_t nowTs = time(nullptr);
        uint32_t rangeSeconds = (nowTs > (time_t)fromTs) ? (uint32_t)(nowTs - (time_t)fromTs) : 86400UL;
        if (rangeSeconds <= 3600UL) maxPoints = 72;
        else if (rangeSeconds <= 28800UL) maxPoints = 144;
    }
    uint16_t bufCount = 0;
    uint32_t ringOldestTs = 0;
    {
        const SensorHistory& hist = sensorConfigManager.getHistory();
        bufCount = hist.getCount();
        ringOldestTs = hist.oldestTimestamp();
    }
    SensorHistorySource src = sensorHistorySourceFor(fromTs, bufCount, ringOldestTs);
    String buf;
    buf.reserve(8192);
    _StringPrint sp(buf);
    char dateStr[11] = "";
    if (src != kHistSourcePsram) {
        // SD day file for the requested window start (only needed off the live path).
        time_t refTs = (time_t)fromTs;
        struct tm refInfo = {};
        localtime_r(&refTs, &refInfo);
        snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d",
                 refInfo.tm_mday, refInfo.tm_mon + 1, refInfo.tm_year + 1900);
    }
    if (src == kHistSourcePsram) {
        sensorConfigManager.streamHistoryJSON(sp, maxPoints, fromTs);
    } else if (src == kHistSourceMerged) {
        // Ring has post-boot samples but does not reach back to fromTs: backfill
        // the persisted SD head and append the live tail (single response).
        sensorConfigManager.streamMergedWindow(sp, String(dateStr), fromTs, maxPoints);
    } else {
        sensorConfigManager.getHistory().streamDailyWindow(sp, String(dateStr), fromTs, maxPoints,
                                                           &sensorConfigManager.getConfig());
    }
    res.send(200, "application/json", buf);
}

void handleSensorHistoryFilesGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    res.sendJson(sensorConfigManager.getHistory().listDailyFiles());
}

void handleSensorHistoryFileGet(HttpRequest& req, HttpResponse& res) {
    if (!configManager.getChartsPageEnabled()) {
        res.send(404, "application/json", "{\"error\":\"charts disabled\"}");
        return;
    }
    String dateId;
    int eq = req.query.indexOf('=');
    int am = req.query.indexOf('&');
    if (eq > 0) {
        int end = (am > eq) ? am : (int)req.query.length();
        dateId = req.query.substring(eq + 1, end);
    }
    if (dateId.length() == 0) {
        res.send(400, "application/json", "{\"error\":\"Missing date parameter\"}");
        return;
    }
    String buf;
    buf.reserve(16384);
    _StringPrint sp(buf);
    sensorConfigManager.getHistory().streamDailyFile(sp, dateId, &sensorConfigManager.getConfig());
    res.send(200, "application/json", buf);
}
#endif

// ===== MQ-135 ========================================================================
#if ENABLE_MQ135==1
void handleMq135ConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(mq135ConfigManager.toJson());
}

void handleMq135ConfigPost(HttpRequest& req, HttpResponse& res) {
    bool ok = mq135ConfigManager.updateFromJson(req.body);
    if (ok) {
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
        mq135ConfigManager.registerExtraSlots(sensorConfigManager.getConfig());
#endif
        res.sendJson("{\"ok\":true}");
    } else {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\""+L(activeLang->apiErrSaveMq135)+"\"}");
    }
}

void handleMq135MeasurePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    float temperature = NAN;
    float humidity = NAN;
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    const SensorConfig& env = sensorConfigManager.getConfig();
    temperature = env.last_temp_avg;
    humidity = env.last_humidity;
#endif
    mq135ConfigManager.performReading(temperature, humidity);
    res.sendJson(mq135ConfigManager.toJson());
}

void handleMq135CalibrateR0Post(HttpRequest& req, HttpResponse& res) {
    (void)req;
    float temperature = NAN;
    float humidity = NAN;
#if ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1
    const SensorConfig& env = sensorConfigManager.getConfig();
    temperature = env.last_temp_avg;
    humidity = env.last_humidity;
#endif
    mq135ConfigManager.performReading(temperature, humidity);
    float rs = mq135Sensor.getCurrentRs();
    if (rs <= 0.0f || isnan(rs)) {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\""+L(activeLang->apiNoValidRs)+"\"}");
        return;
    }
    mq135Sensor.setR0(rs);
    String body = "{\"r0\":" + String(rs, 4) + "}";
    mq135ConfigManager.updateFromJson(body);
    String json = "{\"ok\":true,\"r0\":" + String(rs, 4) + "}";
    res.sendJson(json);
}
#endif

// ===== HX711 =========================================================================
#if ENABLE_HX711==1
void handleHx711ConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(hx711ConfigManager.toJson());
}

void handleHx711ConfigPost(HttpRequest& req, HttpResponse& res) {
    bool ok = hx711ConfigManager.updateFromJson(req.body);
    if (ok) {
        res.sendJson("{\"ok\":true}");
    } else {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\""+L(activeLang->apiErrSaveHx711)+"\"}");
    }
}

void handleHx711MeasurePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    hx711ConfigManager.performReading();
    res.sendJson(hx711ConfigManager.toJson());
}

void handleHx711TarePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool ok = hx711ConfigManager.performTare();
    if (ok) {
        res.sendJson(hx711ConfigManager.toJson());
    } else {
        res.send(500, "application/json", "{\"ok\":false,\"msg\":\""+L(activeLang->apiTareFail)+"\"}");
    }
}
#endif

// ===== INA226 ========================================================================
#if ENABLE_INA226==1
void handleIna226ConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(ina226ConfigManager.toJson());
}

void handleIna226ConfigPost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    extern void touch7FlashHold(bool hold);
    touch7FlashHold(true);
#endif
    bool ok = ina226ConfigManager.updateFromJson(req.body);
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    touch7FlashHold(false);
#endif
    if (ok) {
        res.sendJson("{\"ok\":true}");
    } else {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\""+L(activeLang->apiErrSaveIna226)+"\"}");
    }
}

void handleIna226MeasurePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    ina226ConfigManager.performReading();
    res.sendJson(ina226ConfigManager.toJson());
}
#endif

// ===== MAX31865 ======================================================================
#if ENABLE_MAX31865==1
void handleMax31865ConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(max31865ConfigManager.toJson());
}

void handleMax31865ConfigPost(HttpRequest& req, HttpResponse& res) {
    bool ok = max31865ConfigManager.updateFromJson(req.body);
    if (ok) {
        res.sendJson("{\"ok\":true}");
    } else {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\""+L(activeLang->apiErrSaveMax31865)+"\"}");
    }
}

void handleMax31865MeasurePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    max31865ConfigManager.performReading();
    res.sendJson(max31865ConfigManager.toJson());
}
#endif

// ===== Sound =========================================================================
#if ENABLE_SOUND_SENSOR==1
void handleSoundConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.sendJson(soundConfigManager.toJson());
}

void handleSoundConfigPost(HttpRequest& req, HttpResponse& res) {
    bool ok = soundConfigManager.updateFromJson(req.body);
    if (ok) {
        res.sendJson("{\"ok\":true}");
    } else {
        res.send(400, "application/json", "{\"ok\":false,\"msg\":\""+L(activeLang->apiErrSaveSound)+"\"}");
    }
}

void handleSoundMeasurePost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    soundConfigManager.performReading();
    res.sendJson(soundConfigManager.toJson());
}
#endif
