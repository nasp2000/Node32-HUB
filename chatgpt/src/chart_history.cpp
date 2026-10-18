#include "chart_history.h"

#include "feature_flags.h"
#if ENABLE_SDCARD==1

#include "sdcard.h"
#include "littlefs_manager.h" // no-SD LittleFS mirror reads (begin/mounted)
#include <LittleFS.h>
#include "psram_helpers.h"
#include "freertos/task.h"  // vTaskDelay — needed to yield to idle task and reset TWDT

#include "sd_lib.h" // SD backend shim (SDMMC or SPI)
#include "chart_stream_decimate.h" // single-pass online decimation for streaming
#include <vector>
#include <cstring>
#include "feature_flags.h"

static const char CHART_HISTORY_MAGIC[8] = "N32CHT1";

static bool chartHistoryReadHeaderFor(File &file, ChartFileHeader &header,
                                      uint8_t sourceType, uint8_t recordFormat,
                                      uint8_t maxColumns) {
    if (!file || file.size() < (int)sizeof(ChartFileHeader)) return false;
    file.seek(0);
    if (file.read((uint8_t*)&header, sizeof(header)) != sizeof(header)) return false;
    if (memcmp(header.magic, CHART_HISTORY_MAGIC, sizeof(header.magic)) != 0) return false;
    if (header.version != CHART_HISTORY_VERSION) return false;
    if (header.sourceType != sourceType) return false;
    if (header.recordFormat != recordFormat) return false;
    if (header.columnCount == 0 || header.columnCount > maxColumns) return false;
    return true;
}

static bool chartHistoryAppendRaw(std::vector<char, PsramAllocatorStd<char>> &buf, const char *text) {
    if (!text) return true;
    try {
        while (*text) buf.push_back(*text++);
        return true;
    } catch (...) {
        return false;
    }
}

static bool chartHistoryAppendNumber(std::vector<char, PsramAllocatorStd<char>> &buf, float value, int decimals) {
    char num[32];
    snprintf(num, sizeof(num), "%.*f", decimals, value);
    return chartHistoryAppendRaw(buf, num);
}

static bool chartHistoryEnsureParentDirsFS(fs::FS &fs, const String &path) {
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash <= 0) return false;

    String dir = path.substring(0, lastSlash);
    int secondSlash = dir.indexOf('/', 1);
    if (secondSlash > 0) {
        String rootDir = dir.substring(0, secondSlash);
        if (!fs.exists(rootDir.c_str())) fs.mkdir(rootDir.c_str());
    }
    if (!fs.exists(dir.c_str())) fs.mkdir(dir.c_str());
    return true;
}

static bool chartHistoryReadHeader(File &file, ChartFileHeader &header) {
    return chartHistoryReadHeaderFor(file, header, CHART_HISTORY_SOURCE_SENSOR,
                                     CHART_HISTORY_RECORD_INT16_SCALED,
                                     CHART_HISTORY_SENSOR_MAX_COLUMNS);
}

static bool chartHistoryReadDefs(File &file, const ChartFileHeader &header,
                                 ChartColumnDef defs[CHART_HISTORY_SENSOR_MAX_COLUMNS]) {
    file.seek(sizeof(ChartFileHeader));
    size_t defsBytes = (size_t)header.columnCount * sizeof(ChartColumnDef);
    return file.read((uint8_t*)defs, defsBytes) == defsBytes;
}

String chartHistorySensorPathForDate(const String &dateId) {
    return "/sensor_history/" + dateId + "/" + dateId + ".chart";
}

String chartHistorySanitizeSystemName(const String &systemName) {
    String safeName = systemName;
    if (safeName.isEmpty()) safeName = "Node32-HUB";
    safeName.replace(' ', '_');
    if (safeName.length() > 20) safeName = safeName.substring(0, 20);
    return safeName;
}

String chartHistoryRs232PathForDate(const String &systemName, const String &dateId) {
    String safeName = chartHistorySanitizeSystemName(systemName);
    // Chart lives in the parent folder (source-agnostic): /{safeName}/{safeName}_{dateId}.chart
    // All sources (RS232, LPT, MODBUS_RTU, ...) share the same chart per day.
    // chartHistoryEnsureParentDirs() will auto-create "/{safeName}/" on first write.
    return "/" + safeName + "/" + safeName + "_" + dateId + ".chart";
}

String chartHistorySensorPathForTm(const struct tm &ti) {
    char dateId[11];
    snprintf(dateId, sizeof(dateId), "%04d-%02d-%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    return chartHistorySensorPathForDate(String(dateId));
}

String chartHistoryRs232PathForTm(const String &systemName, const struct tm &ti) {
    char dateId[11];
    snprintf(dateId, sizeof(dateId), "%02d-%02d-%04d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    return chartHistoryRs232PathForDate(systemName, String(dateId));
}

uint32_t chartHistoryDayStartFromDateId(const String &dateId) {
    int year = 0, month = 0, day = 0;
    if (sscanf(dateId.c_str(), "%d-%d-%d", &year, &month, &day) != 3) return 0;
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t ts = mktime(&t);
    return ts > 0 ? (uint32_t)ts : 0;
}

uint32_t chartHistoryDayStartFromRs232DateId(const String &dateId) {
    int day = 0, month = 0, year = 0;
    if (sscanf(dateId.c_str(), "%d-%d-%d", &day, &month, &year) != 3) return 0;
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t ts = mktime(&t);
    return ts > 0 ? (uint32_t)ts : 0;
}

bool chartHistoryFileExists(const String &path) {
    return SD_MMC.exists(path.c_str());
}

bool chartHistoryOpenSensorWriter(ChartSensorWriter &writer, const String &path,
                                  const ChartColumnDef *defs, uint8_t columnCount,
                                  uint32_t dayStartTs) {
    if (!sdcardShouldUseSD()) return false;
    return chartHistoryOpenSensorWriterFS(writer, SD_MMC, path, defs, columnCount, dayStartTs);
}

bool chartHistoryOpenSensorWriterFS(ChartSensorWriter &writer, fs::FS &fs, const String &path,
                                    const ChartColumnDef *defs, uint8_t columnCount,
                                    uint32_t dayStartTs) {
    writer.valid = false;
    writer.columnCount = 0;
    if (columnCount == 0 || columnCount > CHART_HISTORY_SENSOR_MAX_COLUMNS) return false;

    bool fileExists = fs.exists(path.c_str());
    if (!fileExists) {
        int slash = path.lastIndexOf('/');
        if (slash > 0) {
            String dir = path.substring(0, slash);
            int second = dir.indexOf('/', 1);
            if (second > 0 && !fs.exists(dir.substring(0, second).c_str()))
                fs.mkdir(dir.substring(0, second).c_str());
            if (!fs.exists(dir.c_str())) fs.mkdir(dir.c_str());
        }
        File createFile = fs.open(path.c_str(), FILE_WRITE);
        if (!createFile) return false;

        ChartFileHeader header = {};
        memcpy(header.magic, CHART_HISTORY_MAGIC, sizeof(header.magic));
        header.version = CHART_HISTORY_VERSION;
        header.sourceType = CHART_HISTORY_SOURCE_SENSOR;
        header.recordFormat = CHART_HISTORY_RECORD_INT16_SCALED;
        header.columnCount = columnCount;
        header.dayStartTs = dayStartTs;
        header.recordCount = 0;
        header.firstRecordOffset = sizeof(ChartFileHeader) + (uint32_t)(columnCount * sizeof(ChartColumnDef));
        header.indexOffset = 0;

        if (createFile.write((const uint8_t*)&header, sizeof(header)) != sizeof(header)) {
            createFile.close();
            return false;
        }
        size_t defsBytes = (size_t)columnCount * sizeof(ChartColumnDef);
        if (createFile.write((const uint8_t*)defs, defsBytes) != defsBytes) {
            createFile.close();
            return false;
        }
        createFile.close();
    } else {
        File verifyFile = fs.open(path.c_str(), FILE_READ);
        if (!verifyFile) return false;
        ChartFileHeader header;
        bool ok = chartHistoryReadHeader(verifyFile, header) && header.columnCount == columnCount;
        verifyFile.close();
        if (!ok) return false;
    }

    writer.file = fs.open(path.c_str(), FILE_APPEND);
    writer.columnCount = columnCount;
    writer.valid = (bool)writer.file;
    writer.startSize = writer.valid ? writer.file.size() : 0;
    return writer.valid;
}

bool chartHistoryAppendSensorRecord(ChartSensorWriter &writer, const SensorChartRecord &record) {
    if (!writer.valid || !writer.file || writer.columnCount == 0) return false;

    if (writer.file.write((const uint8_t*)&record.timestamp, sizeof(record.timestamp)) != sizeof(record.timestamp)) return false;
    if (writer.file.write((const uint8_t*)&record.validMask, sizeof(record.validMask)) != sizeof(record.validMask)) return false;

    size_t valuesBytes = (size_t)writer.columnCount * sizeof(int16_t);
    return writer.file.write((const uint8_t*)record.values, valuesBytes) == valuesBytes;
}

void chartHistoryCloseSensorWriter(ChartSensorWriter &writer) {
    if (writer.file) {
        writer.file.flush();
        size_t endSize = writer.file.size();
        writer.file.close();
        storNoteSdWrite(endSize > writer.startSize ? endSize - writer.startSize : endSize);
    }
    writer.valid = false;
    writer.columnCount = 0;
}

// Resolve the writable chart backend: SD when mounted, else the LittleFS
// mirror (same rule as the sensor CSV/.chart flush). Flash writes keep a 1MB
// /config reserve, mirroring sensor_config.h.
static bool chartHistoryResolveChartFS(fs::FS *&outFs) {
    if (sdcardShouldUseSD()) {
        outFs = &SD_MMC;
        return true;
    }
    if (!littlefsBegin() || !littlefsMounted()) return false;
    if (littlefsFreeBytes() < (1024ULL * 1024ULL)) return false;
    outFs = &getLittleFS();
    return true;
}

// FS-parameterized float32 writer shared by RS232/MODBUS/PROXIMITY (same
// binary layout, only sourceType/maxCols differ). defs==nullptr auto-generates
// the legacy rs232_colN defs (RS232 behaviour); otherwise caller defs are
// stored. No file content is ever duplicated in String (OOM-safe).
static bool chartHistoryOpenFloat32WriterFS(ChartRs232Writer &writer, fs::FS &fs, const String &path,
                                            const ChartColumnDef *defs, uint8_t columnCount,
                                            uint32_t dayStartTs, uint8_t sourceType, uint8_t maxCols) {
    writer.valid = false;
    writer.columnCount = 0;
    writer.onFlash = (&fs != (fs::FS*)&SD_MMC);
    if (columnCount == 0 || columnCount > maxCols) return false;

    bool fileExists = fs.exists(path.c_str());
    if (!fileExists) {
        chartHistoryEnsureParentDirsFS(fs, path);
        File createFile = fs.open(path.c_str(), FILE_WRITE);
        if (!createFile) return false;

        ChartFileHeader header = {};
        memcpy(header.magic, CHART_HISTORY_MAGIC, sizeof(header.magic));
        header.version = CHART_HISTORY_VERSION;
        header.sourceType = sourceType;
        header.recordFormat = CHART_HISTORY_RECORD_FLOAT32;
        header.columnCount = columnCount;
        header.dayStartTs = dayStartTs;
        header.recordCount = 0;
        header.firstRecordOffset = sizeof(ChartFileHeader) + (uint32_t)(columnCount * sizeof(ChartColumnDef));
        header.indexOffset = 0;

        if (createFile.write((const uint8_t*)&header, sizeof(header)) != sizeof(header)) {
            createFile.close();
            return false;
        }

        if (defs) {
            size_t defsBytes = (size_t)columnCount * sizeof(ChartColumnDef);
            if (createFile.write((const uint8_t*)defs, defsBytes) != defsBytes) {
                createFile.close();
                return false;
            }
        } else {
            for (uint8_t i = 0; i < columnCount; ++i) {
                ChartColumnDef def = {};
                snprintf(def.key, sizeof(def.key), "rs232_col%d", i + 1);
                snprintf(def.label, sizeof(def.label), "RS232 Col %d", i + 1);
                def.dataType = CHART_HISTORY_DATA_FLOAT32;
                def.decimals = 4;
                def.scale = 1.0f;
                if (createFile.write((const uint8_t*)&def, sizeof(def)) != sizeof(def)) {
                    createFile.close();
                    return false;
                }
            }
        }
        size_t cfSize = createFile.size();
        createFile.close();
        if (writer.onFlash) storNoteLfsWrite(cfSize);
        else storNoteSdWrite(cfSize);
    } else {
        File verifyFile = fs.open(path.c_str(), FILE_READ);
        if (!verifyFile) return false;
        ChartFileHeader header;
        bool ok = chartHistoryReadHeaderFor(verifyFile, header, sourceType,
                                            CHART_HISTORY_RECORD_FLOAT32,
                                            maxCols) &&
                  header.columnCount == columnCount;
        verifyFile.close();
        if (!ok) return false;
    }

    writer.file = fs.open(path.c_str(), FILE_APPEND);
    writer.columnCount = columnCount;
    writer.valid = (bool)writer.file;
    writer.startSize = writer.valid ? writer.file.size() : 0;
    return writer.valid;
}

bool chartHistoryOpenRs232WriterFS(ChartRs232Writer &writer, fs::FS &fs, const String &path,
                                   uint8_t columnCount, uint32_t dayStartTs) {
    return chartHistoryOpenFloat32WriterFS(writer, fs, path, nullptr, columnCount,
                                           dayStartTs, CHART_HISTORY_SOURCE_RS232,
                                           CHART_HISTORY_RS232_MAX_COLUMNS);
}

bool chartHistoryOpenRs232Writer(ChartRs232Writer &writer, const String &path,
                                 uint8_t columnCount, uint32_t dayStartTs) {
    fs::FS *fsp = nullptr;
    if (!chartHistoryResolveChartFS(fsp)) return false;
    return chartHistoryOpenRs232WriterFS(writer, *fsp, path, columnCount, dayStartTs);
}

bool chartHistoryAppendRs232Record(ChartRs232Writer &writer, const Rs232ChartRecord &record) {
    if (!writer.valid || !writer.file || writer.columnCount == 0) return false;
    if (writer.file.write((const uint8_t*)&record.timestamp, sizeof(record.timestamp)) != sizeof(record.timestamp)) return false;
    if (writer.file.write((const uint8_t*)&record.valueCount, sizeof(record.valueCount)) != sizeof(record.valueCount)) return false;
    if (writer.file.write((const uint8_t*)record.reserved, sizeof(record.reserved)) != sizeof(record.reserved)) return false;
    size_t valuesBytes = (size_t)writer.columnCount * sizeof(float);
    return writer.file.write((const uint8_t*)record.values, valuesBytes) == valuesBytes;
}

void chartHistoryCloseRs232Writer(ChartRs232Writer &writer) {
    if (writer.file) {
        writer.file.flush();
        size_t endSize = writer.file.size();
        writer.file.close();
        size_t delta = endSize > writer.startSize ? endSize - writer.startSize : endSize;
        if (writer.onFlash) storNoteLfsWrite(delta);
        else storNoteSdWrite(delta);
    }
    writer.valid = false;
    writer.columnCount = 0;
    writer.onFlash = false;
}

bool chartHistoryReadRs232Meta(const String &path, uint8_t &columnCount,
                               ChartColumnDef defs[CHART_HISTORY_RS232_MAX_COLUMNS]) {
    columnCount = 0;
    // Order: SD first, then the LittleFS mirror at the identical path.
    File file;
    if (sdcardShouldUseSD()) file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file && littlefsBegin() && littlefsMounted()) {
        file = LittleFS.open(path.c_str(), FILE_READ);
    }
    if (!file) return false;
    ChartFileHeader header;
    bool ok = chartHistoryReadHeaderFor(file, header, CHART_HISTORY_SOURCE_RS232,
                                        CHART_HISTORY_RECORD_FLOAT32,
                                        CHART_HISTORY_RS232_MAX_COLUMNS);
    if (!ok) {
        file.close();
        return false;
    }
    if (defs) {
        file.seek(sizeof(ChartFileHeader));
        size_t defsBytes = (size_t)header.columnCount * sizeof(ChartColumnDef);
        ok = file.read((uint8_t*)defs, defsBytes) == defsBytes;
    }
    file.close();
    if (!ok) return false;
    columnCount = header.columnCount;
    return true;
}

static bool chartHistorySensorInWindow(uint32_t ts, uint32_t fromTimestamp, uint32_t endTimestamp) {
    if (ts < fromTimestamp) return false;
    if (endTimestamp != 0 && ts >= endTimestamp) return false;
    return true;
}

bool chartHistoryReadSensorJson(ChartSensorJsonSet &set, const String &path,
                                uint32_t fromTimestamp, uint32_t endTimestamp,
                                uint16_t maxPoints) {
    // Order: SD first, then the LittleFS mirror at the identical path
    // (sensor_history + logs mirrors share the SD layouts).
    File file;
    if (sdcardShouldUseSD()) file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file && littlefsBegin() && littlefsMounted()) {
        file = LittleFS.open(path.c_str(), FILE_READ);
    }
    if (!file) return false;

    ChartFileHeader header;
    if (!chartHistoryReadHeader(file, header)) {
        file.close();
        return false;
    }

    ChartColumnDef defs[CHART_HISTORY_SENSOR_MAX_COLUMNS] = {};
    if (!chartHistoryReadDefs(file, header, defs)) {
        file.close();
        return false;
    }

    const size_t recordSize = sizeof(uint32_t) + sizeof(uint32_t) + (size_t)header.columnCount * sizeof(int16_t);
    if (recordSize > sizeof(uint32_t) + sizeof(uint32_t) + (size_t)CHART_HISTORY_SENSOR_MAX_COLUMNS * sizeof(int16_t)) {
        file.close();
        return false;
    }
    if (header.firstRecordOffset >= (uint32_t)file.size()) {
        file.close();
        return true;
    }

    int idxTempAht20 = -1;
    int idxTempBmp280 = -1;
    int idxTempAvg = -1;
    int idxHumidity = -1;
    int idxPressure = -1;
    int idxDewPoint = -1;
    int idxHeatIndex = -1;
    int idxAltitude = -1;
    int idxExtra[8];
    memset(idxExtra, -1, sizeof(idxExtra));

    for (uint8_t i = 0; i < header.columnCount; i++) {
        const char *key = defs[i].key;
        if      (!strcmp(key, "temp_aht20"))   idxTempAht20 = i;
        else if (!strcmp(key, "temp_bmp280"))  idxTempBmp280 = i;
        else if (!strcmp(key, "temp_avg"))     idxTempAvg = i;
        else if (!strcmp(key, "humidity"))     idxHumidity = i;
        else if (!strcmp(key, "pressure_hpa")) idxPressure = i;
        else if (!strcmp(key, "dew_point"))    idxDewPoint = i;
        else if (!strcmp(key, "heat_index"))   idxHeatIndex = i;
        else if (!strcmp(key, "altitude"))     idxAltitude = i;
        else if (!strncmp(key, "extra_", 6)) {
            int slot = atoi(key + 6);
            if (slot >= 0 && slot < 8) idxExtra[slot] = i;
        }
    }

    uint32_t matchCount = 0;
    uint8_t rawRecord[8 + (CHART_HISTORY_SENSOR_MAX_COLUMNS * sizeof(int16_t))] = {};
    file.seek(header.firstRecordOffset);
    while ((size_t)(file.position() + recordSize) <= (size_t)file.size()) {
        if (file.read(rawRecord, recordSize) != recordSize) break;
        uint32_t ts = 0;
        memcpy(&ts, rawRecord, sizeof(uint32_t));
        if (chartHistorySensorInWindow(ts, fromTimestamp, endTimestamp)) matchCount++;
    }

    uint32_t keepCount = matchCount;
    if (maxPoints > 0 && keepCount > maxPoints) keepCount = maxPoints;
    uint32_t skipCount = matchCount - keepCount;

    try {
        set.labels.reserve(8192);
        set.timestamps.reserve(12288);
        set.tempAht20.reserve(8192);
        set.tempBmp280.reserve(8192);
        set.temperature.reserve(8192);
        set.tempAvg.reserve(8192);
        set.humidity.reserve(8192);
        set.pressure.reserve(8192);
        set.pressureHpa.reserve(8192);
        set.pressureMmhg.reserve(8192);
        set.dewPoint.reserve(8192);
        set.heatIndex.reserve(8192);
        set.altitude.reserve(8192);
        for (int i = 0; i < 8; i++) set.extra[i].reserve(4096);
    } catch (...) {
        file.close();
        return false;
    }

    auto appendValueOrNull = [&](std::vector<char, PsramAllocatorStd<char>> &buf,
                                 bool hasValue, float value, int decimals) -> bool {
        if (!hasValue) return chartHistoryAppendRaw(buf, "null");
        return chartHistoryAppendNumber(buf, value, decimals);
    };

    auto decodeValue = [&](int idx, const int16_t *values) -> float {
        float scale = defs[idx].scale;
        if (scale == 0.0f) return (float)values[idx];
        return values[idx] / scale;
    };

    uint32_t emitted = 0;
    uint32_t skipped = 0;
    file.seek(header.firstRecordOffset);
    while ((size_t)(file.position() + recordSize) <= (size_t)file.size()) {
        if (file.read(rawRecord, recordSize) != recordSize) break;

        uint32_t ts = 0;
        uint32_t validMask = 0;
        int16_t values[CHART_HISTORY_SENSOR_MAX_COLUMNS] = {};
        memcpy(&ts, rawRecord, sizeof(uint32_t));
        memcpy(&validMask, rawRecord + sizeof(uint32_t), sizeof(uint32_t));
        memcpy(values, rawRecord + sizeof(uint32_t) + sizeof(uint32_t), header.columnCount * sizeof(int16_t));

        if (!chartHistorySensorInWindow(ts, fromTimestamp, endTimestamp)) continue;
        if (skipped < skipCount) {
            skipped++;
            continue;
        }

        time_t rawTs = (time_t)ts;
        struct tm *ti = localtime(&rawTs);
        char tmp[32];
        if (emitted > 0) {
            if (!chartHistoryAppendRaw(set.labels, ",") || !chartHistoryAppendRaw(set.timestamps, ",") ||
                !chartHistoryAppendRaw(set.tempAht20, ",") || !chartHistoryAppendRaw(set.tempBmp280, ",") ||
                !chartHistoryAppendRaw(set.temperature, ",") || !chartHistoryAppendRaw(set.tempAvg, ",") ||
                !chartHistoryAppendRaw(set.humidity, ",") || !chartHistoryAppendRaw(set.pressure, ",") ||
                !chartHistoryAppendRaw(set.pressureHpa, ",") || !chartHistoryAppendRaw(set.pressureMmhg, ",") ||
                !chartHistoryAppendRaw(set.dewPoint, ",") || !chartHistoryAppendRaw(set.heatIndex, ",") ||
                !chartHistoryAppendRaw(set.altitude, ",")) {
                file.close();
                return false;
            }
            for (int i = 0; i < 8; i++) {
                if (!chartHistoryAppendRaw(set.extra[i], ",")) {
                    file.close();
                    return false;
                }
            }
        }

        snprintf(tmp, sizeof(tmp), "\"%02d:%02d\"", ti->tm_hour, ti->tm_min);
        if (!chartHistoryAppendRaw(set.labels, tmp)) {
            file.close();
            return false;
        }
        snprintf(tmp, sizeof(tmp), "%lu", (unsigned long)ts);
        if (!chartHistoryAppendRaw(set.timestamps, tmp)) {
            file.close();
            return false;
        }

        bool hasTempAht20 = idxTempAht20 >= 0 && (validMask & (1UL << idxTempAht20));
        bool hasTempBmp280 = idxTempBmp280 >= 0 && (validMask & (1UL << idxTempBmp280));
        bool hasTempAvg = idxTempAvg >= 0 && (validMask & (1UL << idxTempAvg));
        bool hasHumidity = idxHumidity >= 0 && (validMask & (1UL << idxHumidity));
        bool hasPressure = idxPressure >= 0 && (validMask & (1UL << idxPressure));
        bool hasDewPoint = idxDewPoint >= 0 && (validMask & (1UL << idxDewPoint));
        bool hasHeatIndex = idxHeatIndex >= 0 && (validMask & (1UL << idxHeatIndex));
        bool hasAltitude = idxAltitude >= 0 && (validMask & (1UL << idxAltitude));

        float tempAvgValue = hasTempAvg ? decodeValue(idxTempAvg, values) : (hasTempAht20 ? decodeValue(idxTempAht20, values) : 0.0f);
        float pressureValue = hasPressure ? decodeValue(idxPressure, values) : 0.0f;

        if (!appendValueOrNull(set.tempAht20, hasTempAht20, hasTempAht20 ? decodeValue(idxTempAht20, values) : 0.0f, 1) ||
            !appendValueOrNull(set.tempBmp280, hasTempBmp280, hasTempBmp280 ? decodeValue(idxTempBmp280, values) : 0.0f, 1) ||
            !appendValueOrNull(set.temperature, hasTempAvg || hasTempAht20, tempAvgValue, 1) ||
            !appendValueOrNull(set.tempAvg, hasTempAvg || hasTempAht20, tempAvgValue, 1) ||
            !appendValueOrNull(set.humidity, hasHumidity, hasHumidity ? decodeValue(idxHumidity, values) : 0.0f, 1) ||
            !appendValueOrNull(set.pressure, hasPressure, pressureValue, 0) ||
            !appendValueOrNull(set.pressureHpa, hasPressure, pressureValue, 0) ||
            !appendValueOrNull(set.pressureMmhg, hasPressure, pressureValue * 0.750064f, 1) ||
            !appendValueOrNull(set.dewPoint, hasDewPoint, hasDewPoint ? decodeValue(idxDewPoint, values) : 0.0f, 1) ||
            !appendValueOrNull(set.heatIndex, hasHeatIndex, hasHeatIndex ? decodeValue(idxHeatIndex, values) : 0.0f, 1) ||
            !appendValueOrNull(set.altitude, hasAltitude, hasAltitude ? decodeValue(idxAltitude, values) : 0.0f, 1)) {
            file.close();
            return false;
        }

        for (int i = 0; i < 8; i++) {
            bool hasExtra = idxExtra[i] >= 0 && (validMask & (1UL << idxExtra[i]));
            if (!appendValueOrNull(set.extra[i], hasExtra, hasExtra ? decodeValue(idxExtra[i], values) : 0.0f,
                                   idxExtra[i] >= 0 ? defs[idxExtra[i]].decimals : 1)) {
                file.close();
                return false;
            }
        }
        emitted++;
    }
    file.close();

    set.count = emitted;
    return true;
}

void chartHistoryWriteSensorJson(Print &out, const ChartSensorJsonSet &set) {
    out.print("{\"count\":");
    out.print(set.count);
    if (set.count == 0) {
        out.print(",\"labels\":[],\"timestamps\":[]}");
        return;
    }
    out.print(",\"labels\":[");
    if (!set.labels.empty()) out.write((const uint8_t*)set.labels.data(), set.labels.size());
    out.print("],\"timestamps\":[");
    if (!set.timestamps.empty()) out.write((const uint8_t*)set.timestamps.data(), set.timestamps.size());
    out.print("]");

    auto writeSeries = [&](const char *key, const std::vector<char, PsramAllocatorStd<char>> &buf) {
        out.print(",\"");
        out.print(key);
        out.print("\":[");
        if (!buf.empty()) out.write((const uint8_t*)buf.data(), buf.size());
        out.print("]");
    };

    writeSeries("temp_aht20", set.tempAht20);
    writeSeries("temp_bmp280", set.tempBmp280);
    writeSeries("temperature", set.temperature);
    writeSeries("temp_avg", set.tempAvg);
    writeSeries("humidity", set.humidity);
    writeSeries("pressure", set.pressure);
    writeSeries("pressure_hpa", set.pressureHpa);
    writeSeries("pressure_mmhg", set.pressureMmhg);
    writeSeries("dew_point", set.dewPoint);
    writeSeries("heat_index", set.heatIndex);
    writeSeries("altitude", set.altitude);
    for (int i = 0; i < 8; i++) {
        char key[16];
        snprintf(key, sizeof(key), "extra_%d", i);
        writeSeries(key, set.extra[i]);
    }
    out.print("}");
}

bool chartHistoryStreamSensorJson(Print &out, const String &path,
                                  uint32_t fromTimestamp, uint16_t maxPoints) {
    ChartSensorJsonSet set;
    if (!chartHistoryReadSensorJson(set, path, fromTimestamp, 0, maxPoints)) return false;
    chartHistoryWriteSensorJson(out, set);
    return true;
}

// Internal: stream any FLOAT32 chart file as JSON (RS232, MODBUS or PROXIMITY).
// sourceType and maxCols distinguish between record types.
//
// Single-pass implementation: records are read in large SD blocks and fed to an
// online decimator (chart_stream_decimate.h) that keeps at most maxPoints evenly
// spaced samples without knowing the total record count in advance. The previous
// two-pass algorithm read the whole file twice (count matching records, then
// re-read the strided records) and the pass loop yielded vTaskDelay(1) per 256
// records (~10 ms each) — together the doubling of I/O and the sleep time made
// large chart files exceed the web server's request time budget.
static bool chartHistoryStreamFloat32JsonImpl(Print &out, const String &path,
                                             uint8_t sourceType, uint8_t maxCols,
                                             uint32_t fromTimestamp, uint16_t maxPoints) {
    // Order: SD first, then the LittleFS mirror at the identical path.
    File file;
    if (sdcardShouldUseSD()) file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file && littlefsBegin() && littlefsMounted()) {
        file = LittleFS.open(path.c_str(), FILE_READ);
    }
    if (!file) return false;

    ChartFileHeader header;
    if (!chartHistoryReadHeaderFor(file, header, sourceType,
                                   CHART_HISTORY_RECORD_FLOAT32,
                                   maxCols)) {
        file.close();
        return false;
    }

    ChartColumnDef defs[CHART_HISTORY_MODBUS_MAX_COLUMNS] = {};
    file.seek(sizeof(ChartFileHeader));
    size_t defsBytes = (size_t)header.columnCount * sizeof(ChartColumnDef);
    if (file.read((uint8_t*)defs, defsBytes) != defsBytes) {
        file.close();
        return false;
    }

    const uint8_t ncols = header.columnCount;
    const size_t recordSize = sizeof(uint32_t) + sizeof(uint8_t) + 3 + (size_t)ncols * sizeof(float);
    const size_t storedSize = sizeof(uint32_t) + 1 + (size_t)ncols * sizeof(float);

    if (header.firstRecordOffset >= (uint32_t)file.size()) {
        file.close();
        out.print("{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}");
        return true;
    }

    // Bounded PSRAM buffer: maxPoints entries when downsampling, or one slot per
    // record when maxPoints==0 (keep everything, legacy behaviour).
    uint32_t slots = maxPoints;
    if (slots == 0) {
        slots = (uint32_t)(((size_t)file.size() - header.firstRecordOffset) / recordSize);
    }
    uint8_t* recBuf = (uint8_t*)ps_malloc((size_t)slots * storedSize);
    if (!recBuf) {
        file.close();
        return false;
    }

    // Block-buffered sequential reader. Records are a fixed size, so we read whole
    // blocks of them with a single (large) SD read and parse the records out of the
    // in-memory block — one slow SD syscall per record would dominate the scan of a
    // large file. A block must hold at least one record; the read loop tops up
    // partial trailing bytes so record boundaries stay aligned.
    size_t recsPerBlock = 4096U / recordSize;
    if (recsPerBlock < 1) recsPerBlock = 1;
    const size_t blockBytes = recsPerBlock * recordSize;

    uint8_t* block = (uint8_t*)ps_malloc(blockBytes);
    if (!block) {
        free(recBuf);
        file.close();
        return false;
    }
    size_t blockFilled = 0, blockPos = 0;

    // Refill the block so that recordSize bytes are available at blockPos; returns
    // false when there are no more complete records in the file/block.
    auto blockAdvance = [&]() -> bool {
        while (blockPos + recordSize > blockFilled) {
            if (blockPos == blockFilled) {
                size_t n = file.read(block, blockBytes);
                if (n == 0) return false;
                blockFilled = n;
                blockPos = 0;
            } else {
                // Partial trailing record — shift it to the front and top it up so
                // record boundaries always align to the start of the block.
                size_t keep = blockFilled - blockPos;
                memmove(block, block + blockPos, keep);
                size_t n = file.read(block + keep, blockBytes - keep);
                blockFilled = keep + n;
                blockPos = 0;
                if (n == 0) return false;
            }
        }
        return true;
    };

    // Single in-memory pass: feed every matching record to the decimator and keep
    // its (bounded) output in recBuf. Per-record work here is microseconds, so the
    // watchdog is not at risk inside a block; a small delay every 4096 records
    // still gives the idle task room while scanning a very large chart file.
    ChartStreamDecimator dec(maxPoints);
    uint32_t scanned = 0;
    file.seek(header.firstRecordOffset);
    while (blockAdvance()) {
        if ((++scanned & 0xFFF) == 0) vTaskDelay(1);
        const uint8_t* rec = block + blockPos;
        uint32_t ts = 0;
        memcpy(&ts, rec, sizeof(uint32_t));
        blockPos += recordSize;
        if (ts < fromTimestamp) continue;

        const uint32_t beforeLive = dec.live();
        const uint8_t act = dec.feed();
        if (act == ChartStreamDecimator::kSkip) continue;
        if (act == ChartStreamDecimator::kCompactThenKeep) {
            chartStreamDecimateCompact(recBuf, beforeLive, storedSize);
        }
        uint8_t* dest = recBuf + (dec.live() - 1) * storedSize;
        memcpy(dest, rec, sizeof(uint32_t));                                          // ts
        dest[sizeof(uint32_t)] = rec[sizeof(uint32_t)];                              // valueCount
        memcpy(dest + sizeof(uint32_t) + 1, rec + 8, (size_t)ncols * sizeof(float)); // values
    }
    file.close();  // SD released — remainder of work runs from PSRAM
    free(block);

    const uint32_t stride = dec.stride();
    const uint32_t stored = dec.live();

    if (stored == 0) {
        free(recBuf);
        out.print("{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}");
        return true;
    }

    char tmp[24];
    out.print("{\"count\":"); out.print(stored);
    out.print(",\"stride\":"); out.print(stride);

    out.print(",\"labels\":[");
    for (uint32_t i = 0; i < stored; ++i) {
        uint32_t ts = 0;
        memcpy(&ts, recBuf + i * storedSize, sizeof(uint32_t));
        time_t rawTs = (time_t)ts;
        struct tm *ti = localtime(&rawTs);
        snprintf(tmp, sizeof(tmp), "%s\"%02d:%02d\"", i ? "," : "", ti->tm_hour, ti->tm_min);
        out.print(tmp);
    }

    out.print("],\"timestamps\":[");
    for (uint32_t i = 0; i < stored; ++i) {
        uint32_t ts = 0;
        memcpy(&ts, recBuf + i * storedSize, sizeof(uint32_t));
        snprintf(tmp, sizeof(tmp), "%s%lu", i ? "," : "", (unsigned long)ts);
        out.print(tmp);
    }
    out.print("]");

    for (uint8_t ci = 0; ci < ncols; ++ci) {
        out.print(",\"");
        if (defs[ci].key[0]) out.print(defs[ci].key);
        else { out.print("col_"); out.print(ci + 1); }
        out.print("\":[");
        for (uint32_t i = 0; i < stored; ++i) {
            const uint8_t* src = recBuf + i * storedSize;
            uint8_t vcnt = src[sizeof(uint32_t)];
            float val = NAN;
            if (ci < vcnt) memcpy(&val, src + sizeof(uint32_t) + 1 + (size_t)ci * sizeof(float), sizeof(float));
            if (i) out.print(",");
            if (!isnan(val) && !isinf(val)) {
                char nbuf[24];
                snprintf(nbuf, sizeof(nbuf), "%.*f", defs[ci].decimals >= 0 ? defs[ci].decimals : 4, (double)val);
                out.print(nbuf);
            } else {
                out.print("null");
            }
        }
        out.print("]");
    }

    free(recBuf);
    out.print("}");
    return true;
}

bool chartHistoryStreamRs232Json(Print &out, const String &path,
                                 uint32_t fromTimestamp, uint16_t maxPoints) {
    return chartHistoryStreamFloat32JsonImpl(out, path,
        CHART_HISTORY_SOURCE_RS232, CHART_HISTORY_RS232_MAX_COLUMNS,
        fromTimestamp, maxPoints);
}

// --- Modbus chart functions ---

String chartHistoryModbusPathForDate(const String &systemName, const String &dateId) {
    String safeName = chartHistorySanitizeSystemName(systemName);
    return "/" + safeName + "/" + safeName + "_MODBUS_" + dateId + ".chart";
}

String chartHistoryModbusPathForTm(const String &systemName, const struct tm &ti) {
    char dateId[11];
    snprintf(dateId, sizeof(dateId), "%02d-%02d-%04d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    return chartHistoryModbusPathForDate(systemName, String(dateId));
}

bool chartHistoryOpenModbusWriterFS(ChartRs232Writer &writer, fs::FS &fs, const String &path,
                                    const ChartColumnDef *defs, uint8_t columnCount,
                                    uint32_t dayStartTs) {
    return chartHistoryOpenFloat32WriterFS(writer, fs, path, defs, columnCount,
                                           dayStartTs, CHART_HISTORY_SOURCE_MODBUS,
                                           CHART_HISTORY_MODBUS_MAX_COLUMNS);
}

bool chartHistoryOpenModbusWriter(ChartRs232Writer &writer, const String &path,
                                  const ChartColumnDef *defs, uint8_t columnCount,
                                  uint32_t dayStartTs) {
    writer.valid = false;
    writer.columnCount = 0;
    if (columnCount == 0 || columnCount > CHART_HISTORY_MODBUS_MAX_COLUMNS) return false;
    fs::FS *fsp = nullptr;
    if (!chartHistoryResolveChartFS(fsp)) return false;
    return chartHistoryOpenModbusWriterFS(writer, *fsp, path, defs, columnCount, dayStartTs);
}

bool chartHistoryAppendModbusRecord(ChartRs232Writer &writer, const ModbusChartRecord &record) {
    if (!writer.valid || !writer.file || writer.columnCount == 0) return false;
    if (writer.file.write((const uint8_t*)&record.timestamp,
                          sizeof(record.timestamp)) != sizeof(record.timestamp)) return false;
    if (writer.file.write((const uint8_t*)&record.valueCount,
                          sizeof(record.valueCount)) != sizeof(record.valueCount)) return false;
    if (writer.file.write((const uint8_t*)record.reserved,
                          sizeof(record.reserved)) != sizeof(record.reserved)) return false;
    size_t valuesBytes = (size_t)writer.columnCount * sizeof(float);
    return writer.file.write((const uint8_t*)record.values, valuesBytes) == valuesBytes;
}

bool chartHistoryReadModbusMeta(const String &path, uint8_t &columnCount,
                                ChartColumnDef defs[CHART_HISTORY_MODBUS_MAX_COLUMNS]) {
    columnCount = 0;
    // Order: SD first, then the LittleFS mirror at the identical path.
    File file;
    if (sdcardShouldUseSD()) file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file && littlefsBegin() && littlefsMounted()) {
        file = LittleFS.open(path.c_str(), FILE_READ);
    }
    if (!file) return false;
    ChartFileHeader header;
    bool ok = chartHistoryReadHeaderFor(file, header, CHART_HISTORY_SOURCE_MODBUS,
                                        CHART_HISTORY_RECORD_FLOAT32,
                                        CHART_HISTORY_MODBUS_MAX_COLUMNS);
    if (!ok) { file.close(); return false; }
    if (defs) {
        file.seek(sizeof(ChartFileHeader));
        size_t defsBytes = (size_t)header.columnCount * sizeof(ChartColumnDef);
        ok = (file.read((uint8_t*)defs, defsBytes) == defsBytes);
    }
    file.close();
    if (!ok) return false;
    columnCount = header.columnCount;
    return true;
}

bool chartHistoryStreamModbusJson(Print &out, const String &path,
                                  uint32_t fromTimestamp, uint16_t maxPoints) {
    return chartHistoryStreamFloat32JsonImpl(out, path,
        CHART_HISTORY_SOURCE_MODBUS, CHART_HISTORY_MODBUS_MAX_COLUMNS,
        fromTimestamp, maxPoints);
}

// --- Proximity chart functions ---

// Path alongside Modbus chart: /{sdcardName}/{sdcardName}_PROXIMITY_DD-MM-YYYY.chart
String chartHistoryProximityPathForDate(const String &systemName, const String &dateId) {
    String safeName = chartHistorySanitizeSystemName(systemName);
    return "/" + safeName + "/" + safeName + "_PROXIMITY_" + dateId + ".chart";
}

String chartHistoryProximityPathForTm(const String &systemName, const struct tm &ti) {
    char dateId[11];
    snprintf(dateId, sizeof(dateId), "%02d-%02d-%04d",
             ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    return chartHistoryProximityPathForDate(systemName, String(dateId));
}

bool chartHistoryOpenProximityWriterFS(ChartRs232Writer &writer, fs::FS &fs, const String &path,
                                       const ChartColumnDef *defs, uint8_t columnCount,
                                       uint32_t dayStartTs) {
    return chartHistoryOpenFloat32WriterFS(writer, fs, path, defs, columnCount,
                                           dayStartTs, CHART_HISTORY_SOURCE_PROXIMITY,
                                           CHART_HISTORY_PROXIMITY_MAX_COLUMNS);
}

bool chartHistoryOpenProximityWriter(ChartRs232Writer &writer, const String &path,
                                     const ChartColumnDef *defs, uint8_t columnCount,
                                     uint32_t dayStartTs) {
    writer.valid = false;
    writer.columnCount = 0;
    if (columnCount == 0 || columnCount > CHART_HISTORY_PROXIMITY_MAX_COLUMNS) return false;
    fs::FS *fsp = nullptr;
    if (!chartHistoryResolveChartFS(fsp)) return false;
    return chartHistoryOpenProximityWriterFS(writer, *fsp, path, defs, columnCount, dayStartTs);
}

bool chartHistoryAppendProximityRecord(ChartRs232Writer &writer,
                                       const ProximityChartRecord &record) {
    if (!writer.valid || !writer.file || writer.columnCount == 0) return false;
    if (writer.file.write((const uint8_t*)&record.timestamp,
                          sizeof(record.timestamp)) != sizeof(record.timestamp)) return false;
    if (writer.file.write((const uint8_t*)&record.valueCount,
                          sizeof(record.valueCount)) != sizeof(record.valueCount)) return false;
    if (writer.file.write((const uint8_t*)record.reserved,
                          sizeof(record.reserved)) != sizeof(record.reserved)) return false;
    size_t valuesBytes = (size_t)writer.columnCount * sizeof(float);
    return writer.file.write((const uint8_t*)record.values, valuesBytes) == valuesBytes;
}

bool chartHistoryReadProximityMeta(const String &path, uint8_t &columnCount,
                                   ChartColumnDef defs[CHART_HISTORY_PROXIMITY_MAX_COLUMNS]) {
    columnCount = 0;
    // Order: SD first, then the LittleFS mirror at the identical path.
    File file;
    if (sdcardShouldUseSD()) file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file && littlefsBegin() && littlefsMounted()) {
        file = LittleFS.open(path.c_str(), FILE_READ);
    }
    if (!file) return false;
    ChartFileHeader header;
    bool ok = chartHistoryReadHeaderFor(file, header, CHART_HISTORY_SOURCE_PROXIMITY,
                                        CHART_HISTORY_RECORD_FLOAT32,
                                        CHART_HISTORY_PROXIMITY_MAX_COLUMNS);
    if (!ok) { file.close(); return false; }
    if (defs) {
        file.seek(sizeof(ChartFileHeader));
        size_t defsBytes = (size_t)header.columnCount * sizeof(ChartColumnDef);
        ok = (file.read((uint8_t*)defs, defsBytes) == defsBytes);
    }
    file.close();
    if (!ok) return false;
    columnCount = header.columnCount;
    return true;
}

bool chartHistoryStreamProximityJson(Print &out, const String &path,
                                     uint32_t fromTimestamp, uint16_t maxPoints) {
    return chartHistoryStreamFloat32JsonImpl(out, path,
        CHART_HISTORY_SOURCE_PROXIMITY, CHART_HISTORY_PROXIMITY_MAX_COLUMNS,
        fromTimestamp, maxPoints);
}

// Raw float32 record reader for merged serving (file history half of the
// file + PSRAM-ring-tail union). Same block-buffered single pass as the
// streaming impl, but keeps raw points (no decimation here — the union is
// decimated once, over file + tail together, by chartHistoryServePointsJson).
uint16_t chartHistoryReadRawPoints(const String &path, uint8_t sourceType, uint8_t maxCols,
                                   uint32_t fromTs, ChartServePoint* out, uint16_t cap) {
    if (!out || cap == 0) return 0;
#if ENABLE_FILESYSTEM==1
    // Serialized under lfsMutex when the read can hit flash (same crash
    // guard: stat/dir traversal vs concurrent writers). SD path untouched.
    // The whole block read shares one hold; released before JSON formatting.
    extern bool lfsMutexLock(TickType_t timeoutTicks);
    extern void lfsMutexUnlock();
    bool rawLfsHeld = false;
    if (!sdcardShouldUseSD()) {
        rawLfsHeld = lfsMutexLock(pdMS_TO_TICKS(10000));
        if (!rawLfsHeld) return 0;
    }
#endif
    File file;
    if (sdcardShouldUseSD()) file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file && littlefsBegin() && littlefsMounted()) {
        file = LittleFS.open(path.c_str(), FILE_READ);
    }
    if (!file) {
#if ENABLE_FILESYSTEM==1
        if (rawLfsHeld) lfsMutexUnlock();
#endif
        return 0;
    }

    ChartFileHeader header;
    if (!chartHistoryReadHeaderFor(file, header, sourceType,
                                   CHART_HISTORY_RECORD_FLOAT32,
                                   maxCols)) {
        file.close();
#if ENABLE_FILESYSTEM==1
        if (rawLfsHeld) lfsMutexUnlock();
#endif
        return 0;
    }
    uint8_t fileCols = header.columnCount;
    const size_t recordSize = sizeof(uint32_t) + sizeof(uint8_t) + 3 + (size_t)fileCols * sizeof(float);
    uint8_t keepCols = fileCols;
    if (keepCols > maxCols) keepCols = maxCols;
    if (keepCols > CHART_SERVE_MAX_COLS) keepCols = CHART_SERVE_MAX_COLS;

    if (header.firstRecordOffset >= (uint32_t)file.size()) {
        file.close();
#if ENABLE_FILESYSTEM==1
        if (rawLfsHeld) lfsMutexUnlock();
#endif
        return 0;
    }

    size_t recsPerBlock = 4096U / recordSize;
    if (recsPerBlock < 1) recsPerBlock = 1;
    const size_t blockBytes = recsPerBlock * recordSize;
    uint8_t* block = (uint8_t*)ps_malloc(blockBytes);
    if (!block) {
        file.close();
#if ENABLE_FILESYSTEM==1
        if (rawLfsHeld) lfsMutexUnlock();
#endif
        return 0;
    }
    size_t blockFilled = 0, blockPos = 0;
    uint16_t n = 0;
    file.seek(header.firstRecordOffset);
    for (;;) {
        while (blockPos + recordSize > blockFilled) {
            if (blockPos == blockFilled) {
                size_t rd = file.read(block, blockBytes);
                if (rd == 0) break;
                blockFilled = rd;
                blockPos = 0;
            } else {
                size_t keep = blockFilled - blockPos;
                memmove(block, block + blockPos, keep);
                size_t rd = file.read(block + keep, blockBytes - keep);
                blockFilled = keep + rd;
                blockPos = 0;
                if (rd == 0) break;
            }
        }
        if (blockPos + recordSize > blockFilled) break;
        const uint8_t* rec = block + blockPos;
        uint32_t ts = 0;
        memcpy(&ts, rec, sizeof(uint32_t));
        blockPos += recordSize;
        if (ts < fromTs) continue;
        if (n >= cap) {
            // File holds more than cap: drop the oldest (tail survives).
            memmove(out, out + 1, (size_t)(cap - 1) * sizeof(ChartServePoint));
            n = cap - 1;
        }
        ChartServePoint* p = &out[n++];
        p->timestamp = ts;
        uint8_t vc = rec[sizeof(uint32_t)];
        if (vc > keepCols) vc = keepCols;
        p->valueCount = vc;
        for (uint8_t c = 0; c < keepCols; c++) {
            float v = NAN;
            if (c < fileCols) memcpy(&v, rec + 8 + (size_t)c * sizeof(float), sizeof(float));
            p->values[c] = v;
        }
        for (uint8_t c = keepCols; c < CHART_SERVE_MAX_COLS; c++) p->values[c] = NAN;
    }
    free(block);
    file.close();
#if ENABLE_FILESYSTEM==1
    if (rawLfsHeld) lfsMutexUnlock();
#endif
    return n;
}

// Merged JSON emitter: one stride budget over the union (history + tail),
// same envelope as the file streams. Empty input prints the empty shape.
bool chartHistoryServePointsJson(Print &out, const ChartServePoint* pts, uint32_t nPts,
                                 const char keys[][16], const int8_t* decimals, uint8_t nSeries,
                                 uint16_t maxOut) {
    uint32_t stride = 1, planned = 0;
    chartRingServePlan(nPts, maxOut, &stride, &planned);
    if (planned == 0 || !pts) {
        out.print("{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}");
        return true;
    }
    uint32_t idx[288];
    uint32_t n = chartRingServePick(nPts, stride, idx, 288);
    if (n == 0) {
        out.print("{\"count\":0,\"stride\":1,\"labels\":[],\"timestamps\":[]}");
        return true;
    }
    char tmp[32];
    out.print("{\"count\":"); out.print(n);
    out.print(",\"stride\":"); out.print(stride);
    out.print(",\"labels\":[");
    for (uint32_t k = 0; k < n; k++) {
        time_t raw = (time_t)pts[idx[k]].timestamp;
        struct tm ti;
        localtime_r(&raw, &ti);
        snprintf(tmp, sizeof(tmp), "%s\"%02d:%02d\"", k ? "," : "", ti.tm_hour, ti.tm_min);
        out.print(tmp);
    }
    out.print("],\"timestamps\":[");
    for (uint32_t k = 0; k < n; k++) {
        snprintf(tmp, sizeof(tmp), "%s%lu", k ? "," : "", (unsigned long)pts[idx[k]].timestamp);
        out.print(tmp);
    }
    out.print("]");
    for (uint8_t c = 0; c < nSeries && c < CHART_SERVE_MAX_COLS; c++) {
        out.print(",\"");
        if (keys[c][0]) out.print(keys[c]);
        else { out.print("col_"); out.print(c + 1); }
        out.print("\":[");
        int dec = (decimals && decimals[c] >= 0) ? decimals[c] : 4;
        for (uint32_t k = 0; k < n; k++) {
            const ChartServePoint* p = &pts[idx[k]];
            float v = (c < p->valueCount) ? p->values[c] : NAN;
            if (k) out.print(",");
            if (!isnan(v) && !isinf(v)) {
                snprintf(tmp, sizeof(tmp), "%.*f", dec, (double)v);
                out.print(tmp);
            } else {
                out.print("null");
            }
        }
        out.print("]");
    }
    out.print("}");
    return true;
}

#endif