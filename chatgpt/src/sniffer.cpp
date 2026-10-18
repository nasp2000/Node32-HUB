// =============================================================
// Serial sniffer — implementation (dual passive channels)
// =============================================================
// Implements two independent receivers that listen in parallel.
// Aggregates bytes until CR (\r) and/or LF (\n); when a terminator is seen,
// invokes a per-line callback.
// NOTE (S3-N16R8): uses PSRAM buffers to avoid fragmenting internal RAM.
// On ESP32 we use two HardwareSerial instances in RX-only mode via the GPIO matrix:
//   - Channel 1: UART1 (Serial1) RX-only at SNIFFER1_RX_PIN
//   - Channel 2: UART2 (Serial2) RX-only at SNIFFER2_RX_PIN
// =============================================================
#include <Arduino.h>
#include "sniffer.h"
#include "littlefs_manager.h"
#include <LittleFS.h>
#include <time.h>

#if defined(ESP32)
#include "psram_helpers.h" // Helper for safe allocation in PSRAM/heap
#endif

#include "feature_flags.h"
#if ENABLE_SNIFFER==1

// Avoid conflict: use dedicated Serial1 and Serial2 when sniffer is active
static HardwareSerial SNIFF1(1); // UART1
static HardwareSerial SNIFF2(2); // UART2

// PSRAM buffers (char arrays) instead of String to avoid heap fragmentation
// Generous buffer size (128KB) - PSRAM is available on N16R8 modules.
// Allows capturing long bursts without heap fragmentation or data loss.
static const size_t SNIFF_BUF_SIZE = 128 * 1024; 
static char* buf1 = nullptr;
static size_t idx1 = 0;

static char* buf2 = nullptr;
static size_t idx2 = 0;
// Use global PSRAM allocator
static SpiRamAllocator allocator;

static SnifferLineCallback onLine = nullptr; // Registered callback

// Helper: process a byte and invoke the line callback when a terminator is found
static inline void feedByte(uint8_t ch, char* &buf, size_t &idx, uint8_t channel) {
    if (!buf) return; // Safety: allocation failed
    if (ch == 0) return; // Ignore NUL bytes

    // If terminator, emit line
    if (ch == '\r' || ch == '\n') {
        if (idx > 0 && onLine) {
            buf[idx] = 0; // Null terminate
            onLine(channel, buf, idx); // invoke line callback
        }
        idx = 0; // Reset index
        return;
    }

    // Normal case: append byte
    if (idx < SNIFF_BUF_SIZE - 1) {
        buf[idx++] = (char)ch;
    } else {
        // Buffer full: flush partial content to avoid data loss
        if (onLine) {
            buf[idx] = 0;
            onLine(channel, buf, idx);
        }
        idx = 0;
        buf[idx++] = (char)ch;
    }
}

void snifferBegin(uint32_t baud, uint32_t serialCfg, SnifferLineCallback cb) {
    onLine = cb; // Register callback

    // Allocate large buffers in PSRAM if available, otherwise fall back to heap
    if (!buf1) {
        buf1 = (char*)allocator.allocate(SNIFF_BUF_SIZE);
        buf2 = (char*)allocator.allocate(SNIFF_BUF_SIZE);
    }

    // Reset line accumulators so partial lines from the old baud don't bleed into the new config
    idx1 = 0;
    idx2 = 0;

    // Close any already-running UARTs before reconfiguring — without end() the new
    // baud/framing is silently ignored by the ESP32 IDF UART driver.
    SNIFF1.end();
    SNIFF2.end();

    // Initialize UART1 and UART2 as RX-only (TX disabled, no invert).
    SNIFF1.begin(baud, serialCfg, SNIFFER1_RX_PIN, -1, false); // RX only, no invert
    SNIFF2.begin(baud, serialCfg, SNIFFER2_RX_PIN, -1, false); // RX only, no invert
}

void snifferPoll() {
    // Read everything available from each UART and feed the buffers
    while (SNIFF1.available() > 0) { feedByte((uint8_t)SNIFF1.read(), buf1, idx1, 1); }
    while (SNIFF2.available() > 0) { feedByte((uint8_t)SNIFF2.read(), buf2, idx2, 2); }
}

// ----------------------------------------------------------------
// LittleFS periodic flush — 32 KB PSRAM staging buffer
// Lines are pushed here by snifferQueueForFlush() and written to
// /sniffer/YYYY-MM-DD.log every 30 seconds by snifferFlushToLittleFS().
// ----------------------------------------------------------------
static const size_t SNIFF_FLUSH_BUF_SIZE = 32 * 1024;
static char* flushBuf = nullptr;
static size_t flushIdx = 0;
static SpiRamAllocator flushAllocator;

void snifferQueueForFlush(uint8_t channel, const char* line, size_t len) {
    if (!flushBuf) {
        flushBuf = (char*)flushAllocator.allocate(SNIFF_FLUSH_BUF_SIZE);
        if (!flushBuf) return; // PSRAM not available
    }

    // Build prefix: [HH:MM:SS][S1] or [S1] if NTP not synced
    char prefix[28];
    size_t pLen = 0;
    time_t nowTs = time(nullptr);
    if (nowTs > 100000) {
        struct tm ti;
        localtime_r(&nowTs, &ti);
        pLen = (size_t)snprintf(prefix, sizeof(prefix), "[%02d:%02d:%02d][S%u] ",
                                ti.tm_hour, ti.tm_min, ti.tm_sec, (unsigned)channel);
    } else {
        pLen = (size_t)snprintf(prefix, sizeof(prefix), "[S%u] ", (unsigned)channel);
    }

    size_t needed = pLen + len + 2; // +2 for \r\n
    if (flushIdx + needed >= SNIFF_FLUSH_BUF_SIZE) return; // Staging buffer full, drop line

    memcpy(flushBuf + flushIdx, prefix, pLen); flushIdx += pLen;
    memcpy(flushBuf + flushIdx, line,   len);  flushIdx += len;
    flushBuf[flushIdx++] = '\r';
    flushBuf[flushIdx++] = '\n';

    // Early flush: if staging buffer reaches 24 KB, flush immediately without waiting for the 30s timer
    if (flushIdx >= 24 * 1024) {
        snifferFlushToLittleFS();
    }
}

void snifferFlushToLittleFS() {
    if (!flushBuf || flushIdx == 0) return; // Nothing queued

    if (!littlefsBegin()) return; // LittleFS not available

    // Build dated file path
    char path[36];
    time_t nowTs = time(nullptr);
    if (nowTs > 100000) {
        struct tm ti;
        localtime_r(&nowTs, &ti);
        snprintf(path, sizeof(path), "/sniffer/%04d-%02d-%02d.log",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    } else {
        strncpy(path, "/sniffer/nodate.log", sizeof(path));
    }

    // Ensure directory exists
    if (!LittleFS.exists("/sniffer")) LittleFS.mkdir("/sniffer");

    // Append to file (no tmp→rename: append-mode partial writes are acceptable for logs)
    File f = LittleFS.open(path, "a");
    if (!f) return;

    size_t written = f.write((const uint8_t*)flushBuf, flushIdx);
    f.close();
    storNoteLfsWrite(written);

    if (written > 0) flushIdx = 0; // Clear staging buffer only if write succeeded
}

#endif // ENABLE_SNIFFER
