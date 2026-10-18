#pragma once
// Day-report fallback journal (EMAILDAY / LOGDAY / SENSORDAY / SHIFTDAY).
//
// Problem: sched_dt marks a daily slot as done even when the email fails,
// and the 3 immediate attempts in emailDayTask do not survive reboot/panic.
// This module keeps a tiny persistent outbox in LittleFS (/config/dayrep.json)
// so a missed report is retried ~20min later.
//
// Design (safe, minimal):
// - Core decision logic below is pure C (no Arduino deps) so it is unit
//   tested natively and shared verbatim by the firmware.
// - Persistence + task hooks live in day_report_fallback.cpp and are thin.
// - Journal stores the *intent* (slot + full cmd line + dayKey + schedEpoch),
//   NOT the file contents. The SD folder itself is the data store.
// - Idempotency key = slot + dayKey. A SENT entry never refires.
// - Only day-report commands are tracked; everything else is ignored.

#include <ctype.h>
#include <string.h>

namespace dayrep {

// 20 minutes after the scheduled time before the first fallback fires.
static const long kFallbackDelaySec = 20L * 60L;
// Max fallback re-dispatches per slot+day (on top of the 3 immediate attempts).
static const int kMaxFallbackAttempts = 3;
// Retry window: ignore entries older than this (stale SD folders may rotate).
static const long kRetryWindowSec = 24L * 3600L;
// Max tracked entries in RAM/flash (covers 8 sched_dt slots + manual slot 0 + spare).
static const int kMaxEntries = 10;
// Manual (non-scheduler) commands use slot 0.
static const int kManualSlot = 0;

enum Status {
    ST_PENDING = 0,
    ST_SENT = 1,
    ST_FAILED = 2
};

struct Entry {
    int slot;              // sched_dt slot 1..16, or 0 for manual
    char cmd[12];          // EMAILDAY / LOGDAY / SENSORDAY / SHIFTDAY (upper)
    char cmdLine[64];      // full command line for re-dispatch (e.g. "emailday 29-12-2025")
    int dayKey;            // YYYYMMDD of the scheduled day
    long schedEpoch;       // epoch when originally scheduled
    int status;            // Status
    int attempts;          // fallback dispatches so far (not counting immediate 3x)
    long lastTryEpoch;     // epoch of last fallback dispatch (0 = never)
};

inline void entryInit(Entry& e) {
    e.slot = -1;
    e.cmd[0] = '\0';
    e.cmdLine[0] = '\0';
    e.dayKey = -1;
    e.schedEpoch = 0;
    e.status = ST_PENDING;
    e.attempts = 0;
    e.lastTryEpoch = 0;
}

// Case-insensitive prefix match for the 4 day-report commands.
// Accepts "emailday", "EMAILDAY 29-12-2025", "  logday", etc.
// Returns true and copies the canonical upper-case verb into outCmd (if given).
inline bool isDayReportCommand(const char* line, char* outCmd, unsigned outSize) {
    if (!line) return false;
    while (*line == ' ' || *line == '\t') line++;
    char verb[12];
    unsigned n = 0;
    while (line[n] && line[n] != ' ' && line[n] != '\t' && n < sizeof(verb) - 1) {
        verb[n] = (char)toupper((unsigned char)line[n]);
        n++;
    }
    verb[n] = '\0';
    const char* match = NULL;
    if (strcmp(verb, "EMAILDAY") == 0) match = "EMAILDAY";
    else if (strcmp(verb, "LOGDAY") == 0) match = "LOGDAY";
    else if (strcmp(verb, "SENSORDAY") == 0) match = "SENSORDAY";
    else if (strcmp(verb, "SHIFTDAY") == 0) match = "SHIFTDAY";
    if (!match) return false;
    if (outCmd && outSize > 0) {
        unsigned i = 0;
        while (match[i] && i + 1 < outSize) {
            outCmd[i] = match[i];
            i++;
        }
        outCmd[i] = '\0';
    }
    return true;
}

// Eligible for a fallback dispatch now?
// - must be PENDING or FAILED (never SENT)
// - must have a valid schedEpoch and a sane nowEpoch (NTP synced)
// - now must be >= sched + 20min (and >= lastTry + 20min for spacing)
// - attempts < max, and entry younger than the retry window
inline bool needsRetry(const Entry& e, long nowEpoch) {
    if (e.slot < 0 || e.dayKey < 0) return false;
    if (e.status == ST_SENT) return false;
    if (e.schedEpoch <= 1000 || nowEpoch <= 1000) return false;
    if (nowEpoch < e.schedEpoch) return false;  // clock went backwards: never fire early
    if (nowEpoch - e.schedEpoch < kFallbackDelaySec) return false;
    if (nowEpoch - e.schedEpoch > kRetryWindowSec) return false;
    if (e.attempts >= kMaxFallbackAttempts) return false;
    if (e.lastTryEpoch > 0 && nowEpoch - e.lastTryEpoch < kFallbackDelaySec) return false;
    if (e.cmdLine[0] == '\0') return false;
    return true;
}

// True if the entry can never fire again (older than the retry window).
// Such entries are dead weight and must be pruned so the journal rotates.
inline bool isStale(const Entry& e, long nowEpoch) {
    if (e.slot < 0) return true;
    if (e.schedEpoch <= 1000 || nowEpoch <= 1000) return false;  // clock unknown: keep
    return (nowEpoch - e.schedEpoch) > kRetryWindowSec;
}

// Find entry index for slot+dayKey, or -1.
inline int findEntry(const Entry* list, int count, int slot, int dayKey) {
    for (int i = 0; i < count; ++i) {
        if (list[i].slot == slot && list[i].dayKey == dayKey) return i;
    }
    return -1;
}

// True if the command line already carries an explicit DD-MM-YYYY date param
// (e.g. "emailday 29-12-2025"). Bare commands ("emailday") return false and
// must be pinned to the scheduled day before journaling, otherwise a retry
// after midnight would resolve to the wrong day's folder.
inline bool hasDateParam(const char* line) {
    if (!line) return false;
    size_t len = strlen(line);
    for (size_t i = 0; i + 10 <= len; ++i) {
        const char* p = line + i;
        // look for DD-MM-YYYY: 2 digits, '-', 2 digits, '-', 4 digits
        if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] == '-' &&
            p[3] >= '0' && p[3] <= '9' && p[4] >= '0' && p[4] <= '9' && p[5] == '-' &&
            p[6] >= '0' && p[6] <= '9' && p[7] >= '0' && p[7] <= '9' &&
            p[8] >= '0' && p[8] <= '9' && p[9] >= '0' && p[9] <= '9') {
            int day = (p[0] - '0') * 10 + (p[1] - '0');
            int mon = (p[3] - '0') * 10 + (p[4] - '0');
            int year = (p[6] - '0') * 1000 + (p[7] - '0') * 100 + (p[8] - '0') * 10 + (p[9] - '0');
            if (day >= 1 && day <= 31 && mon >= 1 && mon <= 12 && year >= 2000 && year <= 2099) return true;
        }
    }
    return false;
}

// Day-boundary rule with 1h tolerance: reports dispatched between 00:00 and
// 00:59 belong to the previous day's files; from 01:00 on they belong to the
// current day. Returns the epoch whose calendar date must label the report.
// Calendar correctness (month/year rollover) comes from applying localtime to
// the returned epoch at the call site.
inline long effectiveDayEpoch(long schedEpoch, int schedHour) {
    if (schedEpoch <= 1000) return schedEpoch;
    if (schedHour >= 0 && schedHour < 1) return schedEpoch - 3600L;
    return schedEpoch;
}

}  // namespace dayrep

// ---- Firmware hooks (defined in day_report_fallback.cpp) ----
// Included here so scheduler / email task / manual handlers share them.
// On host unit tests Arduino.h comes from tests/native/stubs.
#ifdef ARDUINO
#include <Arduino.h>
#else
class String;
#endif
void dayReportBeginAttempt(int slot, const String& cmdLine, int dayKey, long schedEpoch);
void dayReportEndAttempt(bool success);
bool dayReportNextRetry(String& outCmdLine, int& outSlot);
// Re-dispatch guard: the scheduler and the fallback worker re-enter processLine,
// which would otherwise re-track the same command as a manual (slot 0) intent
// and cause a duplicate retry. Wrap those processLine calls with Enter/Exit;
// dayReportTrackManual becomes a no-op while the guard is held.
void dayReportAutoEnter();
void dayReportAutoExit();
bool dayReportIsAutoDispatch();
