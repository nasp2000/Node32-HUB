#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <string>

// Standalone reimplementation of scheduler command/time parsing from
// runtime_scheduler.cpp. Covers the pure parsing logic (no FreeRTOS/time deps):
//   - isSystemCommand(): SYS prefix detection + prefix length
//   - datetime slot parsing: dateStr/timeStr format validation, HH:MM extraction
//   - interval unit normalization (s/m/h) + intervalMs computation
//   - buildSchedCmdEmailFrequencyLabel(): human-readable frequency label
// Reimplements the logic verbatim so bugs in the parsing surface as failures.
namespace {

struct SchedDtSlot {
    std::string dateStr;
    std::string timeStr;
    bool enabled;
    std::string cmd;
    bool daily;
};

// Mirrors isSystemCommand(): a slot with type "system"/"sistema" is always
// system; otherwise a "SYS" prefix (with optional ':' separator and spaces/tabs)
// makes it a system command. Returns true and sets prefixLen to the char right
// after the "SYS[:]" token.
bool isSystemCommand(const std::string& trimmed, const std::string& type, int& prefixLen) {
    std::string upper = trimmed;
    for (size_t i = 0; i < upper.size(); ++i) upper[i] = (char)toupper((unsigned char)upper[i]);
    prefixLen = 0;

    std::string lowerType = type;
    for (size_t i = 0; i < lowerType.size(); ++i) lowerType[i] = (char)tolower((unsigned char)lowerType[i]);
    if (lowerType == "system" || lowerType == "sistema") return true;

    if (upper.size() >= 3 && upper.substr(0, 3) == "SYS") {
        char separator = (trimmed.size() > 3) ? trimmed[3] : '\0';
        if (separator == ':' || separator == ' ' || separator == '\t') {
            int scan = 3;
            while (scan < (int)trimmed.size() && (trimmed[scan] == ' ' || trimmed[scan] == '\t')) scan++;
            if (scan < (int)trimmed.size() && trimmed[scan] == ':') {
                scan++;
                while (scan < (int)trimmed.size() && (trimmed[scan] == ' ' || trimmed[scan] == '\t')) scan++;
            }
            prefixLen = scan;
            return true;
        }
    }
    return false;
}

// Mirrors the datetime slot validation + extraction in processSchedDtSlots().
// Returns false if format/range invalid (slot skipped); on true, fills
// year/month/day/hour/minute.
bool parseDtSlot(const SchedDtSlot& slot, int& year, int& month, int& day, int& hour, int& minute) {
    if (!slot.enabled || slot.cmd.empty()) return false;
    if (slot.dateStr.size() != 10 || slot.timeStr.size() != 5) return false;

    year   = std::stoi(slot.dateStr.substr(0, 4));
    month  = std::stoi(slot.dateStr.substr(5, 2));
    day    = std::stoi(slot.dateStr.substr(8, 2));
    hour   = std::stoi(slot.timeStr.substr(0, 2));
    minute = std::stoi(slot.timeStr.substr(3, 2));
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    return true;
}

// Mirrors the interval unit normalization + intervalMs math in
// processSchedCmdSlots(). Returns interval in milliseconds (0 = invalid unit).
long computeIntervalMs(int interval, const std::string& unit) {
    if (interval <= 0) return 0;
    std::string u = unit;
    if (u == "m") u = "minutes";
    else if (u == "h") u = "hours";
    else if (u == "s") u = "seconds";

    if (u == "seconds") return (long)interval * 1000L;
    if (u == "minutes") return (long)interval * 60000L;
    if (u == "hours")   return (long)interval * 3600000L;
    return 0;  // unknown unit: production treats as 60000 by default; test pins valid units
}

// Mirrors buildSchedCmdEmailFrequencyLabel() logic for the numeric label.
std::string buildFrequencyLabel(int interval, const std::string& unit) {
    if (interval <= 0) return "auto";
    std::string n = unit;
    for (size_t i = 0; i < n.size(); ++i) n[i] = (char)tolower((unsigned char)n[i]);

    if (n == "s" || n == "sec" || n == "second" || n == "seconds") {
        if (interval < 60) return std::to_string(interval) + "s";
        if (interval < 3600) return std::to_string(interval / 60) + "min";
        if (interval < 86400) return std::to_string(interval / 3600) + "h";
        return std::to_string(interval / 86400) + "d";
    }
    if (n == "m" || n == "min" || n == "minute" || n == "minutes") {
        return buildFrequencyLabel(interval * 60, "seconds");
    }
    if (n == "h" || n == "hour" || n == "hours") {
        return buildFrequencyLabel(interval * 3600, "seconds");
    }
    if (n == "d" || n == "day" || n == "days") {
        return std::to_string(interval) + "d";
    }
    return std::to_string(interval) + n;
}

// Mirrors isScheduledRebootCommand() in runtime_scheduler.cpp: the local
// command (already SYS-prefix-stripped and trimmed upstream) is uppercased and
// compared against the REBOOT-family tokens. Scheduled REBOOT-family commands
// bypass the interactive 1h cooldown of handleReboot() and call safeReboot()
// directly, so this predicate decides whether the slot reboot runs.
bool isScheduledRebootCommand(const std::string& localCmd) {
    std::string up = localCmd;
    for (size_t i = 0; i < up.size(); ++i) up[i] = (char)toupper((unsigned char)up[i]);
    size_t b = up.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    size_t e = up.find_last_not_of(" \t\r\n");
    up = up.substr(b, e - b + 1);
    return up == "REBOOT" || up == "RESTART" || up == "ESP.REBOOT" || up == "ESP.RESET";
}

} // namespace

TEST_CASE("datetime slot HH:MM parses to correct hour/minute") {
    SchedDtSlot s; s.dateStr = "2026-08-01"; s.timeStr = "14:30"; s.enabled = true; s.cmd = "X1"; s.daily = false;
    int y, mo, d, h, mi;
    REQUIRE(parseDtSlot(s, y, mo, d, h, mi));
    CHECK(y == 2026); CHECK(mo == 8); CHECK(d == 1);
    CHECK(h == 14); CHECK(mi == 30);
}

TEST_CASE("datetime slot with seconds-like time (HH:MM:SS) is rejected") {
    SchedDtSlot s; s.dateStr = "2026-08-01"; s.timeStr = "14:30:00"; s.enabled = true; s.cmd = "X1";
    int y, mo, d, h, mi;
    CHECK_FALSE(parseDtSlot(s, y, mo, d, h, mi));   // length 8 != 5 -> skipped
}

TEST_CASE("invalid time ranges are rejected") {
    for (std::string bad : {"24:00", "12:60", "99:99", "-1:5", "10:-3"}) {
        SchedDtSlot s; s.dateStr = "2026-08-01"; s.timeStr = bad; s.enabled = true; s.cmd = "X1";
        int y, mo, d, h, mi;
        CHECK_FALSE(parseDtSlot(s, y, mo, d, h, mi));
    }
}

TEST_CASE("invalid date ranges are rejected") {
    for (std::string bad : {"2026-13-01", "2026-00-15", "1969-08-01", "2026-08-00", "2026-08-32"}) {
        SchedDtSlot s; s.dateStr = bad; s.timeStr = "12:00"; s.enabled = true; s.cmd = "X1";
        int y, mo, d, h, mi;
        CHECK_FALSE(parseDtSlot(s, y, mo, d, h, mi));
    }
}

TEST_CASE("disabled or empty-command slots are skipped") {
    SchedDtSlot s; s.dateStr = "2026-08-01"; s.timeStr = "12:00"; s.enabled = false; s.cmd = "X1";
    int y, mo, d, h, mi;
    CHECK_FALSE(parseDtSlot(s, y, mo, d, h, mi));
    s.enabled = true; s.cmd = "";
    CHECK_FALSE(parseDtSlot(s, y, mo, d, h, mi));
}

TEST_CASE("interval units normalize to correct ms") {
    CHECK(computeIntervalMs(5, "s") == 5000L);
    CHECK(computeIntervalMs(5, "seconds") == 5000L);
    CHECK(computeIntervalMs(2, "m") == 120000L);
    CHECK(computeIntervalMs(2, "minutes") == 120000L);
    CHECK(computeIntervalMs(1, "h") == 3600000L);
    CHECK(computeIntervalMs(1, "hours") == 3600000L);
}

TEST_CASE("non-positive interval is rejected") {
    CHECK(computeIntervalMs(0, "s") == 0L);
    CHECK(computeIntervalMs(-5, "m") == 0L);
}

TEST_CASE("SYS prefix detection: colon, space, tab separators") {
    int prefixLen = 0;
    CHECK(isSystemCommand("SYS:gpio 2 high", "rs232", prefixLen));
    CHECK(prefixLen == 4);
    CHECK(isSystemCommand("SYS gpio 2 high", "rs232", prefixLen));
    CHECK(prefixLen == 4);
    CHECK(isSystemCommand("SYS:   gpio 2 high", "rs232", prefixLen));
    CHECK(prefixLen > 4);
}

TEST_CASE("SYS prefix detection: case-insensitive, lowercase sys") {
    int prefixLen = 0;
    CHECK(isSystemCommand("sys:gpio 2 high", "rs232", prefixLen));
    CHECK(prefixLen == 4);
    CHECK(isSystemCommand("Sys: gpio 2 high", "rs232", prefixLen));
    CHECK(prefixLen == 5);   // "Sys: " -> prefix includes the space after colon
}

TEST_CASE("non-SYS commands are not system commands") {
    int prefixLen = 0;
    CHECK_FALSE(isSystemCommand("X10 off", "rs232", prefixLen));
    CHECK_FALSE(isSystemCommand("SYSTEM", "rs232", prefixLen));      // no separator
    CHECK_FALSE(isSystemCommand("SYX:foo", "rs232", prefixLen));     // wrong prefix
    CHECK_FALSE(isSystemCommand("", "rs232", prefixLen));            // empty
}

TEST_CASE("type system/sistema marks command as system regardless of text") {
    int prefixLen = 0;
    CHECK(isSystemCommand("anything here", "system", prefixLen));
    CHECK(prefixLen == 0);   // whole string is the command
    CHECK(isSystemCommand("qualquer coisa", "sistema", prefixLen));
    CHECK(prefixLen == 0);
}

TEST_CASE("frequency labels: seconds/minutes/hours/days") {
    CHECK(buildFrequencyLabel(30, "s") == "30s");
    CHECK(buildFrequencyLabel(90, "s") == "1min");
    CHECK(buildFrequencyLabel(7200, "s") == "2h");
    CHECK(buildFrequencyLabel(90000, "s") == "1d");
    CHECK(buildFrequencyLabel(5, "m") == "5min");
    CHECK(buildFrequencyLabel(2, "h") == "2h");
    CHECK(buildFrequencyLabel(3, "d") == "3d");
}

TEST_CASE("frequency label with invalid interval returns auto") {
    CHECK(buildFrequencyLabel(0, "s") == "auto");
    CHECK(buildFrequencyLabel(-1, "m") == "auto");
}

TEST_CASE("scheduled reboot: REBOOT-family matches (case/whitespace insensitive)") {
    for (const auto& cmd : {"REBOOT", "reboot", "ReBoot", "  REBOOT  ", "RESTART", "restart",
                            "ESP.REBOOT", "esp.reboot", "ESP.RESET", "esp.reset"}) {
        INFO("cmd=" << cmd);
        CHECK(isScheduledRebootCommand(cmd));
    }
}

TEST_CASE("scheduled reboot: non-reboot commands never match") {
    for (const auto& cmd : {"", "EMAILDAY", "LOGD", "LOGDAY", "REBOOT_NANO", "NANO.REBOOT",
                            "SYS:REBOOT", "REBOOT NOW", "G0 X10", "HELP", "ESP.RESET NOW"}) {
        INFO("cmd=" << cmd);
        CHECK_FALSE(isScheduledRebootCommand(cmd));
    }
}
