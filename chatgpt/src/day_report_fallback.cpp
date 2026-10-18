#include "feature_flags.h"

#if ENABLE_SCHEDULER==1 && (ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1)

#include "day_report_fallback.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

#include "commands.h"
#include "email_flash_rpc.h" // stack-safe LittleFS (journal reachable from PSRAM email tasks)
#include "littlefs_manager.h"
#include "runtime_email.h"

namespace {

const char* kJournalPath = "/config/dayrep.json";
const size_t kJournalMaxBytes = 2048;

dayrep::Entry s_entries[dayrep::kMaxEntries];
bool s_loaded = false;
// Active attempt context: set by Begin, consumed by End (email task completion).
int s_activeSlot = -1;
int s_activeDayKey = -1;
// True while the scheduler or the fallback worker is inside processLine().
// Manual tracking must skip then: the intent is already journaled under its
// real slot, and re-tracking as slot 0 would duplicate the retry.
bool s_autoDispatch = false;
unsigned long s_saveThrottleMs = 0;

int dayKeyOf(time_t t) {
    struct tm ti;
    localtime_r(&t, &ti);
    return (ti.tm_year + 1900) * 10000 + (ti.tm_mon + 1) * 100 + ti.tm_mday;
}

void entriesInit() {
    for (int i = 0; i < dayrep::kMaxEntries; ++i) dayrep::entryInit(s_entries[i]);
}

void journalPrune(int todayKey, long nowEpoch);  // defined below journalLoad

bool journalSaveNow() {
    // Atomic write through the stack-safe flash API (emailFlash*): each op
    // locks inside and is served by the loop when the caller's stack is in
    // PSRAM (dayReportEndAttempt runs on emailQueueProc — a direct LittleFS
    // write there panics, HISTORIC/HomeE32 (8)). No gate on littlefsBegin()
    // here: that call is itself a potential flash op, and the API's executor
    // performs it on a safe stack.
    SpiRamJsonDocument doc(2048);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < dayrep::kMaxEntries; ++i) {
        if (s_entries[i].slot < 0) continue;
        JsonObject o = arr.createNestedObject();
        o["slot"] = s_entries[i].slot;
        o["cmd"] = s_entries[i].cmd;
        o["line"] = s_entries[i].cmdLine;
        o["day"] = s_entries[i].dayKey;
        o["sched"] = (long)s_entries[i].schedEpoch;
        o["st"] = s_entries[i].status;
        o["att"] = s_entries[i].attempts;
        o["last"] = (long)s_entries[i].lastTryEpoch;
    }
    String tmpPath = String(kJournalPath) + ".tmp";
    String payload;
    serializeJson(doc, payload);
    emailFlashRemove(tmpPath);
    if (payload.isEmpty()) return false;
    if (!emailFlashWriteFile(tmpPath, (const uint8_t*)payload.c_str(), payload.length())) {
        emailFlashRemove(tmpPath);
        return false;
    }
    if (emailFlashExists(kJournalPath)) emailFlashRemove(kJournalPath);
    bool renamed = emailFlashRename(tmpPath, kJournalPath);
    if (!renamed) {
        emailFlashRemove(tmpPath);
    }
    return renamed;
}

void journalLoad() {
    entriesInit();
    s_loaded = true;
    // Reads through the stack-safe flash API (see journalSaveNow).
    int32_t efh = emailFlashOpenRead(kJournalPath);
    if (efh < 0) return;
    String raw;
    raw.reserve(2048);
    uint8_t jbuf[512];
    while (true) {
        size_t n = emailFlashRead(efh, jbuf, sizeof(jbuf));
        if (n == 0) break;
        raw.concat((const char*)jbuf, n);
        if (raw.length() > 32768) break; // corrupt/oversize file: cap, never grow
    }
    emailFlashClose(efh);
    if (raw.isEmpty()) return;
    SpiRamJsonDocument doc(2048);
    if (deserializeJson(doc, raw) != DeserializationError::Ok) {
        return;
    }
    if (!doc.is<JsonArray>()) return;
    JsonArray arr = doc.as<JsonArray>();
    int i = 0;
    for (JsonObject o : arr) {
        if (i >= dayrep::kMaxEntries) break;  // corrupt/oversize file: cap, never grow
        int slot = o["slot"] | -1;
        int day = o["day"] | -1;
        if (slot < 0 || day < 0) continue;
        const char* cmd = o["cmd"] | "";
        const char* line = o["line"] | "";
        s_entries[i].slot = slot;
        strncpy(s_entries[i].cmd, cmd, sizeof(s_entries[i].cmd) - 1);
        s_entries[i].cmd[sizeof(s_entries[i].cmd) - 1] = '\0';
        strncpy(s_entries[i].cmdLine, line, sizeof(s_entries[i].cmdLine) - 1);
        s_entries[i].cmdLine[sizeof(s_entries[i].cmdLine) - 1] = '\0';
        s_entries[i].dayKey = day;
        s_entries[i].schedEpoch = (long)(o["sched"] | 0L);
        s_entries[i].status = o["st"] | (int)dayrep::ST_PENDING;
        s_entries[i].attempts = o["att"] | 0;
        s_entries[i].lastTryEpoch = (long)(o["last"] | 0L);
        i++;
    }
    // Rotate on boot: drop receipts from previous days and expired entries so
    // a stale journal never survives across days.
    {
        time_t nowT = time(nullptr);
        if (nowT > 1000) journalPrune(dayKeyOf(nowT), (long)nowT);
    }
}

inline void ensureLoaded() {
    if (!s_loaded) journalLoad();
}

void journalSaveThrottled() {
    unsigned long now = millis();
    if (now - s_saveThrottleMs < 2000UL) return;
    s_saveThrottleMs = now;
    journalSaveNow();
}

// Drop SENT entries from previous days AND entries older than the retry
// window. The journal is a fixed-size ring (kMaxEntries, ~2KB max on flash)
// and must rotate, never grow: stale entries can never fire again.
void journalPrune(int todayKey, long nowEpoch) {
    for (int i = 0; i < dayrep::kMaxEntries; ++i) {
        if (s_entries[i].slot < 0) continue;
        if (s_entries[i].status == (int)dayrep::ST_SENT && s_entries[i].dayKey != todayKey) {
            dayrep::entryInit(s_entries[i]);
        } else if (dayrep::isStale(s_entries[i], nowEpoch)) {
            dayrep::entryInit(s_entries[i]);
        }
    }
}

}  // namespace

void dayReportAutoEnter() { s_autoDispatch = true; }
void dayReportAutoExit() { s_autoDispatch = false; }
bool dayReportIsAutoDispatch() { return s_autoDispatch; }

// Record the intent BEFORE dispatch. Safe to call for non-day commands (no-op).
void dayReportBeginAttempt(int slot, const String& cmdLine, int dayKey, long schedEpoch) {
    char lineBuf[64];
    cmdLine.toCharArray(lineBuf, sizeof(lineBuf));
    char verb[12];
    if (!dayrep::isDayReportCommand(lineBuf, verb, sizeof(verb))) return;
    if (dayKey < 0 || schedEpoch <= 1000) return;
    // Pin bare commands to the scheduled day (1h tolerance: 00:00-00:59 counts
    // as the previous day). A retry after midnight must re-send the ORIGINAL
    // day's folder, never "today" re-resolved at retry time. All 4 handlers
    // accept an explicit DD-MM-YYYY param.
    if (!dayrep::hasDateParam(lineBuf)) {
        time_t st = (time_t)schedEpoch;
        struct tm sti;
        localtime_r(&st, &sti);
        time_t eff = (time_t)dayrep::effectiveDayEpoch(schedEpoch, sti.tm_hour);
        struct tm eti;
        localtime_r(&eff, &eti);
        char dateStr[16];
        snprintf(dateStr, sizeof(dateStr), "%02d-%02d-%04d",
                 eti.tm_mday, eti.tm_mon + 1, eti.tm_year + 1900);
        char pinned[64];
        if (strlen(lineBuf) + 1 + 10 < sizeof(pinned)) {
            snprintf(pinned, sizeof(pinned), "%s %s", lineBuf, dateStr);
            strncpy(lineBuf, pinned, sizeof(lineBuf) - 1);
            lineBuf[sizeof(lineBuf) - 1] = '\0';
        }
    }
    ensureLoaded();
    time_t nowT = time(nullptr);
    long nowEpoch = (long)nowT;
    int todayKey = (nowT > 1000) ? dayKeyOf(nowT) : dayKey;
    journalPrune(todayKey, nowEpoch);
    int idx = dayrep::findEntry(s_entries, dayrep::kMaxEntries, slot, dayKey);
    if (idx < 0) {
        for (int i = 0; i < dayrep::kMaxEntries; ++i) {
            if (s_entries[i].slot < 0) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) {
        // Journal full (only possible with 10 live entries <24h old, i.e. never
        // in practice): evict the oldest retry-exhausted entry so the newest
        // report is always tracked. SENT-today entries are never evicted
        // (idempotency). Original send still proceeds regardless.
        int oldest = -1;
        for (int i = 0; i < dayrep::kMaxEntries; ++i) {
            if (s_entries[i].status == (int)dayrep::ST_SENT) continue;
            if (oldest < 0 || s_entries[i].schedEpoch < s_entries[oldest].schedEpoch) oldest = i;
        }
        if (oldest < 0) return;
        idx = oldest;
    }
    // CMD-7 duplicate fire (live .29 2026-10-14: same slot+day+cmd fired twice
    // ~1 min apart): when the found entry is already PENDING for the SAME
    // command, this is a re-fire of an in-flight/awaiting send — keep its
    // retry clock (schedEpoch/attempts/lastTryEpoch) so the duplicate neither
    // re-arms nor multiplies the fallback series. Only a genuinely new intent
    // (different cmd, FAILED/SENT state, or fresh slot) resets the series.
    bool dupePending = (idx >= 0 && s_entries[idx].slot == slot &&
                        s_entries[idx].dayKey == dayKey &&
                        s_entries[idx].status == (int)dayrep::ST_PENDING &&
                        strncmp(s_entries[idx].cmd, verb, sizeof(s_entries[idx].cmd)) == 0);
    s_entries[idx].slot = slot;
    strncpy(s_entries[idx].cmd, verb, sizeof(s_entries[idx].cmd) - 1);
    s_entries[idx].cmd[sizeof(s_entries[idx].cmd) - 1] = '\0';
    strncpy(s_entries[idx].cmdLine, lineBuf, sizeof(s_entries[idx].cmdLine) - 1);
    s_entries[idx].cmdLine[sizeof(s_entries[idx].cmdLine) - 1] = '\0';
    s_entries[idx].dayKey = dayKey;
    // Fresh schedule resets state unless it was already SENT (duplicate fire in same minute).
    if (s_entries[idx].status != (int)dayrep::ST_SENT) {
        s_entries[idx].status = (int)dayrep::ST_PENDING;
        if (!dupePending) {
            s_entries[idx].schedEpoch = schedEpoch;
            s_entries[idx].attempts = 0;
            s_entries[idx].lastTryEpoch = 0;
        }
    }
    // Owner of the completion receipt: do NOT steal the active context from a
    // send already in flight (e.g. manual command while the scheduler's email
    // is sending). The new intent is journaled above and will be retried; the
    // in-flight send keeps its receipt so a success is never misattributed
    // (which would cause a spurious duplicate retry of the finished report).
    extern volatile bool emailDayInProgress;
    if (!(emailDayInProgress && s_activeSlot >= 0)) {
        s_activeSlot = slot;
        s_activeDayKey = dayKey;
    }
    journalSaveThrottled();
}

// Record the outcome. Called once per emailDayTask completion.
void dayReportEndAttempt(bool success) {
    if (s_activeSlot < 0 || s_activeDayKey < 0) return;
    ensureLoaded();
    int idx = dayrep::findEntry(s_entries, dayrep::kMaxEntries, s_activeSlot, s_activeDayKey);
    s_activeSlot = -1;
    s_activeDayKey = -1;
    if (idx < 0) return;
    // Only the sender knows the truth: SMTP 250 OK / Google 200 OK == SENT.
    s_entries[idx].status = success ? (int)dayrep::ST_SENT : (int)dayrep::ST_FAILED;
    journalSaveNow();
    sendVirtualMessage(
        String(F("DayReport: ")) + s_entries[idx].cmd + (success ? F(" receipt saved (sent)") : F(" marked failed, fallback in ~20min")),
        INFO, false, true);
}

// Worker: called once per minute from the scheduler block. If an entry is due,
// copies its cmd line out and marks the attempt BEFORE dispatch (crash-safe:
// a panic between dispatch and SENT still leaves FAILED -> retried, never lost;
// idempotency key prevents double-send on success path).
bool dayReportNextRetry(String& outCmdLine, int& outSlot) {
    ensureLoaded();
    time_t nowT = time(nullptr);
    if (nowT <= 1000) return false;
    if (emailDayInProgress) return false;
    for (int i = 0; i < dayrep::kMaxEntries; ++i) {
        if (!dayrep::needsRetry(s_entries[i], (long)nowT)) continue;
        s_entries[i].attempts++;
        s_entries[i].lastTryEpoch = (long)nowT;
        s_entries[i].status = (int)dayrep::ST_PENDING;
        outCmdLine = String(s_entries[i].cmdLine);
        outSlot = s_entries[i].slot;
        s_activeSlot = s_entries[i].slot;
        s_activeDayKey = s_entries[i].dayKey;
        journalSaveNow();
        sendVirtualMessage(
            String(F("DayReport fallback: retrying ")) + outCmdLine + F(" (attempt ") + String(s_entries[i].attempts) + F(")"),
            WARN, false, true);
        return true;
    }
    return false;
}

#else
// Scheduler or email disabled: nothing to track.
#endif  // ENABLE_SCHEDULER && (ENABLE_SMTP || ENABLE_GOOGLE_EMAIL)
