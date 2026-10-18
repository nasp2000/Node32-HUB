// SD Card Rotation Manager
// Manages buffered writes to SD card with:
// - Periodic flush (configurable intervals: 2s to 60m)
// - File rotation at 1024KB max size
// - Ensures complete lines (no split across flushes/files)
// - Email integration (up to 4 files, ~4MB total)

#ifndef SD_ROTATION_MANAGER_H
#define SD_ROTATION_MANAGER_H

#include <Arduino.h>

// Flush-interval helpers shared by crash logging, the data pipeline and the
// proximity CSV flusher. The legacy SDRotationManager was removed (it duplicated
// RS232 data to /logs/data_*.txt with no consumer); only these pure helpers remain.

// Flush interval options (milliseconds)
// Changed from uint16_t to uint32_t to support values up to 3600000 (60 minutes)
enum class FlushInterval : uint32_t {
    INTERVAL_2S   = 2000,
    INTERVAL_10S  = 10000,
    INTERVAL_15S  = 15000,
    INTERVAL_30S  = 30000,
    INTERVAL_1M   = 60000,
    INTERVAL_5M   = 300000,
    INTERVAL_10M  = 600000,
    INTERVAL_30M  = 1800000,
    INTERVAL_60M  = 3600000
};

// Convert interval enum to milliseconds
inline uint32_t intervalToMs(FlushInterval interval) {
    return static_cast<uint32_t>(interval);
}

// Convert interval to string for UI/logging
inline String intervalToString(FlushInterval interval) {
    switch(interval) {
        case FlushInterval::INTERVAL_2S:   return "2s";
        case FlushInterval::INTERVAL_10S:  return "10s";
        case FlushInterval::INTERVAL_15S:  return "15s";
        case FlushInterval::INTERVAL_30S:  return "30s";
        case FlushInterval::INTERVAL_1M:   return "1m";
        case FlushInterval::INTERVAL_5M:   return "5m";
        case FlushInterval::INTERVAL_10M:  return "10m";
        case FlushInterval::INTERVAL_30M:  return "30m";
        case FlushInterval::INTERVAL_60M:  return "60m";
        default: return "Unknown";
    }
}

// Parse string to FlushInterval (returns INTERVAL_30S as default)
inline FlushInterval stringToInterval(const String& str) {
    if(str == "2s") return FlushInterval::INTERVAL_2S;
    if(str == "10s") return FlushInterval::INTERVAL_10S;
    if(str == "15s") return FlushInterval::INTERVAL_15S;
    if(str == "30s") return FlushInterval::INTERVAL_30S;
    if(str == "1m") return FlushInterval::INTERVAL_1M;
    if(str == "5m") return FlushInterval::INTERVAL_5M;
    if(str == "10m") return FlushInterval::INTERVAL_10M;
    if(str == "30m") return FlushInterval::INTERVAL_30M;
    if(str == "60m") return FlushInterval::INTERVAL_60M;
    return FlushInterval::INTERVAL_30S; // default
}

#endif // SD_ROTATION_MANAGER_H
