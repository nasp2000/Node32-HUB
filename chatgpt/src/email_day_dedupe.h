// email_day_dedupe.h — duplicate-send guard for startEmailDayAsync (header-only).
//
// One funnel serves EMAILDAY/LOGDAY/SENSORDAY/SHIFTDAY/test sends. Scheduler
// double-fires, journal races and API double-clicks all land here, so this is
// the single choke point: the same (folder, subject) is never started twice
// inside EMAILDAY_RESEND_WINDOW_MS (observed doubles were 60-120 s apart;
// journal retries are 20 min apart and pass through untouched).
// Suppression is LOUD (caller logs WARN with bypassDedup) so a skipped send
// is always visible on Serial + Web monitor.
//
// Pure / host-testable (String only): tests/native/test_email_day_dedupe.cpp.
#pragma once

#include <Arduino.h>
#include <stdint.h>

#define EMAILDAY_RESEND_WINDOW_MS 180000UL // 3 min

struct EmailDayDedupeState {
    String folder;
    String subject;
    uint32_t ms; // truncated millis(): wrap math identical on 32/64-bit hosts
    bool armed;
};

// Returns true when the send may start (records this send); false when an
// identical send started inside the window (caller must refuse + log).
// Unsigned subtraction keeps millis() wrap safe (window << 2^31).
static inline bool emailDayResendAllowed(EmailDayDedupeState& st, const String& folder,
                                         const String& subject, uint32_t nowMs,
                                         uint32_t windowMs = EMAILDAY_RESEND_WINDOW_MS) {
    if (st.armed && st.folder == folder && st.subject == subject &&
        (nowMs - st.ms) < windowMs) {
        return false;
    }
    st.folder = folder;
    st.subject = subject;
    st.ms = nowMs;
    st.armed = true;
    return true;
}
