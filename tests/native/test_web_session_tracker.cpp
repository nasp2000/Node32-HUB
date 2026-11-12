// Native tests for the multi-client session tracker staleness rule
// (chatgpt/src/web_session_tracker.h).
//
// Live defect (device logs, .9/.29 2026-11-12): a browser that stayed on one
// page and only polled APIs was reset every 30 s — "Web: reset multi-client
// sessions (stale tracker)" repeating — because heartbeats never refreshed the
// activity stamp, and each wipe zeroed active_html_sessions.
//
// These cases model the cleanup loop (runs every 5 s, threshold 30 s) with and
// without the heartbeat refresh, so the bug is reproduced first and the fix
// proven after.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "web_session_tracker.h"

namespace {

// Models webclientCleanup() ticking every 5 s with one tracked client.
// refreshOnHeartbeat = the fix; when false it reproduces the old behaviour
// (only a page load moved the stamp, so it stays frozen here).
struct TrackerLoop {
    unsigned long nowMs = 0;
    unsigned long lastActivityMs = 0;
    uint8_t sessions = 0;
    bool refreshOnHeartbeat = true;
    uint32_t resets = 0;

    void heartbeat() {
        sessions = 1;  // multiTrackTouch() re-adds the IP after a wipe
        if (refreshOnHeartbeat) {
            lastActivityMs = webTrackerTouch(nowMs, lastActivityMs, sessions);
        } else if (lastActivityMs == 0) {
            lastActivityMs = nowMs;  // the single page load that started the session
        }
    }

    // One 100 ms slice of the main loop: poll every second, cleanup every 5 s.
    void run(unsigned long durationMs) {
        const unsigned long end = nowMs + durationMs;
        while (nowMs < end) {
            nowMs += 100;
            if ((nowMs % 1000) == 0) heartbeat();
            if ((nowMs % 5000) == 0) {
                if (webTrackerIsStale(nowMs, lastActivityMs, sessions)) {
                    resets++;
                    sessions = 0;
                    // the old code also zeroed active_html_sessions here
                }
            }
        }
    }
};

} // namespace

TEST_CASE("defect: without a heartbeat refresh an active client is reset every 30 s") {
    TrackerLoop t;
    t.refreshOnHeartbeat = false;
    t.heartbeat();                       // page load at t=0
    t.run(10UL * 60UL * 1000UL);         // 10 minutes of 1 s API polls

    CHECK(t.resets > 0);                 // the bug: "stale" while actively polling
    // one reset per stale window from the same frozen stamp (30 s, 60 s, ...)
    CHECK(t.resets >= 19);
}

TEST_CASE("fix: a polling client is never considered stale") {
    TrackerLoop t;
    t.refreshOnHeartbeat = true;
    t.heartbeat();
    t.run(10UL * 60UL * 1000UL);

    CHECK(t.resets == 0);
    CHECK(t.sessions == 1);              // tracker intact
    CHECK(webTrackerIsStale(t.nowMs, t.lastActivityMs, t.sessions) == false);
}

TEST_CASE("real silence is still recovered") {
    TrackerLoop t;
    t.heartbeat();                       // client present
    t.run(2000);                         // two polls
    const unsigned long before = t.nowMs;
    // Client disappears: no heartbeats, only the cleanup keeps running.
    for (unsigned long i = 0; i < 10; i++) {
        t.nowMs += 5000;
        if (webTrackerIsStale(t.nowMs, t.lastActivityMs, t.sessions)) { t.resets++; t.sessions = 0; }
        if (t.sessions == 0) break;
    }
    CHECK(t.nowMs - before >= WEB_TRACKER_STALE_MS);
    CHECK(t.resets == 1);
    CHECK(t.sessions == 0);
}

TEST_CASE("nothing tracked (sessions 0) is never stale") {
    CHECK(webTrackerIsStale(100000, 1000, 0) == false);
    CHECK(webTrackerIsStale(100000, 0, 0) == false);
}

TEST_CASE("never stamped (boot) is never stale") {
    CHECK(webTrackerIsStale(100000, 0, 1) == false);
    CHECK(webTrackerTouch(5000, 0, 0) == 0);      // no sessions: stamp untouched
    CHECK(webTrackerTouch(5000, 0, 1) == 5000);   // first tracked request stamps it
}

TEST_CASE("threshold edges and millis() wrap") {
    CHECK(webTrackerIsStale(29999, 0, 1) == false);
    CHECK(webTrackerIsStale(30000, 0, 1) == false);   // stamp 0 is "unknown"
    CHECK(webTrackerIsStale(30000, 1, 1) == false);   // 29 999 ms elapsed
    CHECK(webTrackerIsStale(30001, 1, 1) == true);    // 30 000 ms elapsed
    CHECK(webTrackerIsStale(30000, 0, 0) == false);

    // 32-bit millis rollover: 0xFFFFFF00 -> 0x00000200 is 768 ms, not stale.
    const unsigned long last = 0xFFFFFF00UL;
    const unsigned long now = 0x00000200UL;
    CHECK(webTrackerIsStale(now, last, 1) == false);
    CHECK(webTrackerIsStale((unsigned long)(last + 40000UL), last, 1) == true);
}
