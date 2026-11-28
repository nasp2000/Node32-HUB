// tdk_lambda.cpp — TDK-Lambda / Genesys SCPI console + CSV module.
//
// See tdk_lambda.h for the design rules. Everything here is compiled out when
// ENABLE_TDK_LAMBDA is 0 (zero RAM, zero code).
#include "tdk_lambda.h"
#include "tdk_lambda_scpi.h"

#include "feature_flags.h"
#if ENABLE_TDK_LAMBDA == 1

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#include "ConfigManager.h"
#include "log.h"
#include "runtime_rs232_queue.h"
#include "sdcard.h"
#include "littlefs_manager.h"
#include "sd_layout.h"

extern ConfigManager configManager;

namespace {

// ── Catalogue (manual cap. 7; group/usage/desc/reply/notes) ─────────────────
static const TdkCommandInfo kCatalog[] = {
    // Identify
    {"IDN?",  "Identify", "IDN?",  "Model identification.",              "LAMBDA,GEN600-1.3", "Put this first in a handshake."},
    {"REV?",  "Identify", "REV?",  "Firmware version.",                  "1.xx",              "Varies per unit."},
    {"SN?",   "Identify", "SN?",   "Serial number (up to 12 chars).",    "ABCD1234",          "Matches the rear label."},
    {"DATE?", "Identify", "DATE?", "Date of last test (yyyy/mm/dd).",    "2026/01/01",        ""},
    {"MDAV?", "Identify", "MDAV?", "Multi-drop option installed?",       "0 or 1",            "1 = MD option fitted."},
    {"RMT?",  "Identify", "RMT?",  "Local/remote state.",                "LOC / REM / LLO",   ""},
    {"MS?",   "Identify", "MS?",   "Master/slave (advanced parallel).",  "0..4",              "0 = slave."},
    {"FILTER?","Identify","FILTER?","ADC low-pass filter frequency.",     "18 / 23 / 46",      "Hz."},

    // Monitor (read only)
    {"MV?",   "Monitor",  "MV?",   "Measured output voltage.",           "599.99",            "5 significant digits."},
    {"MC?",   "Monitor",  "MC?",   "Measured output current.",           "000.50",            "Advanced parallel: system total."},
    {"PV?",   "Monitor",  "PV?",   "Programmed voltage.",                "600.00",            "In Local returns the PREVIEW setting."},
    {"PC?",   "Monitor",  "PC?",   "Programmed current.",                "001.30",            ""},
    {"DVC?",  "Monitor",  "DVC?",  "V/I measured+programmed, OVP, UVL.", "599.99,600.00,000.50,001.30,660.0,0.000", "Six comma separated fields."},
    {"MODE?", "Monitor",  "MODE?", "Operating mode.",                    "CV / CC / OFF",     "CC = current limiting."},
    {"OUT?",  "Monitor",  "OUT?",  "Output on/off.",                     "ON / OFF",          ""},
    {"FLD?",  "Monitor",  "FLD?",  "Foldback armed?",                    "ON / OFF",          ""},
    {"AST?",  "Monitor",  "AST?",  "Auto-restart mode.",                 "ON / OFF",          ""},
    {"OVP?",  "Monitor",  "OVP?",  "Over-voltage setting.",              "660.0",             ""},
    {"UVL?",  "Monitor",  "UVL?",  "Under-voltage limit.",               "0.000",             ""},
    {"FBD?",  "Monitor",  "FBD?",  "Extra foldback delay.",              "0",                 "x0.1 s."},
    {"STT?",  "Monitor",  "STT?",  "Full status in one line.",           "MV(..),PV(..),MC(..),PC(..),SR(xx),FR(xx)", "Hex registers inside."},
    {"STAT?", "Monitor",  "STAT?", "Status condition register.",         "30",                "2 hex digits."},
    {"FLT?",  "Monitor",  "FLT?",  "Fault condition register.",          "00",                "2 hex digits."},
    {"SEVE?", "Monitor",  "SEVE?", "Status event register (clears).",    "00",                "Reading clears it."},
    {"FEVE?", "Monitor",  "FEVE?", "Fault event register (clears).",     "00",                "Reading clears it."},
    {"SENA?", "Monitor",  "SENA?", "Status enable mask.",                "00",                ""},
    {"FENA?", "Monitor",  "FENA?", "Fault enable mask.",                 "00",                ""},

    // Control
    {"PV",    "Control",  "PV n",   "Set output voltage (V).",           "OK",                "Switches the unit to Remote."},
    {"PC",    "Control",  "PC n",   "Set current limit (A).",            "OK",                ""},
    {"OUT",   "Control",  "OUT 1|0","Output on/off.",                    "OK",                "E07 if a latching fault is active."},
    {"RMT",   "Control",  "RMT 0|1|2","Local / Remote / Local Lockout.","OK",                "RMT? to read back."},
    {"FILTER","Control",  "FILTER nn","ADC filter: 18 / 23 / 46 Hz.",    "OK",                ""},

    // Protect
    {"OVP",   "Protect",  "OVP n",  "Set over-voltage level.",           "OK",                "Must be >=105% of PV (else E04)."},
    {"OVM",   "Protect",  "OVM",    "OVP to maximum.",                   "OK",                ""},
    {"UVL",   "Protect",  "UVL n",  "Set under-voltage limit.",          "OK",                "Above PV -> E06."},
    {"FLD",   "Protect",  "FLD 1|0","Arm/cancel foldback.",              "OK",                ""},
    {"FBD",   "Protect",  "FBD nn", "Add nn x0.1 s foldback delay.",     "OK",                "0..255."},
    {"FBDRST","Protect",  "FBDRST", "Reset the added foldback delay.",   "OK",                ""},
    {"AST",   "Protect",  "AST 1|0","Auto-restart on/off.",              "OK",                ""},

    // Status / housekeeping
    {"SENA",  "Status",   "SENA xx", "Set status enable mask (hex).",    "OK",                ""},
    {"FENA",  "Status",   "FENA xx", "Set fault enable mask (hex).",     "OK",                ""},
    {"CLS",   "Status",   "CLS",     "Clear status/fault events.",       "OK",                ""},
    {"RST",   "Status",   "RST",     "Safe reset (V=0,I=0,OUT off).",    "OK",                "Remote lockout released."},

    // Save / recall
    {"SAV",   "Store",    "SAV",     "Save present settings.",           "OK",                ""},
    {"RCL",   "Store",    "RCL",     "Recall last settings.",            "OK",                "From last SAV or power-down."},

    // Global (multi-unit, no response)
    {"GRST",  "Global",   "GRST",    "Global safe reset.",               "(none)",            "All units on the bus; wait >=200 ms."},
    {"GPV",   "Global",   "GPV n",   "Global output voltage.",           "(none)",            ""},
    {"GPC",   "Global",   "GPC n",   "Global current limit.",            "(none)",            ""},
    {"GOUT",  "Global",   "GOUT",    "Global output on/off.",            "(none)",            ""},
    {"GSAV",  "Global",   "GSAV",    "Global save settings.",            "(none)",            "Address/baud not saved."},
    {"GRCL",  "Global",   "GRCL",    "Global recall settings.",          "(none)",            ""},
};

static const uint8_t TDK_LOG_MAX = 32;
static const uint8_t TDK_LINE_MAX = 95;

typedef TdkPending PendingQuery;
static const PendingQuery P_NONE = TDK_P_NONE, P_DVC = TDK_P_DVC, P_MV = TDK_P_MV,
    P_MC = TDK_P_MC, P_MODE = TDK_P_MODE, P_OUT = TDK_P_OUT, P_STT = TDK_P_STT, P_OTHER = TDK_P_OTHER;

struct LogSlot {
    uint32_t ts;
    char dir[4];
    char text[160];
};

LogSlot       s_log[TDK_LOG_MAX];
uint8_t       s_logStart = 0;
uint8_t       s_logCount = 0;

char          s_line[TDK_LINE_MAX + 1];
uint8_t       s_lineLen = 0;
bool          s_inEscape = false;

TdkValues     s_values = {false, 0, 0, 0, 0, 0, 0, "--", false, 0};
uint32_t      s_lastRxMs = 0;
char          s_lastCmd[32] = {0};
char          s_lastLine[96] = {0};
char          s_idn[40] = {0};
char          s_rev[16] = {0};
char          s_sn[24] = {0};
char          s_lastError[8] = {0};
uint32_t      s_lastErrorMs = 0;

// Always-on sample ring (RAM only, newest last): TDK_SAMPLES_MAX = 4 h at the
// 1-minute poll floor. No heap and no file write for the chart.
static const uint16_t TDK_RING_MAX = TDK_SAMPLES_MAX;
TdkSample     s_ring[TDK_RING_MAX];
uint16_t      s_ringStart = 0;
uint16_t      s_ringCount = 0;

void buildInjectLine();   // defined below (after the globals they use)
void checkLimits();

void pushSample() {
    if (!s_values.valid) return;
    TdkSample smp;
    smp.ts = millis();
    smp.v = s_values.vMeas;
    smp.i = s_values.iMeas;
    buildInjectLine();
    checkLimits();
    if (s_ringCount < TDK_RING_MAX) {
        s_ring[(s_ringStart + s_ringCount) % TDK_RING_MAX] = smp;
        s_ringCount++;
    } else {
        s_ring[s_ringStart] = smp;
        s_ringStart = (uint16_t)((s_ringStart + 1) % TDK_RING_MAX);
    }
}
PendingQuery  s_pending = P_NONE;
char          s_pendingCmd[16] = {0};

bool          s_polling = true;
uint32_t      s_pollMs = 60000UL;
uint32_t      s_lastPollMs = 0;
uint8_t       s_pollStep = 0;
// CSV batch buffer: same size as the pack-wide RS232 buffer
// (BUFFER_DATA_SIZE = 131072 in diagnostics_impl.h) so every pack behaves alike.
static const size_t TDK_CSV_BUF_BYTES = 131072UL;   // 128 KB capacity (PSRAM)
static const size_t TDK_CSV_FLUSH_BYTES = 98304UL;  // 96 KB high-watermark: write now
bool          s_csvBufReserved = false;
bool          s_csvEnabled = true;
uint16_t      s_csvEvery = 1;        // one CSV row per N polls (1 = every poll)
TdkAgg        s_agg;                 // window being accumulated for the next row
TdkSession    s_session;             // since boot: Wh, CC, limit, off, transitions
uint32_t      s_sessionLastMs = 0;   // millis() of the previous session sample
// ── device profile (which queries, how to read the reply) ──────────────────
bool          s_profileDvc = true;          // "dvc" (one query, N fields) vs "split"
char          s_cmdV[16]   = "DVC?";        // dvc mode: the only query; split: voltage
char          s_cmdI[16]   = "MC?";         // split mode only
char          s_cmdState[16] = "MODE?";     // CV/CC/OFF
uint8_t       s_map[6]     = {0, 1, 2, 3, 4, 5};
float         s_scaleV = 1.0f, s_scaleI = 1.0f, s_offV = 0.0f, s_offI = 0.0f;
bool          s_autoMap = true;          // adopt the layout found in a real reply
bool          s_listen = false;          // passive listener: never transmit
uint8_t       s_addr = 0;                // multi-drop address (0 = none)
bool          s_inject = false;          // push a sensor-style line each sample
char          s_injectLine[220] = {0};   // pending line for loop_impl to inject
bool          s_injectPending = false;

// Limit alerts (warn on the monitor + badge on the page).
bool          s_limitsEnabled = false;
float         s_limitVMin = 0.0f, s_limitVMax = 0.0f, s_limitIMax = 0.0f;
bool          s_alertActive = false;
char          s_alertText[64] = {0};
uint32_t      s_alertSinceMs = 0;
float         s_rateV = 600.0f, s_rateI = 1.3f;
char          s_autoNote[64] = {0};      // last automatic adjustment (for the page)
uint16_t      s_unattributed = 0;        // listen mode: single values ignored
uint32_t      s_unattributedLogMs = 0;

// ── first-contact line test ────────────────────────────────────────────────
// One query at a time, only queries, never while listen mode is on. It fills the
// same values the page shows, so a pass means the real poll will work too.
static const char* const kSelfTestCmds[] = {"IDN?", "REV?", "SN?", "DVC?", "MODE?"};
static const uint8_t kSelfTestCount = sizeof(kSelfTestCmds) / sizeof(kSelfTestCmds[0]);
bool          s_selfTestActive = false;
uint8_t       s_selfTestStep = 0;        // steps already sent
bool          s_selfTestAnswered[kSelfTestCount] = {false};
uint32_t      s_selfTestSentMs = 0;
uint32_t      s_selfTestRxMark = 0;
char          s_selfTestVerdict[12] = {0};
char          s_selfTestDetail[96] = {0};

void pushLog(const char* dir, const char* text);   // defined below
void tdkLogSys(const char* text) {
    if (!text || !text[0]) return;
    pushLog("SYS", text);
    sendVirtualMessage(String(F("[TDK] ")) + text, INFO);
}

bool          s_csvSensorFmt = true;   // default: sensor-style key/value (see PAGES/tdk_lambda.md)
uint32_t      s_csvRows = 0;

String        s_csvPending;          // LittleFS rows buffered until the window
uint32_t      s_csvLastFlushMs = 0;

// ── helpers ────────────────────────────────────────────────────────────────
void buildInjectLine() {
    if (!s_inject || !s_values.valid) return;
    time_t now = time(nullptr);
    struct tm ti = {};
    localtime_r(&now, &ti);
    char ts[24];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    snprintf(s_injectLine, sizeof(s_injectLine),
             "TDK %s, vout, %.2f, vset, %.2f, iout, %.3f, iset, %.3f, power, %.2f, mode, %s",
             ts, s_values.vMeas, s_values.vSet, s_values.iMeas, s_values.iSet,
             s_values.vMeas * s_values.iMeas, s_values.mode);
    s_injectPending = true;
}

void checkLimits() {
    if (!s_limitsEnabled || !s_values.valid) { s_alertActive = false; s_alertText[0] = '\0'; return; }
    const char* why = nullptr;
    if (s_limitVMax > 0.0f && s_values.vMeas > s_limitVMax)      why = "voltage above limit";
    else if (s_limitVMin > 0.0f && s_values.vMeas < s_limitVMin) why = "voltage below limit";
    else if (s_limitIMax > 0.0f && s_values.iMeas > s_limitIMax) why = "current above limit";

    if (why) {
        if (!s_alertActive) {
            s_alertSinceMs = millis();
            snprintf(s_alertText, sizeof(s_alertText), "%s (%.2f V / %.3f A)", why, s_values.vMeas, s_values.iMeas);
            tdkLogSys((String(F("ALERT: ")) + String(s_alertText)).c_str());
        }
        s_alertActive = true;
    } else if (s_alertActive) {
        s_alertActive = false;
        s_alertText[0] = '\0';
        tdkLogSys("alert cleared");
    }
}

void pushLog(const char* dir, const char* text) {
    if (!text || !text[0]) return;
    uint8_t slot;
    if (s_logCount < TDK_LOG_MAX) {
        slot = (uint8_t)((s_logStart + s_logCount) % TDK_LOG_MAX);
        s_logCount++;
    } else {
        slot = s_logStart;
        s_logStart = (uint8_t)((s_logStart + 1) % TDK_LOG_MAX);
    }
    s_log[slot].ts = millis();
    strncpy(s_log[slot].dir, dir, sizeof(s_log[slot].dir) - 1);
    s_log[slot].dir[sizeof(s_log[slot].dir) - 1] = '\0';
    strncpy(s_log[slot].text, text, sizeof(s_log[slot].text) - 1);
    s_log[slot].text[sizeof(s_log[slot].text) - 1] = '\0';
}

void parseMap(const char* in, uint8_t out[6]) {
    uint8_t i = 0;
    const char* p = in;
    while (p && *p && i < 6) {
        while (*p == ' ') ++p;
        char* end = nullptr;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[i++] = (uint8_t)((v < 0) ? 0 : (v > 15 ? 15 : v));
        p = end;
        while (*p == ' ') ++p;
        if (*p == ',') ++p;
    }
    while (i < 6) out[i++] = (uint8_t)(i);   // missing entries: identity
}

const char* csvDateSuffix() {
    static char buf[12];
    time_t now = time(nullptr);
    struct tm ti = {};
    localtime_r(&now, &ti);
    snprintf(buf, sizeof(buf), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
    return buf;
}

String csvPathNow() {
    String sys = configManager.getSdcardName();
    if (sys.isEmpty()) sys = configManager.getSystemName();
    if (sys.isEmpty()) sys = "SD_LOG";
    return sd_layout::dayFile(sys, "TDK", String(csvDateSuffix()), "csv");
}

unsigned long csvWindowMs() {
    String v = configManager.getSdcardFlushInterval();
    if (v == "10m") return 600000UL;
    if (v == "60m") return 3600000UL;
    return 1800000UL;
}

void csvHeader(File& f) {
    // Table format only: the sensor-style line carries its own keys, so a
    // header row would be redundant (the sensors write no header either).
    if (!s_csvSensorFmt && f.size() == 0) {
        // v2: the row is a window (csv_every polls) — averages plus the min/max
        // inside the window and its energy, so a spike cannot hide between rows.
        f.println(F("timestamp,v_meas,v_set,i_meas,i_set,p_w,ovp,uvl,mode,out,"
                    "samples,v_min,v_max,i_min,i_max,wh"));
    }
}

bool appendBlockSd(const String& path, const String& rows) {
#if ENABLE_SDCARD==1
    if (!sdMutexLock(pdMS_TO_TICKS(1500))) return false;
    File f = SD_MMC.open(path.c_str(), FILE_APPEND);
    if (!f) { sdMutexUnlock(); return false; }
    csvHeader(f);
    f.print(rows);
    if (!rows.endsWith("\n")) f.println();
    f.close();
    sdMutexUnlock();
    return true;
#else
    (void)path; (void)rows;
    return false;   // no SD slot on this build: LittleFS mirror owns persistence
#endif
}

bool appendRowsFlash(const String& path, const String& rows) {
#if ENABLE_FILESYSTEM==1
    if (!littlefsBegin() || !littlefsMounted()) return false;
    if (littlefsFreeBytes() < (1024ULL * 1024ULL)) return false;   // /config reserve
    if (!lfsMutexLock(pdMS_TO_TICKS(2000))) return false;
    File f = LittleFS.open(path.c_str(), FILE_APPEND);
    if (!f) { lfsMutexUnlock(); return false; }
    csvHeader(f);
    f.print(rows);
    if (!rows.endsWith("\n")) f.println();
    f.close();
    lfsMutexUnlock();
    return true;
#else
    (void)path; (void)rows;
    return false;
#endif
}

bool tdkCsvFlushPending();   // defined below (used on overflow)

// Every poll feeds this: the session counters always run (they are what the
// page shows), and the CSV row is emitted once per `s_csvEvery` polls.
void csvAppendSample() {
    if (!s_values.valid) return;
    const uint32_t nowMs = millis();

    // Session counters: the elapsed time is attributed to this sample's state.
    // The gap is capped so a silent line (listen mode, PLC idle, long poll) does
    // not credit an hour of "in CC" to a single sample.
    uint32_t dt = (s_sessionLastMs && nowMs > s_sessionLastMs) ? (nowMs - s_sessionLastMs) : 0;
    const uint32_t dtCap = (s_pollMs > 120000UL) ? (s_pollMs * 3UL) : 600000UL;
    if (dt > dtCap) dt = dtCap;
    const bool atLimit = (s_values.iSet > 0.0f && s_values.iMeas >= 0.98f * s_values.iSet);
    s_session.add(s_values.vMeas, s_values.iMeas, s_values.mode, s_values.outOn, atLimit, dt);
    s_sessionLastMs = nowMs;

    if (!s_csvEnabled) { s_agg.clear(nowMs); return; }
    if (s_csvEvery < 1) s_csvEvery = 1;
    s_agg.add(s_values.vMeas, s_values.iMeas, nowMs);
    if (s_agg.n < s_csvEvery) return;          // window still open

    time_t now = time(nullptr);
    struct tm ti = {};
    localtime_r(&now, &ti);
    char ts[24];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    s_agg.close(nowMs);                        // integrate the tail of the window
    char row[320];
    const size_t made = tdkFormatCsvRow(row, sizeof(row), ts, s_csvSensorFmt,
                                        s_values.vMeas, s_values.vSet, s_values.iMeas, s_values.iSet,
                                        s_values.ovp, s_values.uvl, s_values.mode, s_values.outOn,
                                        &s_agg);
    s_agg.clear(nowMs);
    if (made == 0) {
        return;
    }

    // Both backends write in batches at the configured window (30 min default):
    // less flash wear and one predictable cadence (ARCHITECTURE §6.1 rule 2).
    if (!s_csvBufReserved) {
        s_csvBufReserved = s_csvPending.reserve(TDK_CSV_BUF_BYTES);
        if (!s_csvBufReserved) tdkLogSys("CSV buffer: 128 KB reserve failed (will grow on demand)");
    }
    s_csvPending += row;
    s_csvPending += "\n";
    // A fast poll can outgrow one window: write at the 96 KB watermark (capacity
    // is 128 KB, so there is always headroom for the row in flight) instead of
    // dropping rows — the window is a wear/cadence rule, not a hard limit.
    if (s_csvPending.length() >= TDK_CSV_FLUSH_BYTES) (void)tdkCsvFlushPending();
}

// Adopt the layout/scale found in a real multi-field reply when the profile
// allows it. Used by the bulk query and by any unrequested multi-field line
// (listener mode), so both read the same way.
void applyAutoMap(const float* f, uint8_t n) {
    if (!f || n < 4 || !s_autoMap) return;
    uint8_t sug[6];
    float sv = 1.0f, si = 1.0f;
    char why[64] = {0};
    if (!tdkSuggestMap(f, n, s_rateV, s_rateI, sug, &sv, &si, why, sizeof(why))) return;
    bool differs = (fabsf(sv - s_scaleV) > 1e-6f) || (fabsf(si - s_scaleI) > 1e-6f);
    for (uint8_t k = 0; k < 6; ++k) if (sug[k] != s_map[k]) differs = true;
    if (!differs) return;
    // Keep the layout in use when it already explains the reply: at CV the two
    // voltage orders are separated by ripple alone, and flipping them on every
    // poll only makes vMeas/vSet jitter and the monitor noisy.
    if (tdkMapPlausible(f, n, s_map, s_scaleV, s_scaleI, s_rateV, s_rateI)) return;
    for (uint8_t k = 0; k < 6; ++k) s_map[k] = sug[k];
    s_scaleV = sv;
    s_scaleI = si;
    snprintf(s_autoNote, sizeof(s_autoNote), "%s", why);
    tdkLogSys((String(F("perfil ajustado automaticamente: ")) + String(why)).c_str());
}

// Did the step that was just sent get its own reply? Identity steps compare the
// string (a cached value from an earlier read is still a valid answer for the
// link); DVC? needs four fields; MODE? needs a mode other than the default.
bool selfTestStepAnswered(uint8_t step) {
    if (s_lastRxMs == s_selfTestRxMark) return false;   // nothing arrived
    switch (step) {
        case 0: return s_idn[0] != '\0';
        case 1: return s_rev[0] != '\0';
        case 2: return s_sn[0] != '\0';
        case 3: { float f[16]; return tdkParseFloats(s_lastLine, f, 16) >= 4; }
        case 4: return strcmp(s_values.mode, "--") != 0;
        default: return false;
    }
}

void selfTestFinish() {
    uint8_t answered = 0;
    for (uint8_t i = 0; i < kSelfTestCount; ++i) if (s_selfTestAnswered[i]) answered++;
    const char* verdict = tdkSelfTestVerdict(answered, kSelfTestCount, s_selfTestAnswered[3]);
    snprintf(s_selfTestVerdict, sizeof(s_selfTestVerdict), "%s", verdict);
    char detail[sizeof(s_selfTestDetail)] = {0};
    for (uint8_t i = 0; i < kSelfTestCount; ++i) {
        if (i) strncat(detail, " | ", sizeof(detail) - strlen(detail) - 1);
        strncat(detail, kSelfTestCmds[i], sizeof(detail) - strlen(detail) - 1);
        strncat(detail, s_selfTestAnswered[i] ? " ok" : " no reply",
                sizeof(detail) - strlen(detail) - 1);
    }
    snprintf(s_selfTestDetail, sizeof(s_selfTestDetail), "%s", detail);
    s_selfTestActive = false;
    char msg[128];
    snprintf(msg, sizeof(msg), "line test: %s (%u/%u) %s",
             verdict, (unsigned)answered, (unsigned)kSelfTestCount, detail);
    tdkLogSys(msg);
}

// Called with the slot free: records the previous step and sends the next one
// (or finishes when there is no next).
void selfTestAdvance(bool previousAnswered) {
    if (s_selfTestStep > 0) {
        const uint8_t prev = (uint8_t)(s_selfTestStep - 1);
        s_selfTestAnswered[prev] = previousAnswered && selfTestStepAnswered(prev);
    }
    if (s_selfTestStep >= kSelfTestCount) { selfTestFinish(); return; }
    const uint8_t step = s_selfTestStep;
    s_selfTestRxMark = s_lastRxMs;
    s_selfTestSentMs = millis();
    s_selfTestStep++;
    if (!tdkLambdaSend(String(kSelfTestCmds[step]))) {
        s_selfTestAnswered[step] = false;   // could not queue: next tick moves on
    }
}

// ── RX parsing ─────────────────────────────────────────────────────────────
void handleLine(char* line) {
    while (*line == ' ' || *line == '\t') ++line;
    if (!*line) return;
    strncpy(s_lastLine, line, sizeof(s_lastLine) - 1);
    s_lastLine[sizeof(s_lastLine) - 1] = '\0';
    pushLog("RX", line);

    // Error replies (C01..C05, E01..E07) are surfaced as a badge on the page.
    if ((line[0] == 'C' || line[0] == 'E') && isdigit((unsigned char)line[1]) && isdigit((unsigned char)line[2])) {
        strncpy(s_lastError, line, sizeof(s_lastError) - 1);
        s_lastError[sizeof(s_lastError) - 1] = '\0';
        s_lastErrorMs = millis();
    }

    switch (s_pending) {
        case P_DVC: {
            float f[16];
            const uint8_t n = tdkParseFloats(line, f, 16);
            applyAutoMap(f, n);
            if (n > 0) {
                tdkApplyMap(f, n, s_map, s_scaleV, s_scaleI, s_offV, s_offI,
                            &s_values.vMeas, &s_values.vSet, &s_values.iMeas,
                            &s_values.iSet, &s_values.ovp, &s_values.uvl);
                s_values.valid = true;
                s_values.lastUpdateMs = millis();
                pushSample();
                csvAppendSample();
            }
            break;
        }
        case P_MV: {
            float v = 0;
            if (tdkParseFloat(line, &v)) {
                s_values.vMeas = v * s_scaleV + s_offV;
                s_values.valid = true;
                s_values.lastUpdateMs = millis();
                pushSample();
                csvAppendSample();
            }
            break;
        }
        case P_MC: {
            float i = 0;
            if (tdkParseFloat(line, &i)) {
                s_values.iMeas = i * s_scaleI + s_offI;
                s_values.valid = true;
                s_values.lastUpdateMs = millis();
                pushSample();
            }
            break;
        }
        case P_MODE:
            strncpy(s_values.mode, line, sizeof(s_values.mode) - 1);
            s_values.mode[sizeof(s_values.mode) - 1] = '\0';
            // CV/CC = output on, OFF = disabled (the cycle does not ask OUT?).
            s_values.outOn = tdkOutFromMode(s_values.mode, s_values.outOn);
            break;
        case P_OUT:
            s_values.outOn = (toupper((unsigned char)line[0]) == 'O' &&
                              toupper((unsigned char)line[1]) == 'N');
            break;
        default: {
            // Identity replies (Identify button / manual queries).
            if (!strcmp(s_pendingCmd, "IDN?")) {
                strncpy(s_idn, line, sizeof(s_idn) - 1); s_idn[sizeof(s_idn) - 1] = '\0';
            } else if (!strcmp(s_pendingCmd, "REV?")) {
                strncpy(s_rev, line, sizeof(s_rev) - 1); s_rev[sizeof(s_rev) - 1] = '\0';
            } else if (!strcmp(s_pendingCmd, "SN?")) {
                strncpy(s_sn, line, sizeof(s_sn) - 1); s_sn[sizeof(s_sn) - 1] = '\0';
            }
            // Listener mode: a multi-field line arrives without us asking it.
            float f[16];
            const uint8_t n = tdkParseFloats(line, f, 16);
            if (s_listen && n == 1 && tdkIsSingleNumber(line)) {
                // A lone number is a reply to MV? or MC? on the master's TX line,
                // which we cannot see: guessing V or I would invent data.
                if (s_unattributed < 0xFFFF) s_unattributed++;
                if (s_unattributedLogMs == 0 || (millis() - s_unattributedLogMs) > 60000UL) {
                    s_unattributedLogMs = millis();
                    tdkLogSys("listen: single value ignored (cannot tell V from I) - use DVC? on the master");
                }
            }
            if (n >= 4) {
                applyAutoMap(f, n);
                tdkApplyMap(f, n, s_map, s_scaleV, s_scaleI, s_offV, s_offI,
                            &s_values.vMeas, &s_values.vSet, &s_values.iMeas,
                            &s_values.iSet, &s_values.ovp, &s_values.uvl);
                s_values.valid = true;
                s_values.lastUpdateMs = millis();
                csvAppendSample();
            }
            break;
        }
    }
    s_pending = P_NONE;
}

bool tdkCsvFlushPending() {
    if (s_csvPending.isEmpty()) return true;
    const String path = csvPathNow();
    if (sdcardShouldUseSD()) {
        if (appendBlockSd(path, s_csvPending)) {
            for (size_t i = 0; i < s_csvPending.length(); ++i) if (s_csvPending[i] == '\n') s_csvRows++;
            s_csvPending = "";
            return true;
        }
        return false;
    }
    if (appendRowsFlash(path, s_csvPending)) {
        for (size_t i = 0; i < s_csvPending.length(); ++i) if (s_csvPending[i] == '\n') s_csvRows++;
        s_csvPending = "";
        return true;
    }
    return false;
}

} // namespace

// ── public API ─────────────────────────────────────────────────────────────
void tdkLambdaBegin() {
    s_lineLen = 0;
    s_line[0] = '\0';
    s_pending = P_NONE;
    // Persisted settings (safe defaults when the section is absent).
    s_polling = configManager.getTdkLambdaPoll();
    const int sec = configManager.getTdkLambdaPollSec();
    s_pollMs = (uint32_t)sec * 1000UL;
    s_csvEnabled = configManager.getTdkLambdaCsv();
    s_autoMap = configManager.getTdkLambdaAutoMap();
    s_listen = configManager.getTdkLambdaListen();
    s_addr = (uint8_t)configManager.getTdkLambdaAddr();
    s_inject = configManager.getTdkLambdaInject();
    s_limitsEnabled = configManager.getTdkLambdaLimitsEnabled();
    s_limitVMin = configManager.getTdkLambdaLimitVMin();
    s_limitVMax = configManager.getTdkLambdaLimitVMax();
    s_limitIMax = configManager.getTdkLambdaLimitIMax();
    s_rateV = configManager.getTdkLambdaRateV();
    s_rateI = configManager.getTdkLambdaRateI();
    s_csvSensorFmt = configManager.getTdkLambdaCsvFormat();
    s_csvEvery = (uint16_t)configManager.getTdkLambdaCsvEvery();
    s_agg.clear(millis());
    s_session.clear();
    s_sessionLastMs = millis();
    tdkLambdaApplyProfile(configManager.getTdkLambdaMode().c_str(),
                          configManager.getTdkLambdaCmdV().c_str(),
                          configManager.getTdkLambdaCmdI().c_str(),
                          configManager.getTdkLambdaCmdState().c_str(),
                          configManager.getTdkLambdaMap().c_str(),
                          configManager.getTdkLambdaScaleV(),
                          configManager.getTdkLambdaScaleI(),
                          configManager.getTdkLambdaOffV(),
                          configManager.getTdkLambdaOffI());
    s_lastPollMs = millis();
    s_csvLastFlushMs = millis();
    pushLog("SYS", "TDK-Lambda module ready (SCPI console, read-only by default)");
}

void tdkLambdaOnRxByte(char c) {
    // Non-destructive copy: loop_impl.h keeps feeding the normal RS232 pipeline.
    if (c == '\n') return;
    if (c == '\r') {
        s_line[s_lineLen] = '\0';
        if (s_lineLen > 0) handleLine(s_line);
        s_lineLen = 0;
        s_line[0] = '\0';
        s_lastRxMs = millis();
        return;
    }
    if (s_lineLen < TDK_LINE_MAX) s_line[s_lineLen++] = c;
}

bool tdkLambdaSend(const String& command) {
    String cmd = command;
    cmd.trim();
    if (cmd.isEmpty()) return false;
    if (s_listen) return false;   // passive listener: never transmit
#if ENABLE_RS232!=1
    // The module has no UART of its own: it talks through the pack's RS232 port.
    // A pack that enables TDK_LAMBDA without RS232 compiles, but cannot send.
    tdkLogSys("pack has no RS232 port - command not sent");
    return false;
#endif
    char upper[32];
    tdkUpperTrim(cmd.c_str(), upper, sizeof(upper));
    strncpy(s_pendingCmd, upper, sizeof(s_pendingCmd) - 1);
    s_pendingCmd[sizeof(s_pendingCmd) - 1] = '\0';
    // DVC? must be recognised as the bulk query even though it also occupies the
    // profile's "voltage" slot in dvc mode (it used to be parsed as one float).
    s_pending = (PendingQuery)tdkPendingForSend(upper, s_cmdV, s_cmdI, s_cmdState);
    strncpy(s_lastCmd, upper, sizeof(s_lastCmd) - 1);
    s_lastCmd[sizeof(s_lastCmd) - 1] = '\0';
    pushLog("TX", cmd.c_str());
    return enqueueRS232Command(cmd, String(), String("TDK"), String("rs232"));
}

void tdkLambdaUpdate() {
    const uint32_t now = millis();

    // First-contact line test: one query at a time, 4 s per step at most.
    if (s_selfTestActive) {
        if (s_pending != P_NONE) {
            if ((now - s_selfTestSentMs) > 4000UL) {
                s_pending = P_NONE;             // free the slot for the next step
                selfTestAdvance(false);
            }
        } else if ((now - s_selfTestSentMs) >= 300UL) {
            selfTestAdvance(true);
        }
    }

    // A request without a reply must not wedge the poller: on a silent line the
    // cycle would stop at the first command and never send another one (measured
    // on the bench: a single TX per boot). Drop it and retry next interval.
    if (!s_listen && s_pending != P_NONE && (now - s_lastPollMs) > tdkReplyTimeoutMs(s_pollMs)) {
        s_pending = P_NONE;
        s_pollStep = 0;
        s_lastPollMs = now;
        tdkLogSys("no reply (timeout) - retrying next interval");
    }

    // Poll cycle: DVC? -> MODE? -> OUT? then wait the configured interval.
    if (!s_listen && !s_selfTestActive && s_polling && s_pending == P_NONE &&
        (now - s_lastPollMs) >= s_pollMs) {
        // Profile-driven cycle:
        //   dvc   -> <query>            then <state>
        //   split -> <V query>, <I query> then <state>
        // Multi-drop: address the unit first (its "OK" is ignored).
        if (s_addr > 0 && s_pollStep == 0) {
            char adr[12];
            snprintf(adr, sizeof(adr), "ADR %u", (unsigned)s_addr);
            tdkLambdaSend(String(adr));
            s_pollStep = 9;   // sentinel: next tick starts the real cycle
            return;
        }
        if (s_pollStep == 9) s_pollStep = 0;
        switch (s_pollStep) {
            case 0: tdkLambdaSend(String(s_cmdV));     s_pollStep = 1; break;
            case 1:
                if (!s_profileDvc) { tdkLambdaSend(String(s_cmdI)); s_pollStep = 2; break; }
                tdkLambdaSend(String(s_cmdState)); s_pollStep = 0; s_lastPollMs = now; break;
            default: tdkLambdaSend(String(s_cmdState)); s_pollStep = 0; s_lastPollMs = now; break;
        }
        return;
    }

    // Buffered CSV flush at the configured window (SD or LittleFS: same rule).
    if (s_csvPending.length() > 0 && (now - s_csvLastFlushMs) >= csvWindowMs()) {
        (void)tdkCsvFlushPending();
        s_csvLastFlushMs = now;
    }
}

void tdkLambdaClearLog() {
    s_logStart = 0;
    s_logCount = 0;
}

TdkStatus tdkLambdaGetStatus() {
    TdkStatus st = {};
    st.enabled = true;
    st.polling = s_polling;
    st.csvEnabled = s_csvEnabled;
    st.csvSensorFormat = s_csvSensorFmt;
    st.pollIntervalMs = s_pollMs;
    st.lastRxAgeMs = (s_lastRxMs == 0) ? 0 : (millis() - s_lastRxMs);
    st.csvRows = s_csvRows;
    st.logCount = s_logCount;
    strncpy(st.lastCommand, s_lastCmd, sizeof(st.lastCommand) - 1);
    st.autoMap = s_autoMap;
    st.listen = s_listen;
    snprintf(st.mapUsed, sizeof(st.mapUsed), "%u,%u,%u,%u,%u,%u",
             s_map[0], s_map[1], s_map[2], s_map[3], s_map[4], s_map[5]);
    strncpy(st.idn, s_idn, sizeof(st.idn) - 1);
    strncpy(st.rev, s_rev, sizeof(st.rev) - 1);
    strncpy(st.sn, s_sn, sizeof(st.sn) - 1);
    strncpy(st.lastError, s_lastError, sizeof(st.lastError) - 1);
    st.lastErrorAgeMs = (s_lastErrorMs == 0) ? 0 : (millis() - s_lastErrorMs);
    st.address = s_addr;
    st.inject = s_inject;
    st.alertActive = s_alertActive;
    strncpy(st.alertText, s_alertText, sizeof(st.alertText) - 1);
    st.scaleVUsed = s_scaleV;
    st.scaleIUsed = s_scaleI;
    strncpy(st.autoNote, s_autoNote, sizeof(st.autoNote) - 1);
    strncpy(st.profileMode, s_profileDvc ? "dvc" : "split", sizeof(st.profileMode) - 1);
    st.profileMode[sizeof(st.profileMode) - 1] = '\0';
    strncpy(st.lastLine, s_lastLine, sizeof(st.lastLine) - 1);
    String p = csvPathNow();
    strncpy(st.csvPath, p.c_str(), sizeof(st.csvPath) - 1);
    st.csvOnSd = sdcardShouldUseSD();
    st.csvEvery = s_csvEvery;
    st.csvRowMs = s_pollMs * (uint32_t)(s_csvEvery ? s_csvEvery : 1);
    st.aggCount = s_agg.n;
    st.sessionWh = s_session.wh;
    st.sessionCcMs = s_session.ccMs;
    st.sessionLimitMs = s_session.limMs;
    st.sessionOffMs = s_session.offMs;
    st.cvccTransitions = s_session.transitions;
    st.rateV = s_rateV;
    st.rateI = s_rateI;
    st.unattributed = s_unattributed;
    st.selfTestActive = s_selfTestActive;
    st.selfTestStep = s_selfTestStep;
    st.selfTestTotal = kSelfTestCount;
    uint8_t okCount = 0;
    for (uint8_t i = 0; i < kSelfTestCount; ++i) if (s_selfTestAnswered[i]) okCount++;
    st.selfTestOk = okCount;
    st.selfTestFail = (uint8_t)(kSelfTestCount - okCount);
    strncpy(st.selfTestVerdict, s_selfTestVerdict, sizeof(st.selfTestVerdict) - 1);
    strncpy(st.selfTestDetail, s_selfTestDetail, sizeof(st.selfTestDetail) - 1);
    return st;
}

TdkValues tdkLambdaGetValues() { return s_values; }

uint16_t tdkLambdaCopyLog(TdkLogEntry* out, uint16_t maxEntries) {
    if (!out || maxEntries == 0) return 0;
    uint16_t n = (s_logCount < maxEntries) ? s_logCount : maxEntries;
    uint32_t now = millis();
    for (uint16_t i = 0; i < n; ++i) {
        uint8_t idx = (uint8_t)((s_logStart + (s_logCount - n + i)) % TDK_LOG_MAX);
        out[i].ageMs = now - s_log[idx].ts;
        strncpy(out[i].direction, s_log[idx].dir, sizeof(out[i].direction) - 1);
        out[i].direction[sizeof(out[i].direction) - 1] = '\0';
        strncpy(out[i].text, s_log[idx].text, sizeof(out[i].text) - 1);
        out[i].text[sizeof(out[i].text) - 1] = '\0';
    }
    return n;
}

bool tdkLambdaTakeInjectLine(char* out, size_t maxLen) {
    if (!s_injectPending || !out || maxLen == 0) return false;
    snprintf(out, maxLen, "%s", s_injectLine);
    s_injectPending = false;
    return true;
}

uint16_t tdkLambdaCopySamples(TdkSample* out, uint16_t maxEntries) {
    if (!out || maxEntries == 0) return 0;
    const uint16_t n = (s_ringCount < maxEntries) ? s_ringCount : maxEntries;
    for (uint16_t k = 0; k < n; ++k) {
        const uint16_t idx = (uint16_t)((s_ringStart + (s_ringCount - n + k)) % TDK_RING_MAX);
        out[k] = s_ring[idx];
    }
    return n;
}

const TdkCommandInfo* tdkLambdaCatalog(size_t* count) {
    if (count) *count = sizeof(kCatalog) / sizeof(kCatalog[0]);
    return kCatalog;
}

String tdkLambdaCsvPath() { return csvPathNow(); }

bool tdkLambdaCsvFlushNow() {
    const bool ok = tdkCsvFlushPending();
    s_csvLastFlushMs = millis();
    return ok;
}

bool tdkLambdaAllowWrite() {
#if TDK_READ_ONLY
    return false;   // poll + receive only
#else
    return configManager.getTdkLambdaAllowWrite();
#endif
}

void tdkLambdaSetCsvEvery(uint16_t polls) {
    s_csvEvery = (polls < 1) ? 1 : polls;
    s_agg.clear(millis());   // the open window restarts with the new cadence
}

void tdkLambdaSetPoll(bool enabled, uint32_t intervalMs) {
    s_polling = enabled;
    if (intervalMs >= 60000UL) s_pollMs = intervalMs;   // 1 min floor
    s_lastPollMs = millis();
    s_pollStep = 0;
}

void tdkLambdaApplyProfile(const char* mode, const char* vCmd, const char* iCmd,
                           const char* stateCmd, const char* map,
                           float scaleV, float scaleI, float offV, float offI) {
    s_profileDvc = !(mode && !strcmp(mode, "split"));
    tdkUpperTrim(vCmd ? vCmd : "DVC?", s_cmdV, sizeof(s_cmdV));
    tdkUpperTrim(iCmd ? iCmd : "MC?",  s_cmdI, sizeof(s_cmdI));
    tdkUpperTrim(stateCmd ? stateCmd : "MODE?", s_cmdState, sizeof(s_cmdState));
    if (!s_cmdV[0])     tdkUpperTrim("DVC?",  s_cmdV, sizeof(s_cmdV));
    if (!s_cmdI[0])     tdkUpperTrim("MC?",   s_cmdI, sizeof(s_cmdI));
    if (!s_cmdState[0]) tdkUpperTrim("MODE?", s_cmdState, sizeof(s_cmdState));
    // The poll sends these without going through the send guard: with "allow
    // write" off, a setter typed into the profile would change the supply every
    // cycle. Only queries are accepted then; anything else falls back and says so.
    if (!tdkLambdaAllowWrite()) {
        if (!tdkIsQueryCmd(s_cmdV)) {
            tdkLogSys("profile: V command is not a query, using DVC?");
            tdkUpperTrim("DVC?", s_cmdV, sizeof(s_cmdV));
        }
        if (!tdkIsQueryCmd(s_cmdI)) {
            tdkLogSys("profile: I command is not a query, using MC?");
            tdkUpperTrim("MC?", s_cmdI, sizeof(s_cmdI));
        }
        if (!tdkIsQueryCmd(s_cmdState)) {
            tdkLogSys("profile: state command is not a query, using MODE?");
            tdkUpperTrim("MODE?", s_cmdState, sizeof(s_cmdState));
        }
    }
    parseMap(map ? map : "0,1,2,3,4,5", s_map);
    s_scaleV = (scaleV == 0.0f) ? 1.0f : scaleV;
    s_scaleI = (scaleI == 0.0f) ? 1.0f : scaleI;
    s_offV = offV;
    s_offI = offI;
    s_pollStep = 0;
}

void tdkLambdaSetAddress(uint8_t address) {
    s_addr = (address > 30) ? 0 : address;
    s_pollStep = 0;
}

void tdkLambdaSetInject(bool enabled) {
    s_inject = enabled;
    if (!enabled) { s_injectPending = false; s_injectLine[0] = '\0'; }
}

void tdkLambdaSetLimits(float vMin, float vMax, float iMax, bool enabled) {
    s_limitVMin = (vMin < 0) ? 0 : vMin;
    s_limitVMax = (vMax < 0) ? 0 : vMax;
    s_limitIMax = (iMax < 0) ? 0 : iMax;
    s_limitsEnabled = enabled;
    s_alertActive = false;
    s_alertText[0] = '\0';
}

void tdkLambdaSetListen(bool listen) {
    s_listen = listen;
    if (listen) {
        if (s_selfTestActive) { s_selfTestActive = false; s_pending = P_NONE; }
        tdkLogSys("modo escuta: TX desactivado (o mestre da linha e o PLC)");
    } else {
        tdkLogSys("modo mestre: o hub passa a perguntar");
    }
}

void tdkLambdaSetAutoMap(bool enabled) {
    s_autoMap = enabled;
    if (!enabled) s_autoNote[0] = '\0';
}

void tdkLambdaSetRates(float rateV, float rateI) {
    if (rateV > 0.0f) s_rateV = rateV;
    if (rateI > 0.0f) s_rateI = rateI;
}

bool tdkLambdaStartSelfTest() {
    if (s_listen) {
        tdkLogSys("line test refused: listen mode never transmits");
        return false;
    }
    if (s_selfTestActive) return false;
    s_selfTestActive = true;
    s_selfTestStep = 0;
    for (uint8_t i = 0; i < kSelfTestCount; ++i) s_selfTestAnswered[i] = false;
    s_selfTestSentMs = millis();
    s_selfTestRxMark = s_lastRxMs;
    s_selfTestVerdict[0] = '\0';
    s_selfTestDetail[0] = '\0';
    tdkLogSys("line test started: IDN? REV? SN? DVC? MODE?");
    return true;
}

void tdkLambdaSetCsvFormat(bool sensorFormat) {
    s_csvSensorFmt = sensorFormat;
    if (s_csvPending.length() > 0) tdkLambdaCsvFlushNow();  // never mix formats in one file
}

void tdkLambdaSetCsv(bool enabled) {
    s_csvEnabled = enabled;
    if (!enabled) s_csvPending = "";
}

#endif // ENABLE_TDK_LAMBDA == 1
