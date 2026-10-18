#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

#include "../feature_flags.h"
#include "../sensor_rtc_tail.h"

// Compile this module when AHT20/BMP280 is active, or when MQ-135 needs the
// SensorHistory/HistoryPoint infrastructure (extra slots, ring buffer, SD archive).
#if (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1

#include <Arduino.h>
#include <ArduinoJson.h>

// PSRAM and SD card support for large history buffer
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
#include <esp32-hal-psram.h>
#include <esp_heap_caps.h>
#endif

#if ENABLE_SDCARD==1
#include "../sdcard.h"
#include "../chart_history.h"
#include "../littlefs_manager.h" // no-SD LittleFS fallback (begin/mounted/free)
#include <LittleFS.h>
#include <FS.h>
#endif

// PSRAM VFS support (fallback when SD card is absent)
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
#include "../psram_vfs.h"
#endif

bool isChartsPageEnabledRuntime();

// ====================================================================================
// Sensor Configuration Module
// ====================================================================================
// This module provides web-based configuration for environmental sensors (AHT20+BMP280)
// Features:
// - Enable/disable individual sensor readings
// - Calibration offsets for each measurement
// - Automatic reading intervals
// - Multiple output formats (Pretty, CSV)
// - Data injection into RX buffer (simulate RS232/LPT input)
// - Configuration stored in LittleFS (not NVS)
// - History buffer: 7 days (2016 points) in PSRAM with SD card archive
//   * ~20KB allocated in PSRAM (zero DRAM impact)
//   * Auto-save to SD every 100 points (reduces wear)
//   * Older data archived weekly on SD card
//   * Supports 1h, 12h, 24h chart periods; previous days via SD daily file picker

// Reading interval options (in seconds, minimum 1 minute)
enum SensorInterval {
    INTERVAL_1M = 60,
    INTERVAL_2M = 120,
    INTERVAL_3M = 180,
    INTERVAL_5M = 300,
    INTERVAL_10M = 600,
    INTERVAL_30M = 1800,
    INTERVAL_1H = 3600,
    INTERVAL_3H = 10800,
    INTERVAL_6H = 21600,
    INTERVAL_12H = 43200,
    INTERVAL_24H = 86400
};

// Individual sensor reading configuration
struct SensorReadingConfig {
    bool enabled;           // Enable this reading
    float offset;           // Calibration offset (added to raw value)
    
    SensorReadingConfig() : enabled(true), offset(0.0f) {}
};

// Number of generic extra sensor slots (sound, gas, CO2, light, etc.).
// Each slot occupies 2 bytes in HistoryPoint (int16_t, pre-scaled).
// PSRAM impact per slot: HISTORY_BUFFER_SIZE * 2 bytes = ~4 KB.
#define EXTRA_SENSOR_SLOTS 8

// Configuration for one generic extra sensor slot.
// Future sensor modules register themselves here and write data via
// SensorHistory::setExtraSlot() before the next updateHistory() call.
struct ExtraSlotConfig {
    bool  enabled;            // Slot is active and has a sensor writing data
    char  name[20];           // Short CSV column key (e.g., "som_db", "gas_ppm")
    char  label[32];          // Human-readable label shown in charts (e.g., "Som (dB)")
    char  unit[8];            // Unit string (e.g., "dB", "ppm", "lux")
    float scale;              // Stored value = raw_float * scale (e.g., 10 = 1 decimal place)
    float offset;             // Calibration offset applied to raw value before storing
    bool  inject_to_buffer;   // Include this slot's value in the RX buffer injection output

    ExtraSlotConfig() : enabled(false), scale(10.0f), offset(0.0f), inject_to_buffer(false) {
        name[0]  = '\0';
        label[0] = '\0';
        unit[0]  = '\0';
    }
};

// Main sensor configuration structure
struct SensorConfig {
    // Global sensor control
    bool sensor_enabled;                    // Master enable/disable
    SensorInterval read_interval;           // Automatic reading interval
    
    // Output format control
    bool output_pretty;                     // Pretty format with emojis
    bool output_csv;                        // Simple CSV format
    bool inject_to_buffer;                  // Inject into RX buffer
    
    // CSV options
    char csv_separator;                     // ',' '\t' or ';'
    bool csv_include_timestamp;             // Add timestamp column
    bool csv_include_headers;               // First line with column names
    
    // Individual readings configuration
    SensorReadingConfig temp_aht20;         // Temperature from AHT20
    SensorReadingConfig humidity;           // Humidity from AHT20
    SensorReadingConfig temp_bmp280;        // Temperature from BMP280
    SensorReadingConfig pressure_hpa;       // Pressure in hPa
    SensorReadingConfig pressure_mmhg;      // Pressure in mmHg (derived)
    SensorReadingConfig temp_avg;           // Average temperature (derived)
    SensorReadingConfig dew_point;          // Dew point (derived)
    SensorReadingConfig heat_index;         // Heat index (derived)
    SensorReadingConfig altitude;           // Altitude from pressure (derived)

    // Generic extra sensor slots (sound, gas, CO2, light, etc.)
    ExtraSlotConfig extra_slots[EXTRA_SENSOR_SLOTS];

    // Altitude reference
    float sea_level_pressure_hpa;           // Reference pressure for altitude calculation
    
    // Last reading values (for display)
    float last_temp_aht20;
    float last_humidity;
    float last_temp_bmp280;
    float last_pressure_hpa;
    float last_pressure_mmhg;
    float last_temp_avg;
    float last_dew_point;
    float last_heat_index;
    float last_altitude;
    float last_extra[EXTRA_SENSOR_SLOTS];   // Last raw float value per extra slot
    unsigned long last_reading_time;        // Unix timestamp (seconds since 1970) from NTP
    
    // Constructor with defaults
    SensorConfig() :
        sensor_enabled(true),
        read_interval(INTERVAL_1M),
        output_pretty(false),   // Default: off — import format is the default output
        output_csv(true),       // Default: on — sends key-value CSV line to RX buffer
        inject_to_buffer(true), // Default: enabled for intuitive behavior
        csv_separator(','),
        csv_include_timestamp(true),
        csv_include_headers(true),
        sea_level_pressure_hpa(1013.25f),
        last_temp_aht20(0.0f),
        last_humidity(0.0f),
        last_temp_bmp280(0.0f),
        last_pressure_hpa(0.0f),
        last_pressure_mmhg(0.0f),
        last_temp_avg(0.0f),
        last_dew_point(0.0f),
        last_heat_index(0.0f),
        last_altitude(0.0f),
        last_reading_time(0)
    {
        altitude.enabled = false;  // Altitude requires user calibration
        memset(last_extra, 0, sizeof(last_extra));
    }
};

// ====================================================================================
// Alarm Configuration (threshold monitoring)
// ====================================================================================

struct AlarmThreshold {
    bool enabled;                   // Alarm active
    float min_value;                // Low threshold (trigger if below)
    float max_value;                // High threshold (trigger if above)
    bool email_on_alarm;            // Send email when triggered
    unsigned long last_alarm_time;  // Last alarm timestamp (for throttling)
    bool currently_alarming;        // Current alarm state (for hysteresis)
    
    AlarmThreshold() :
        enabled(false),
        min_value(-999.0f),
        max_value(999.0f),
        email_on_alarm(false),
        last_alarm_time(0),
        currently_alarming(false)
    {}
};

struct AlarmConfig {
    AlarmThreshold temperature;     // Temperature alarm (uses temp_avg)
    AlarmThreshold humidity;        // Humidity alarm
    AlarmThreshold pressure;        // Pressure alarm (hPa)
    
    unsigned long alarm_throttle_ms;  // Min time between alarm emails (default 1h)
    float hysteresis_percent;         // Hysteresis to prevent flapping (default 2%)
    
    AlarmConfig() :
        alarm_throttle_ms(3600000),   // 1 hour
        hysteresis_percent(2.0f)      // 2%
    {
        // Example defaults (disabled by default)
        temperature.min_value = 15.0f;
        temperature.max_value = 30.0f;
        humidity.min_value = 30.0f;
        humidity.max_value = 80.0f;
        pressure.min_value = 980.0f;
        pressure.max_value = 1040.0f;
    }
};

// ====================================================================================
// 24-Hour Statistics (min/max/avg tracking)
// ====================================================================================

struct Stats24h {
    float min_value;
    float max_value;
    float sum;
    uint16_t count;
    unsigned long window_start;  // Start of 24h window
    
    Stats24h() : min_value(9999.0f), max_value(-9999.0f), sum(0.0f), count(0), window_start(0) {}
    
    void reset() {
        min_value = 9999.0f;
        max_value = -9999.0f;
        sum = 0.0f;
        count = 0;
        window_start = millis();
    }
    
    void update(float value) {
        if (value < min_value) min_value = value;
        if (value > max_value) max_value = value;
        sum += value;
        count++;
    }
    
    float getAverage() const {
        return (count > 0) ? (sum / count) : 0.0f;
    }
    
    bool isValid() const {
        return count > 0;
    }
};

struct SensorStats {
    Stats24h temperature;  // Temperature stats (uses temp_avg)
    Stats24h humidity;     // Humidity stats
    Stats24h pressure;     // Pressure stats (hPa)
    
    void reset() {
        temperature.reset();
        humidity.reset();
        pressure.reset();
    }
    
    void update(float temp, float hum, float press) {
        temperature.update(temp);
        humidity.update(hum);
        pressure.update(press);
    }
    
    // Check if 24h window expired and reset if needed
    void checkWindowExpiry() {
        unsigned long now = millis();
        unsigned long window_24h = 86400000UL;  // 24h in ms
        
        if (temperature.window_start > 0 && (now - temperature.window_start) > window_24h) {
            reset();
        }
    }
};

// ====================================================================================
// History Ring Buffer (efficient storage for graphing)
// ====================================================================================

// Compact history point — 22 bytes per sample.
// Stores all individual sensor readings and derived values.
// PSRAM usage: 2016 * 22 = ~44 KB
struct HistoryPoint {
    uint32_t timestamp;         // Unix timestamp (4 bytes)
    int16_t temp_aht20_x10;     // AHT20 temperature * 10 (2 bytes)
    int16_t temp_bmp280_x10;    // BMP280 temperature * 10 (2 bytes)
    int16_t temp_avg_x10;       // Average temperature * 10 (2 bytes)
    uint16_t humidity_x10;      // Humidity * 10 (2 bytes)
    uint16_t pressure;          // Pressure in hPa, integer (2 bytes)
    int16_t dew_point_x10;      // Dew point * 10 (2 bytes)
    int16_t heat_index_x10;     // Heat index * 10 (2 bytes)
    int16_t altitude_m;                     // Altitude in meters (2 bytes)
    int16_t extra[EXTRA_SENSOR_SLOTS];      // Generic extra sensor slots (pre-scaled int16)
                                            // PSRAM: 2016 * (22 + 16) = ~76 KB total

    HistoryPoint() :
        timestamp(0), temp_aht20_x10(0), temp_bmp280_x10(0), temp_avg_x10(0),
        humidity_x10(0), pressure(0), dew_point_x10(0), heat_index_x10(0), altitude_m(0) {
        memset(extra, 0, sizeof(extra));
    }

    HistoryPoint(uint32_t ts, float t_aht20, float t_bmp280, float t_avg,
                 float hum, float press_hpa, float dew_pt, float heat_idx, float alt) :
        timestamp(ts),
        temp_aht20_x10((int16_t)(t_aht20 * 10.0f)),
        temp_bmp280_x10((int16_t)(t_bmp280 * 10.0f)),
        temp_avg_x10((int16_t)(t_avg * 10.0f)),
        humidity_x10((uint16_t)(hum * 10.0f)),
        pressure((uint16_t)press_hpa),
        dew_point_x10((int16_t)(dew_pt * 10.0f)),
        heat_index_x10((int16_t)(heat_idx * 10.0f)),
        altitude_m((int16_t)alt) {
        memset(extra, 0, sizeof(extra));
    }

    // Environmental field accessors
    float getTempAht20()    const { return temp_aht20_x10 / 10.0f; }
    float getTempBmp280()   const { return temp_bmp280_x10 / 10.0f; }
    float getTempAvg()      const { return temp_avg_x10 / 10.0f; }
    float getTemperature()  const { return temp_avg_x10 / 10.0f; }  // backward-compat alias
    float getHumidity()     const { return humidity_x10 / 10.0f; }
    float getPressure()     const { return pressure; }
    float getPressureMmhg() const { return pressure * 0.750064f; }
    float getDewPoint()     const { return dew_point_x10 / 10.0f; }
    float getHeatIndex()    const { return heat_index_x10 / 10.0f; }
    float getAltitude()     const { return (float)altitude_m; }

    // Extra slot accessor — scale is ExtraSlotConfig::scale for this slot index
    float getExtra(uint8_t i, float scale) const {
        return (i < EXTRA_SENSOR_SLOTS && scale != 0.0f) ? extra[i] / scale : 0.0f;
    }
};

// POD mirror of HistoryPoint kept in RTC_SLOW_MEM (zero flash wear). No
// constructors: the area is RTC_NOINIT and must stay plain-copyable. The field
// set matches history-merge reads 1:1 so a re-seeded ring streams identically.
struct SensorRtcTailPoint {
    uint32_t timestamp;
    int16_t  temp_aht20_x10;
    int16_t  temp_bmp280_x10;
    int16_t  temp_avg_x10;
    uint16_t humidity_x10;
    uint16_t pressure;
    int16_t  dew_point_x10;
    int16_t  heat_index_x10;
    int16_t  altitude_m;
    int16_t  extra[EXTRA_SENSOR_SLOTS];
};

// RTC mirror container: newest SENSOR_RTC_TAIL_SAMPLES samples, oldest→newest.
// magic is written LAST (after the samples) so a crash mid-mirror keeps the
// previous coherent snapshot; sensorRtcTailValid() rejects power-on garbage.
struct SensorRtcTail {
    uint32_t          magic;     // SENSOR_RTC_MAGIC
    uint32_t          version;   // SENSOR_RTC_VERSION
    uint16_t          count;     // samples mirrored (<= SENSOR_RTC_TAIL_SAMPLES)
    uint16_t          reserved;
    SensorRtcTailPoint p[SENSOR_RTC_TAIL_SAMPLES];
};

#if ENABLE_SDCARD==1
static inline uint8_t buildFixedSensorChartDefs(const SensorConfig &cfg, ChartColumnDef defs[CHART_HISTORY_SENSOR_FIXED_COLUMNS]) {
    memset(defs, 0, sizeof(ChartColumnDef) * CHART_HISTORY_SENSOR_FIXED_COLUMNS);

    auto fillDef = [&](uint8_t index, const char *key, const char *label, const char *unit,
                       int8_t decimals, float scale, bool enabled) {
        strncpy(defs[index].key, key, sizeof(defs[index].key) - 1);
        strncpy(defs[index].label, label, sizeof(defs[index].label) - 1);
        strncpy(defs[index].unit, unit, sizeof(defs[index].unit) - 1);
        defs[index].dataType = CHART_HISTORY_DATA_INT16_SCALED;
        defs[index].decimals = decimals;
        defs[index].scale = scale;
        defs[index].flags = enabled ? 0x0001 : 0x0000;
    };

    fillDef(0, "temp_aht20", "AHT20 Temp", "C", 1, 10.0f, cfg.temp_aht20.enabled);
    fillDef(1, "temp_bmp280", "BMP280 Temp", "C", 1, 10.0f, cfg.temp_bmp280.enabled);
    fillDef(2, "temp_avg", "Avg Temp", "C", 1, 10.0f, cfg.temp_avg.enabled);
    fillDef(3, "humidity", "Humidity", "%", 1, 10.0f, cfg.humidity.enabled);
    fillDef(4, "pressure_hpa", "Pressure", "hPa", 0, 1.0f, cfg.pressure_hpa.enabled);
    fillDef(5, "dew_point", "Dew Point", "C", 1, 10.0f, cfg.dew_point.enabled);
    fillDef(6, "heat_index", "Heat Index", "C", 1, 10.0f, cfg.heat_index.enabled);
    fillDef(7, "altitude", "Altitude", "m", 0, 1.0f, cfg.altitude.enabled);

    for (uint8_t slot = 0; slot < EXTRA_SENSOR_SLOTS; slot++) {
        char key[16];
        snprintf(key, sizeof(key), "extra_%u", slot);
        const char *label = cfg.extra_slots[slot].label[0] ? cfg.extra_slots[slot].label : key;
        const char *unit = cfg.extra_slots[slot].unit;
        float scale = cfg.extra_slots[slot].scale != 0.0f ? cfg.extra_slots[slot].scale : 10.0f;
        int8_t decimals = (scale >= 10.0f) ? 1 : 0;
        fillDef(8 + slot, key, label, unit, decimals, scale, cfg.extra_slots[slot].enabled);
    }

    return CHART_HISTORY_SENSOR_FIXED_COLUMNS;
}

static inline void buildFixedSensorChartRecord(const HistoryPoint &pt, SensorChartRecord &record) {
    record = SensorChartRecord();
    record.timestamp = pt.timestamp;

    auto setValue = [&](uint8_t index, int16_t value) {
        record.values[index] = value;
        record.validMask |= (1UL << index);
    };

    setValue(0, pt.temp_aht20_x10);
    setValue(1, pt.temp_bmp280_x10);
    setValue(2, pt.temp_avg_x10);
    setValue(3, (int16_t)pt.humidity_x10);
    setValue(4, (int16_t)pt.pressure);
    setValue(5, pt.dew_point_x10);
    setValue(6, pt.heat_index_x10);
    setValue(7, pt.altitude_m);
    for (uint8_t slot = 0; slot < EXTRA_SENSOR_SLOTS; slot++) {
        setValue(8 + slot, pt.extra[slot]);
    }
}
#endif

// Ring buffer for history storage
// 2016 points = 7 days at 5min intervals = ~20KB (allocated in PSRAM)
// 288 points = 24h at 5min = ~2.8KB
// 144 points = 12h at 5min = ~1.4KB
#define HISTORY_BUFFER_SIZE 2016  // 7 days at 5min intervals (~20KB in PSRAM)

struct SensorHistory {
    HistoryPoint *buffer;   // Dynamically allocated in PSRAM
    uint16_t capacity;      // Buffer capacity (HISTORY_BUFFER_SIZE)
    uint16_t head;          // Next write position
    uint16_t count;         // Number of valid samples
    unsigned long last_save_time;  // Last time we saved a point
    uint32_t save_interval_ms;     // Interval between saves (default 5min)
    String sd_file_path;    // SD card file for persistence (/sensor_history.csv)
    
    uint32_t last_periodic_flush_ts;       // Unix timestamp of newest point written to SD
    uint32_t last_psram_flush_ts;          // Unix timestamp of newest point written to PSRAM VFS

    int16_t pendingExtraRaw[EXTRA_SENSOR_SLOTS]; // Pre-scaled extra values staged for next addPoint()
    bool    pendingExtraSet[EXTRA_SENSOR_SLOTS];  // Which pending slots have been written this cycle

    SensorHistory() : buffer(nullptr), capacity(HISTORY_BUFFER_SIZE), head(0), count(0),
                      last_save_time(0), save_interval_ms(300000), sd_file_path("/sensor_history.csv"),
                      last_periodic_flush_ts(0), last_psram_flush_ts(0) {
        memset(pendingExtraRaw, 0, sizeof(pendingExtraRaw));
        memset(pendingExtraSet, 0, sizeof(pendingExtraSet));
    }

    // Stage a pre-scaled value for an extra sensor slot before the next addPoint() call.
    // The caller applies ExtraSlotConfig::scale:
    //   history.setExtraSlot(slot, (int16_t)(raw_float * cfg.extra_slots[slot].scale));
    // Staged values are committed into HistoryPoint::extra[] on the next addPoint() and then cleared.
    void setExtraSlot(uint8_t slot, int16_t scaledValue) {
        if (slot < EXTRA_SENSOR_SLOTS) {
            pendingExtraRaw[slot] = scaledValue;
            pendingExtraSet[slot] = true;
        }
    }
    
    ~SensorHistory() {
        end();
    }
    
    // Initialize buffer in PSRAM
    bool begin() {
        if (buffer != nullptr) return true; // Already initialized
        
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        if (psramFound()) {
            buffer = (HistoryPoint*)heap_caps_malloc(capacity * sizeof(HistoryPoint), MALLOC_CAP_SPIRAM);
            if (buffer) {
                Serial.printf("[SENSOR_HISTORY] Allocated %u points (%u KB) in PSRAM\n", 
                              capacity, (capacity * sizeof(HistoryPoint)) / 1024);
                // Data is recovered from the daily sub-folder via periodicFlushToSD/loadDailyFile.
                // The old root /sensor_history.csv is no longer used.
                return true;
            }
        }
#endif
        // Fallback to regular heap if PSRAM not available
        buffer = (HistoryPoint*)malloc(capacity * sizeof(HistoryPoint));
        if (buffer) {
            Serial.printf("[SENSOR_HISTORY] Allocated %u points (%u KB) in heap (PSRAM not available)\n",
                          capacity, (capacity * sizeof(HistoryPoint)) / 1024);
            return true;
        }
        
        Serial.println("[SENSOR_HISTORY] ERROR: Failed to allocate buffer!");
        return false;
    }
    
    // Free buffer memory
    void end() {
        if (buffer) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
            if (psramFound()) {
                heap_caps_free(buffer);
            } else
#endif
            {
                free(buffer);
            }
            buffer = nullptr;
        }
    }
    
    void setSaveInterval(uint32_t interval_ms) {
        save_interval_ms = interval_ms;
    }
    
    bool shouldSave() const {
        unsigned long now = millis();
        return (now - last_save_time) >= save_interval_ms;
    }
    
    // Add a full reading to the ring buffer.
    // All computed values are passed from SensorConfigManager::updateHistory().
    void addPoint(float t_aht20, float t_bmp280, float t_avg,
                  float hum, float press_hpa, float dew_pt, float heat_idx, float alt) {
        if (!buffer) {
            Serial.println("[SENSOR_HISTORY] CRITICAL: Buffer is NULL! Cannot add point. Call begin() first!");
            return;
        }

        // Use NTP time if available, else fall back to millis()
        time_t now = time(nullptr);
        uint32_t ts = (now > 1000000000) ? (uint32_t)now : (millis() / 1000);

        uint16_t newHead = head;
        buffer[newHead] = HistoryPoint(ts, t_aht20, t_bmp280, t_avg, hum, press_hpa, dew_pt, heat_idx, alt);

        // Bake pending extra slot values into the new point, then clear staging buffers
        for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
            buffer[newHead].extra[s] = pendingExtraSet[s] ? pendingExtraRaw[s] : 0;
            pendingExtraSet[s] = false;
            pendingExtraRaw[s] = 0;
        }

        head = (newHead + 1) % capacity;
        if (count < capacity) count++;
        last_save_time = millis();
        // Persistence is handled exclusively by periodicFlushToSD() called at
        // the configured MicroSD flush interval (default 30m).
        // saveToSD() (old root CSV) is intentionally not called here.
    }
    
    uint16_t getCount() const { return count; }
    
    // Oldest retained sample timestamp (0 when the buffer is empty).
    uint32_t oldestTimestamp() const { return (count > 0) ? getPoint(0).timestamp : 0; }
    
    // Get point at index (0 = oldest, count-1 = newest)
    const HistoryPoint& getPoint(uint16_t index) const {
        if (!buffer) {
            static HistoryPoint dummy;
            return dummy;
        }
        
        uint16_t actualIndex;
        if (count < capacity) {
            // Buffer not full: points start at 0
            actualIndex = index;
        } else {
            // Buffer full: points start at head (oldest)
            actualIndex = (head + index) % capacity;
        }
        return buffer[actualIndex];
    }
    
    void clear() {
        head = 0;
        count = 0;
        last_save_time = 0;
    }
    
    // Legacy root-CSV save — disabled. Use periodicFlushToSD() instead.
    bool saveToSD() { return false; }
    
    // Legacy root-CSV load — disabled. History is loaded via loadDailyFile() on demand.
    bool loadFromSD() { return false; }
    
    // Append only new points (since last periodic flush) to a daily SD file.
    // File path: /sensor_history/YYYY-MM-DD/YYYY-MM-DD.csv (one subfolder per day).
    // Only writes columns for readings that are enabled in cfg.
    // Called every 30 minutes from SensorConfigManager::periodicPersist().
    bool periodicFlushToSD(const SensorConfig &cfg) {
#if ENABLE_SDCARD==1
        if (!buffer || count == 0) return false;
        // Dual sink: SD card when mounted, else LittleFS mirror at identical
        // paths (.csv + .chart, 30min cadence — small files by design).
        const bool useFlash = !sdcardShouldUseSD();
        if (useFlash) {
            if (!littlefsBegin() || !littlefsMounted()) return false;
            if (littlefsFreeBytes() < (1024ULL * 1024ULL)) return false; // /config reserve
        }

        time_t now = time(nullptr);
        if (now < 1000000000) {
            // NTP not synced yet: cannot name daily file correctly, skip write.
            // Data stays safe in PSRAM buffer and will be flushed after NTP sync.
            Serial.printf("[SENSOR_HISTORY] periodicFlushToSD: NTP not synced (time=%lu), skipping. Buffer has %u points.\n", (unsigned long)now, count);
            return false;
        }

        struct tm ti;
        localtime_r(&now, &ti);

        // Daily file: /sensor_history/YYYY-MM-DD/YYYY-MM-DD.csv (one subfolder per day)
        char dirPath[] = "/sensor_history";
        char subDirPath[36];
        snprintf(subDirPath, sizeof(subDirPath), "/sensor_history/%04d-%02d-%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        char filename[64];
        snprintf(filename, sizeof(filename), "/sensor_history/%04d-%02d-%02d/%04d-%02d-%02d.csv",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
        String chartFilename = chartHistorySensorPathForTm(ti);
        uint32_t dayStartTs = (uint32_t)now - (uint32_t)(ti.tm_hour * 3600UL) - (uint32_t)(ti.tm_min * 60UL) - (uint32_t)ti.tm_sec;

        fs::FS* fsp = useFlash ? &getLittleFS() : &SD_MMC;
#if ENABLE_FILESYSTEM==1
        // Serialize LittleFS metadata+write ops vs the email/FTP tasks (same
        // crash guard as the SMTP sender: concurrent lfs dir traversal
        // panics). On contention the flush is skipped — the ring keeps the
        // points and the next interval retries.
        extern bool lfsMutexLock(TickType_t timeoutTicks);
        extern void lfsMutexUnlock();
        bool sensLfsHeld = false;
        if (useFlash) {
            sensLfsHeld = lfsMutexLock(pdMS_TO_TICKS(5000));
            if (!sensLfsHeld) {
                Serial.println(F("[SENSOR_HISTORY] periodicFlushToSD: LittleFS busy, skipped"));
                return false;
            }
        }
#endif
        if (!fsp->exists(dirPath)) {
            fsp->mkdir(dirPath);
        }
        if (!fsp->exists(subDirPath)) {
            fsp->mkdir(subDirPath);
        }

        File file = fsp->open(filename, FILE_APPEND);
        if (!file) {
            Serial.printf("[SENSOR_HISTORY] periodicFlushToSD: cannot open %s\n", filename);
#if ENABLE_FILESYSTEM==1
            if (sensLfsHeld) lfsMutexUnlock();
#endif
            return false;
        }

        // Write header on first write to this file.
        // Only include columns for enabled readings.
        if (file.size() == 0) {
            String hdr = "timestamp";
            if (cfg.temp_aht20.enabled)    hdr += ",temp_aht20";
            if (cfg.temp_bmp280.enabled)   hdr += ",temp_bmp280";
            if (cfg.temp_avg.enabled)      hdr += ",temp_avg";
            if (cfg.humidity.enabled)      hdr += ",humidity";
            if (cfg.pressure_hpa.enabled)  hdr += ",pressure_hpa";
            if (cfg.pressure_mmhg.enabled) hdr += ",pressure_mmhg";
            if (cfg.dew_point.enabled)     hdr += ",dew_point";
            if (cfg.heat_index.enabled)    hdr += ",heat_index";
            if (cfg.altitude.enabled)      hdr += ",altitude";
            // Use slot name when configured (e.g. "mq135_co2"), else generic "extra_N"
            for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
                if (cfg.extra_slots[s].enabled) {
                    hdr += ',';
                    if (cfg.extra_slots[s].name[0] != '\0') hdr += cfg.extra_slots[s].name;
                    else { hdr += "extra_"; hdr += s; }
                }
            }
            file.println(hdr);
        }

        ChartSensorWriter chartWriter;
        bool chartOpen = false;
        if (isChartsPageEnabledRuntime()) {
            ChartColumnDef chartDefs[CHART_HISTORY_SENSOR_FIXED_COLUMNS];
            uint8_t chartColumnCount = buildFixedSensorChartDefs(cfg, chartDefs);
            if (useFlash) {
                chartOpen = chartHistoryOpenSensorWriterFS(chartWriter, *fsp, chartFilename, chartDefs,
                                                           chartColumnCount, dayStartTs);
            } else {
                chartOpen = chartHistoryOpenSensorWriter(chartWriter, chartFilename, chartDefs,
                                                         chartColumnCount, dayStartTs);
            }
            if (!chartOpen) {
                Serial.printf("[SENSOR_HISTORY] periodicFlushToSD: cannot open %s\n", chartFilename.c_str());
            }
        }

        uint16_t written = 0;
        uint32_t newestWritten = last_periodic_flush_ts;
        for (uint16_t i = 0; i < count; i++) {
            const HistoryPoint &pt = getPoint(i);
            if (pt.timestamp > last_periodic_flush_ts) {
                // Build CSV row with only enabled columns
                char row[256];  // Extra size for up to 8 extra slot values
                char *p = row;
                int remaining = (int)sizeof(row);
                // Write ISO 8601 datetime (human-readable, also parseable by loadDailyFile)
                { time_t ts_ = (time_t)pt.timestamp; struct tm *ti_ = localtime(&ts_);
                  int n_ = snprintf(p, remaining, "%04d-%02d-%02d %02d:%02d:%02d",
                      ti_->tm_year+1900, ti_->tm_mon+1, ti_->tm_mday,
                      ti_->tm_hour, ti_->tm_min, ti_->tm_sec);
                  p += n_; remaining -= n_; }
                int n = 0;

                if (cfg.temp_aht20.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getTempAht20()); p += n; remaining -= n; }
                if (cfg.temp_bmp280.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getTempBmp280()); p += n; remaining -= n; }
                if (cfg.temp_avg.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getTempAvg()); p += n; remaining -= n; }
                if (cfg.humidity.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getHumidity()); p += n; remaining -= n; }
                if (cfg.pressure_hpa.enabled && remaining > 0) {
                    // Use pt.pressure directly (uint16_t) — avoids float→%u UB that yields 0
                    n = snprintf(p, remaining, ",%u", (unsigned)pt.pressure); p += n; remaining -= n; }
                if (cfg.pressure_mmhg.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getPressureMmhg()); p += n; remaining -= n; }
                if (cfg.dew_point.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getDewPoint()); p += n; remaining -= n; }
                if (cfg.heat_index.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getHeatIndex()); p += n; remaining -= n; }
                if (cfg.altitude.enabled && remaining > 0) {
                    n = snprintf(p, remaining, ",%.1f", pt.getAltitude()); p += n; remaining -= n; }
                // Append enabled extra slot values
                for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS && remaining > 0; s++) {
                    if (cfg.extra_slots[s].enabled) {
                        float sc = (cfg.extra_slots[s].scale != 0.0f) ? cfg.extra_slots[s].scale : 10.0f;
                        n = snprintf(p, remaining, ",%.2f", pt.extra[s] / sc); p += n; remaining -= n;
                    }
                }

                file.println(row);
                if (chartOpen) {
                    SensorChartRecord chartRecord;
                    buildFixedSensorChartRecord(pt, chartRecord);
                    if (!chartHistoryAppendSensorRecord(chartWriter, chartRecord)) {
                        chartOpen = false;
                        Serial.printf("[SENSOR_HISTORY] periodicFlushToSD: write failed for %s\n", chartFilename.c_str());
                    }
                }
                if (pt.timestamp > newestWritten) newestWritten = pt.timestamp;
                written++;
            }
        }

        file.close();
        chartHistoryCloseSensorWriter(chartWriter);
        last_periodic_flush_ts = newestWritten;
        Serial.printf("[SENSOR_HISTORY] Periodic flush: %u new points -> %s | %s\n", written, filename, chartFilename.c_str());
#if ENABLE_FILESYSTEM==1
        if (sensLfsHeld) lfsMutexUnlock();
#endif
        return written > 0;
#else
        // No SD: delegate to PSRAM VFS so SENSORDAY can email current session data.
        return periodicFlushToPSRAM(cfg);
#endif
    }

    // Flush new sensor history points to PSRAM VFS (fallback when SD card is absent).
    // Writes CSV rows under source "SENSOR_HIST" into today's PSRAM virtual folder.
    bool periodicFlushToPSRAM(const SensorConfig &cfg) {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
        if (!buffer || count == 0) return false;
        if (!psramVFSIsEnabled()) return false;

        time_t now = time(nullptr);
        if (now < 1000000000) return false;  // NTP not synced yet

        // Write CSV header once per boot (PSRAM files are volatile, always start fresh)
        static bool headerWritten = false;
        if (!headerWritten) {
            String hdr = F("timestamp");
            if (cfg.temp_aht20.enabled)    hdr += F(",temp_aht20");
            if (cfg.temp_bmp280.enabled)   hdr += F(",temp_bmp280");
            if (cfg.temp_avg.enabled)      hdr += F(",temp_avg");
            if (cfg.humidity.enabled)      hdr += F(",humidity");
            if (cfg.pressure_hpa.enabled)  hdr += F(",pressure_hpa");
            if (cfg.pressure_mmhg.enabled) hdr += F(",pressure_mmhg");
            if (cfg.dew_point.enabled)     hdr += F(",dew_point");
            if (cfg.heat_index.enabled)    hdr += F(",heat_index");
            if (cfg.altitude.enabled)      hdr += F(",altitude");
            // Use slot name when configured, else generic "extra_N"
            for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
                if (cfg.extra_slots[s].enabled) {
                    hdr += ',';
                    if (cfg.extra_slots[s].name[0] != '\0') hdr += cfg.extra_slots[s].name;
                    else { hdr += F("extra_"); hdr += s; }
                }
            }
            hdr += '\n';
            psramVFSWrite("SENSOR_HIST", hdr.c_str(), hdr.length());
            headerWritten = true;
        }

        uint16_t written = 0;
        uint32_t newestWritten = last_psram_flush_ts;

        for (uint16_t i = 0; i < count; i++) {
            const HistoryPoint &pt = getPoint(i);
            if (pt.timestamp <= last_psram_flush_ts) continue;

            char row[256];
            char *p = row;
            int rem = (int)sizeof(row);
            int n;

            // ISO 8601 datetime — human-readable and parseable by loadDailyFile
            { time_t ts_ = (time_t)pt.timestamp; struct tm *ti_ = localtime(&ts_);
              int n_ = snprintf(p, rem, "%04d-%02d-%02d %02d:%02d:%02d",
                  ti_->tm_year+1900, ti_->tm_mon+1, ti_->tm_mday,
                  ti_->tm_hour, ti_->tm_min, ti_->tm_sec);
              p += n_; rem -= n_; }
            n = 0;
            if (cfg.temp_aht20.enabled   && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getTempAht20());   p += n; rem -= n; }
            if (cfg.temp_bmp280.enabled  && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getTempBmp280());  p += n; rem -= n; }
            if (cfg.temp_avg.enabled     && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getTempAvg());     p += n; rem -= n; }
            if (cfg.humidity.enabled     && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getHumidity());    p += n; rem -= n; }
            if (cfg.pressure_hpa.enabled && rem > 0) {
                // Use pt.pressure directly (uint16_t) — avoids float→%u UB that yields 0
                n = snprintf(p, rem, ",%u", (unsigned)pt.pressure); p += n; rem -= n; }
            if (cfg.pressure_mmhg.enabled && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getPressureMmhg()); p += n; rem -= n; }
            if (cfg.dew_point.enabled    && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getDewPoint());    p += n; rem -= n; }
            if (cfg.heat_index.enabled   && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getHeatIndex());   p += n; rem -= n; }
            if (cfg.altitude.enabled     && rem > 0) { n = snprintf(p, rem, ",%.1f", pt.getAltitude());    p += n; rem -= n; }
            for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS && rem > 0; s++) {
                if (cfg.extra_slots[s].enabled) {
                    float sc = (cfg.extra_slots[s].scale != 0.0f) ? cfg.extra_slots[s].scale : 10.0f;
                    n = snprintf(p, rem, ",%.2f", pt.extra[s] / sc); p += n; rem -= n;
                }
            }
            if (rem > 1) { *p++ = '\n'; *p = '\0'; }

            psramVFSWrite("SENSOR_HIST", row, strlen(row));
            if (pt.timestamp > newestWritten) newestWritten = pt.timestamp;
            written++;
        }

        last_psram_flush_ts = newestWritten;
        if (written > 0) {
            Serial.printf("[SENSOR_HISTORY] PSRAM flush: %u new points\n", written);
        }
        return written > 0;
#else
        return false;
#endif
    }

    // archiveOldData removed — only daily files are kept (/sensor_history/YYYY-MM-DD/).
    // The ring buffer wraps around silently; periodicFlushToSD writes all new points to SD.
    bool archiveOldData(const SensorConfig* = nullptr) { return false; }

    // listArchivedWeeks removed — weekly archive no longer exists.
    String listArchivedWeeks() const { return "[]"; }

    // loadArchivedWeek removed — weekly archive no longer exists.
    String loadArchivedWeek(const String &) const {
        return "{\"count\":0,\"labels\":[],\"temperature\":[],\"humidity\":[],\"pressure\":[]}";
    }

    // Build a JSON date list from the in-RAM ring buffer.
    // Returns unique dates (YYYY-MM-DD) present in the buffer, sorted newest-first.
    // Used when SD is unavailable so charts still have data.
    String buildBufferDateList() const {
        if (!buffer || count == 0) return "[]";
        // Collect unique YYYY-MM-DD strings (up to 8 distinct days)
        char seen[8][11];  // max 8 days, 10 chars + null
        uint8_t nSeen = 0;
        // Iterate newest-first (head-1 down to head-count)
        for (int i = (int)count - 1; i >= 0; i--) {
            const HistoryPoint &pt = buffer[(head - 1 - i + capacity) % capacity];
            if (pt.timestamp < 1000000000UL) continue;
            time_t ts = (time_t)pt.timestamp;
            struct tm *ti = localtime(&ts);
            char date[11];
            snprintf(date, sizeof(date), "%04d-%02d-%02d",
                     ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
            // Check if already seen
            bool found = false;
            for (uint8_t d = 0; d < nSeen; d++) {
                if (strncmp(seen[d], date, 10) == 0) { found = true; break; }
            }
            if (!found && nSeen < 8) {
                strncpy(seen[nSeen++], date, 10);
                seen[nSeen-1][10] = '\0';
            }
        }
        if (nSeen == 0) return "[]";
        String result = "[";
        for (uint8_t d = 0; d < nSeen; d++) {
            if (d > 0) result += ",";
            result += '"';
            result += seen[d];
            result += '"';
        }
        result += "]";
        return result;
    }

    // Build chart-compatible JSON from the in-RAM ring buffer, filtered by date (YYYY-MM-DD).
    // Produces the exact same JSON structure as loadDailyFile() from SD.
    // Safe to call at any time — only reads the ring buffer, never touches SD or PSRAM VFS.
    String buildBufferDailyJSON(const String &dateId) const {
        const String empty = "{\"count\":0,\"labels\":[],\"timestamps\":[]}";
        if (!buffer || count == 0) return empty;

        String labels, timestamps;
        String col_temp_aht20, col_temp_bmp280, col_temp_avg;
        String col_humidity, col_pressure_hpa, col_pressure_mmhg;
        String col_dew_point, col_heat_index, col_altitude;
        String col_extra[EXTRA_SENSOR_SLOTS];
        uint16_t nPts = 0;

        for (uint16_t i = 0; i < count; i++) {
            const HistoryPoint &pt = buffer[(head - count + i + capacity) % capacity];
            if (pt.timestamp < 1000000000UL) continue;

            // Filter by date
            time_t ts = (time_t)pt.timestamp;
            struct tm *ti = localtime(&ts);
            char date[11];
            snprintf(date, sizeof(date), "%04d-%02d-%02d",
                     ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
            if (dateId != date) continue;

            char timeStr[6];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d", ti->tm_hour, ti->tm_min);

            if (nPts > 0) {
                labels += ","; timestamps += ",";
                col_temp_aht20 += ","; col_temp_bmp280 += ","; col_temp_avg += ",";
                col_humidity += ","; col_pressure_hpa += ","; col_pressure_mmhg += ",";
                col_dew_point += ","; col_heat_index += ","; col_altitude += ",";
                for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) col_extra[s] += ",";
            }
            labels     += '"'; labels     += timeStr; labels     += '"';
            timestamps += String(pt.timestamp);

            char buf[16];
            snprintf(buf, sizeof(buf), "%.1f", pt.getTempAht20());   col_temp_aht20  += buf;
            snprintf(buf, sizeof(buf), "%.1f", pt.getTempBmp280());  col_temp_bmp280 += buf;
            snprintf(buf, sizeof(buf), "%.1f", pt.getTempAvg());     col_temp_avg    += buf;
            snprintf(buf, sizeof(buf), "%.1f", pt.getHumidity());    col_humidity    += buf;
            snprintf(buf, sizeof(buf), "%u",   (unsigned)pt.pressure); col_pressure_hpa += buf; // direct uint16_t avoids float→%u UB
            snprintf(buf, sizeof(buf), "%.1f", pt.getPressureMmhg()); col_pressure_mmhg += buf;
            snprintf(buf, sizeof(buf), "%.1f", pt.getDewPoint());    col_dew_point   += buf;
            snprintf(buf, sizeof(buf), "%.1f", pt.getHeatIndex());   col_heat_index  += buf;
            snprintf(buf, sizeof(buf), "%.1f", pt.getAltitude());    col_altitude    += buf;
            for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
                snprintf(buf, sizeof(buf), "%.1f", pt.extra[s] / 10.0f);
                col_extra[s] += buf;
            }
            nPts++;
        }
        if (nPts == 0) return empty;

        String json;
        json.reserve(nPts * 80 + 512);
        json += "{\"count\":" + String(nPts);
        json += ",\"labels\":[" + labels + "],\"timestamps\":[" + timestamps + "]";
        json += ",\"temp_aht20\":[" + col_temp_aht20 + "]";
        json += ",\"temp_bmp280\":[" + col_temp_bmp280 + "]";
        json += ",\"temperature\":[" + col_temp_avg + "],\"temp_avg\":[" + col_temp_avg + "]";
        json += ",\"humidity\":[" + col_humidity + "]";
        json += ",\"pressure\":[" + col_pressure_hpa + "],\"pressure_hpa\":[" + col_pressure_hpa + "]";
        json += ",\"pressure_mmhg\":[" + col_pressure_mmhg + "]";
        json += ",\"dew_point\":[" + col_dew_point + "]";
        json += ",\"heat_index\":[" + col_heat_index + "]";
        json += ",\"altitude\":[" + col_altitude + "]";
        for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++)
            json += ",\"extra_" + String(s) + "\":[" + col_extra[s] + "]";
        json += "}";
        return json;
    }

    // Build a complete CSV string for a given date (YYYY-MM-DD) from the in-RAM ring buffer.
    // Header + data rows, same column order as periodicFlushToSD().
    // Only includes columns enabled in cfg. No filesystem access — safe to call at any time.
    // Used by SENSORDAY to send sensor data via email when SD is absent.
    String exportDayCSV(const String &dateId, const SensorConfig &cfg) const {
        if (!buffer || count == 0) return "";

        // Build header line (same logic as periodicFlushToSD)
        String csv = "timestamp";
        if (cfg.temp_aht20.enabled)    csv += ",temp_aht20";
        if (cfg.temp_bmp280.enabled)   csv += ",temp_bmp280";
        if (cfg.temp_avg.enabled)      csv += ",temp_avg";
        if (cfg.humidity.enabled)      csv += ",humidity";
        if (cfg.pressure_hpa.enabled)  csv += ",pressure_hpa";
        if (cfg.pressure_mmhg.enabled) csv += ",pressure_mmhg";
        if (cfg.dew_point.enabled)     csv += ",dew_point";
        if (cfg.heat_index.enabled)    csv += ",heat_index";
        if (cfg.altitude.enabled)      csv += ",altitude";
        // Use slot name when configured, else generic "extra_N"
        for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
            if (cfg.extra_slots[s].enabled) {
                csv += ',';
                if (cfg.extra_slots[s].name[0] != '\0') csv += cfg.extra_slots[s].name;
                else { csv += "extra_"; csv += s; }
            }
        }
        csv += "\n";

        uint16_t written = 0;
        for (uint16_t i = 0; i < count; i++) {
            const HistoryPoint &pt = getPoint(i);
            if (pt.timestamp < 1000000000UL) continue;

            // Filter by date
            time_t ts = (time_t)pt.timestamp;
            struct tm *ti = localtime(&ts);
            char date[11];
            snprintf(date, sizeof(date), "%04d-%02d-%02d",
                     ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
            if (dateId != date) continue;

            // Build row — same column order as header above
            char row[256];
            char *p = row;
            int rem = (int)sizeof(row);
            // ISO 8601 datetime — human-readable and parseable by loadDailyFile
            { time_t ts_ = (time_t)pt.timestamp; struct tm *ti_ = localtime(&ts_);
              int n_ = snprintf(p, rem, "%04d-%02d-%02d %02d:%02d:%02d",
                  ti_->tm_year+1900, ti_->tm_mon+1, ti_->tm_mday,
                  ti_->tm_hour, ti_->tm_min, ti_->tm_sec);
              p += n_; rem -= n_; }
            int n = 0;

            if (cfg.temp_aht20.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getTempAht20()); p += n; rem -= n; }
            if (cfg.temp_bmp280.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getTempBmp280()); p += n; rem -= n; }
            if (cfg.temp_avg.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getTempAvg()); p += n; rem -= n; }
            if (cfg.humidity.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getHumidity()); p += n; rem -= n; }
            if (cfg.pressure_hpa.enabled && rem > 0) {
                // Use pt.pressure directly (uint16_t) — avoids float→%u UB that yields 0
                n = snprintf(p, rem, ",%u", (unsigned)pt.pressure); p += n; rem -= n; }
            if (cfg.pressure_mmhg.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getPressureMmhg()); p += n; rem -= n; }
            if (cfg.dew_point.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getDewPoint()); p += n; rem -= n; }
            if (cfg.heat_index.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getHeatIndex()); p += n; rem -= n; }
            if (cfg.altitude.enabled && rem > 0) {
                n = snprintf(p, rem, ",%.1f", pt.getAltitude()); p += n; rem -= n; }
            for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS && rem > 0; s++) {
                if (cfg.extra_slots[s].enabled) {
                    float sc = (cfg.extra_slots[s].scale != 0.0f) ? cfg.extra_slots[s].scale : 10.0f;
                    n = snprintf(p, rem, ",%.2f", pt.extra[s] / sc); p += n; rem -= n;
                }
            }

            csv += row;
            csv += "\n";
            written++;
        }

        if (written == 0) return "";
        return csv;
    }

    // No-SD: dates from the LittleFS /sensor_history mirror merged with the
    // live RAM buffer (dedup). Mirrors the SD scan filter (YYYY-MM-DD dirs).
    String buildFlashMergedDateList() const {
        String seen[24];
        uint8_t nSeen = 0;
        auto note = [&](const String &d) {
            if (d.length() != 10) return;
            for (uint8_t i = 0; i < nSeen; i++) {
                if (seen[i] == d) return;
            }
            if (nSeen < 24) seen[nSeen++] = d;
        };
        if (littlefsBegin() && littlefsMounted() && LittleFS.exists("/sensor_history")) {
            File root = LittleFS.open("/sensor_history");
            if (root && root.isDirectory()) {
                File file = root.openNextFile();
                while (file) {
                    String name = file.name();
                    bool isDir = file.isDirectory();
                    int slashPos = name.lastIndexOf('/');
                    String base = name.substring(slashPos + 1);
                    file.close();
                    if (isDir && base.length() == 10 &&
                        isDigit(base[0]) && base[4] == '-' && base[7] == '-') {
                        note(base);
                    }
                    file = root.openNextFile();
                    yield();
                }
                root.close();
            }
        }
        // + dates from the live ring (quoted tokens of the buffer JSON list)
        String buf = buildBufferDateList();
        int p = 0;
        while ((p = buf.indexOf('"', p)) >= 0) {
            int q = buf.indexOf('"', p + 1);
            if (q < 0) break;
            note(buf.substring(p + 1, q));
            p = q + 1;
        }
        String result = "[";
        for (uint8_t i = 0; i < nSeen; i++) {
            if (i > 0) result += ",";
            result += "\"" + seen[i] + "\"";
        }
        result += "]";
        return result;
    }

    // List daily history files.
    // Primary: SD card /sensor_history/YYYY-MM-DD/ subdirectories.
    // No-SD: LittleFS mirror merged with the in-RAM ring buffer dates.
    // Fallback: dates present in the in-RAM ring buffer (file not found).
    String listDailyFiles() const {
#if ENABLE_SDCARD==1
        if (!sdcardShouldUseSD()) return buildFlashMergedDateList();
        if (!SD_MMC.exists("/sensor_history")) return buildBufferDateList();

        File root = SD_MMC.open("/sensor_history");
        if (!root || !root.isDirectory()) return buildBufferDateList();

        String result = "[";
        bool first = true;

        File file = root.openNextFile();
        while (file) {
            String name = file.name();
            // Match per-day subdirectory names: YYYY-MM-DD (10 chars)
            int slashPos = name.lastIndexOf('/');
            String base = name.substring(slashPos + 1);
            if (file.isDirectory() && base.length() == 10 &&
                isDigit(base[0]) && base[4] == '-' && base[7] == '-') {
                if (!first) result += ",";
                result += "\"" + base + "\"";
                first = false;
            }
            file.close();
            file = root.openNextFile();
        }

        root.close();
        result += "]";
        return result;
        // SD path gave results — return them directly
#else
        // SD not compiled: fall through to buffer below
#endif
        // No SD or SD in fallback: serve dates from RAM ring buffer
        return buildBufferDateList();
    }

    // Load one daily history file and return chart-compatible JSON.
    // dateId format: "YYYY-MM-DD"
    // Primary: SD /sensor_history/YYYY-MM-DD/YYYY-MM-DD.csv, then the
    // LittleFS mirror at the identical path (no-SD packs), then RAM.
    // Fallback: in-RAM ring buffer filtered by date (file not found).
    // Produces identical JSON structure on both paths — charts work transparently.
    String loadDailyFile(const String &dateId, const SensorConfig *cfgPtr = nullptr) const {
        const String empty = "{\"count\":0,\"labels\":[],\"timestamps\":[]}";
#if ENABLE_SDCARD==1
        // Files are stored in per-day subfolders: /sensor_history/YYYY-MM-DD/YYYY-MM-DD.csv
        String filename = "/sensor_history/" + dateId + "/" + dateId + ".csv";
        File file;
        if (sdcardShouldUseSD()) file = SD_MMC.open(filename.c_str(), FILE_READ);
        if (!file && littlefsBegin() && littlefsMounted()) {
            file = LittleFS.open(filename.c_str(), FILE_READ);
        }
        if (!file) {
            Serial.printf("[SENSOR_HISTORY] Daily file not found on SD/Flash: %s, serving from RAM buffer\n", filename.c_str());
            return buildBufferDailyJSON(dateId);
        }

        // Read header line and parse column names
        String header = file.readStringUntil('\n');
        header.trim();

        // Map column index -> column name (9 env fields + 8 extra slots + timestamp = 18 max)
        String colNames[20];
        int colCount = 0;
        {
            int pos = 0;
            while (pos <= (int)header.length() && colCount < 20) {
                int comma = header.indexOf(',', pos);
                if (comma < 0) comma = header.length();
                String col = header.substring(pos, comma);
                col.trim();
                colNames[colCount++] = col;
                pos = comma + 1;
            }
        }

        // Accumulators per column name
        String labels, timestamps;
        String col_temp_aht20, col_temp_bmp280, col_temp_avg, col_temperature;
        String col_humidity, col_pressure_hpa, col_pressure_mmhg;
        String col_dew_point, col_heat_index, col_altitude;
        String col_extra[EXTRA_SENSOR_SLOTS];  // Extra slot series accumulators
        String col_extra_key[EXTRA_SENSOR_SLOTS]; // Actual column name in the file per slot

        // Detect which legacy name is present for temperature (old files used "temperature")
        bool has_temp_aht20 = false, has_temp_bmp280 = false, has_temp_avg = false;
        bool has_temperature_legacy = false;
        bool has_humidity = false, has_pressure_hpa = false, has_pressure_legacy = false;
        bool has_pressure_mmhg = false, has_dew_point = false;
        bool has_heat_index = false, has_altitude = false;
        bool has_extra[EXTRA_SENSOR_SLOTS] = {};

        for (int c = 0; c < colCount; c++) {
            if (colNames[c] == "temp_aht20")    has_temp_aht20    = true;
            else if (colNames[c] == "temp_bmp280")   has_temp_bmp280   = true;
            else if (colNames[c] == "temp_avg")      has_temp_avg      = true;
            else if (colNames[c] == "temperature")   has_temperature_legacy = true; // old format
            else if (colNames[c] == "humidity")      has_humidity      = true;
            else if (colNames[c] == "pressure_hpa")  has_pressure_hpa  = true;
            else if (colNames[c] == "pressure")      has_pressure_legacy = true;   // old format
            else if (colNames[c] == "pressure_mmhg") has_pressure_mmhg = true;
            else if (colNames[c] == "dew_point")     has_dew_point     = true;
            else if (colNames[c] == "heat_index")    has_heat_index    = true;
            else if (colNames[c] == "altitude")      has_altitude      = true;
            else {
                // Detect generic extra slot columns: "extra_0" ... "extra_N"
                for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
                    char xkey[10];
                    snprintf(xkey, sizeof(xkey), "extra_%u", s);
                    if (colNames[c] == xkey) { has_extra[s] = true; col_extra_key[s] = xkey; break; }
                }
                // Also detect named extra slot columns (new firmware, e.g., "mq135_co2")
                if (cfgPtr) {
                    for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
                        if (cfgPtr->extra_slots[s].name[0] != '\0' &&
                            colNames[c] == cfgPtr->extra_slots[s].name) {
                            has_extra[s] = true; col_extra_key[s] = cfgPtr->extra_slots[s].name; break;
                        }
                    }
                }
            }
        }

        uint16_t count = 0;

        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            // Parse CSV row into column values
            String vals[20];
            int nVals = 0;
            int pos = 0;
            while (pos <= (int)line.length() && nVals < 20) {
                int comma = line.indexOf(',', pos);
                if (comma < 0) comma = line.length();
                vals[nVals++] = line.substring(pos, comma);
                pos = comma + 1;
            }
            if (nVals < 1) continue;

            // Parse timestamp: supports ISO 8601 (YYYY-MM-DD HH:MM:SS, new format)
            // and unix epoch (all digits, old format) for backward compatibility.
            uint32_t ts = 0;
            {
                const String &tv = vals[0];
                if (tv.length() >= 19 && tv[4] == '-') {
                    // ISO 8601 format
                    struct tm t = {};
                    int y,mo,d,h,mi,s;
                    if (sscanf(tv.c_str(), "%d-%d-%d %d:%d:%d", &y,&mo,&d,&h,&mi,&s) == 6) {
                        t.tm_year = y-1900; t.tm_mon = mo-1; t.tm_mday = d;
                        t.tm_hour = h; t.tm_min = mi; t.tm_sec = s; t.tm_isdst = -1;
                        ts = (uint32_t)mktime(&t);
                    }
                } else {
                    ts = (uint32_t)tv.toInt(); // legacy unix epoch
                }
            }

            // Format HH:MM label
            time_t raw = (time_t)ts;
            struct tm *ti = localtime(&raw);
            char timeStr[6];
            snprintf(timeStr, sizeof(timeStr), "%02d:%02d", ti->tm_hour, ti->tm_min);

            if (count > 0) { labels += ","; timestamps += ","; }
            labels     += "\"" + String(timeStr) + "\"";
            timestamps += String(ts);

            // Helper: get value string for a named column
            auto getColVal = [&](const String &name) -> String {
                for (int c = 0; c < colCount && c < nVals; c++) {
                    if (colNames[c] == name) return vals[c];
                }
                return "null";
            };

            auto appendSeries = [&](String &series, const String &colName) {
                if (count > 0) series += ",";
                series += getColVal(colName);
            };

            if (has_temp_aht20)        appendSeries(col_temp_aht20,    "temp_aht20");
            if (has_temp_bmp280)       appendSeries(col_temp_bmp280,   "temp_bmp280");
            if (has_temp_avg)          appendSeries(col_temp_avg,       "temp_avg");
            else if (has_temperature_legacy) appendSeries(col_temperature, "temperature"); // old format fallback
            if (has_humidity)          appendSeries(col_humidity,       "humidity");
            if (has_pressure_hpa)      appendSeries(col_pressure_hpa,  "pressure_hpa");
            else if (has_pressure_legacy)    appendSeries(col_pressure_hpa, "pressure"); // old format fallback
            if (has_pressure_mmhg)     appendSeries(col_pressure_mmhg, "pressure_mmhg");
            if (has_dew_point)         appendSeries(col_dew_point,     "dew_point");
            if (has_heat_index)        appendSeries(col_heat_index,    "heat_index");
            if (has_altitude)          appendSeries(col_altitude,      "altitude");
            // Accumulate extra slot series
            for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
                if (has_extra[s]) {
                    // Use actual column name from file (handles both "extra_N" and named slots)
                    appendSeries(col_extra[s], col_extra_key[s]);
                }
            }

            count++;
        }

        file.close();

        // Build JSON response with all available series
        String json;
        json.reserve(count * 70 + 512);
        json += "{\"count\":" + String(count);
        json += ",\"labels\":[" + labels + "],\"timestamps\":[" + timestamps + "]";

        // Include each series present in file
        if (has_temp_aht20)
            json += ",\"temp_aht20\":[" + col_temp_aht20 + "]";
        if (has_temp_bmp280)
            json += ",\"temp_bmp280\":[" + col_temp_bmp280 + "]";
        if (has_temp_avg)
            json += ",\"temperature\":[" + col_temp_avg + "],\"temp_avg\":[" + col_temp_avg + "]";
        else if (has_temperature_legacy)
            json += ",\"temperature\":[" + col_temperature + "],\"temp_avg\":[" + col_temperature + "]";
        if (has_humidity)
            json += ",\"humidity\":[" + col_humidity + "]";
        if (has_pressure_hpa || has_pressure_legacy)
            json += ",\"pressure\":[" + col_pressure_hpa + "],\"pressure_hpa\":[" + col_pressure_hpa + "]";
        if (has_pressure_mmhg)
            json += ",\"pressure_mmhg\":[" + col_pressure_mmhg + "]";
        if (has_dew_point)
            json += ",\"dew_point\":[" + col_dew_point + "]";
        if (has_heat_index)
            json += ",\"heat_index\":[" + col_heat_index + "]";
        if (has_altitude)
            json += ",\"altitude\":[" + col_altitude + "]";
        // Emit extra slot series present in this file
        for (uint8_t s = 0; s < EXTRA_SENSOR_SLOTS; s++) {
            if (has_extra[s])
                json += ",\"extra_" + String(s) + "\":[" + col_extra[s] + "]";
        }
        json += "}";
        return json;
#else
        // SD not compiled: serve from RAM ring buffer
        return buildBufferDailyJSON(dateId);
#endif
    }

    // Stream daily CSV file as chart JSON directly to a Print target.
    // Uses multi-pass SD reads + PSRAM timestamp buffer — peak heap ~1 KB.
    void streamDailyFile(Print &out, const String &dateId, const SensorConfig *cfgPtr = nullptr) const;
    // Stream only a time window from the daily CSV file.
    // Used when RAM history does not cover the requested 1h/8h window after a reboot.
    void streamDailyWindow(Print &out, const String &dateId, uint32_t fromTimestamp, uint16_t maxPoints,
                           const SensorConfig *cfgPtr = nullptr) const;
};

// ====================================================================================
// Sensor Configuration Manager
// ====================================================================================

class SensorConfigManager {
private:
    SensorConfig config;
    AlarmConfig alarms;
    SensorStats stats;
    SensorHistory history;
    String configFilePath;
    unsigned long lastAutoReadTime;
    bool initialized;
    
    // Internal helpers
    bool loadFromFile();
    bool saveToFile();
    void applyDefaults();
    
    // Alarm helpers
    bool checkAlarm(AlarmThreshold &alarm, float value, const String &name);
    void sendAlarmEmail(const String &alarmType, float value, float threshold, bool isHigh);
    
public:
    SensorConfigManager();
    
    // Initialization
    bool begin(const String &filePath = "/config/sensor_config.json");
    bool isInitialized() const { return initialized; }
    
    // Configuration access
    SensorConfig& getConfig() { return config; }
    const SensorConfig& getConfig() const { return config; }
    AlarmConfig& getAlarms() { return alarms; }
    const AlarmConfig& getAlarms() const { return alarms; }
    SensorStats& getStats() { return stats; }
    const SensorStats& getStats() const { return stats; }
    
    // Load/save
    bool load();
    bool save();
    bool reset();  // Reset to defaults and save
    
    // JSON serialization
    bool toJson(JsonDocument &doc) const;
    bool fromJson(const JsonDocument &doc);
    String toJsonString() const;
    bool fromJsonString(const String &jsonStr);
    
    // Reading operations
    bool shouldAutoRead();  // Check if interval elapsed
    void markReadingDone(); // Update last reading timestamp
    bool performReading();  // Force immediate reading and apply offsets
    // Alarm checking (called after each reading)
    void checkAlarms();
    
    // Statistics
    void updateStats();     // Update 24h stats with latest reading
    void resetStats();      // Clear 24h statistics
    
    // History management
    SensorHistory& getHistory() { return history; }
    const SensorHistory& getHistory() const { return history; }
    void updateHistory();   // Add current reading to history (if interval elapsed)
    void clearHistory();    // Clear all history
    void setHistorySaveInterval(uint32_t interval_ms); // Set history save interval
    String formatHistoryJSON(uint16_t maxPoints = 0, uint32_t fromTimestamp = 0) const; // Format history as JSON; fromTimestamp filters by time (0 = use maxPoints only)
    void streamHistoryJSON(Print &out, uint16_t maxPoints = 0, uint32_t fromTimestamp = 0) const; // Stream history JSON to Print — avoids large String heap allocation
    // Stream [fromTimestamp, now) as one envelope: SD .chart head (persisted
    // pre-boot samples) + the live ring tail, backfilling what the buffer does
    // not cover. Used when the ring no longer reaches back to fromTimestamp.
    void streamMergedWindow(Print &out, const String &dateId, uint32_t fromTimestamp,
                            uint16_t maxPoints) const;
    
    // Periodic persistence: SD flush at the configured MicroSD flush interval
    // (default 30m; page-configurable 30m/20m/10m/...), LittleFS config save every 8h.
    // Call from loop() — uses internal millis() timers, safe to call every loop iteration.
    void periodicPersist();

    // Data injection
    void injectToBuffer();  // Send formatted data to RX buffer
    
    // Output formatters
    String formatPretty() const;
    String formatCSV(bool includeHeaders = false) const;
    String formatStats() const;  // Format 24h statistics
};

// Global instance
extern SensorConfigManager sensorConfigManager;

#endif // (ENABLE_AHT20_BMP280==1 && ENABLE_SENSOR_CONFIG_PAGE==1) || ENABLE_MQ135==1
#endif // SENSOR_CONFIG_H
