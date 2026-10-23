// ftp_status_store.h — persisted "last sends" state for the FTP page.
//
// The live FtpStatus (ftp_client.h) is RAM-only: after a reboot the page showed
// "OK 0 / FAIL 0" and no file, so the operator could not see what had been
// uploaded (or why it failed) 12 h earlier. This store is the durable summary:
// last cycle time, upload totals and the newest ~8 sends, written ONLY on an
// upload event (never the hot path) to /config/ftp_status.json by
// ftp_client.cpp. The only_new journal (/config/ftp_sent.log) is untouched —
// different purpose, different format.
//
// Header-only and host-testable (tests/native/test_ftp_status_store.cpp): no
// filesystem access here, only pure data + JSON.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <Arduino.h>
#include <ArduinoJson.h>

// Kept short: basenames from the configured filters ("turno1_2026-10-23_05-59.txt").
#define FTP_STATUS_RECENT_MAX  8
#define FTP_STATUS_NAME_MAX    64
#define FTP_STATUS_RESULT_MAX  96

struct FtpSendRecord {
    char     name[FTP_STATUS_NAME_MAX];
    uint32_t size;   // bytes streamed
    bool     ok;     // STOR + SIZE verify
    uint32_t ts;     // epoch seconds (UTC); 0 when the clock was not set
};

struct FtpStatusStore {
    uint32_t      lastTs;       // epoch of the last recorded send
    uint32_t      okCount;      // cumulative uploads, survives reboot
    uint32_t      failCount;    // cumulative failures, survives reboot
    char          lastResult[FTP_STATUS_RESULT_MAX];
    uint8_t       count;        // valid entries in recent[] (newest first)
    FtpSendRecord recent[FTP_STATUS_RECENT_MAX];
};

// Copy a plain log/JSON-safe string (printable ASCII, single line).
static inline void ftpStatusCopyText(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    size_t o = 0;
    if (src) {
        for (const char* p = src; *p && o + 1 < cap; p++) {
            unsigned char c = (unsigned char)*p;
            if (c < 0x20 || c == 0x7f) continue;  // CR/LF/TAB/control out
            dst[o++] = (char)c;
        }
    }
    dst[o] = '\0';
}

static inline void ftpStatusStoreInit(FtpStatusStore& s) {
    memset(&s, 0, sizeof(s));
}

// Record one upload attempt (newest first) and bump the persisted totals.
// Returns the index of the new entry (always 0).
static inline int ftpStatusStoreAddSend(FtpStatusStore& s, const char* file, uint32_t size,
                                        bool ok, uint32_t ts, const char* result) {
    if (s.count > FTP_STATUS_RECENT_MAX) s.count = FTP_STATUS_RECENT_MAX;  // paranoid
    const uint8_t keep = (s.count < FTP_STATUS_RECENT_MAX) ? s.count : (uint8_t)(FTP_STATUS_RECENT_MAX - 1);
    for (int i = keep; i > 0; i--) s.recent[i] = s.recent[i - 1];
    ftpStatusCopyText(s.recent[0].name, sizeof(s.recent[0].name), file);
    s.recent[0].size = size;
    s.recent[0].ok = ok;
    s.recent[0].ts = ts;
    if (s.count < FTP_STATUS_RECENT_MAX) s.count++;
    s.lastTs = ts;
    ftpStatusCopyText(s.lastResult, sizeof(s.lastResult), result);
    if (ok) s.okCount++; else s.failCount++;
    return 0;
}

// "DD-MM-YYYY HH:MM:SS" (local time) or "-" when the clock was never set.
static inline void ftpStatusFormatTs(uint32_t ts, char* out, size_t cap) {
    if (!out || cap == 0) return;
    if (ts == 0) { snprintf(out, cap, "-"); return; }
    time_t t = (time_t)ts;
    struct tm lt;
    if (!localtime_r(&t, &lt)) { snprintf(out, cap, "-"); return; }
    snprintf(out, cap, "%02d-%02d-%04d %02d:%02d:%02d",
             lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900,
             lt.tm_hour, lt.tm_min, lt.tm_sec);
}

// Serialize the store (file format on LittleFS). ~1.2 KB worst case → the
// 2 KB document below has headroom; escaping is ArduinoJson's job.
static inline String ftpStatusStoreToJson(const FtpStatusStore& s) {
    DynamicJsonDocument doc(2048);
    doc["v"] = 1;
    doc["last_time"] = s.lastTs;
    doc["ok_count"] = s.okCount;
    doc["fail_count"] = s.failCount;
    doc["last_result"] = s.lastResult;
    JsonArray arr = doc.createNestedArray("recent");
    const uint8_t n = (s.count < FTP_STATUS_RECENT_MAX) ? s.count : (uint8_t)FTP_STATUS_RECENT_MAX;
    for (uint8_t i = 0; i < n; i++) {
        JsonObject o = arr.createNestedObject();
        o["file"] = s.recent[i].name;
        o["size"] = s.recent[i].size;
        o["ok"] = s.recent[i].ok;
        o["ts"] = s.recent[i].ts;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

// Tolerant parse: missing/extra keys are ignored (older/newer file), but a
// malformed document leaves the store untouched and returns false.
static inline bool ftpStatusStoreFromJson(const String& json, FtpStatusStore& s) {
    if (json.length() == 0) return false;
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return false;
    FtpStatusStore tmp;
    ftpStatusStoreInit(tmp);
    tmp.lastTs = doc["last_time"] | (uint32_t)0;
    tmp.okCount = doc["ok_count"] | (uint32_t)0;
    tmp.failCount = doc["fail_count"] | (uint32_t)0;
    ftpStatusCopyText(tmp.lastResult, sizeof(tmp.lastResult), doc["last_result"] | "");
    JsonArray arr = doc["recent"].as<JsonArray>();
    for (JsonVariant o : arr) {
        if (tmp.count >= FTP_STATUS_RECENT_MAX) break;
        FtpSendRecord& r = tmp.recent[tmp.count];
        ftpStatusCopyText(r.name, sizeof(r.name), o["file"] | "");
        r.size = o["size"] | (uint32_t)0;
        r.ok = o["ok"] | false;
        r.ts = o["ts"] | (uint32_t)0;
        if (r.name[0] == '\0') continue;  // skip junk entries, keep the slot
        tmp.count++;
    }
    s = tmp;
    return true;
}
