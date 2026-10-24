// Handlers for RS232 charts/history APIs.

#include "rs232_history_handler.h"

#include <Arduino.h>

#include <vector>

#include "../../ConfigManager.h"
#include "../../chart_history.h"
#include "../../sensors/proximity_manager.h"
#include "../../feature_flags.h"
#include "../../psram_helpers.h"
#include "../../sdcard.h"
#include "../../structures.h"
#include "heavy_api_request_guard.h"

extern ConfigManager configManager;
#if ENABLE_RS232==1
extern PsramCircularBuffer<Rs232LivePoint> rs232LiveBuffer;
extern SemaphoreHandle_t rs232LiveMutex;
#else
// Stubs: mutex nullptr prevents all live-buffer code paths from executing.
static PsramCircularBuffer<Rs232LivePoint> rs232LiveBuffer(1, true);
static SemaphoreHandle_t rs232LiveMutex = nullptr;
#endif // ENABLE_RS232

namespace {

bool rs232PsramJsonReserve(std::vector<char, PsramAllocatorStd<char>>& buffer, size_t capacity) {
    try {
        if (buffer.capacity() < capacity) buffer.reserve(capacity);
        return true;
    } catch (...) {
        return false;
    }
}

bool rs232PsramJsonAppendRaw(std::vector<char, PsramAllocatorStd<char>>& buffer, const char* text) {
    if (!text) return true;
    while (*text) {
        try {
            buffer.push_back(*text++);
        } catch (...) {
            return false;
        }
    }
    return true;
}

bool rs232PsramJsonAppendEscaped(std::vector<char, PsramAllocatorStd<char>>& buffer, const String& text) {
    for (size_t i = 0; i < text.length(); ++i) {
        char ch = text.charAt(i);
        switch (ch) {
            case '\\': if (!rs232PsramJsonAppendRaw(buffer, "\\\\")) return false; break;
            case '"': if (!rs232PsramJsonAppendRaw(buffer, "\\\"")) return false; break;
            case '\n': if (!rs232PsramJsonAppendRaw(buffer, "\\n")) return false; break;
            case '\r': if (!rs232PsramJsonAppendRaw(buffer, "\\r")) return false; break;
            case '\t': if (!rs232PsramJsonAppendRaw(buffer, "\\t")) return false; break;
            default:
                try {
                    buffer.push_back(ch);
                } catch (...) {
                    return false;
                }
                break;
        }
    }
    return true;
}

bool rs232PsramJsonTerminate(std::vector<char, PsramAllocatorStd<char>>& buffer) {
    try {
        if (buffer.empty() || buffer.back() != '\0') buffer.push_back('\0');
        return true;
    } catch (...) {
        return false;
    }
}

bool rs232SendPsramJson(AsyncWebServerRequest* request, const std::vector<char, PsramAllocatorStd<char>>& buffer) {
    AsyncResponseStream* response = request->beginResponseStream("application/json");
    if (!response) {
        request->send(500, "application/json", "{\"error\":\"no memory\"}");
        return false;
    }
    response->addHeader("Cache-Control", "no-store");
    if (!buffer.empty()) response->write((const uint8_t*)buffer.data(), buffer.size() - 1);
    request->send(response);
    return true;
}

}  // namespace

void registerRs232HistoryRoutes(AsyncWebServer& server, const WebRouteContext& context) {
#if ENABLE_RS232==1 && ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1
    server.on("/api/rs232-cols", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }
        HeavyApiRequestGuard heavyGuard(request, "rs232-cols");
        if (!heavyGuard.begin()) return;

        static std::vector<char, PsramAllocatorStd<char>> cacheJson;
        static char cacheDate[11] = {0};
        static unsigned long cacheMs = 0;
        String requestedDate = request->hasParam("date") ? request->getParam("date")->value() : String("");

        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        char today[11];
        snprintf(today, sizeof(today), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        const char* cacheKeyDate = (requestedDate.length() == 10) ? requestedDate.c_str() : today;
        if (cacheJson.size() > 1 && strcmp(cacheDate, cacheKeyDate) == 0 && (millis() - cacheMs) < 30000UL) {
            rs232SendPsramJson(request, cacheJson);
            return;
        }

        String resolvedDate = (requestedDate.length() == 10) ? requestedDate : String(today);
        String chartPath = chartHistoryRs232PathForDate(configManager.getSystemName(), resolvedDate);

        int cols = 0;
        uint8_t chartCols = 0;
        ChartColumnDef chartDefs[CHART_HISTORY_RS232_MAX_COLUMNS] = {};
        bool useLiveColumns = !request->hasParam("date") && (resolvedDate == String(today));
        if (useLiveColumns && rs232LiveMutex && xSemaphoreTakeRecursive(rs232LiveMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            if (!rs232LiveBuffer.isEmpty()) cols = rs232LiveBuffer.last().valueCount;
            xSemaphoreGiveRecursive(rs232LiveMutex);
        }
        chartHistoryReadRs232Meta(chartPath, chartCols, chartDefs);
        if (cols == 0 && chartCols == 0 && !request->hasParam("date")) {
            int bestRank = -1;
            String bestDate;
            if (sdMutexLock(pdMS_TO_TICKS(3000))) {
                File root = SD_MMC.open("/");
                if (root && root.isDirectory()) {
                    File entry = root.openNextFile();
                    while (entry) {
                        if (entry.isDirectory()) {
                            String name = String(entry.name());
                            int slashPos = name.lastIndexOf('/');
                            String base = name.substring(slashPos + 1);
                            if (base.length() >= 10) {
                                String datePart = base.substring(base.length() - 10);
                                if (datePart.charAt(2) == '-' && datePart.charAt(5) == '-') {
                                    int day = datePart.substring(0, 2).toInt();
                                    int month = datePart.substring(3, 5).toInt();
                                    int year = datePart.substring(6, 10).toInt();
                                    if (day > 0 && month > 0 && year > 2000) {
                                        int rank = (year * 10000) + (month * 100) + day;
                                        if (rank > bestRank) {
                                            bestRank = rank;
                                            bestDate = datePart;
                                        }
                                    }
                                }
                            }
                        }
                        entry.close();
                        entry = root.openNextFile();
                    }
                    root.close();
                }
                sdMutexUnlock();
            }
            if (bestDate.length() == 10) {
                uint8_t fallbackCols = 0;
                ChartColumnDef fallbackDefs[CHART_HISTORY_RS232_MAX_COLUMNS] = {};
                String fallbackPath = chartHistoryRs232PathForDate(configManager.getSystemName(), bestDate);
                if (chartHistoryReadRs232Meta(fallbackPath, fallbackCols, fallbackDefs) && fallbackCols > 0) {
                    resolvedDate = bestDate;
                    chartCols = fallbackCols;
                    memcpy(chartDefs, fallbackDefs, sizeof(fallbackDefs));
                }
            }
        }
        if (cols == 0) cols = chartCols;

        bool mtf1Active = configManager.getAmadaISQ20MTF1Enabled();
        bool awseActive = configManager.getAmadaISQ20AWSEEnabled();
        bool autoDetectStructures = configManager.getStructuresAutoDetectEnabled();
        cacheJson.clear();
        if (!rs232PsramJsonReserve(cacheJson, 512) || !rs232PsramJsonAppendRaw(cacheJson, "{\"cols\":")) {
            request->send(500, "application/json", "{\"error\":\"OOM\"}");
            return;
        }

        char numBuf[16];
        snprintf(numBuf, sizeof(numBuf), "%d", cols);
        if (!rs232PsramJsonAppendRaw(cacheJson, numBuf) ||
            !rs232PsramJsonAppendRaw(cacheJson, ",\"date\":\"") ||
            !rs232PsramJsonAppendEscaped(cacheJson, resolvedDate) ||
            !rs232PsramJsonAppendRaw(cacheJson, "\"")) {
            request->send(500, "application/json", "{\"error\":\"OOM\"}");
            return;
        }

        if (cols > 0) {
            const char** structureNames = nullptr;
            int structureNamesCount = 0;
            const char* structureId = "chart";
            if (!mtf1Active && !awseActive && autoDetectStructures) {
                int detectedCols = chartCols > 0 ? (int)chartCols : cols;
                if (detectedCols >= 16) {
                    awseActive = true;
                } else if (detectedCols == 10 || detectedCols == 12) {
                    mtf1Active = true;
                }
            }
            if (mtf1Active) {
                structureNames = ISQ20_MTF1_SHORT_NAMES;
                structureNamesCount = 12;
                structureId = "amada_isq20_mtf1";
            } else if (awseActive) {
                structureNames = ISQ20_AWSE_SHORT_NAMES;
                structureNamesCount = 21;
                structureId = "amada_isq20_awse";
            }
            if (!rs232PsramJsonAppendRaw(cacheJson, ",\"structure\":\"") ||
                !rs232PsramJsonAppendRaw(cacheJson, structureId) ||
                !rs232PsramJsonAppendRaw(cacheJson, "\",\"col_names\":[")) {
                request->send(500, "application/json", "{\"error\":\"OOM\"}");
                return;
            }
            for (int ci = 0; ci < cols; ++ci) {
                String chosenName;
                if (structureNames && ci < structureNamesCount && structureNames[ci] && structureNames[ci][0]) {
                    chosenName = structureNames[ci];
                } else if (chartCols > ci) {
                    String label = chartDefs[ci].label[0] ? String(chartDefs[ci].label) : String(chartDefs[ci].key);
                    String normalized = label;
                    normalized.trim();
                    String lower = normalized;
                    lower.toLowerCase();
                    String genericCol = String("col") + String(ci + 1);
                    String genericRs232Col = String("rs232 col ") + String(ci + 1);
                    String genericRs232Key = String("rs232_col") + String(ci + 1);
                    String keyLower = String(chartDefs[ci].key);
                    keyLower.toLowerCase();
                    if (normalized.length() > 0 && lower != genericCol && lower != genericRs232Col && lower != genericRs232Key && lower != keyLower) {
                        chosenName = normalized;
                    }
                }
                if (chosenName.length() == 0) chosenName = String("RS232 Col ") + String(ci + 1);
                if ((ci > 0 && !rs232PsramJsonAppendRaw(cacheJson, ",")) ||
                    !rs232PsramJsonAppendRaw(cacheJson, "\"") ||
                    !rs232PsramJsonAppendEscaped(cacheJson, chosenName) ||
                    !rs232PsramJsonAppendRaw(cacheJson, "\"")) {
                    request->send(500, "application/json", "{\"error\":\"OOM\"}");
                    return;
                }
            }
            if (!rs232PsramJsonAppendRaw(cacheJson, "]")) {
                request->send(500, "application/json", "{\"error\":\"OOM\"}");
                return;
            }
        }

        if (!rs232PsramJsonAppendRaw(cacheJson, "}") || !rs232PsramJsonTerminate(cacheJson)) {
            request->send(500, "application/json", "{\"error\":\"OOM\"}");
            return;
        }

        memset(cacheDate, 0, sizeof(cacheDate));
        strncpy(cacheDate, resolvedDate.c_str(), sizeof(cacheDate) - 1);
        cacheMs = millis();
        rs232SendPsramJson(request, cacheJson);
    });

    server.on("/api/rs232-history-files", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }
        HeavyApiRequestGuard heavyGuard(request, "rs232-history-files");
        if (!heavyGuard.begin()) return;

        static std::vector<char, PsramAllocatorStd<char>> cacheJson;
        static unsigned long cacheMs = 0;
        if (cacheJson.size() > 1 && (millis() - cacheMs) < 120000UL) {
            rs232SendPsramJson(request, cacheJson);
            return;
        }

        // RS232 chart files live at /{systemName}/{systemName}_{dateId}.chart —
        // same folder as Modbus/Proximity charts, but without a _MODBUS_ or _PROXIMITY_ infix.
        String safeName = chartHistorySanitizeSystemName(configManager.getSystemName());
        String dir      = "/" + safeName;
        String prefix   = safeName + "_";

        cacheJson.clear();
        if (!rs232PsramJsonReserve(cacheJson, 1024) || !rs232PsramJsonAppendRaw(cacheJson, "[")) {
            request->send(500, "application/json", "{\"error\":\"OOM\"}");
            return;
        }
        bool first = true;
        if (sdcardShouldUseSD() && sdMutexLock(pdMS_TO_TICKS(3000))) {
            File root = SD_MMC.open(dir.c_str());
            if (root && root.isDirectory()) {
                File entry = root.openNextFile();
                while (entry) {
                    if (!entry.isDirectory()) {
                        String fname = String(entry.name());
                        int sl = fname.lastIndexOf('/');
                        String base = sl >= 0 ? fname.substring(sl + 1) : fname;
                        // Match {safeName}_{dateId}.chart, but NOT _MODBUS_ or _PROXIMITY_ variants.
                        if (base.startsWith(prefix) && base.endsWith(".chart") &&
                            base.indexOf("_MODBUS_") < 0 && base.indexOf("_PROXIMITY_") < 0) {
                            String datePart = base.substring(prefix.length(), base.length() - 6);
                            if (datePart.length() == 10 &&
                                datePart.charAt(2) == '-' && datePart.charAt(5) == '-') {
                                if ((!first && !rs232PsramJsonAppendRaw(cacheJson, ",")) ||
                                    !rs232PsramJsonAppendRaw(cacheJson, "\"") ||
                                    !rs232PsramJsonAppendEscaped(cacheJson, datePart) ||
                                    !rs232PsramJsonAppendRaw(cacheJson, "\"")) {
                                    entry.close();
                                    root.close();
                                    sdMutexUnlock();
                                    request->send(500, "application/json", "{\"error\":\"OOM\"}");
                                    return;
                                }
                                first = false;
                            }
                        }
                    }
                    entry.close();
                    vTaskDelay(pdMS_TO_TICKS(2));
                    entry = root.openNextFile();
                    yield();
                }
                root.close();
            }
            sdMutexUnlock();
        }
        if (!rs232PsramJsonAppendRaw(cacheJson, "]") || !rs232PsramJsonTerminate(cacheJson)) {
            request->send(500, "application/json", "{\"error\":\"OOM\"}");
            return;
        }
        cacheMs = millis();
        rs232SendPsramJson(request, cacheJson);
    });

    server.on("/api/rs232-history", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }
        HeavyApiRequestGuard heavyGuard(request, "rs232-history");
        if (!heavyGuard.begin()) return;

        static volatile bool busy = false;
        if (busy) {
            request->send(503, "application/json", "{\"error\":\"busy\"}");
            return;
        }
        busy = true;

        uint32_t fromTs = request->hasParam("from") ? (uint32_t)request->getParam("from")->value().toInt() : 0;
        String period = request->hasParam("period") ? request->getParam("period")->value() : String("");
        String date = request->hasParam("date") ? request->getParam("date")->value() : String("");
        if (date.length() == 0) {
            time_t refTs = fromTs > 0 ? (time_t)fromTs : time(nullptr);
            struct tm refInfo = {};
            localtime_r(&refTs, &refInfo);
            char autoDate[11];
            snprintf(autoDate, sizeof(autoDate), "%02d-%02d-%04d", refInfo.tm_mday, refInfo.tm_mon + 1, refInfo.tm_year + 1900);
            date = String(autoDate);
        }
        if (date.length() != 10 || date.charAt(2) != '-' || date.charAt(5) != '-') {
            busy = false;
            request->send(400, "application/json", "{\"error\":\"Invalid date\"}");
            return;
        }

        // Do NOT override mode to "from" when fromTs is provided — that was blocking
        // the live buffer from ever being used. fromTs is used directly as minTs.
        String mode = request->hasParam("mode") ? request->getParam("mode")->value() : String("day");
        if (mode != "live" && mode != "8h" && mode != "day") mode = "day";
        if (fromTs == 0 && period.length() > 0 && !request->hasParam("mode")) {
            if (period == "1h") mode = "live";
            else if (period == "8h" || period == "12h") mode = "8h";
            else mode = "day";
        }

        String chartPath = chartHistoryRs232PathForDate(configManager.getSystemName(), date);

        time_t minTs = fromTs > 0 ? (time_t)fromTs : 0;
        time_t nowTs = time(nullptr);
        struct tm nowInfo = {};
        localtime_r(&nowTs, &nowInfo);
        char today[11];
        snprintf(today, sizeof(today), "%02d-%02d-%04d", nowInfo.tm_mday, nowInfo.tm_mon + 1, nowInfo.tm_year + 1900);
        // Apply mode-based minTs only when JS didn't supply a fromTs
        if (fromTs == 0 && date == String(today)) {
            if (mode == "live") minTs = nowTs - 3600;
            else if (mode == "8h") minTs = nowTs - 28800;
        }

        // Keep RS232 payloads symmetric with sensor limits and small enough for ESP32.
        uint32_t rangeSeconds = (minTs > 0 && nowTs > minTs) ? (uint32_t)(nowTs - minTs) : 86400UL;
        const int maxOut = (rangeSeconds <= 3600UL) ? 72 : ((rangeSeconds <= 28800UL) ? 144 : 288);
        const int targetOut = maxOut;
        // Merged serving (file history + live ring tail, always minute-fresh —
        // same envelope as before, one stride budget over the union).
        ChartServePoint* raw = (ChartServePoint*)ps_malloc((size_t)1500 * sizeof(ChartServePoint));
        ChartServePoint* tail = (ChartServePoint*)ps_malloc((size_t)512 * sizeof(ChartServePoint));
        if (!raw || !tail) {
            if (raw) free(raw);
            if (tail) free(tail);
            busy = false;
            request->send(500, "application/json", "{\"error\":\"no memory\"}");
            return;
        }
        uint16_t nRaw = chartHistoryReadRawPoints(chartPath, CHART_HISTORY_SOURCE_RS232,
                                                  CHART_HISTORY_RS232_MAX_COLUMNS,
                                                  (uint32_t)minTs, raw, 1500);
        uint16_t nTail = 0;
        uint8_t nCols = 0;
        if (rs232LiveMutex && xSemaphoreTakeRecursive(rs232LiveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            size_t liveSize = rs232LiveBuffer.size();
            for (size_t i = 0; i < liveSize && nTail < 512; i++) {
                const Rs232LivePoint& pt = rs232LiveBuffer[i];
                if (minTs != 0 && (time_t)pt.timestamp < minTs) continue;
                ChartServePoint* q = &tail[nTail++];
                q->timestamp = pt.timestamp;
                q->valueCount = (pt.valueCount > CHART_SERVE_MAX_COLS) ? CHART_SERVE_MAX_COLS : pt.valueCount;
                for (uint8_t c = 0; c < CHART_SERVE_MAX_COLS; c++) {
                    q->values[c] = (c < RS232_LIVE_MAX_COLS) ? pt.values[c] : NAN;
                }
                if (pt.valueCount > nCols) nCols = pt.valueCount;
            }
            xSemaphoreGiveRecursive(rs232LiveMutex);
        }
        if (nCols > CHART_HISTORY_RS232_MAX_COLUMNS) nCols = CHART_HISTORY_RS232_MAX_COLUMNS;
        if (nCols == 0) {
            for (uint16_t i = 0; i < nRaw; i++) {
                if (raw[i].valueCount > nCols) nCols = raw[i].valueCount;
            }
            if (nCols > CHART_HISTORY_RS232_MAX_COLUMNS) nCols = CHART_HISTORY_RS232_MAX_COLUMNS;
        }
        char keys[CHART_HISTORY_RS232_MAX_COLUMNS][16];
        int8_t decs[CHART_HISTORY_RS232_MAX_COLUMNS];
        for (uint8_t c = 0; c < nCols; c++) {
            snprintf(keys[c], sizeof(keys[c]), "rs232_col%u", (unsigned)(c + 1));
            decs[c] = 4;
        }
        uint16_t mCap = nRaw + nTail;
        ChartServePoint* merged = (ChartServePoint*)ps_malloc(((size_t)mCap + 1) * sizeof(ChartServePoint));
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response || !merged) {
            if (merged) free(merged);
            free(raw);
            free(tail);
            busy = false;
            if (!response) request->send(500, "application/json", "{\"error\":\"no memory\"}");
            else request->send(response);
            return;
        }
        response->addHeader("Cache-Control", "no-store");
        uint32_t n = chartMergePoints(raw, nRaw, tail, nTail, merged, mCap + 1);
        chartHistoryServePointsJson(*response, merged, n, keys, decs, nCols, (uint16_t)targetOut);
        free(raw);
        free(tail);
        free(merged);
        busy = false;
        request->send(response);

    // GET /api/modbus-chart-files
    // Returns sorted list of dates (DD-MM-YYYY) for which Modbus .chart files exist on SD.
    server.on("/api/modbus-chart-files", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }
        String safeName = chartHistorySanitizeSystemName(configManager.getSystemName());
        String dir = "/" + safeName;
        String prefix = safeName + "_MODBUS_";
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response) { request->send(500); return; }
        response->addHeader("Cache-Control", "no-store");
        response->print("[");
        bool first = true;
        if (sdcardShouldUseSD() && sdMutexLock(pdMS_TO_TICKS(3000))) {
            File root = SD_MMC.open(dir.c_str());
            if (root && root.isDirectory()) {
                File entry = root.openNextFile();
                while (entry) {
                    String fname = String(entry.name());
                    int sl = fname.lastIndexOf('/');
                    String base = sl >= 0 ? fname.substring(sl + 1) : fname;
                    if (base.startsWith(prefix) && base.endsWith(".chart")) {
                        String datePart = base.substring(prefix.length(), base.length() - 6);
                        if (datePart.length() == 10 &&
                            datePart.charAt(2) == '-' && datePart.charAt(5) == '-') {
                            if (!first) response->print(",");
                            response->print('"');
                            response->print(datePart);
                            response->print('"');
                            first = false;
                        }
                    }
                    entry.close();
                    entry = root.openNextFile();
                }
                root.close();
            }
            sdMutexUnlock();
        }
        response->print("]");
        request->send(response);
    });

    // --- Modbus chart endpoints ---
    // GET /api/modbus-chart-cols?date=DD-MM-YYYY
    // Returns column metadata for the Modbus chart file for the given date.
    server.on("/api/modbus-chart-cols", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }

        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        char today[11];
        snprintf(today, sizeof(today), "%02d-%02d-%04d",
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        String requestedDate = request->hasParam("date") ? request->getParam("date")->value() : String(today);
        String resolvedDate = (requestedDate.length() == 10) ? requestedDate : String(today);

        String chartPath = chartHistoryModbusPathForDate(configManager.getSystemName(), resolvedDate);
        uint8_t colCount = 0;
        ChartColumnDef defs[CHART_HISTORY_MODBUS_MAX_COLUMNS] = {};
        chartHistoryReadModbusMeta(chartPath, colCount, defs);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response) { request->send(500); return; }
        response->addHeader("Cache-Control", "no-store");
        response->print("{\"cols\":");
        response->print(colCount);
        response->print(",\"date\":\"");
        response->print(resolvedDate);
        response->print("\",\"col_names\":[");
        for (uint8_t i = 0; i < colCount; i++) {
            if (i) response->print(",");
            response->print("\"");
            if (defs[i].key[0]) response->print(defs[i].key);
            else { response->print("col_"); response->print(i + 1); }
            response->print("\"");
        }
        response->print("]}");
        request->send(response);
    });

    // GET /api/modbus-chart-data?date=DD-MM-YYYY&from=<epoch>&mode=day|8h|live
    // Streams a Modbus chart file as JSON time-series (same shape as /api/rs232-history).
    server.on("/api/modbus-chart-data", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }
        HeavyApiRequestGuard heavyGuard(request, "modbus-chart-data");
        if (!heavyGuard.begin()) return;

        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        char today[11];
        snprintf(today, sizeof(today), "%02d-%02d-%04d",
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        String requestedDate = request->hasParam("date") ? request->getParam("date")->value() : String(today);
        String resolvedDate = (requestedDate.length() == 10) ? requestedDate : String(today);
        String mode = request->hasParam("mode") ? request->getParam("mode")->value() : String("day");

        uint32_t fromTs = 0;
        if (request->hasParam("from")) {
            fromTs = (uint32_t)strtoul(request->getParam("from")->value().c_str(), nullptr, 10);
        } else {
            // Default window based on mode.
            time_t nowTs = time(nullptr);
            if (mode == "live") fromTs = (uint32_t)(nowTs > 900 ? nowTs - 900 : 0);        // 15 min
            else if (mode == "8h") fromTs = (uint32_t)(nowTs > 28800 ? nowTs - 28800 : 0); // 8 h
            else fromTs = chartHistoryDayStartFromRs232DateId(resolvedDate);                // full day
        }

        uint32_t rangeSeconds = (uint32_t)(time(nullptr) > (time_t)fromTs ? time(nullptr) - (time_t)fromTs : 86400UL);
        const int maxOut = (rangeSeconds <= 3600UL) ? 72 : ((rangeSeconds <= 28800UL) ? 144 : 288);

        String chartPath = chartHistoryModbusPathForDate(configManager.getSystemName(), resolvedDate);
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response) {
            request->send(500, "application/json", "{\"error\":\"no memory\"}");
            return;
        }
        response->addHeader("Cache-Control", "no-store");
        // Merged serving (file history + PSRAM ring tail, always minute-fresh).
        extern bool mbChartServeMergedJson(Print& out, const String& path, uint32_t fromTs, uint16_t maxOut);
        if (!mbChartServeMergedJson(*response, chartPath, fromTs, (uint16_t)maxOut)) {
            request->send(200, "application/json", "{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}");
            return;
        }
        request->send(response);
    });

#endif // ENABLE_RS232==1 && ENABLE_RS232_CHARTS==1 && ENABLE_SDCARD==1

// --- Proximity chart API endpoints ---
#if ENABLE_PROXIMITY_CHARTS==1

    // GET /api/proximity-chart-files
    // Returns sorted list of dates (DD-MM-YYYY) for which proximity .chart files exist on SD.
    // Chart files are stored as /{sdcardName}/{sdcardName}_PROXIMITY_DD-MM-YYYY.chart.
    server.on("/api/proximity-chart-files", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }
        String safeName = chartHistorySanitizeSystemName(configManager.getSystemName());
        String dir = "/" + safeName;
        String prefix = safeName + "_PROXIMITY_";
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response) { request->send(500); return; }
        response->addHeader("Cache-Control", "no-store");
        response->print("[");
        bool first = true;
        if (sdcardShouldUseSD() && sdMutexLock(pdMS_TO_TICKS(3000))) {
            File root = SD_MMC.open(dir.c_str());
            if (root && root.isDirectory()) {
                File entry = root.openNextFile();
                while (entry) {
                    String fname = String(entry.name());
                    int sl = fname.lastIndexOf('/');
                    String base = sl >= 0 ? fname.substring(sl + 1) : fname;
                    // Match {safeName}_PROXIMITY_DD-MM-YYYY.chart
                    if (base.startsWith(prefix) && base.endsWith(".chart")) {
                        String datePart = base.substring(prefix.length(), base.length() - 6);
                        // Validate DD-MM-YYYY (10 chars, dashes at positions 2,5)
                        if (datePart.length() == 10 &&
                            datePart.charAt(2) == '-' && datePart.charAt(5) == '-') {
                            if (!first) response->print(",");
                            response->print('"');
                            response->print(datePart);
                            response->print('"');
                            first = false;
                        }
                    }
                    entry.close();
                    entry = root.openNextFile();
                }
                root.close();
            }
            sdMutexUnlock();
        }
        response->print("]");
        request->send(response);
    });

    // GET /api/proximity-chart-cols?date=DD-MM-YYYY
    // Returns column metadata for the proximity chart file for the given date.
    server.on("/api/proximity-chart-cols", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }

        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        char today[11];
        snprintf(today, sizeof(today), "%02d-%02d-%04d",
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        String requestedDate = request->hasParam("date") ? request->getParam("date")->value() : String(today);
        String resolvedDate  = (requestedDate.length() == 10) ? requestedDate : String(today);

        String chartPath = chartHistoryProximityPathForDate(configManager.getSystemName(), resolvedDate);
        uint8_t colCount = 0;
        ChartColumnDef defs[CHART_HISTORY_PROXIMITY_MAX_COLUMNS] = {};
        chartHistoryReadProximityMeta(chartPath, colCount, defs);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response) { request->send(500); return; }
        response->addHeader("Cache-Control", "no-store");
        response->print("{\"cols\":");
        response->print(colCount);
        response->print(",\"date\":\"");
        response->print(resolvedDate);
        response->print("\",\"col_names\":[");
        for (uint8_t i = 0; i < colCount; i++) {
            if (i) response->print(",");
            response->print("\"");
            if (defs[i].key[0]) response->print(defs[i].key);
            else { response->print("col_"); response->print(i + 1); }
            response->print("\"");
        }
        response->print("]}");
        request->send(response);
    });

    // GET /api/proximity-chart-data?date=DD-MM-YYYY&from=<epoch>&mode=day|8h|live
    // Streams a proximity chart file as JSON time-series.
    server.on("/api/proximity-chart-data", HTTP_GET, [&context](AsyncWebServerRequest* request) {
        if (!context.ensureBasicAuth(request)) return;
        if (!configManager.getChartsPageEnabled()) {
            request->send(404, "application/json", "{\"error\":\"charts disabled\"}");
            return;
        }
        HeavyApiRequestGuard heavyGuard(request, "proximity-chart-data");
        if (!heavyGuard.begin()) return;

        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        char today[11];
        snprintf(today, sizeof(today), "%02d-%02d-%04d",
                 ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
        String requestedDate = request->hasParam("date") ? request->getParam("date")->value() : String(today);
        String resolvedDate  = (requestedDate.length() == 10) ? requestedDate : String(today);
        String mode          = request->hasParam("mode") ? request->getParam("mode")->value() : String("day");

        // Production-day window (same day the CSV/totals use: label D covers
        // [D-1 b:00, D b:00)) — mirrors handleProximityChartDataGet (unified).
        const uint32_t prodDayStart =
            proxShiftDayStartFromCivilMidnight(chartHistoryDayStartFromRs232DateId(resolvedDate),
                                               proximityShiftDayBoundaryHour());
        // OEE timeline squares (10-min stop seconds, RAM only) — compact side
        // channel of this route, kept identical to the unified handler.
        if (request->hasParam("stops") && request->getParam("stops")->value() != "0") {
            AsyncResponseStream* stops = request->beginResponseStream("application/json");
            if (!stops) { request->send(500, "application/json", "{\"error\":\"no memory\"}"); return; }
            stops->addHeader("Cache-Control", "no-store");
            extern bool proxChartStopBinsJson(Print& out);
            if (!proxChartStopBinsJson(*stops)) {
                request->send(200, "application/json", "{\"day_start\":0,\"bins\":0,\"stops\":{}}");
                return;
            }
            request->send(stops);
            return;
        }
        uint32_t fromTs = 0;
        if (request->hasParam("from")) {
            fromTs = (uint32_t)strtoul(request->getParam("from")->value().c_str(), nullptr, 10);
        } else {
            time_t nowTs = time(nullptr);
            if      (mode == "live") fromTs = (uint32_t)(nowTs > 900   ? nowTs - 900   : 0); // 15 min
            else if (mode == "8h")   fromTs = (uint32_t)(nowTs > 28800 ? nowTs - 28800 : 0); // 8 h
            else                     fromTs = prodDayStart; // full production day
        }

        uint32_t rangeSeconds = (uint32_t)(time(nullptr) > (time_t)fromTs ? time(nullptr) - (time_t)fromTs : 86400UL);
        const int maxOut = (rangeSeconds <= 3600UL) ? 72 : ((rangeSeconds <= 28800UL) ? 144 : 288);

        String chartPath = chartHistoryProximityPathForDate(configManager.getSystemName(), resolvedDate);
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        if (!response) {
            request->send(500, "application/json", "{\"error\":\"no memory\"}");
            return;
        }
        response->addHeader("Cache-Control", "no-store");
        // Merged serving (file history + PSRAM ring tail, always minute-fresh).
        // untilTs = end of the requested PRODUCTION day: the live tail must not
        // leak into a past date's series.
        uint32_t untilTs = prodDayStart ? prodDayStart + 86400UL
                                        : chartHistoryDayStartFromRs232DateId(resolvedDate) + 86400UL;
        extern bool proxChartServeMergedJson(Print& out, const String& path, uint32_t fromTs,
                                             uint16_t maxOut, uint32_t untilTs);
        if (!proxChartServeMergedJson(*response, chartPath, fromTs, (uint16_t)maxOut, untilTs)) {
            request->send(200, "application/json", "{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}");
            return;
        }
        request->send(response);
    });

#else
    (void)server;
    (void)context;
#endif

}