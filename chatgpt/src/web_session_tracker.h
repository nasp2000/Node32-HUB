// web_session_tracker.h — staleness rule for the multi-client session tracker.
//
// webclientCleanup() wipes the per-IP tracker when it looks leaked (a client
// that disappeared without its onDisconnect firing). The "last activity" stamp
// used to be refreshed only by allowHtmlClient() (a new page load) and, with
// basic auth on, by ensureBasicAuth(). A browser that stays on one live page
// and only polls APIs (camera status, /m step 3, status page, ...) therefore
// looked idle: after 30 s the tracker — and active_html_sessions — were wiped
// and then wiped again every 5 s, even though the client was requesting data
// the whole time (device log: "Web: reset multi-client sessions (stale
// tracker)" repeating while a browser was open).
//
// The rule now lives here, FreeRTOS-free, so it can be unit-tested natively:
// a heartbeat counts as activity, and a tracker with no sessions or with no
// activity stamp yet is never considered stale.
#pragma once

#include <stdint.h>

static const unsigned long WEB_TRACKER_STALE_MS = 30000UL;

// New activity stamp: only requests from tracked sessions move it forward.
inline unsigned long webTrackerTouch(unsigned long nowMs, unsigned long lastActivityMs, uint8_t sessions) {
    if (sessions == 0) return lastActivityMs;
    (void)lastActivityMs;
    return nowMs;
}

// True only when there is something to reset and it has really been silent.
inline bool webTrackerIsStale(unsigned long nowMs, unsigned long lastActivityMs,
                              uint8_t sessions, unsigned long staleMs = WEB_TRACKER_STALE_MS) {
    if (sessions == 0) return false;         // nothing tracked: nothing to reset
    if (lastActivityMs == 0) return false;   // never stamped: do not wipe right after boot
    // Compare in 32 bits like millis() on the target: an unsigned long is 64-bit
    // on the host, and a plain subtraction would break at the millis() rollover.
    const uint32_t elapsed = (uint32_t)nowMs - (uint32_t)lastActivityMs;
    return elapsed >= (uint32_t)staleMs;
}
