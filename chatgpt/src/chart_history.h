#ifndef CHART_HISTORY_H
#define CHART_HISTORY_H

#include "feature_flags.h"

#if ENABLE_SDCARD==1

#include <Arduino.h>
#include <Print.h>
#include <FS.h>
#include <time.h>
#include <vector>
#include "psram_helpers.h"
#include "chart_ring_serve.h" // ChartServePoint + merge/stride math (pure)

static const uint8_t CHART_HISTORY_VERSION = 1;
static const uint8_t CHART_HISTORY_SOURCE_SENSOR = 1;
static const uint8_t CHART_HISTORY_SOURCE_RS232 = 2;
static const uint8_t CHART_HISTORY_RECORD_INT16_SCALED = 1;
static const uint8_t CHART_HISTORY_RECORD_FLOAT32 = 2;
static const uint8_t CHART_HISTORY_DATA_INT16_SCALED = 1;
static const uint8_t CHART_HISTORY_DATA_FLOAT32 = 2;
static const uint8_t CHART_HISTORY_SENSOR_FIXED_COLUMNS = 17;
static const uint8_t CHART_HISTORY_SENSOR_MAX_COLUMNS = 24;
static const uint8_t CHART_HISTORY_RS232_MAX_COLUMNS = 16;

struct __attribute__((packed)) ChartFileHeader {
    char magic[8];
    uint8_t version;
    uint8_t sourceType;
    uint8_t recordFormat;
    uint8_t columnCount;
    uint32_t dayStartTs;
    uint32_t recordCount;
    uint32_t firstRecordOffset;
    uint32_t indexOffset;
    uint32_t reserved[4];
};

struct __attribute__((packed)) ChartColumnDef {
    char key[16];
    char label[24];
    char unit[8];
    uint8_t dataType;
    int8_t decimals;
    float scale;
    uint16_t flags;
};

struct SensorChartRecord {
    uint32_t timestamp;
    uint32_t validMask;
    int16_t values[CHART_HISTORY_SENSOR_MAX_COLUMNS];

    SensorChartRecord() : timestamp(0), validMask(0) {
        memset(values, 0, sizeof(values));
    }
};

struct ChartSensorWriter {
    File file;
    uint8_t columnCount;
    bool valid;
    size_t startSize;

    ChartSensorWriter() : columnCount(0), valid(false), startSize(0) {}
};

struct Rs232ChartRecord {
    uint32_t timestamp;
    uint8_t valueCount;
    uint8_t reserved[3];
    float values[CHART_HISTORY_RS232_MAX_COLUMNS];

    Rs232ChartRecord() : timestamp(0), valueCount(0), reserved{0, 0, 0} {
        for (uint8_t i = 0; i < CHART_HISTORY_RS232_MAX_COLUMNS; ++i) values[i] = NAN;
    }
};

struct ChartRs232Writer {
    File file;
    uint8_t columnCount;
    bool valid;
    size_t startSize;
    bool onFlash; // true when file lives on LittleFS (close notes LFS bytes)

    ChartRs232Writer() : columnCount(0), valid(false), startSize(0), onFlash(false) {}
};

String chartHistorySensorPathForDate(const String &dateId);
String chartHistorySensorPathForTm(const struct tm &ti);
uint32_t chartHistoryDayStartFromDateId(const String &dateId);
String chartHistorySanitizeSystemName(const String &systemName);

// JSON fragments (comma-separated strings per series) of a sensor .chart window,
// plus the record count. Populated by chartHistoryReadSensorJson and emitted as
// a full {count,labels,timestamps,...} envelope by chartHistoryWriteSensorJson.
struct ChartSensorJsonSet {
    std::vector<char, PsramAllocatorStd<char>> labels;
    std::vector<char, PsramAllocatorStd<char>> timestamps;
    std::vector<char, PsramAllocatorStd<char>> tempAht20;
    std::vector<char, PsramAllocatorStd<char>> tempBmp280;
    std::vector<char, PsramAllocatorStd<char>> temperature;
    std::vector<char, PsramAllocatorStd<char>> tempAvg;
    std::vector<char, PsramAllocatorStd<char>> humidity;
    std::vector<char, PsramAllocatorStd<char>> pressure;
    std::vector<char, PsramAllocatorStd<char>> pressureHpa;
    std::vector<char, PsramAllocatorStd<char>> pressureMmhg;
    std::vector<char, PsramAllocatorStd<char>> dewPoint;
    std::vector<char, PsramAllocatorStd<char>> heatIndex;
    std::vector<char, PsramAllocatorStd<char>> altitude;
    std::vector<char, PsramAllocatorStd<char>> extra[8];
    uint32_t count;
    ChartSensorJsonSet() : count(0) {}
};
String chartHistoryRs232PathForDate(const String &systemName, const String &dateId);
String chartHistoryRs232PathForTm(const String &systemName, const struct tm &ti);
uint32_t chartHistoryDayStartFromRs232DateId(const String &dateId);
bool chartHistoryFileExists(const String &path);
bool chartHistoryOpenSensorWriter(ChartSensorWriter &writer, const String &path,
                                  const ChartColumnDef *defs, uint8_t columnCount,
                                  uint32_t dayStartTs);
// FS-parameterized variant (LittleFS fallback mirror); the plain version
// forwards to SD_MMC. Append/close stay FS-agnostic (writer.file).
bool chartHistoryOpenSensorWriterFS(ChartSensorWriter &writer, fs::FS &fs, const String &path,
                                    const ChartColumnDef *defs, uint8_t columnCount,
                                    uint32_t dayStartTs);
bool chartHistoryAppendSensorRecord(ChartSensorWriter &writer, const SensorChartRecord &record);
void chartHistoryCloseSensorWriter(ChartSensorWriter &writer);
bool chartHistoryOpenRs232Writer(ChartRs232Writer &writer, const String &path,
                                 uint8_t columnCount, uint32_t dayStartTs);
// FS-parameterized variant (LittleFS fallback mirror); the plain version
// picks SD or LittleFS via sdcardShouldUseSD(). Append/close stay
// FS-agnostic (writer.file; close notes LFS bytes when writer.onFlash).
bool chartHistoryOpenRs232WriterFS(ChartRs232Writer &writer, fs::FS &fs, const String &path,
                                   uint8_t columnCount, uint32_t dayStartTs);
bool chartHistoryAppendRs232Record(ChartRs232Writer &writer, const Rs232ChartRecord &record);
void chartHistoryCloseRs232Writer(ChartRs232Writer &writer);
bool chartHistoryReadRs232Meta(const String &path, uint8_t &columnCount,
                               ChartColumnDef defs[CHART_HISTORY_RS232_MAX_COLUMNS]);
// Read a sensor .chart day window [fromTimestamp, endTimestamp) into per-series
// JSON fragments. endTimestamp==0 means unbounded. When maxPoints > 0 the
// NEWEST head records are kept (older ones dropped), mirroring stream tallies.
// Returns false when the file is unusable (missing/invalid header/defs).
bool chartHistoryReadSensorJson(ChartSensorJsonSet &set, const String &path,
                                uint32_t fromTimestamp, uint32_t endTimestamp,
                                uint16_t maxPoints = 0);
// Write a full sensor history JSON envelope from fragment vectors.
void chartHistoryWriteSensorJson(Print &out, const ChartSensorJsonSet &set);
bool chartHistoryStreamSensorJson(Print &out, const String &path,
                                  uint32_t fromTimestamp, uint16_t maxPoints);
bool chartHistoryStreamRs232Json(Print &out, const String &path,
                                 uint32_t fromTimestamp, uint16_t maxPoints);

// --- Modbus chart support (float32 records, same binary layout as RS232) ---
static const uint8_t CHART_HISTORY_SOURCE_MODBUS     = 3;
static const uint8_t CHART_HISTORY_MODBUS_MAX_COLUMNS = 32;

// A Modbus chart record supports up to 32 float columns (vs 16 for RS232).
struct ModbusChartRecord {
    uint32_t timestamp;
    uint8_t  valueCount;
    uint8_t  reserved[3];
    float    values[CHART_HISTORY_MODBUS_MAX_COLUMNS];

    ModbusChartRecord() : timestamp(0), valueCount(0), reserved{0, 0, 0} {
        for (uint8_t i = 0; i < CHART_HISTORY_MODBUS_MAX_COLUMNS; ++i) values[i] = NAN;
    }
};

// Path: /{safeName}/{safeName}_MODBUS_DD-MM-YYYY.chart
String chartHistoryModbusPathForDate(const String &systemName, const String &dateId);
String chartHistoryModbusPathForTm(const String &systemName, const struct tm &ti);

// Open (or verify) a Modbus chart file with caller-supplied column definitions.
bool chartHistoryOpenModbusWriter(ChartRs232Writer &writer, const String &path,
                                  const ChartColumnDef *defs, uint8_t columnCount,
                                  uint32_t dayStartTs);
// FS-parameterized variant (LittleFS fallback mirror).
bool chartHistoryOpenModbusWriterFS(ChartRs232Writer &writer, fs::FS &fs, const String &path,
                                    const ChartColumnDef *defs, uint8_t columnCount,
                                    uint32_t dayStartTs);

// Append one Modbus record (supports up to MODBUS_MAX_COLUMNS floats).
bool chartHistoryAppendModbusRecord(ChartRs232Writer &writer, const ModbusChartRecord &record);

// Read column metadata from a Modbus chart file.
bool chartHistoryReadModbusMeta(const String &path, uint8_t &columnCount,
                                ChartColumnDef defs[CHART_HISTORY_MODBUS_MAX_COLUMNS]);

// Stream a Modbus chart file as JSON (same structure as chartHistoryStreamRs232Json).
bool chartHistoryStreamModbusJson(Print &out, const String &path,
                                  uint32_t fromTimestamp, uint16_t maxPoints);

// --- Proximity chart support (float32 records, same binary layout as Modbus) ---
static const uint8_t CHART_HISTORY_SOURCE_PROXIMITY      = 4;
// Max columns: PROXIMITY_CHANNELS (up to 8) × 3 metrics (in, out, ppm) = 24
static const uint8_t CHART_HISTORY_PROXIMITY_MAX_COLUMNS = 24;

// Proximity chart record (up to 24 float columns).
struct ProximityChartRecord {
    uint32_t timestamp;
    uint8_t  valueCount;
    uint8_t  reserved[3];
    float    values[CHART_HISTORY_PROXIMITY_MAX_COLUMNS];

    ProximityChartRecord() : timestamp(0), valueCount(0), reserved{0, 0, 0} {
        for (uint8_t i = 0; i < CHART_HISTORY_PROXIMITY_MAX_COLUMNS; ++i) values[i] = NAN;
    }
};

// Path: /{sdcardName}/{sdcardName}_PROXIMITY_DD-MM-YYYY.chart  (alongside Modbus chart)
String chartHistoryProximityPathForDate(const String &systemName, const String &dateId);
String chartHistoryProximityPathForTm(const String &systemName, const struct tm &ti);

// Open (or verify) a proximity chart file with caller-supplied column definitions.
bool chartHistoryOpenProximityWriter(ChartRs232Writer &writer, const String &path,
                                     const ChartColumnDef *defs, uint8_t columnCount,
                                     uint32_t dayStartTs);
// FS-parameterized variant (LittleFS fallback mirror).
bool chartHistoryOpenProximityWriterFS(ChartRs232Writer &writer, fs::FS &fs, const String &path,
                                       const ChartColumnDef *defs, uint8_t columnCount,
                                       uint32_t dayStartTs);

// Append one proximity record (up to PROXIMITY_MAX_COLUMNS floats).
bool chartHistoryAppendProximityRecord(ChartRs232Writer &writer,
                                       const ProximityChartRecord &record);

// Read column metadata from a proximity chart file.
bool chartHistoryReadProximityMeta(const String &path, uint8_t &columnCount,
                                   ChartColumnDef defs[CHART_HISTORY_PROXIMITY_MAX_COLUMNS]);

// Stream a proximity chart file as JSON.
bool chartHistoryStreamProximityJson(Print &out, const String &path,
                                     uint32_t fromTimestamp, uint16_t maxPoints);

// Merged serving (file history + PSRAM ring tail, always minute-fresh):
// read raw float32 records with ts >= fromTs (cap `cap` points, oldest kept
// unless the file holds more — then the OLDEST are dropped, tail survives).
// Dual backend: SD first, then the identical-path LittleFS mirror.
// Returns the points read (0 = missing/unusable/empty, never an error).
uint16_t chartHistoryReadRawPoints(const String &path, uint8_t sourceType, uint8_t maxCols,
                                   uint32_t fromTs, ChartServePoint* out, uint16_t cap);

// Emit the merged point list as JSON (same shape as the file streams:
// count/stride/labels/timestamps + one array per series in `keys` with
// `decimals` places; NaN/inf -> null). Decimation follows chartRingServePlan
// over the union so history + tail share one stride budget.
bool chartHistoryServePointsJson(Print &out, const ChartServePoint* pts, uint32_t nPts,
                                 const char keys[][16], const int8_t* decimals, uint8_t nSeries,
                                 uint16_t maxOut);

#endif

#endif