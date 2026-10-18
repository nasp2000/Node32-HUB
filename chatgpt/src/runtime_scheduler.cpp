#include "feature_flags.h"

#include <Arduino.h>
#include <time.h>
#include <string.h>

#include "runtime_scheduler.h"
#include "commands.h"
#include "config_manager_runtime.h"
#include "telnet_transport.h"
#include "reboot_anchor_logic.h"

#if ENABLE_PROXIMITY==1
#include "sensors/proximity_manager.h"
#endif
#if ENABLE_FTP_CLIENT==1
#include "ftp_client.h"
#endif

// Task-busy signals owned by other translation units (single definitions in
// the main chatgpt.ino TU / runtime_email.cpp). Read-only peeks for the
// graceful-reboot gate — never written from here.
extern volatile bool emailBusy;              // SMTP core send in progress
extern bool isEmailSendingActive;            // runtime email send in progress
extern size_t runtimeEmailGetStreamQueueCount();
extern size_t emailSdQueuePending();         // SD email queue (email_queues_impl.h)

namespace {
constexpr unsigned long kBootStabilizationMs = 40000UL;
constexpr unsigned long kSchedulerTickMs = 1000UL;

// 3-layer anti-loop anchor for scheduled reboots (project-wide, pack-agnostic):
//   1. RTC RAM mirror  -> warm reboot / OTA / WDT (instant, no flash during restart)
//   2. LittleFS truth  -> config/schedDoc `last_run_day` + one-shot clear (power cut)
//   3. legacy NVS      -> read-once migration for boards updated in place
constexpr uint32_t kRebootAnchorMagic = 0x52424F54UL; // "RBOT"

struct RebootAnchorRtc {
    uint32_t magic;
    int dailyLastRunDay[3];     // daily reboot slots 1..2, YYYYMMDD
    int schedDtLastRunDay[17];  // sched_dt slots 1..16 (daily day-key + once marker)
};

RebootAnchorRtc s_rebootAnchorRtc RTC_NOINIT_ATTR;

bool rebootAnchorRtcValid() {
    return s_rebootAnchorRtc.magic == kRebootAnchorMagic;
}

// Graceful-reboot policy constants (unconditional: used by the shift-follow
// helpers below even when the scheduler engine itself is compiled out).
// T19: slot 1 fires 1 min after the last shift end; the post-email quiet
// window (kRebootPostEmailQuietMs in reboot_anchor_logic.h) orders the actual
// reboot to ~1 min AFTER the last shift's email ("email primeiro, reboot
// depois") — the old fixed 5 min grace is gone.
constexpr int kRebootShiftGraceMin = 1;                          // fire 1 min after last shift end
constexpr unsigned long kRebootDeferMaxMs = 15UL * 60UL * 1000UL; // reboot anyway after 15 min busy
constexpr unsigned long kRebootDeferLogMs = 60000UL;             // wait-progress log cadence
} // namespace

#if ENABLE_DAILY_REBOOT==1 || ENABLE_SCHEDULER==1
namespace {
// Graceful scheduled reboot: never interrupt email sending, queued emails,
// SD writes or FTP uploads. When the fire minute arrives with tasks busy, arm
// a pending reboot and only restart once idle (or when the deadline passes).
struct ScheduledRebootPending {
    bool active = false;
    bool requireQuiet = false;   // T19: also wait out the post-email quiet window
    unsigned long deadlineMs = 0;
    unsigned long lastWaitLogMs = 0;
    char reason[64] = {0};
};
ScheduledRebootPending s_schedRebootPending;

// T19: last millis() tick where email/SD/FTP work was observed busy. Stamped
// on every scheduler tick (all three processors call servicePendingScheduled-
// Reboot), so the shift-following reboot can measure "1 min after the email".
// 0 = no busyness observed since boot (trivially quiet).
unsigned long s_emailLastBusyMs = 0;

// True while any task a reboot must not interrupt is still running.
bool scheduledRebootTasksBusy() {
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    if (emailBusy) return true;
    if (isEmailSendingActive) return true;
    if (runtimeEmailGetStreamQueueCount() > 0) return true;
    if (emailSdQueuePending() > 0) return true;
#endif
#if ENABLE_FTP_CLIENT==1
    if (ftpIsBusy()) return true;
#endif
    return false;
}

// Service an armed pending reboot. Called on every scheduler tick.
void servicePendingScheduledReboot(unsigned long nowMs) {
    if (!s_schedRebootPending.active) return;
    bool busy = scheduledRebootTasksBusy();
    if (busy) s_emailLastBusyMs = nowMs;   // T19: stamp last email/SD/FTP activity
    // T19: a quiet-gated reboot additionally waits out the post-email quiet
    // window, so it lands ~1 min after the last shift's email.
    bool quietOk = !s_schedRebootPending.requireQuiet || rebootQuietElapsed(s_emailLastBusyMs, nowMs);
    if (!busy && quietOk) {
        sendVirtualMessage(String(F("Scheduled reboot: tasks idle, rebooting now (")) + String(s_schedRebootPending.reason) + F(")"), INFO, true, true);
        s_schedRebootPending.active = false;
        s_schedRebootPending.requireQuiet = false;
        safeReboot(s_schedRebootPending.reason);
        return;
    }
    if ((long)(nowMs - s_schedRebootPending.deadlineMs) >= 0) {
        // Timeout with work still pending: RAM email queues do NOT survive the
        // reboot (SD data files do). Make the loss visible in the monitor log.
        char detail[128];
#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
        snprintf(detail, sizeof(detail), "smtpBusy=%d streamBusy=%d streamQ=%u sdQ=%u",
                 (int)emailBusy, (int)isEmailSendingActive,
                 (unsigned)runtimeEmailGetStreamQueueCount(),
                 (unsigned)emailSdQueuePending());
#else
        snprintf(detail, sizeof(detail), "email=off");
#endif
#if ENABLE_FTP_CLIENT==1
        bool fb = ftpIsBusy();
        char fbs[16];
        snprintf(fbs, sizeof(fbs), " ftpBusy=%d", (int)fb);
        strncat(detail, fbs, sizeof(detail) - strlen(detail) - 1);
#endif
        sendVirtualMessage(String(F("Scheduled reboot: defer timeout (15min) with tasks busy, rebooting anyway (")) + String(s_schedRebootPending.reason) + F(") [") + String(detail) + F("]"), WARN, true, true);
        s_schedRebootPending.active = false;
        s_schedRebootPending.requireQuiet = false;
        safeReboot(s_schedRebootPending.reason);
        return;
    }
    if (nowMs - s_schedRebootPending.lastWaitLogMs >= kRebootDeferLogMs) {
        s_schedRebootPending.lastWaitLogMs = nowMs;
        // T19: say which gate is holding the reboot: busy tasks, or the
        // post-email quiet window (idle, but <1 min since the last email).
        if (!busy) {
            sendVirtualMessage(String(F("Scheduled reboot: waiting 60s quiet after email (")) + String(s_schedRebootPending.reason) + F(")"), INFO, true, false);
        } else {
            sendVirtualMessage(String(F("Scheduled reboot deferred: email/SD/FTP busy, waiting (")) + String(s_schedRebootPending.reason) + F(")"), INFO, true, false);
        }
    }
}

// Arm (or immediately run) a scheduled reboot through the graceful gate.
void scheduleGracefulReboot(const char* reason, unsigned long nowMs) {
    // Already armed (e.g. recurring interval slot re-firing while deferred):
    // keep the original deadline, do not extend it.
    if (s_schedRebootPending.active) return;
    char r[64];
    snprintf(r, sizeof(r), "%s", reason ? reason : "scheduled reboot");
    if (scheduledRebootTasksBusy()) {
        s_schedRebootPending.active = true;
        s_schedRebootPending.requireQuiet = false;
        s_schedRebootPending.deadlineMs = nowMs + kRebootDeferMaxMs;
        s_schedRebootPending.lastWaitLogMs = nowMs;
        snprintf(s_schedRebootPending.reason, sizeof(s_schedRebootPending.reason), "%s", r);
        sendVirtualMessage(String(F("Scheduled reboot deferred 15min max: tasks busy (")) + String(r) + F(")"), WARN, true, true);
        return;
    }
    safeReboot(r);
}

// T19: arm a reboot that additionally waits out the post-email quiet window
// (used by the shift-following daily slot so the reboot lands ~1 min AFTER
// the last shift's email). Even when tasks are idle right now, the reboot
// goes through the pending gate so the 60 s quiet window is enforced —
// unless it already elapsed (email long done), in which case reboot now.
void scheduleGracefulRebootQuiet(const char* reason, unsigned long nowMs) {
    // Already armed: keep the original deadline, but upgrade to quiet-gated
    // (stricter is always safe for ordering after the email).
    if (s_schedRebootPending.active) {
        s_schedRebootPending.requireQuiet = true;
        return;
    }
    char r[64];
    snprintf(r, sizeof(r), "%s", reason ? reason : "scheduled reboot");
    if (scheduledRebootTasksBusy()) {
        s_emailLastBusyMs = nowMs;
        s_schedRebootPending.active = true;
        s_schedRebootPending.requireQuiet = true;
        s_schedRebootPending.deadlineMs = nowMs + kRebootDeferMaxMs;
        s_schedRebootPending.lastWaitLogMs = nowMs;
        snprintf(s_schedRebootPending.reason, sizeof(s_schedRebootPending.reason), "%s", r);
        sendVirtualMessage(String(F("Scheduled reboot deferred 15min max: tasks busy (")) + String(r) + F(")"), WARN, true, true);
        return;
    }
    if (!rebootQuietElapsed(s_emailLastBusyMs, nowMs)) {
        s_schedRebootPending.active = true;
        s_schedRebootPending.requireQuiet = true;
        s_schedRebootPending.deadlineMs = nowMs + kRebootDeferMaxMs;
        s_schedRebootPending.lastWaitLogMs = nowMs;
        snprintf(s_schedRebootPending.reason, sizeof(s_schedRebootPending.reason), "%s", r);
        sendVirtualMessage(String(F("Scheduled reboot: waiting 60s quiet after email (")) + String(r) + F(")"), INFO, true, true);
        return;
    }
    safeReboot(r);
}
} // namespace
#endif // ENABLE_DAILY_REBOOT==1 || ENABLE_SCHEDULER==1

// --- Slot-1 shift following (unconditional: handlers query it whenever the
// daily-reboot API is compiled, independent of ENABLE_DAILY_REBOOT) ---------
// When OEE shifts are configured, slot 1 ignores its stored HH:MM and fires
// at the end of the last shift of the production day (= shift day-boundary)
// plus a 1 min grace. The reboot itself goes through the quiet-gated path so
// it lands ~1 min AFTER the last shift's email ("email primeiro, reboot
// depois"). Slot 2 always uses its stored time. A disabled slot never fires.
bool dailyRebootFollowsShifts() {
#if ENABLE_PROXIMITY==1
    ProximityShiftConfig sc = proximityGetShiftConfig();
    return rebootS1FollowsShifts(sc.enabled, sc.nSlots);
#else
    return false;
#endif
}

void dailyRebootEffectiveHM(int* hourOut, int* minuteOut) {
    int sh = 0, sm = 0;
    if (dailyRebootFollowsShifts()) {
#if ENABLE_PROXIMITY==1
        int tot = rebootEffectiveTotalMin(true, (int)proximityShiftDayBoundaryHour(),
                                          kRebootShiftGraceMin, 0);
        sh = tot / 60;
        sm = tot % 60;
#endif
    } else {
        String t;
        bool en = false;
        if (configManager.getDailyRebootSlot(1, t, en) && t.length() == 5) {
            sh = t.substring(0, 2).toInt();
            sm = t.substring(3, 5).toInt();
        }
    }
    if (hourOut) *hourOut = sh;
    if (minuteOut) *minuteOut = sm;
}

// ===========================================================================
// Daily reboot (standalone: builds when ENABLE_DAILY_REBOOT==1 even with
// ENABLE_SCHEDULER==0 — config schema and /api/status are pack-agnostic).
// ===========================================================================
#if ENABLE_DAILY_REBOOT==1

namespace {
int dailyRebootLastRunDayKey[3] = { -1, -1, -1 };
bool dailyRebootLastRunDayKeyLoaded = false;
bool dailyRebootSlotNotFoundLogged[3] = { false, false, false };

// Persist the once-per-day anchor across all 3 layers (RAM + LittleFS truth
// + RTC warm mirror). Shared by the immediate-fire and quiet-armed paths.
void markDailyRebootAnchor(int slotIdx, int curDayKey) {
    dailyRebootLastRunDayKey[slotIdx] = curDayKey;
    configManager.setDailyRebootLastRunDay(slotIdx, curDayKey);        // LittleFS truth
    bool anchorFlushed = configManager.saveSection("core");
    if (!anchorFlushed) {
        sendVirtualMessage(F("Daily reboot: LittleFS anchor save FAILED - RTC mirror covers warm reboot only"), WARN);
    }
    s_rebootAnchorRtc.magic = kRebootAnchorMagic;                     // RTC warm mirror
    s_rebootAnchorRtc.dailyLastRunDay[1] = dailyRebootLastRunDayKey[1];
    s_rebootAnchorRtc.dailyLastRunDay[2] = dailyRebootLastRunDayKey[2];
}
} // namespace

void processDailyRebootSlots() {
    static unsigned long lastTick = 0;
    static unsigned long lastDiagMinute = 0;

    unsigned long nowMs = millis();
    if (nowMs - lastTick < kSchedulerTickMs) return;
    lastTick = nowMs;
    servicePendingScheduledReboot(nowMs);

    if (!dailyRebootLastRunDayKeyLoaded) {
        bool rtcOk = rebootAnchorRtcValid();
        for (int i = 1; i <= 2; i++) {
            dailyRebootLastRunDayKey[i] = rebootAnchorPickDayKey(
                rtcOk, s_rebootAnchorRtc.dailyLastRunDay[i],
                configManager.getDailyRebootLastRunDay(i),
                configManager.getDailyRebootLastRunDayNVS(i));
        }
        dailyRebootLastRunDayKeyLoaded = true;
        char anchorBoot[96];
        snprintf(anchorBoot, sizeof(anchorBoot), "[DailyReboot] anchors loaded (%s): s1=%d s2=%d",
                 rtcOk ? "rtc" : "fs",
                 dailyRebootLastRunDayKey[1], dailyRebootLastRunDayKey[2]);
        sendVirtualMessage(String(anchorBoot), DEBUG, true, true);
    }

    time_t now = time(nullptr);
    if (now <= 1000) {
        unsigned long curMinuteInv = (unsigned long)(nowMs / 60000UL);
        if (curMinuteInv != lastDiagMinute) {
            lastDiagMinute = curMinuteInv;
            sendVirtualMessage(F("[DailyReboot] RTC invalid (epoch<=1000) - skipping all slots"), DEBUG, true, true);
        }
        return;
    }

    struct tm infoNow;
    localtime_r(&now, &infoNow);
    int curDayKey = (infoNow.tm_year + 1900) * 10000 + (infoNow.tm_mon + 1) * 100 + infoNow.tm_mday;
    int hour = infoNow.tm_hour;
    int minute = infoNow.tm_min;
    const unsigned long bootGraceMs = 30000UL;
    bool timeLikelySynced = (now > 1577836800UL);
    unsigned long curMinute = (unsigned long)(now / 60);
    if (curMinute != lastDiagMinute) {
        lastDiagMinute = curMinute;
        char diag[128];
        snprintf(diag, sizeof(diag), "[DailyReboot] %02d:%02d day=%d grace=%s synced=%s",
                 hour, minute, curDayKey,
                 (nowMs >= bootGraceMs) ? "ok" : "wait",
                 timeLikelySynced ? "yes" : "no");
        sendVirtualMessage(String(diag), DEBUG, true, true);
    }

    for (int i = 1; i <= 2; i++) {
        String slotTime;
        bool enabled = false;
        if (!configManager.getDailyRebootSlot(i, slotTime, enabled)) {
            if (!dailyRebootSlotNotFoundLogged[i]) {
                dailyRebootSlotNotFoundLogged[i] = true;
                char skipMsg[64];
                snprintf(skipMsg, sizeof(skipMsg), "[DailyReboot] s%d: slot not in NVS yet (normal on first boot)", i);
                sendVirtualMessage(String(skipMsg), DEBUG, true, false);
            }
            continue;
        }
        if (!enabled) {
            if (hour == 0 && minute == 0) {
                char skipMsg[64];
                snprintf(skipMsg, sizeof(skipMsg), "[DailyReboot] s%d (%s): disabled", i, slotTime.c_str());
                sendVirtualMessage(String(skipMsg), DEBUG, true, false);
            }
            continue;
        }
        if (slotTime.length() != 5) {
            char skipMsg[64];
            snprintf(skipMsg, sizeof(skipMsg), "[DailyReboot] s%d: bad time format '%s' (len=%d, expected 5)", i, slotTime.c_str(), (int)slotTime.length());
            sendVirtualMessage(String(skipMsg), WARN, true, false);
            continue;
        }

        int slotHour = slotTime.substring(0, 2).toInt();
        int slotMinute = slotTime.substring(3, 5).toInt();
        if (slotHour < 0 || slotHour > 23 || slotMinute < 0 || slotMinute > 59) {
            char skipMsg[64];
            snprintf(skipMsg, sizeof(skipMsg), "[DailyReboot] s%d: invalid time %02d:%02d", i, slotHour, slotMinute);
            sendVirtualMessage(String(skipMsg), WARN, true, false);
            continue;
        }

        bool atTargetMinute = false;
        bool followsShifts = (i == 1) && dailyRebootFollowsShifts();
        int effHour = slotHour, effMin = slotMinute;
        if (followsShifts) {
            // Slot 1 follows the OEE shift layout: end of the last shift of
            // the production day + 1 min grace. The quiet gate below orders
            // the actual reboot to ~1 min after the last shift's email.
            // Tracks shift-config changes automatically; slot 2 stays manual.
            dailyRebootEffectiveHM(&effHour, &effMin);
        }
        char effStr[8];
        snprintf(effStr, sizeof(effStr), "%02d:%02d", effHour, effMin);
        atTargetMinute = (hour == effHour) && (minute == effMin);
        bool eligibleByTarget = (nowMs >= bootGraceMs) && timeLikelySynced && atTargetMinute;
        if (dailyRebootLastRunDayKey[i] == curDayKey) {
            if (atTargetMinute) {
                char skipMsg[72];
                snprintf(skipMsg, sizeof(skipMsg), "[DailyReboot] s%d %s: already ran today (%d)", i, effStr, curDayKey);
                sendVirtualMessage(String(skipMsg), INFO, true, false);
            }
            continue;
        }
        if (!eligibleByTarget) {
            int currentTotalMin = hour * 60 + minute;
            int slotTotalMin = slotHour * 60 + slotMinute;
            if (currentTotalMin == slotTotalMin - 1) {
                char waitMsg[64];
                snprintf(waitMsg, sizeof(waitMsg), "[DailyReboot] s%d %s: T-1min, waiting...", i, slotTime.c_str());
                sendVirtualMessage(String(waitMsg), INFO, true, false);
            }
            if (atTargetMinute && !timeLikelySynced) {
                char skipMsg[64];
                snprintf(skipMsg, sizeof(skipMsg), "[DailyReboot] s%d %s: SKIP - RTC not synced", i, slotTime.c_str());
                sendVirtualMessage(String(skipMsg), WARN, true, true);
            }
            if (atTargetMinute && nowMs < bootGraceMs) {
                char skipMsg[64];
                snprintf(skipMsg, sizeof(skipMsg), "[DailyReboot] s%d %s: SKIP - boot grace (%lums)", i, slotTime.c_str(), nowMs);
                sendVirtualMessage(String(skipMsg), WARN, true, true);
            }
            continue;
        }

        char msg[96];
        snprintf(msg, sizeof(msg), "\xE2\x99\xBB Daily reboot s%d %s (day=%d)%s", i, effStr, curDayKey,
                 followsShifts ? " [follows shifts]" : "");
        Serial.println(msg);
#if ENABLE_TELNET==1
        if (telnetClient && telnetClient.connected() && telnetAuthenticated) {
            telnetClient.println(msg);
        }
#endif
        sendVirtualMessage(String(msg), INFO, true, true);
        markDailyRebootAnchor(i, curDayKey);
        char reason[40];
        snprintf(reason, sizeof(reason), "Daily reboot s%d %s", i, effStr);
        // T19: a shift-following slot goes through the quiet-gated path so
        // the reboot lands ~1 min AFTER the last shift's email; a busy email
        // at the fire minute is deferred (not skipped) by the pending gate.
        if (followsShifts) scheduleGracefulRebootQuiet(reason, nowMs);
        else scheduleGracefulReboot(reason, nowMs);
    }
}

void resetDailyRebootSlotTimer(int slotIdx) {
    if (slotIdx < 1 || slotIdx > 2) return;
    dailyRebootLastRunDayKey[slotIdx] = -1;
    dailyRebootSlotNotFoundLogged[slotIdx] = false;
    dailyRebootLastRunDayKeyLoaded = false;
    configManager.setDailyRebootLastRunDay(slotIdx, 0);     // clear LittleFS truth
    configManager.clearDailyRebootLastRunDayNVS(slotIdx);   // legacy NVS hygiene
    s_rebootAnchorRtc.dailyLastRunDay[slotIdx] = -1;        // clear RTC mirror
#if ENABLE_DAILY_REBOOT==1 || ENABLE_SCHEDULER==1
    s_schedRebootPending.active = false;                   // drop armed reboot (slot reconfigured)
    s_schedRebootPending.requireQuiet = false;
#endif
    Serial.printf("[DAILY_REBOOT] Timer reset for slot %d\n", slotIdx);
}

#else
void processDailyRebootSlots() {}
void resetDailyRebootSlotTimer(int slotIdx) { (void)slotIdx; }
#endif // ENABLE_DAILY_REBOOT

// ===========================================================================
// Full scheduler (interval + date/time command slots)
// ===========================================================================
#if ENABLE_SCHEDULER==1

#include <esp_task_wdt.h>
#include "languages/lang_active.h"
#include "day_report_fallback.h"
#include "observability.h"

void processLine(const String& line, unsigned long currentMillis);
#if ENABLE_RS232==1
bool enqueueRS232Command(const String& command, const String& templateName, const String& scheduledId, const String& type);
#else
inline bool enqueueRS232Command(const String&, const String&, const String&, const String&) { return false; }
#endif

namespace {

int schedCmdLastExecRAM[31] = {0};
bool schedCmdLastExecInit = false;
int schedDtLastRunDayKey[17] = {0};
bool schedDtFiredThisMinute[17] = {false};
bool schedDtLastRunDayKeyLoaded = false;
String currentTriggeredEmailFrequencyLabel;

String buildSchedCmdEmailFrequencyLabel(int interval, const String& unit) {
    if (interval <= 0) return L(activeLang->schedAutomatic);

    String normalized = unit;
    normalized.toLowerCase();

    if (normalized == F("s") || normalized == F("sec") || normalized == F("second") || normalized == F("seconds")) {
        if (interval < 60) return String(interval) + String(F("s"));
        if (interval < 3600) return String(interval / 60) + String(F("min"));
        if (interval < 86400) return String(interval / 3600) + String(F("h"));
        return String(interval / 86400) + String(F("d"));
    }
    if (normalized == F("m") || normalized == F("min") || normalized == F("minute") || normalized == F("minutes")) {
        return buildSchedCmdEmailFrequencyLabel(interval * 60, F("seconds"));
    }
    if (normalized == F("h") || normalized == F("hour") || normalized == F("hours")) {
        return buildSchedCmdEmailFrequencyLabel(interval * 3600, F("seconds"));
    }
    if (normalized == F("d") || normalized == F("day") || normalized == F("days")) {
        return String(interval) + String(F("d"));
    }

    return String(interval) + normalized;
}

String buildSchedDtEmailFrequencyLabel(const String& dateStr, const String& timeStr, bool daily) {
    if (daily) {
        return L(activeLang->schedDailyAt) + timeStr;
    }
    return L(activeLang->schedOneTimeOn) + dateStr + String(F(" ")) + timeStr;
}

int currentWeekday() {
    time_t now = time(nullptr);
    if (now <= 1000) return -1;
    struct tm info;
    localtime_r(&now, &info);
    return info.tm_wday;
}

bool shouldRunToday(const String& days) {
    if (!days.length() || days == F("everyday")) return true;
    int weekday = currentWeekday();
    if (weekday < 0) return true;
    bool isWeekend = (weekday == 0 || weekday == 6);
    if (days == F("weekends")) return isWeekend;
    if (days == F("weekdays")) return !isWeekend;
    switch (weekday) {
        case 1: return days == F("monday");
        case 2: return days == F("tuesday");
        case 3: return days == F("wednesday");
        case 4: return days == F("thursday");
        case 5: return days == F("friday");
        case 6: return days == F("saturday");
        case 0: return days == F("sunday");
    }
    return true;
}

bool isSystemCommand(String trimmed, const String& type, int& prefixLen) {
    String upper = trimmed;
    upper.toUpperCase();
    prefixLen = 0;

    if (type.equalsIgnoreCase("system") || type.equalsIgnoreCase("sistema")) {
        return true;
    }

    if (upper.length() >= 3 && upper.substring(0, 3) == F("SYS")) {
        char separator = (trimmed.length() > 3) ? trimmed[3] : '\0';
        if (separator == ':' || separator == ' ' || separator == '\t') {
            int scan = 3;
            while (scan < trimmed.length() && (trimmed[scan] == ' ' || trimmed[scan] == '\t')) scan++;
            if (scan < trimmed.length() && trimmed[scan] == ':') {
                scan++;
                while (scan < trimmed.length() && (trimmed[scan] == ' ' || trimmed[scan] == '\t')) scan++;
            }
            prefixLen = scan;
            return true;
        }
    }

    return false;
}

// Scheduled REBOOT-family commands must bypass the interactive 1h cooldown
// (REBOOT_COOLDOWN_MS in handleReboot): a scheduled restart must always run,
// otherwise the slot is marked executed/cleared first and the reboot is lost.
// Mirrors the bare-REBOOT auto-interpretation in processSchedDtSlots().
bool isScheduledRebootCommand(const String& localCmd) {
    String up = localCmd;
    up.toUpperCase();
    up.trim();
    return up == F("REBOOT") || up == F("RESTART") ||
           up == F("ESP.REBOOT") || up == F("ESP.RESET");
}

} // namespace

String getCurrentTriggeredEmailFrequencyLabel() {
    if (currentTriggeredEmailFrequencyLabel.length() > 0) {
        return currentTriggeredEmailFrequencyLabel;
    }
    return String(F("envio manual"));
}

void processSchedCmdSlots() {
    static unsigned long lastTick = 0;

    if (!schedCmdLastExecInit) {
        for (int i = 0; i < 31; ++i) schedCmdLastExecRAM[i] = 0;
        schedCmdLastExecInit = true;
    }

    unsigned long now = millis();
    if (now < kBootStabilizationMs) return;
    if (now - lastTick < kSchedulerTickMs) return;
    lastTick = now;
    servicePendingScheduledReboot(now);

    for (int i = 1; i <= 10; ++i) {
        esp_task_wdt_reset();

        String cmd, unit, days, tpl, type;
        int interval = 0;
        bool enabled = false;
        if (!configManager.getSchedCmdSlot(i, cmd, interval, unit, days, tpl, enabled, type)) continue;
        if (!enabled || !cmd.length() || interval <= 0) continue;

        if (unit == "m") unit = F("minutes");
        else if (unit == "h") unit = F("hours");
        else if (unit == "s") unit = F("seconds");

        unsigned long intervalMs = 60000UL;
        if (unit == F("seconds")) intervalMs = (unsigned long)interval * 1000UL;
        else if (unit == F("minutes")) intervalMs = (unsigned long)interval * 60000UL;
        else if (unit == F("hours")) intervalMs = (unsigned long)interval * 3600000UL;

        unsigned long lastExec = schedCmdLastExecRAM[i];
        if (lastExec == 0) {
            schedCmdLastExecRAM[i] = now;
            continue;
        }
        if ((now - lastExec) < intervalMs) continue;
        if (!shouldRunToday(days)) continue;

        String trimmed = cmd;
        trimmed.trim();
        int prefixLen = 0;
        if (isSystemCommand(trimmed, type, prefixLen)) {
            String localCmd = trimmed.substring(prefixLen);
            localCmd.trim();
            if (!localCmd.length()) continue;

            if (isScheduledRebootCommand(localCmd)) {
                sendVirtualMessage(String(F("Scheduled reboot (interval slot ")) + String(i) + String(F("): ")) + localCmd, INFO, true, true);
                observabilityLogSchedulerEvent("sched_cmd", i, localCmd.c_str(), true, true, false);
                char reason[64];
                snprintf(reason, sizeof(reason), "Scheduled reboot sched_cmd %d", i);
                scheduleGracefulReboot(reason, now);
                continue;
            }

            String previousLabel = currentTriggeredEmailFrequencyLabel;
            currentTriggeredEmailFrequencyLabel = buildSchedCmdEmailFrequencyLabel(interval, unit);
            processLine(localCmd, now);
            currentTriggeredEmailFrequencyLabel = previousLabel;
            schedCmdLastExecRAM[i] = now;
            observabilityLogSchedulerEvent("sched_cmd", i, localCmd.c_str(), true, true, false);
            sendVirtualMessage(String(F("Slot interval (SYS) s")) + String(i) + String(F(": ")) + localCmd, INFO);
            continue;
        }

        String cmdType = type.length() > 0 ? type : "rs232";
#if ENABLE_RS232==1
        bool queued = enqueueRS232Command(cmd, tpl, String("CMD_") + String(i), cmdType);
        if (queued) {
            schedCmdLastExecRAM[i] = now;
            observabilityLogSchedulerEvent("sched_cmd", i, cmd.c_str(), true, false, false);
            sendVirtualMessage(String(F("Slot interval executed s")) + String(i) + String(F(": ")) + cmd, INFO);
        } else {
            observabilityLogSchedulerEvent("sched_cmd", i, cmd.c_str(), false, false, false);
            sendVirtualMessage(String(F("Queue full slot interval s")) + String(i), WARN);
        }
#else
        observabilityLogSchedulerEvent("sched_cmd", i, cmd.c_str(), false, false, false);
        sendVirtualMessage(String(F("RS232 disabled, skip slot interval s")) + String(i), WARN);
#endif
    }
}

void processSchedDtSlots() {
    static unsigned long lastTick = 0;
    static unsigned long lastMinuteMarker = 0;
    static bool warnedInvalidTime = false;

    if (!schedDtLastRunDayKeyLoaded) {
        bool rtcOk = rebootAnchorRtcValid();
        for (int i = 1; i <= 16; ++i) {
            schedDtLastRunDayKey[i] = rebootAnchorPickDayKey(
                rtcOk, s_rebootAnchorRtc.schedDtLastRunDay[i],
                configManager.getSchedDtLastRunDayJson(i),
                configManager.getSchedDtLastRunDay(i));
        }
        schedDtLastRunDayKeyLoaded = true;
    }

    unsigned long nowMs = millis();
    if (nowMs < kBootStabilizationMs) return;
    if (nowMs - lastTick < kSchedulerTickMs) return;
    lastTick = nowMs;
    servicePendingScheduledReboot(nowMs);

    time_t now = time(nullptr);
    if (now <= 1000) {
        if (!warnedInvalidTime) {
            warnedInvalidTime = true;
            sendVirtualMessage(String(F("Time not yet valid (NTP/RTC) - sched_dt inactive until sync")), DEBUG, true, true);
        }
        return;
    }
    warnedInvalidTime = false;

    struct tm infoNow;
    localtime_r(&now, &infoNow);
    unsigned long minuteKey = (unsigned long)(now / 60);
    if (minuteKey != lastMinuteMarker) {
        for (int i = 0; i < 17; ++i) schedDtFiredThisMinute[i] = false;
        lastMinuteMarker = minuteKey;
    }

    bool anyCleared = false;
    for (int i = 1; i <= 10 && i <= 16; ++i) {
        esp_task_wdt_reset();

        String cmd, dateStr, timeStr, tpl, type;
        bool enabled = false;
        bool daily = false;
        if (!configManager.getSchedDtSlot(i, cmd, dateStr, timeStr, tpl, enabled, type, daily)) continue;
        if (!enabled || !cmd.length()) continue;
        if (dateStr.length() != 10 || timeStr.length() != 5) continue;

        int year = dateStr.substring(0, 4).toInt();
        int month = dateStr.substring(5, 7).toInt();
        int day = dateStr.substring(8, 10).toInt();
        int hour = timeStr.substring(0, 2).toInt();
        int minute = timeStr.substring(3, 5).toInt();
        if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) continue;

        bool shouldExecute = false;
        if (daily) {
            shouldExecute = (infoNow.tm_hour == hour && infoNow.tm_min == minute);
        } else {
            struct tm slotTm;
            memset(&slotTm, 0, sizeof(slotTm));
            slotTm.tm_year = year - 1900;
            slotTm.tm_mon = month - 1;
            slotTm.tm_mday = day;
            slotTm.tm_hour = hour;
            slotTm.tm_min = minute;
            slotTm.tm_isdst = -1;
            time_t slotEpoch = mktime(&slotTm);
            if (slotEpoch == -1) continue;
            shouldExecute = (now >= slotEpoch);
        }
        if (!shouldExecute) continue;

        int curDayKey = (infoNow.tm_year + 1900) * 10000 + (infoNow.tm_mon + 1) * 100 + infoNow.tm_mday;
        if (schedDtLastRunDayKey[i] == curDayKey) continue;
        if (schedDtFiredThisMinute[i]) continue;

        String trimmed = cmd;
        trimmed.trim();
        int prefixLen = 0;
        bool isSys = isSystemCommand(trimmed, type, prefixLen);
        if (!isSys) {
            String autoUp = trimmed;
            autoUp.toUpperCase();
            autoUp.trim();
            if (autoUp == F("REBOOT") || autoUp == F("RESTART") || autoUp == F("ESP.REBOOT") || autoUp == F("ESP.RESET")) {
                isSys = true;
                prefixLen = 0;
                sendVirtualMessage(F("sched_dt: 'REBOOT' command interpreted as local (no SYS prefix)"), INFO);
            }
        }

        if (isSys) {
            String localCmd = trimmed.substring(prefixLen);
            localCmd.trim();
            if (!localCmd.length()) continue;

            schedDtFiredThisMinute[i] = true;
            if (!daily) {
                bool cleared = configManager.clearSchedDtSlot(i);
                configManager.clearSchedDtLastRunDay(i);
                // CMD-7 (live .29 2026-10-14: one-shot fired twice): persist
                // the clear SYNCHRONOUSLY. The deferred markDirtyAndScheduleSave
                // below leaves a window where the RAM slot can come back (pool
                // relocation / concurrent mutation) and `now >= slotEpoch`
                // stays true, so the next minute fires again. A synchronous
                // save closes that window; the RAM day-guard below it blocks
                // any same-day refire even if the slot data ever returns.
                if (cleared) {
                    anyCleared = true;
                    schedDtLastRunDayKey[i] = curDayKey;
                    if (!configManager.saveSection("schedule")) {
                        sendVirtualMessage(String(F("Slot datetime s Clear save FAILED, refire possible: ")) + String(i), WARN, false, true);
                    }
                }
                sendVirtualMessage(String(F("Slot datetime (SYS) s")) + String(i) + String(F(" executado e removido: ")) + localCmd, INFO);
            } else {
                schedDtLastRunDayKey[i] = curDayKey;
                configManager.setSchedDtLastRunDayJson(i, curDayKey);   // LittleFS truth
                configManager.saveSection("schedule");
                sendVirtualMessage(String(F("Slot datetime (SYS) s")) + String(i) + String(F(" executado (diario): ")) + localCmd, INFO);
            }

            if (isScheduledRebootCommand(localCmd)) {
                sendVirtualMessage(String(F("Scheduled reboot (datetime slot ")) + String(i) + String(F("): ")) + localCmd, INFO, true, true);
                schedDtLastRunDayKey[i] = curDayKey;                    // RAM marker (once-fired / daily)
                s_rebootAnchorRtc.magic = kRebootAnchorMagic;           // RTC warm mirror
                memcpy(s_rebootAnchorRtc.schedDtLastRunDay, schedDtLastRunDayKey,
                       sizeof(s_rebootAnchorRtc.schedDtLastRunDay));
                // Flush the slot state before restarting: a one-shot slot was cleared
                // in RAM only and the deferred markDirtyAndScheduleSave() would never
                // run once we reboot. Without this, the past-date slot re-fires after
                // boot and the device reboots in an infinite loop.
                if (configManager.saveSection("schedule")) {
                    sendVirtualMessage(String(F("Scheduler state flushed before restart (dt slot ")) + String(i), INFO);
                } else {
                    sendVirtualMessage(String(F("Scheduler state flush FAILED before restart (dt slot ")) + String(i), WARN);
                }
                char reason[64];
                snprintf(reason, sizeof(reason), "Scheduled reboot sched_dt %d", i);
                scheduleGracefulReboot(reason, nowMs);
                continue;
            }

            String previousLabel = currentTriggeredEmailFrequencyLabel;
            currentTriggeredEmailFrequencyLabel = buildSchedDtEmailFrequencyLabel(dateStr, timeStr, daily);
            observabilityLogSchedulerEvent("sched_dt", i, localCmd.c_str(), true, true, daily);
#if (ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1)
            dayReportBeginAttempt(i, localCmd, curDayKey, (long)now);
            dayReportAutoEnter();
#endif
            processLine(localCmd, nowMs);
#if (ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1)
            dayReportAutoExit();
#endif
            currentTriggeredEmailFrequencyLabel = previousLabel;
            continue;
        }

        String cmdType = type.length() > 0 ? type : "rs232";
#if ENABLE_RS232==1
        bool queued = enqueueRS232Command(cmd, tpl, String("DT_") + String(i), cmdType);
        if (queued) {
            schedDtFiredThisMinute[i] = true;
            observabilityLogSchedulerEvent("sched_dt", i, cmd.c_str(), true, false, daily);
            if (!daily) {
                bool cleared = configManager.clearSchedDtSlot(i);
                configManager.clearSchedDtLastRunDay(i);
                // CMD-7: same synchronous persist as the SYS path above.
                if (cleared) {
                    anyCleared = true;
                    schedDtLastRunDayKey[i] = curDayKey;
                    if (!configManager.saveSection("schedule")) {
                        sendVirtualMessage(String(F("Slot datetime s Clear save FAILED, refire possible: ")) + String(i), WARN, false, true);
                    }
                }
                sendVirtualMessage(String(F("Slot datetime executed and removed s")) + String(i) + String(F(": ")) + cmd, INFO);
            } else {
                schedDtLastRunDayKey[i] = curDayKey;
                configManager.setSchedDtLastRunDayJson(i, curDayKey);   // LittleFS truth
                configManager.saveSection("schedule");
                sendVirtualMessage(String(F("Slot datetime executed (daily) s")) + String(i) + String(F(": ")) + cmd, INFO);
            }
        } else {
            observabilityLogSchedulerEvent("sched_dt", i, cmd.c_str(), false, false, daily);
            sendVirtualMessage(String(F("Queue full datetime slot s")) + String(i), WARN);
        }
#else
        observabilityLogSchedulerEvent("sched_dt", i, cmd.c_str(), false, false, daily);
        sendVirtualMessage(String(F("RS232 disabled, skip slot dt s")) + String(i), WARN);
#endif
    }

    if (anyCleared) {
        configManager.markDirtyAndScheduleSave();
    }
}

void resetSchedCmdSlotTimer(int slotIdx) {
    if (slotIdx < 1 || slotIdx > 30) return;
    schedCmdLastExecRAM[slotIdx] = 0;
    Serial.printf("[SCHED_CMD] Timer reset for slot %d\n", slotIdx);
}

void resetSchedDtSlotTimer(int slotIdx) {
    if (slotIdx < 1 || slotIdx > 16) return;
    schedDtLastRunDayKey[slotIdx] = -1;
    schedDtFiredThisMinute[slotIdx] = false;
    configManager.setSchedDtLastRunDayJson(slotIdx, 0);     // clear LittleFS truth
    configManager.clearSchedDtLastRunDay(slotIdx);           // legacy NVS hygiene
    s_rebootAnchorRtc.schedDtLastRunDay[slotIdx] = -1;       // clear RTC mirror
    Serial.printf("[SCHED_DT] Timer reset for slot %d\n", slotIdx);
}

#else
String getCurrentTriggeredEmailFrequencyLabel() { return String(); }
void processSchedCmdSlots() {}
void processSchedDtSlots() {}
#endif // ENABLE_SCHEDULER