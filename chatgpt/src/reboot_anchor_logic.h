#pragma once
// Pure decision helpers for the 3-layer scheduled-reboot anchor
// (native-testable; also used by runtime_scheduler.cpp).
//
// Layer precedence for the "slot already ran on day" marker:
//   1. RTC RAM mirror   -> warm reboot / OTA / WDT (zero flash during restart)
//   2. LittleFS truth   -> config/schedDoc `last_run_day` (survives power cut)
//   3. legacy NVS       -> one-shot migration for boards updated in place
// Day keys are YYYYMMDD integers; a value <= 0 means "never ran / not persisted".

// Pick the authoritative day key for a slot given the three candidate layers.
// Values <= 0 are treated as "empty" and skipped in priority order.
inline int rebootAnchorPickDayKey(bool rtcValid, int rtcDay, int jsonDay, int nvsDay) {
    if (rtcValid && rtcDay > 0) return rtcDay;
    if (jsonDay > 0) return jsonDay;
    if (nvsDay > 0) return nvsDay;
    return 0;
}

// True when slot i must NOT run today: it already ran today (anchor == today).
inline bool rebootAnchorShouldSkip(int anchorDay, int curDayKey) {
    return curDayKey > 0 && anchorDay == curDayKey;
}

// --- Shift-following daily reboot (slot 1) ---------------------------------
// When OEE shifts are configured, slot 1 ignores its stored HH:MM and fires
// at the end of the last shift of the production day (= the shift day-boundary
// hour) plus a small grace minute. The actual reboot is additionally gated on
// the post-email quiet window (rebootQuietElapsed): it only proceeds ~1 min
// after the last observed email/SD/FTP activity, so the last shift's email
// always goes first ("email primeiro, reboot depois").
// Slot 2 always uses its stored time. A disabled slot never fires.

// True when slot 1 must follow the shift layout instead of its stored time.
inline bool rebootS1FollowsShifts(bool shiftsEnabled, int nSlots) {
    return shiftsEnabled && nSlots >= 1;
}

// Effective fire time of a slot, in minutes since midnight (0..1439).
// follows: from rebootS1FollowsShifts (slot 1 only); boundaryHour 0..23 and
// graceMin >= 0 shift the fire past the last shift end; otherwise the stored
// time passes through unchanged.
inline int rebootEffectiveTotalMin(bool follows, int boundaryHour, int graceMin,
                                   int storedTotalMin) {
    if (!follows) return storedTotalMin;
    if (boundaryHour < 0) boundaryHour = 0;
    if (boundaryHour > 23) boundaryHour = 23;
    if (graceMin < 0) graceMin = 0;
    int total = boundaryHour * 60 + graceMin;
    if (total > 1439) total = 1439;
    return total;
}

// --- Post-email quiet window (T19) ------------------------------------------
// The shift-following daily reboot must land ~1 min AFTER the last shift's
// email ("email primeiro, reboot depois"). The scheduler stamps every tick
// where email/SD/FTP work is observed busy; a quiet-gated reboot may only
// proceed once the channel has been quiet for kRebootPostEmailQuietMs.
// lastBusyMs == 0 means no busyness was ever observed (idle since boot) and
// trivially satisfies the window.
constexpr unsigned long kRebootPostEmailQuietMs = 60000UL; // 1 min after email

// True when the post-email quiet window has elapsed: never busy, or the last
// observed email/SD/FTP activity is at least quietMs in the past.
inline bool rebootQuietElapsed(unsigned long lastBusyMs, unsigned long nowMs,
                               unsigned long quietMs = kRebootPostEmailQuietMs) {
    if (lastBusyMs == 0) return true;
    return (long)(nowMs - lastBusyMs) >= (long)quietMs;
}

// --- Graceful-reboot deferral ------------------------------------------------
// A scheduled reboot must never interrupt email sending, SD writes or FTP
// uploads. The scheduler arms a pending reboot and only calls safeReboot()
// once tasks are idle — or when the deferral deadline passes (a stuck sender
// must not cancel the daily reboot forever).

// True when the pending reboot may fire now: tasks idle, or deadline reached.
inline bool rebootShouldFireNow(bool tasksBusy, unsigned long nowMs,
                                unsigned long deadlineMs) {
    if (!tasksBusy) return true;
    return (long)(nowMs - deadlineMs) >= 0;
}