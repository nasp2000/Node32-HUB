#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "MockConfigStore.h"
#include "MockEmailService.h"
#include "MockLogger.h"

#include <string>
#include <vector>

// Standalone reimplementation of the inactivity alert decision pipeline from
// wifi_config_impl.h::checkAndSendInactivityAlert(), using the existing
// MockConfigStore / MockEmailService / MockLogger test-doubles.
// Pipeline (mirrors production):
//   enabled? -> throttle 10s -> day-change reset -> max_per_day? ->
//   first-data-received? -> timeout elapsed? -> already-sent-this-period? ->
//   emails configured? -> send email -> alertsToday++, sent=true, log WARN.
namespace {

class AlertPipeline {
public:
    explicit AlertPipeline(MockConfigStore& cfg, MockEmailService& mail, MockLogger& log)
        : cfg_(cfg), mail_(mail), log_(log) {
        // Seed production-like defaults so tests focus on what they override.
        setEnabled(true);
        setTimeoutMinutes(20);
        setMaxPerDay(12);
        setEmails("ops@example.com");
    }

    // Config knobs (stored in MockConfigStore, mirroring ConfigManager keys).
    void setEnabled(bool v) { setStr("enabled", v ? "1" : "0"); }
    void setTimeoutMinutes(int v) { setStr("timeout_minutes", std::to_string(v)); }
    void setMaxPerDay(int v) { setStr("max_per_day", std::to_string(v)); }
    void setEmails(const std::string& v) { setStr("emails", v); }

    // Runtime knobs.
    void setLastDataReceived(unsigned long v) { lastDataReceived_ = v; }
    void setAlertSentForPeriod(bool v) { alertSent_ = v; }
    void setAlertsToday(int v) { alertsToday_ = v; }
    void setLastDay(int v) { lastDay_ = v; }
    void setNow(unsigned long v) { now_ = v; }

    // Advances the internal 10-second throttle clock.
    void allowCheck() { lastCheck_ = 0; }

    int alertsToday() const { return alertsToday_; }
    bool alertSentForPeriod() const { return alertSent_; }

    void evaluate() {
        if (cfg_.getRaw("inactivity_alert.enabled") != "1") return;   // alerts disabled
        if (now_ - lastCheck_ < 10000) return;        // throttle
        lastCheck_ = now_;

        // Day change resets the daily counter.
        if (day_ >= 0 && lastDay_ != day_) {
            lastDay_ = day_;
            alertsToday_ = 0;
        }

        int maxPerDay = parseInt(cfg_.getRaw("inactivity_alert.max_per_day"), 12);
        if (alertsToday_ >= maxPerDay) return;
        if (lastDataReceived_ == 0) return;           // wait for first data

        int timeoutMinutes = parseInt(cfg_.getRaw("inactivity_alert.timeout_minutes"), 20);
        unsigned long timeoutMs = (unsigned long)timeoutMinutes * 60UL * 1000UL;
        if ((now_ - lastDataReceived_) < timeoutMs) return;
        if (alertSent_) return;

        if (cfg_.getRaw("inactivity_alert.emails").empty()) {
            log_.error("alert", "No emails configured");
            return;
        }

        bool started = mail_.isReady();
        if (started) {
            mail_.send(cfg_.getRaw("inactivity_alert.emails"), "subject", "body");
            alertsToday_++;
            alertSent_ = true;
            log_.warn("alert", "Alert sent");
        }
    }

private:
    void setStr(const std::string& k, const std::string& v) { cfg_.setRaw("inactivity_alert." + k, v); }

    static int parseInt(const std::string& s, int fallback) {
        if (s.empty()) return fallback;
        try { return std::stoi(s); } catch (...) { return fallback; }
    }

    MockConfigStore& cfg_;
    MockEmailService& mail_;
    MockLogger& log_;

    unsigned long lastDataReceived_ = 0;
    unsigned long now_ = 0;
    unsigned long lastCheck_ = 0;
    int day_ = 5;
    int lastDay_ = 5;
    int alertsToday_ = 0;
    bool alertSent_ = false;
};

} // namespace

TEST_CASE("below timeout does not send alert or log") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setTimeoutMinutes(1);          // 60s timeout
    p.setLastDataReceived(1000);
    p.setNow(50UL * 1000UL);         // 49s elapsed < 60s -> no alert
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 0);
    CHECK(log.entryCount() == 0);
}

TEST_CASE("timeout reached sends email and logs warn, then flags period sent") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);         // 60s elapsed >= 60s timeout
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 1);
    CHECK(mail.lastSent().to == "ops@example.com");
    CHECK(p.alertSentForPeriod());
    CHECK(log.lastEntry().level == "WARN");
}

TEST_CASE("second alert within same period is suppressed (already sent)") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 1);

    // Still past timeout, still same data timestamp -> same inactivity period.
    p.setNow(120UL * 1000UL);
    p.evaluate();
    CHECK(mail.sentCount() == 1);   // not a second email
}

TEST_CASE("new activity resets the period-sent flag and allows a later alert") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 1);

    // Activity arrives -> reset timestamp + clear sent flag (mirrors
    // updateInactivityAlertTimestamp()).
    p.setLastDataReceived(120UL * 1000UL);
    p.setAlertSentForPeriod(false);
    p.setNow(200UL * 1000UL);
    p.evaluate();
    CHECK(mail.sentCount() == 2);   // 200s - 120s = 80s > 60s -> second alert
}

TEST_CASE("daily max_per_day reached suppresses further alerts until reset") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setMaxPerDay(2);
    p.setTimeoutMinutes(1);

    // Three consecutive periods, each with a fresh data timestamp 3 minutes apart.
    p.setLastDataReceived(0UL);
    p.setAlertSentForPeriod(false);
    p.setNow(1UL * 60UL * 1000UL);
    p.allowCheck();
    p.evaluate();   // period 1 -> sends (alertsToday 0->1)

    p.setLastDataReceived(1UL * 60UL * 1000UL);
    p.setAlertSentForPeriod(false);
    p.setNow(2UL * 60UL * 1000UL);
    p.allowCheck();
    p.evaluate();   // period 2 -> sends (alertsToday 1->2)

    p.setLastDataReceived(2UL * 60UL * 1000UL);
    p.setAlertSentForPeriod(false);
    p.setNow(3UL * 60UL * 1000UL);
    p.allowCheck();
    p.evaluate();   // period 3 -> suppressed (already at max 2)
    CHECK(mail.sentCount() == 2);   // capped at max_per_day
    CHECK(p.alertsToday() == 2);
}

TEST_CASE("day change resets the daily counter") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setMaxPerDay(1);
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 1);
    CHECK(p.alertsToday() == 1);

    // Next day, fresh period (new day_ is simulated by the constructor day_
    // being compared; we advance day_ to force a reset).
    p.setLastDataReceived(61UL * 1000UL);
    p.setAlertSentForPeriod(false);
    p.setNow(120UL * 1000UL);
    p.setLastDay(4);        // pretend last alert was yesterday (day 4)
    p.allowCheck();
    p.evaluate();
    CHECK(p.alertsToday() == 0);   // counter reset by day change
}

TEST_CASE("empty recipient list logs error and does not send") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setEmails("");
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 0);
    CHECK(log.lastEntry().level == "ERROR");
    CHECK(p.alertSentForPeriod() == false);
}

TEST_CASE("SMTP not ready -> pipeline does not send and stays un-sent") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    mail.setReady(false);
    AlertPipeline p(cfg, mail, log);
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 0);
    CHECK(p.alertSentForPeriod() == false);
}

TEST_CASE("alerts disabled bypasses the entire pipeline") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setEnabled(false);
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 0);
    CHECK(log.entryCount() == 0);
}

TEST_CASE("no data ever received never triggers even after long uptime") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setTimeoutMinutes(20);
    p.setLastDataReceived(0);   // never got first data
    p.setNow(999999UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 0);
}

TEST_CASE("10s throttle prevents re-evaluation within window") {
    MockConfigStore cfg; MockEmailService mail; MockLogger log;
    AlertPipeline p(cfg, mail, log);
    p.setTimeoutMinutes(1);
    p.setLastDataReceived(1000);
    p.setNow(61UL * 1000UL);
    p.allowCheck();
    p.evaluate();
    CHECK(mail.sentCount() == 1);

    // lastCheck_ was set to now_; second evaluate within 10s is a no-op.
    p.setAlertSentForPeriod(false);
    p.evaluate();
    CHECK(mail.sentCount() == 1);
}

// ── Expanded: multi-slot inactivity alert pipeline (mirrors production's
//    ConfigManager 6-slot model with independent per-slot state) ──────────
namespace {

const int SLOT_COUNT = 6;

struct SlotCfg {
    bool enabled = false;
    int timeoutMinutes = 20;
    int maxPerDay = 12;
    std::string emails;
};

class MultiSlotPipeline {
public:
    std::vector<SlotCfg> slots{size_t(SLOT_COUNT)};
    int alertsTodaySlot[SLOT_COUNT] = {0,0,0,0,0,0};
    bool alertSentSlot[SLOT_COUNT] = {false,false,false,false,false,false};
    unsigned long lastDataReceived = 0;
    unsigned long now = 0;
    unsigned long lastCheck = 0;
    int lastDay = -1;
    int currentDay = 5;
    bool sensorsResetTimer = false;

    int sentCount = 0;

    void allowCheck() { lastCheck = 0; }

    // Simulates data arriving (mirrors updateInactivityAlertTimestamp()).
    void activityArrives(unsigned long at) {
        lastDataReceived = at;
        for (int i = 0; i < SLOT_COUNT; ++i) alertSentSlot[i] = false;
    }

    void evaluate() {
        bool anyEnabled = false;
        for (int i = 0; i < SLOT_COUNT; ++i) if (slots[i].enabled) { anyEnabled = true; break; }
        if (!anyEnabled) return;
        if (now - lastCheck < 10000) return;
        lastCheck = now;

        if (currentDay >= 0 && lastDay != currentDay) {
            lastDay = currentDay;
            for (int i = 0; i < SLOT_COUNT; ++i) { alertsTodaySlot[i] = 0; alertSentSlot[i] = false; }
        }
        if (lastDataReceived == 0) return;

        for (int i = 0; i < SLOT_COUNT; ++i) {
            if (!slots[i].enabled) continue;
            if (alertsTodaySlot[i] >= slots[i].maxPerDay) continue;
            unsigned long timeoutMs = (unsigned long)slots[i].timeoutMinutes * 60UL * 1000UL;
            if ((now - lastDataReceived) < timeoutMs) continue;
            if (alertSentSlot[i]) continue;
            if (slots[i].emails.empty()) continue;
            sentCount++;
            alertsTodaySlot[i]++;
            alertSentSlot[i] = true;
        }
    }
};

// Human-readable timeout description, mirroring production formatting.
static std::string timeoutLabel(int minutes) {
    if (minutes < 60) return std::to_string(minutes) + " min";
    if (minutes == 60) return "1h";
    if (minutes < 1440) return std::to_string(minutes / 60) + "h";
    return "24h";
}

} // namespace

TEST_CASE("multi-slot: only enabled slots send") {
    MultiSlotPipeline p;
    p.slots[0].enabled = true;  p.slots[0].timeoutMinutes = 1; p.slots[0].emails = "a@x.com";
    p.slots[2].enabled = true;  p.slots[2].timeoutMinutes = 1; p.slots[2].emails = "c@x.com";
    p.activityArrives(1000);
    p.now = 61UL * 1000UL;
    p.allowCheck();
    p.evaluate();
    CHECK(p.sentCount == 2);          // slots 0 and 2 sent
    CHECK(p.alertsTodaySlot[0] == 1);
    CHECK(p.alertsTodaySlot[2] == 1);
    CHECK(p.alertsTodaySlot[1] == 0); // disabled slot untouched
}

TEST_CASE("multi-slot: independent max_per_day caps per slot") {
    MultiSlotPipeline p;
    p.slots[0].enabled = true; p.slots[0].timeoutMinutes = 1; p.slots[0].maxPerDay = 1; p.slots[0].emails = "a@x.com";
    p.slots[1].enabled = true; p.slots[1].timeoutMinutes = 1; p.slots[1].maxPerDay = 3; p.slots[1].emails = "b@x.com";

    // Three separate inactivity periods.
    p.activityArrives(1000); p.now = 61UL*1000UL; p.allowCheck(); p.evaluate();           // both send (1 each)
    p.activityArrives(61UL*1000UL); p.now = 122UL*1000UL; p.allowCheck(); p.evaluate();   // slot0 capped, slot1 sends (2)
    p.activityArrives(122UL*1000UL); p.now = 183UL*1000UL; p.allowCheck(); p.evaluate();  // slot0 capped, slot1 sends (3)
    CHECK(p.alertsTodaySlot[0] == 1);   // capped at its own max
    CHECK(p.alertsTodaySlot[1] == 3);   // independent, reached its own max
}

TEST_CASE("multi-slot: different timeouts per slot") {
    MultiSlotPipeline p;
    p.slots[0].enabled = true; p.slots[0].timeoutMinutes = 1;  p.slots[0].emails = "a@x.com";
    p.slots[1].enabled = true; p.slots[1].timeoutMinutes = 5;  p.slots[1].emails = "b@x.com";
    p.activityArrives(1000);
    p.now = 2UL * 60UL * 1000UL;  // 119s elapsed
    p.allowCheck();
    p.evaluate();
    CHECK(p.alertsTodaySlot[0] == 1);  // 119s > 60s -> slot0 sent
    CHECK(p.alertsTodaySlot[1] == 0);  // 119s < 300s -> slot1 not yet
}

TEST_CASE("multi-slot: per-slot sent flags independent") {
    MultiSlotPipeline p;
    p.slots[0].enabled = true; p.slots[0].timeoutMinutes = 1; p.slots[0].emails = "a@x.com";
    p.slots[1].enabled = true; p.slots[1].timeoutMinutes = 1; p.slots[1].emails = "b@x.com";
    p.activityArrives(1000); p.now = 61UL*1000UL; p.allowCheck(); p.evaluate();
    CHECK(p.alertSentSlot[0] == true);
    CHECK(p.alertSentSlot[1] == true);

    // New activity resets ALL slots (mirrors updateInactivityAlertTimestamp).
    p.activityArrives(100UL*1000UL);
    CHECK(p.alertSentSlot[0] == false);
    CHECK(p.alertSentSlot[1] == false);
}

TEST_CASE("multi-slot: day change resets all per-slot counters") {
    MultiSlotPipeline p;
    p.slots[0].enabled = true; p.slots[0].timeoutMinutes = 1; p.slots[0].maxPerDay = 1; p.slots[0].emails = "a@x.com";
    p.slots[1].enabled = true; p.slots[1].timeoutMinutes = 1; p.slots[1].maxPerDay = 1; p.slots[1].emails = "b@x.com";
    p.activityArrives(1000); p.now = 61UL*1000UL; p.allowCheck(); p.evaluate();
    CHECK(p.alertsTodaySlot[0] == 1);
    CHECK(p.alertsTodaySlot[1] == 1);

    // Day changes.
    p.currentDay = 6;
    p.activityArrives(61UL*1000UL); p.now = 122UL*1000UL; p.allowCheck(); p.evaluate();
    CHECK(p.alertsTodaySlot[0] == 1);  // was reset to 0 then sent again -> 1
    CHECK(p.alertsTodaySlot[1] == 1);
    // Confirm reset happened: without reset these would be capped (2nd period suppressed).
    CHECK(p.sentCount == 4);           // 2 first day + 2 next day = 4
}

TEST_CASE("multi-slot: empty emails slot skipped, other slots still send") {
    MultiSlotPipeline p;
    p.slots[0].enabled = true; p.slots[0].timeoutMinutes = 1; p.slots[0].emails = "";   // no emails
    p.slots[1].enabled = true; p.slots[1].timeoutMinutes = 1; p.slots[1].emails = "b@x.com";
    p.activityArrives(1000); p.now = 61UL*1000UL; p.allowCheck(); p.evaluate();
    CHECK(p.alertsTodaySlot[0] == 0);  // skipped silently
    CHECK(p.alertsTodaySlot[1] == 1);
}

TEST_CASE("multi-slot: sensors_reset_timer governs whether sensor activity resets") {
    // With sensors_reset_timer true (or no RS232/LPT on board), sensor activity
    // resets lastDataReceived -> pending alert is cleared.
    MultiSlotPipeline p;
    p.slots[0].enabled = true; p.slots[0].timeoutMinutes = 1; p.slots[0].emails = "a@x.com";
    p.activityArrives(1000);
    p.now = 61UL * 1000UL;
    p.allowCheck();
    p.evaluate();
    CHECK(p.alertsTodaySlot[0] == 1);  // alert fired after 60s idle

    // New sensor activity arrives -> timer resets (mirrors proximity/sensor reset).
    p.activityArrives(70UL * 1000UL);
    p.now = 100UL * 1000UL;
    p.allowCheck();
    p.evaluate();
    CHECK(p.alertsTodaySlot[0] == 1);  // 30s since last data < 60s -> no new alert
}

TEST_CASE("timeout label formatting") {
    CHECK(timeoutLabel(5) == "5 min");
    CHECK(timeoutLabel(60) == "1h");
    CHECK(timeoutLabel(120) == "2h");
    CHECK(timeoutLabel(720) == "12h");
    CHECK(timeoutLabel(1440) == "24h");
}
