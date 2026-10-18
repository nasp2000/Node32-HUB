#pragma once

// SensorDay daily report body — ShiftEnd-style summary for /sensor_history.
// Pure header-only, no Arduino deps: the exact production logic is compiled
// by the native test (tests/native/test_sensor_day_report.cpp).
//
// Format mirrors proximityBuildShiftEmailBody / Equip15 ShiftEnd .eml:
//   Registos: (SensorDay YYYY-MM-DD)
//
//   Temp min X.XC  med Y.YC  max Z.ZC (N=NNN)
//   Hum min A%  med B%  max C% (N=NNN)        <- only when humidity present
//
//   Temp por hora:
//
//   00H  21.3C ▅   45% ▄
//   ...
//   22H   --
//
// One row every 2 hours (00H, 02H ... 22H — 12 rows) and fixed-width numeric
// columns (%5.1fC / %3.0f%%), so the glyph columns always line up and the
// body stays phone-width (owner pick 2026-10-15, "variant 8 every 2 hours").
// Each row is the MEAN of that hour alone; odd hours are not listed but their
// samples still count in the min/med/max summary and in N.
//
// Per-hour, per-metric sparkline glyph (U+2581..U+2588, 8 levels): scaled on
// the day's sample min-max (temp on tMin/tMax, humidity on hMin/hMax), so the
// 12 rows draw the day's curve shape (night dip, afternoon peak) at a glance
// — apt for continuous signals like temperature/humidity and much lighter
// than the old 10-cell FULL-BLOCK bars. A flat day renders the mid glyph;
// an hour with no samples renders "--" placeholders (row kept short).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Sparkline level for one hourly mean inside the day's sample [lo, hi]:
// U+2581 ▁ .. U+2588 █ (8 levels). Flat day (hi<=lo) renders the mid glyph.
static inline const char* sensorDayLevelGlyph(float v, float lo, float hi) {
    static const char* LV[8] = {"\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
                                "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88"};
    float f = 0.5f;
    if (hi > lo) {
        f = (v - lo) / (hi - lo);
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
    }
    int idx = (int)(f * 7.0f + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > 7) idx = 7;
    return LV[idx];
}

struct SensorDayAggr {
    int n;            // temperature samples
    double tSum;
    float tMin, tMax;
    int hn;           // humidity samples
    double hSum;
    float hMin, hMax;
    double hTempSum[24];
    int hTempN[24];
    double hHumSum[24];
    int hHumN[24];
};

static inline void sensorDayAggrInit(SensorDayAggr* a) {
    a->n = 0; a->tSum = 0.0; a->tMin = 0.0f; a->tMax = 0.0f;
    a->hn = 0; a->hSum = 0.0; a->hMin = 0.0f; a->hMax = 0.0f;
    for (int h = 0; h < 24; h++) {
        a->hTempSum[h] = 0.0; a->hTempN[h] = 0;
        a->hHumSum[h] = 0.0; a->hHumN[h] = 0;
    }
}

static inline void sensorDayAggrAdd(SensorDayAggr* a, int hour,
                                    bool hasT, float t, bool hasH, float h) {
    if (hour < 0 || hour > 23) return;
    if (hasT && (t == t)) {  // NaN guard without isnan (C++11-safe)
        if (a->n == 0) { a->tMin = t; a->tMax = t; }
        else { if (t < a->tMin) a->tMin = t; if (t > a->tMax) a->tMax = t; }
        a->tSum += t; a->n++;
        a->hTempSum[hour] += t; a->hTempN[hour]++;
    }
    if (hasH && (h == h)) {
        if (a->hn == 0) { a->hMin = h; a->hMax = h; }
        else { if (h < a->hMin) a->hMin = h; if (h > a->hMax) a->hMax = h; }
        a->hSum += h; a->hn++;
        a->hHumSum[hour] += h; a->hHumN[hour]++;
    }
}

// Parse "YYYY-MM-DD HH:MM:SS" -> HH (chars 11-12). False on short/garbage.
static inline bool sensorDayParseHour(const char* ts, int& hourOut) {
    if (!ts) return false;
    size_t len = 0;
    while (ts[len] != '\0') len++;
    if (len < 13) return false;
    char c0 = ts[11], c1 = ts[12];
    if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9') return false;
    int h = (c0 - '0') * 10 + (c1 - '0');
    if (h < 0 || h > 23) return false;
    hourOut = h;
    return true;
}

static inline std::string sensorDayTrim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
    return s.substr(b, e - b);
}

static inline std::vector<std::string> sensorDaySplitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i <= line.size(); i++) {
        if (i == line.size() || line[i] == ',') {
            out.push_back(sensorDayTrim(cur));
            cur.clear();
        } else {
            cur += line[i];
        }
    }
    return out;
}

static inline float sensorDayToFloat(const std::string& s, bool& ok) {
    ok = false;
    if (s.empty() || s == "null" || s == "NULL" || s == "--") return 0.0f;
    try {
        size_t pos = 0;
        float v = std::stof(s, &pos);
        if (pos != s.size()) return 0.0f;
        if (v != v) return 0.0f;  // NaN
        ok = true;
        return v;
    } catch (...) {
        return 0.0f;
    }
}

// Temperature column preference: temp_avg > temp_aht20 > temp_bmp280 > temperature.
static inline int sensorDayPickTempCol(const std::vector<std::string>& hdr) {
    const char* pref[] = {"temp_avg", "temp_aht20", "temp_bmp280", "temperature"};
    for (int p = 0; p < 4; p++) {
        for (size_t i = 0; i < hdr.size(); i++) {
            if (hdr[i] == pref[p]) return (int)i;
        }
    }
    return -1;
}

static inline int sensorDayPickHumCol(const std::vector<std::string>& hdr) {
    for (size_t i = 0; i < hdr.size(); i++) {
        if (hdr[i] == "humidity") return (int)i;
    }
    return -1;
}

static inline std::string sensorDayBuildBody(const std::string& dateLabel,
                                             const SensorDayAggr& a) {
    if (a.n <= 0) return "";
    float tAvg = (float)(a.tSum / (double)a.n);
    bool hasHum = (a.hn > 0);
    float hAvg = hasHum ? (float)(a.hSum / (double)a.hn) : 0.0f;
    float lo = a.tMin, hi = a.tMax;

    char h0[160];
    std::snprintf(h0, sizeof(h0), "Registos: (SensorDay %s)\n\n",
                  dateLabel.c_str());
    char s1[160];
    std::snprintf(s1, sizeof(s1), "Temp min %.1fC  med %.1fC  max %.1fC (N=%d)\n",
                  (double)a.tMin, (double)tAvg, (double)a.tMax, a.n);
    std::string body = std::string(h0) + std::string(s1);
    if (hasHum) {
        char s2[160];
        std::snprintf(s2, sizeof(s2), "Hum min %.0f%%  med %.0f%%  max %.0f%% (N=%d)\n",
                      (double)a.hMin, (double)hAvg, (double)a.hMax, a.hn);
        body += std::string(s2);
    }
    body += "\nTemp por hora:\n\n";
    // One row every 2 hours (12 rows): aligned numeric columns + one sparkline
    // glyph per metric with data (owner pick: lighter than the old 10-cell
    // bars, day-curve shape at a glance). Temp field is %5.1fC (6 chars),
    // humidity %3.0f%% (4 chars), so glyph columns always line up, e.g.:
    //   00H  21.3C ▅   45% ▄
    //   02H   9.5C ▁  100% █
    for (int h = 0; h < 24; h += 2) {
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%02dH", h);
        std::string row = std::string(lbl) + " ";
        bool hasT = (a.hTempN[h] > 0);
        bool hasH = (hasHum && a.hHumN[h] > 0);
        if (hasT) {
            float tm = (float)(a.hTempSum[h] / (double)a.hTempN[h]);
            char tf[16];
            std::snprintf(tf, sizeof(tf), "%5.1fC", (double)tm);
            row += std::string(tf) + " " + sensorDayLevelGlyph(tm, lo, hi);
        } else {
            row += "  --  ";
        }
        if (hasHum) {
            row += "  ";
            if (hasH) {
                float hm = (float)(a.hHumSum[h] / (double)a.hHumN[h]);
                char hf[16];
                std::snprintf(hf, sizeof(hf), "%3.0f%%", (double)hm);
                row += std::string(hf) + " " + sensorDayLevelGlyph(hm, a.hMin, a.hMax);
            } else {
                row += " --  ";
            }
        }
        while (!row.empty() && row[row.size() - 1] == ' ') row.erase(row.size() - 1);
        body += row + "\n";
    }
    body += "\n";
    return body;
}

// Streaming parser: same aggregation as sensorDayBuildFromCsv but fed one
// line at a time — peak RAM is one line + the aggr struct (no full-file
// copies). Live .24 2026-10-14: the bulk parse tripled the CSV on the heap
// (~180KB transient on a ~175KB board) and SENSORDAY panicked with OOM.
struct SensorDayParser {
    SensorDayAggr aggr;
    bool headerOk;
    int tCol, hCol;
};

static inline void sensorDayParserInit(SensorDayParser* p) {
    sensorDayAggrInit(&p->aggr);
    p->headerOk = false;
    p->tCol = -1;
    p->hCol = -1;
}

static inline void sensorDayParserFeed(SensorDayParser* p, const std::string& line) {
    std::string t = sensorDayTrim(line);
    if (t.empty()) return;
    std::vector<std::string> cols = sensorDaySplitCsv(t);
    if (cols.empty()) return;
    if (!p->headerOk) {
        p->tCol = sensorDayPickTempCol(cols);
        p->hCol = sensorDayPickHumCol(cols);
        p->headerOk = (p->tCol >= 0);
        return;
    }
    int hour = 0;
    if (!sensorDayParseHour(cols[0].c_str(), hour)) return;
    bool okT = false, okH = false;
    float tv = 0.0f, hv = 0.0f;
    if (p->tCol >= 0 && p->tCol < (int)cols.size()) tv = sensorDayToFloat(cols[p->tCol], okT);
    if (p->hCol >= 0 && p->hCol < (int)cols.size()) hv = sensorDayToFloat(cols[p->hCol], okH);
    if (!okT && !okH) return;
    sensorDayAggrAdd(&p->aggr, hour, okT, tv, okH, hv);
}

static inline bool sensorDayParserHasData(const SensorDayParser* p) {
    return p->headerOk && p->aggr.n > 0;
}

static inline std::string sensorDayParserBody(const SensorDayParser* p,
                                              const std::string& dateLabel) {
    if (!sensorDayParserHasData(p)) return "";
    return sensorDayBuildBody(dateLabel, p->aggr);
}

// Bulk CSV variant (RAM path only): splits the whole text, then feeds the
// streaming parser above. Prefer the streaming parser for files.
static inline std::string sensorDayBuildFromCsv(const std::string& csv,
                                                 const std::string& dateLabel) {
    SensorDayParser p;
    sensorDayParserInit(&p);
    std::string cur;
    cur.reserve(256);
    for (size_t i = 0; i <= csv.size(); i++) {
        if (i == csv.size() || csv[i] == '\n') {
            if (!cur.empty() && cur[cur.size() - 1] == '\r') cur.erase(cur.size() - 1);
            sensorDayParserFeed(&p, cur);
            cur.clear();
        } else {
            cur += csv[i];
        }
    }
    return sensorDayParserBody(&p, dateLabel);
}
