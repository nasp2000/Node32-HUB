#pragma once

// Standard SD layout — single convention for ALL packs (and future packs).
//
//   /{sys}/                            system folder (getSdcardName(), e.g. /Equip15/)
//   /{sys}/{SOURCE}_DD-MM-YYYY.<ext>   single-file-per-day sources (FLAT)
//   /{sys}/{SOURCE}_DD-MM-YYYY/        rotated multi-file sources (SUBFOLDER)
//   /{sys}/<aggregate>                 cross-day aggregates, flat (wkNN-oee.csv, *.chart)
//
// Rules for new packs/modules: one file per day -> sdDayFile(); rotation with
// several files per day -> sdDayDir(); aggregates -> plain names under sysDir().
// Header-only (no new build units); pure string building, native-tested.
#include <Arduino.h>
#include <time.h>

namespace sd_layout {

// System folder: "/Equip15".
inline String sysDir(const String& sys) { return String("/") + sys; }

// Flat day file: "/Equip15/PROXIMITY_18-09-2026.csv".
inline String dayFile(const String& sys, const String& source, const String& date, const String& ext) {
    return sysDir(sys) + String("/") + source + String("_") + date + String(".") + ext;
}

// Dated subfolder for rotated sources: "/Equip15/RS232_18-09-2026".
inline String dayDir(const String& sys, const String& source, const String& date) {
    return sysDir(sys) + String("/") + source + String("_") + date;
}

// Cross-day aggregate (never auto-deleted, always attached to day emails):
// lowercase "wk" prefix + "-oee.csv" suffix (proximityWeeklyOeeFileName).
inline bool isAggregate(const String& name) {
    if (name.length() < 11) return false;
    bool wk = (name.indexOf("wk") == 0) || (name.indexOf("WK") == 0);
    return wk && name.endsWith("-oee.csv");
}

// True for "<base>_DD-MM-YYYY.<ext>" day files. Optionally returns the date
// part ("DD-MM-YYYY"). Used by auto-cleanup candidates and day-email filters.
// Rejects zero-length/aggregate names; validates digit ranges (no sscanf,
// locale-independent).
inline bool isDayFile(const String& name, String* dateOut = nullptr) {
    int len = (int)name.length();
    int dot = -1;
    for (int i = len - 1; i >= 0; i--) {
        char c = name.charAt(i);
        if (c == '.') { dot = i; break; }
        if (c == '/') break;
    }
    // Need "<base>_<10-char date>.<ext>": at least 1 + 1 + 10 + 1 + 1 chars.
    if (dot < 13) return false;
    if (name.charAt(dot - 11) != '_') return false;
    String date = name.substring(dot - 10, dot);
    for (int i = 0; i < 10; i++) {
        char c = date.charAt(i);
        bool wantDigit = (i != 2 && i != 5);
        if (wantDigit && (c < '0' || c > '9')) return false;
        if (!wantDigit && c != '-') return false;
    }
    int d = (date.charAt(0) - '0') * 10 + (date.charAt(1) - '0');
    int m = (date.charAt(3) - '0') * 10 + (date.charAt(4) - '0');
    int y = (date.charAt(6) - '0') * 1000 + (date.charAt(7) - '0') * 100 +
            (date.charAt(8) - '0') * 10 + (date.charAt(9) - '0');
    if (d < 1 || d > 31 || m < 1 || m > 12 || y < 2000 || y > 2099) return false;
    if (isAggregate(name)) return false;
    if (dateOut) *dateOut = date;
    return true;
}

// Retention gate for auto-cleanup SOFT tier (native-tested).
// Above the usage threshold the HARD tier deletes the oldest entry whatever
// its age; at/below threshold only entries at/past keepDays go (this gate).
// keepDays <= 0 is the UI "infinity": no age limit (soft tier never fires,
// hard tier still holds usage at the threshold).
// Negative inputs are defensive-only (getters clamp) and never delete.
inline bool cleanupDeleteAllowed(int ageDays, int keepDays) {
    if (keepDays <= 0) return false;
    if (ageDays < 0) return false;
    return ageDays >= keepDays;
}

// Pre-NTP gate. The RTC boots at epoch ≈ 0 (1970) and NTP syncs it seconds
// later; anything below 2020-09-13 is a meaningless boot-time rollover or a
// factory-reset RTC. Writers use this to avoid fabricated/1970 dated paths.
#define SD_CLOCK_SYNC_EPOCH  1600000000L   // 2020-09-13 00:00:00 UTC
inline bool clockReady(time_t now) { return now >= SD_CLOCK_SYNC_EPOCH; }

namespace {
bool _sdIsDigit(char c) { return c >= '0' && c <= '9'; }
bool _sdValidDate(int d, int m, int y) { return d >= 1 && d <= 31 && m >= 1 && m <= 12 && y >= 2000 && y <= 2099; }
}  // namespace

// Extract any embedded date from a folder/file NAME and return it as a
// canonical integer YYYYMMDD (0 = none / not a date / staged "PENDING").
// Formats understood (in precedence order):
//   YYYY-MM-DD   (camera folders, sensor_history, PSRAM VFS, sniffer)
//   YYYYMMDD     (legacy camera folders, GRBL job logs, time-suffixed names)
//   DD-MM-YYYY   (datastore day files/folders, crash-log folders)
// Pure string scan, no sscanf, locale-independent — native-tested.
inline int parseDateFromName(const String& name) {
    int len = (int)name.length();
    if (len < 8) return 0;
    if (name.indexOf("PENDING") >= 0 || name.indexOf("nodate") >= 0) return 0;

    for (int run = 0; run < 3; run++) {
        for (int i = 0; i <= len - 8; i++) {
            if (run == 0) { // YYYY-MM-DD
                if (!_sdIsDigit(name.charAt(i)))     continue;
                if (!_sdIsDigit(name.charAt(i + 1))) continue;
                if (!_sdIsDigit(name.charAt(i + 2))) continue;
                if (!_sdIsDigit(name.charAt(i + 3))) continue;
                if (name.charAt(i + 4) != '-')    continue;
                if (!_sdIsDigit(name.charAt(i + 5))) continue;
                if (!_sdIsDigit(name.charAt(i + 6))) continue;
                if (name.charAt(i + 7) != '-')    continue;
                if (!_sdIsDigit(name.charAt(i + 8))) continue;
                if (!_sdIsDigit(name.charAt(i + 9))) continue;
                int y = (name.charAt(i) - '0') * 1000 + (name.charAt(i + 1) - '0') * 100 +
                        (name.charAt(i + 2) - '0') * 10 + (name.charAt(i + 3) - '0');
                int m = (name.charAt(i + 5) - '0') * 10 + (name.charAt(i + 6) - '0');
                int d = (name.charAt(i + 8) - '0') * 10 + (name.charAt(i + 9) - '0');
                if (!_sdValidDate(d, m, y)) continue;
                return y * 10000 + m * 100 + d;
            } else if (run == 1) { // YYYYMMDD (8 digits)
                bool allDigits = true;
                for (int k = 0; k < 8; k++) {
                    if (!_sdIsDigit(name.charAt(i + k))) { allDigits = false; break; }
                }
                if (!allDigits) continue;
                int y = (name.charAt(i) - '0') * 1000 + (name.charAt(i + 1) - '0') * 100 +
                        (name.charAt(i + 2) - '0') * 10 + (name.charAt(i + 3) - '0');
                int m = (name.charAt(i + 4) - '0') * 10 + (name.charAt(i + 5) - '0');
                int d = (name.charAt(i + 6) - '0') * 10 + (name.charAt(i + 7) - '0');
                if (!_sdValidDate(d, m, y)) continue;
                return y * 10000 + m * 100 + d;
            } else { // DD-MM-YYYY
                if (!_sdIsDigit(name.charAt(i)))     continue;
                if (!_sdIsDigit(name.charAt(i + 1))) continue;
                if (name.charAt(i + 2) != '-')    continue;
                if (!_sdIsDigit(name.charAt(i + 3))) continue;
                if (!_sdIsDigit(name.charAt(i + 4))) continue;
                if (name.charAt(i + 5) != '-')    continue;
                if (!_sdIsDigit(name.charAt(i + 6))) continue;
                if (!_sdIsDigit(name.charAt(i + 7))) continue;
                if (!_sdIsDigit(name.charAt(i + 8))) continue;
                if (!_sdIsDigit(name.charAt(i + 9))) continue;
                int d = (name.charAt(i) - '0') * 10 + (name.charAt(i + 1) - '0');
                int m = (name.charAt(i + 3) - '0') * 10 + (name.charAt(i + 4) - '0');
                int y = (name.charAt(i + 6) - '0') * 1000 + (name.charAt(i + 7) - '0') * 100 +
                        (name.charAt(i + 8) - '0') * 10 + (name.charAt(i + 9) - '0');
                if (!_sdValidDate(d, m, y)) continue;
                return y * 10000 + m * 100 + d;
            }
        }
    }
    return 0;
}

// Decompose a canonical YYYYMMDD int (as returned by parseDateFromName) into
// calendar fields, for age computation in auto-cleanup.
inline void dateYmdParts(int ymd, int& y, int& m, int& d) {
    d = ymd % 100;
    m = (ymd / 100) % 100;
    y = ymd / 10000;
}

}  // namespace sd_layout
