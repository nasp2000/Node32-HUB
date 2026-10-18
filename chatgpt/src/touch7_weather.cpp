#include "touch7_weather.h"
#include "languages/lang_active.h"

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "psram_helpers.h"
#include "touch7_config.h"
#include "touch7_display.h"
#include "touch7_peers_logic.h"
#include "touch7_geo.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>

#define TOUCH7_WM_INTERVAL_MS (10UL * 60UL * 1000UL)

struct T7WeatherSnapshot {
    uint32_t generation;
    float tempC;
    float humidity;
    float pressureHpa;
    int code;
    bool isDay;
    char condition[48];
    char place[64];
    char sunset[16];
    char today[160];
    char days[5][96];
};

struct T7WeatherRequest {
    float latitude;
    float longitude;
    uint32_t generation;
    char place[64];
};

static portMUX_TYPE s_weatherMux = portMUX_INITIALIZER_UNLOCKED;
static T7WeatherSnapshot s_live = {};
static T7WeatherSnapshot s_pending = {};
static T7WeatherRequest s_request = {};
static bool s_hasData = false;
static bool s_refreshing = false;
static bool s_workerActive = false;
static bool s_resultReady = false;
static bool s_pendingOk = false;
static bool s_stale = false;
static bool s_hasTried = false;
static bool s_forceRefresh = true;
static uint32_t s_generation = 1;
static uint32_t s_lastOkMs = 0;
static uint32_t s_lastTryMs = 0;
static char s_lastError[64] = "";
static char s_pendingError[64] = "";

static void copyText(char* dst, size_t size, const char* src) {
    if (!dst || size == 0) return;
    if (!src) src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

// Compact day-name table (weekday from daily.time[i] "YYYY-MM-DD").
static String dayName(const char* iso) {
    if (!iso || strlen(iso) < 10) return String("---");
    int y = (iso[0]-'0')*1000 + (iso[1]-'0')*100 + (iso[2]-'0')*10 + (iso[3]-'0');
    int m = (iso[5]-'0')*10 + (iso[6]-'0');
    int d = (iso[8]-'0')*10 + (iso[9]-'0');
    // Zeller-ish: days since civil (Howard Hinnant) -> weekday.
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp = (unsigned)(m + (m > 2 ? -3 : 9));
    unsigned doy = (153 * mp + 2) / 5 + (unsigned)(d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    unsigned days = (unsigned)era * 146097 + doe - 719468;
    unsigned wd = (days + 4) % 7; // 0 = domingo
    static const char* names[] = {"DOM","SEG","TER","QUA","QUI","SEX","SAB"};
    return String(names[wd % 7]);
}

static const char* weatherConditionName(int code) {
    if (code == 0) return activeLang->touch7SkyClear;
    if (code == 1) return activeLang->touch7WxMostlyClear;
    if (code == 2) return activeLang->touch7WxPartlyCloudy;
    if (code == 3) return activeLang->touch7WxCloudy;
    if (code == 45 || code == 48) return activeLang->touch7WxFog;
    if (code >= 51 && code <= 57) return activeLang->touch7WxDrizzle;
    if (code >= 61 && code <= 67) return activeLang->touch7WxRain;
    if (code >= 71 && code <= 77) return activeLang->touch7WxSnow;
    if (code >= 80 && code <= 82) return activeLang->touch7WxShowers;
    if (code == 85 || code == 86) return activeLang->touch7WxSnowShowers;
    if (code >= 95 && code <= 99) return activeLang->touch7WxThunder;
    return activeLang->touch7SkyVariable;
}

static bool fetchForecast(const T7WeatherRequest& request, T7WeatherSnapshot& result,
                          char* error, size_t errorSize) {
    memset(&result, 0, sizeof(result));
    result.generation = request.generation;
    if (WiFi.status() != WL_CONNECTED) {
        copyText(error, errorSize, "WiFi disconnected");
        return false;
    }
    char coord[64];
    snprintf(coord, sizeof(coord), "?latitude=%.4f&longitude=%.4f",
             (double)request.latitude, (double)request.longitude);
    String url = String("https://api.open-meteo.com/v1/forecast") + String(coord) +
        String("&current=temperature_2m,relative_humidity_2m,surface_pressure,"
        "weather_code,is_day&daily=weather_code,temperature_2m_max,"
        "temperature_2m_min,sunset&timezone=auto&forecast_days=6");
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(8000);
    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(10000);
    if (!http.begin(cli, url)) {
        copyText(error, errorSize, "Open-Meteo connection failed");
        return false;
    }
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        snprintf(error, errorSize, "Open-Meteo HTTP %d", code);
        return false;
    }
    String payload = http.getString();
    http.end();
    SpiRamJsonDocument doc(8192);
    if (deserializeJson(doc, payload) || doc["current"].isNull() || doc["daily"]["time"].isNull()) {
        copyText(error, errorSize, "Invalid weather response");
        return false;
    }

    float t = (float)(doc["current"]["temperature_2m"] | 0.0);
    int h = (int)(doc["current"]["relative_humidity_2m"] | 0);
    float pressure = (float)(doc["current"]["surface_pressure"] | 0.0);
    int wc = (int)(doc["current"]["weather_code"] | 3);
    int isd = (int)(doc["current"]["is_day"] | 1);
    const char* sunset = doc["daily"]["sunset"][0] | "--:--";
    int todayCode = (int)(doc["daily"]["weather_code"][0] | 3);
    float todayMax = (float)(doc["daily"]["temperature_2m_max"][0] | 0.0);
    float todayMin = (float)(doc["daily"]["temperature_2m_min"][0] | 0.0);
    copyText(result.condition, sizeof(result.condition), weatherConditionName(wc));
    copyText(result.place, sizeof(result.place), request.place);
    copyText(result.sunset, sizeof(result.sunset), sunset);
    result.tempC = t;
    result.humidity = (float)h;
    result.pressureHpa = pressure;
    result.code = wc;
    result.isDay = (isd != 0);
    char today[160];
    snprintf(today, sizeof(today), L(activeLang->touch7TodayForecast).c_str(),
             todayMin, todayMax, weatherConditionName(todayCode), sunset);
    copyText(result.today, sizeof(result.today), today);
    for (int i = 1; i <= 5; i++) {
        const char* dt = doc["daily"]["time"][i] | "";
        int dwc = (int)(doc["daily"]["weather_code"][i] | 3);
        float mx = (float)(doc["daily"]["temperature_2m_max"][i] | 0.0);
        float mn = (float)(doc["daily"]["temperature_2m_min"][i] | 0.0);
        char line[96];
        snprintf(line, sizeof(line), "%s  %.0f/%.0fC  %s",
                 dayName(dt).c_str(), mn, mx, weatherConditionName(dwc));
        copyText(result.days[i - 1], sizeof(result.days[i - 1]), line);
    }
    return true;
}

static void weatherFetchTask(void*) {
    T7WeatherRequest request;
    portENTER_CRITICAL(&s_weatherMux);
    request = s_request;
    portEXIT_CRITICAL(&s_weatherMux);

    T7WeatherSnapshot result;
    char error[64] = "";
    bool ok = fetchForecast(request, result, error, sizeof(error));

    portENTER_CRITICAL(&s_weatherMux);
    if (request.generation == s_generation) {
        s_pending = result;
        s_pendingOk = ok;
        copyText(s_pendingError, sizeof(s_pendingError), error);
        s_resultReady = true;
    } else {
        // Location changed during the request; discard its result and schedule
        // a new fetch for the newer coordinates.
        s_forceRefresh = true;
        s_refreshing = false;
    }
    s_workerActive = false;
    portEXIT_CRITICAL(&s_weatherMux);
    vTaskDelete(nullptr);
}

void touch7WeatherBegin() {
    portENTER_CRITICAL(&s_weatherMux);
    s_lastTryMs = 0;
    s_lastOkMs = 0;
    s_hasTried = false;
    s_forceRefresh = true;
    s_generation++;
    s_hasData = false;
    s_refreshing = false;
    s_workerActive = false;
    s_resultReady = false;
    s_pendingOk = false;
    s_stale = false;
    s_lastError[0] = '\0';
    s_pendingError[0] = '\0';
    memset(&s_live, 0, sizeof(s_live));
    memset(&s_pending, 0, sizeof(s_pending));
    portEXIT_CRITICAL(&s_weatherMux);
}

void touch7WeatherInvalidate() {
    // Config changed (especially the chosen coordinates): bypass the regular
    // ten-minute cache window and let the loop task fetch the new location.
    portENTER_CRITICAL(&s_weatherMux);
    s_generation++;
    if (s_generation == 0) s_generation = 1;
    s_lastOkMs = 0;
    s_forceRefresh = true;
    s_stale = s_hasData;
    s_lastError[0] = '\0';
    portEXIT_CRITICAL(&s_weatherMux);
}

void touch7WeatherTick() {
    if (touch7FlashHeld()) return;

    T7WeatherSnapshot readySnapshot;
    bool applyReady = false;
    bool readyOk = false;
    const uint32_t nowMs = millis();
    portENTER_CRITICAL(&s_weatherMux);
    if (s_resultReady) {
        readyOk = s_pendingOk;
        if (s_pending.generation == s_generation && readyOk) {
            s_live = s_pending;
            s_hasData = true;
            s_stale = false;
            s_lastOkMs = nowMs;
            s_lastError[0] = '\0';
            readySnapshot = s_live;
            applyReady = true;
        } else if (s_pending.generation == s_generation) {
            s_stale = s_hasData;
            copyText(s_lastError, sizeof(s_lastError), s_pendingError);
        } else {
            s_forceRefresh = true;
        }
        s_resultReady = false;
        s_refreshing = false;
    }
    portEXIT_CRITICAL(&s_weatherMux);

    // All LVGL calls stay on the loop task; the network worker only prepares
    // a fixed-size snapshot in the background.
    if (applyReady) {
        touch7Display.setWeatherNow(readySnapshot.tempC, readySnapshot.humidity,
                                    readySnapshot.pressureHpa, readySnapshot.code,
                                    readySnapshot.isDay, String(readySnapshot.condition),
                                    String(readySnapshot.place), String(readySnapshot.sunset));
        String days[5];
        for (int i = 0; i < 5; i++) days[i] = readySnapshot.days[i];
        touch7Display.setForecast(String(readySnapshot.today), days);
    }
    (void)readyOk;

    if (!touch7ConfigManager.isInitialized() || WiFi.status() != WL_CONNECTED) return;

    // Snapshot config between two generation reads. If a location update
    // overlaps this copy, its generation change cancels this scheduling pass.
    uint32_t generationBefore;
    portENTER_CRITICAL(&s_weatherMux);
    generationBefore = s_generation;
    portEXIT_CRITICAL(&s_weatherMux);
    const Touch7Config& cfg = touch7ConfigManager.getConfig();
    T7WeatherRequest request;
    memset(&request, 0, sizeof(request));
    request.latitude = cfg.wx_lat;
    request.longitude = cfg.wx_lon;
    copyText(request.place, sizeof(request.place), cfg.wx_place);

    bool startWorker = false;
    portENTER_CRITICAL(&s_weatherMux);
    if (generationBefore == s_generation && !s_refreshing && !s_workerActive && !s_resultReady) {
        bool due = s_forceRefresh;
        if (!due) {
            if (s_hasData) due = (uint32_t)(nowMs - s_lastOkMs) >= TOUCH7_WM_INTERVAL_MS;
            else due = !s_hasTried || (uint32_t)(nowMs - s_lastTryMs) >= 60000UL;
            if (due && s_hasTried && (uint32_t)(nowMs - s_lastTryMs) < 60000UL) due = false;
        }
        if (due) {
            request.generation = s_generation;
            s_request = request;
            s_lastTryMs = nowMs;
            s_hasTried = true;
            s_forceRefresh = false;
            s_refreshing = true;
            s_workerActive = true;
            startWorker = true;
        }
    }
    portEXIT_CRITICAL(&s_weatherMux);

    if (startWorker && xTaskCreatePinnedToCore(weatherFetchTask, "t7weather", 12288,
                                                nullptr, 1, nullptr, 0) != pdPASS) {
        portENTER_CRITICAL(&s_weatherMux);
        s_workerActive = false;
        s_refreshing = false;
        s_stale = s_hasData;
        copyText(s_lastError, sizeof(s_lastError), "Weather task unavailable");
        portEXIT_CRITICAL(&s_weatherMux);
    }
}

bool touch7WeatherHasData() {
    portENTER_CRITICAL(&s_weatherMux);
    bool value = s_hasData;
    portEXIT_CRITICAL(&s_weatherMux);
    return value;
}
int touch7WeatherCode() {
    portENTER_CRITICAL(&s_weatherMux);
    int value = s_live.code;
    portEXIT_CRITICAL(&s_weatherMux);
    return value;
}
bool touch7WeatherIsDay() {
    portENTER_CRITICAL(&s_weatherMux);
    bool value = s_live.isDay;
    portEXIT_CRITICAL(&s_weatherMux);
    return value;
}
String touch7WeatherToday() {
    char text[sizeof(s_live.today)];
    portENTER_CRITICAL(&s_weatherMux);
    copyText(text, sizeof(text), s_live.today);
    portEXIT_CRITICAL(&s_weatherMux);
    return String(text);
}
void touch7WeatherDays(String out[5]) {
    char copy[5][96];
    portENTER_CRITICAL(&s_weatherMux);
    memcpy(copy, s_live.days, sizeof(copy));
    portEXIT_CRITICAL(&s_weatherMux);
    for (int i = 0; i < 5; i++) out[i] = String(copy[i]);
}

void touch7WeatherGetStatus(Touch7WeatherStatus& out) {
    uint32_t now = millis();
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    bool reportWifiError = false;
    portENTER_CRITICAL(&s_weatherMux);
    out.hasData = s_hasData;
    out.refreshing = s_refreshing;
    out.ageSeconds = s_hasData ? (uint32_t)(now - s_lastOkMs) / 1000UL : 0;
    out.stale = s_stale || (s_hasData && out.ageSeconds >= TOUCH7_WM_INTERVAL_MS / 1000UL);
    copyText(out.lastError, sizeof(out.lastError), s_lastError);
    reportWifiError = !wifiConnected && (s_forceRefresh || out.stale || !s_hasData) && !out.lastError[0];
    portEXIT_CRITICAL(&s_weatherMux);
    if (reportWifiError) copyText(out.lastError, sizeof(out.lastError), "WiFi disconnected");
}

// ---- Geo fetch (raw payloads; parsing via touch7_geo.h, unit-tested) ----

bool touch7GeoSearchFetch(const String& name, String& payload) {
    if (WiFi.status() != WL_CONNECTED) return false;
    char enc[128];
    touch7GeoUrlEncode(name.c_str(), enc, sizeof(enc));
    if (enc[0] == 0) return false;
    String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") +
        String(enc) + String("&count=5&language=pt&format=json");
    WiFiClientSecure cli;
    cli.setInsecure();
    cli.setTimeout(8000);
    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(10000);
    if (!http.begin(cli, url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    payload = http.getString();
    http.end();
    return payload.length() > 0 && payload.length() < 8192;
}

bool touch7GeoAutoFetch(String& payload) {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    http.setReuse(false);
    http.setTimeout(10000);
    if (!http.begin("http://ip-api.com/json/?fields=status,message,city,country,lat,lon")) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    payload = http.getString();
    http.end();
    return payload.length() > 0 && payload.length() < 2048;
}

#endif // SELECTED_PACK == PACK_S3_TOUCH7
