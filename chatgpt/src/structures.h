// Utility header for shared structures (Amada ISQ20, etc.).
// Keep this file small and modular; structure-specific logic should live here.

#ifndef STRUCTURES_H_INCLUDED
#define STRUCTURES_H_INCLUDED

#pragma once

#include <Arduino.h> // String, millis(), etc.
#include "arduinojson_fix.h" // GCC 12.2.0 fix
#include <ArduinoJson.h> // Lightweight JSON helpers
#include "psram_helpers.h" // PsramAllocatorStd for PSRAM-based containers
#include "ConfigManager.h" // Configuration manager (needed for getAmadaISQ20* methods)

// ============================================================================
// STORAGE MODE CONTROL - Auto-failover SD ↔ PSRAM
// ============================================================================
enum StorageMode {
    STORAGE_MODE_SD = 0,      // Primary: SD Card (persistent)
    STORAGE_MODE_PSRAM = 1,    // Fallback: PSRAM Virtual FS (volatile)
    STORAGE_MODE_NONE = 2      // No storage available (discard data)
};

// Read order is always SD first, then LittleFS fallback (PSRAM stays the
// hot volatile tier in between: it holds sub-flush-interval data).
// STORAGE_BACKEND_* tags each listed file so readers open the right backend.
enum StorageBackend : uint8_t {
    STORAGE_BACKEND_SD = 0,      // SD card (persistent, primary)
    STORAGE_BACKEND_PSRAM = 1,   // PSRAM VFS (volatile, hot)
    STORAGE_BACKEND_FLASH = 2    // LittleFS fallback (persistent, no-SD)
};

// Common file info structure for unified SD/PSRAM/Flash listings
struct StorageFileInfo {
    String name;      // display/attachment basename
    size_t size;
    String fullPath;  // backend-usable path; empty = folderPath + "/" + name
    uint8_t backend = STORAGE_BACKEND_SD;
};

// Prototype for global storage helper (defined in chatgpt.ino)
extern std::vector<StorageFileInfo> storageListFiles(const String &folderPath);

// Timestamped data entry used by SD double-buffering.
// seq provides deterministic ordering when multiple chunks share the same timestamp.
// CRITICAL: Uses std::vector<char> with PSRAM allocator instead of String to avoid
// internal heap fragmentation during high-throughput RS232 reception.
struct TimestampedData {
    unsigned long timestamp_ms; // millis() captured at reception time
    uint32_t seq;               // monotonic sequence number (per source)
    std::vector<char, PsramAllocatorStd<char>> data; // received payload (PSRAM-allocated)
    
    TimestampedData() : timestamp_ms(0), seq(0), data() {}
    
    // Constructor from String (copies to PSRAM vector)
    TimestampedData(unsigned long ts, const String& d, uint32_t s = 0) 
        : timestamp_ms(ts), seq(s), data(d.c_str(), d.c_str() + d.length()) {}
    
    // Constructor from const char* (copies to PSRAM vector) - preferred for zero-copy
    TimestampedData(unsigned long ts, const char* d, size_t len, uint32_t s = 0)
        : timestamp_ms(ts), seq(s), data(d, d + len) {}
    
    // Helper: get data as String (for compatibility with existing code)
    String dataAsString() const {
        if (data.empty()) return String();
        return String(data.data(), data.size());
    }
    
    // Helper: get data as const char* with null terminator (WARNING: modifies data!)
    // Only use when you need C-string and can accept modification
    const char* dataAsCStr() {
        if (data.empty()) return "";
        // Ensure null terminator
        if (data.back() != '\0') data.push_back('\0');
        return data.data();
    }
    
    // Helper: get data size (excluding any null terminator)
    size_t dataLength() const {
        if (data.empty()) return 0;
        // If last char is null terminator, don't count it
        if (data.back() == '\0') return data.size() - 1;
        return data.size();
    }
};

// RS232 live chart point stored in PSRAM so the live chart can avoid SD reads.
// 4096 points cover a full hour even at roughly 1 sample/second.
static const uint8_t RS232_LIVE_MAX_COLS = 16;
static const uint16_t RS232_LIVE_BUFFER_CAPACITY = 4096;

struct Rs232LivePoint {
    uint32_t timestamp;
    uint8_t valueCount;
    float values[RS232_LIVE_MAX_COLS];

    Rs232LivePoint() : timestamp(0), valueCount(0) {
        for (uint8_t i = 0; i < RS232_LIVE_MAX_COLS; ++i) values[i] = NAN;
    }
};

// 📦 Message structure for the serial monitor (fixed buffers to save heap)
struct SerialMessage {
    unsigned long timestamp; // ⏰ Timestamp (millis or epoch, as available)
    char text[96];  // 📝 Message (95 useful chars + \0)
    char type[16];  // 🏷️ Log type ("INFO", "ERROR", etc.)
    
    // 🧠 Helper: set text from String (truncate if needed)
    void setText(const String &s) {
        strncpy(text, s.c_str(), 95);
        text[95] = '\0'; // ✅ Ensure terminator
    }
    
    // 🧠 Helper: set type from const char* (truncate if needed)
    void setType(const char* t) {
        strncpy(type, t, 15);
        type[15] = '\0'; // ✅ Ensure terminator
    }
    
    // 🧠 Clean default constructor
    SerialMessage() : timestamp(0) {
        text[0] = '\0';
        type[0] = '\0';
    }
};


// Forward declarations (implementations in structures.cpp).
void fillStructuresInConfigs(JsonObject &configs, ConfigManager &configManager);
void applyStructuresRuntime(ConfigManager &configManager);
bool isNumericToken(const String &s);
bool isIntegerToken(const String &s);
void splitByWhitespace(const String &line, String tokens[], size_t maxTokens, size_t &count);
void appendFixedLeft(String &out, const String &text, size_t width);
void appendFixedCenter(String &out, const String &text, size_t width);
bool looksLikeISQ20_MTF1(const String &line);
bool buildISQ20_MTF1_Table(const String &rawLine, String &outText);
String buildISQ20_MTF1_TabularHeader();
bool buildISQ20_MTF1_TabularRow(const String &rawLine, String &outRow, const String &timestamp = "");
bool buildISQ20_MTF1_CSVRow(const String &rawLine, String &outRow, const String &timestamp = "");
bool tryFormatISQ20_MTF1(const String &inputLine, String &outText);
bool looksLikeISQ20_AWSE(const String &line);
bool buildISQ20_AWSE_Table(const String &rawLine, String &outText);
String buildISQ20_AWSE_TabularHeader();
bool buildISQ20_AWSE_TabularRow(const String &rawLine, String &outRow, const String &timestamp = "");
bool buildISQ20_AWSE_CSVRow(const String &rawLine, String &outRow, const String &timestamp = "");
bool tryFormatISQ20_AWSE(const String &inputLine, String &outText);
void appendISQ20StructuresToEmail(String &emailBody, const String &candidateLine, ConfigManager &configManager);
void processEmailBodyInsertISQ20Tables(String &body, ConfigManager &configManager, int maxTables);
void buildISQ20TablesChunkByLinesBudget(const String &raw, ConfigManager &configManager, int maxLines, String &outTables, int &rawCutIndex, int &linesUsed);

extern const char* ISQ20_MTF1_SHORT_NAMES[12];
extern const char* ISQ20_AWSE_SHORT_NAMES[21];

#endif // STRUCTURES_H_INCLUDED
