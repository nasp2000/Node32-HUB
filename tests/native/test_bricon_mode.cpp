#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <strings.h>

// ====== Standalone reimpls of the Bricon module-selection + RS232 profile logic ======
// Mirror chatgpt/src/bricon_protocol.cpp (no Arduino/RS232 dependencies).

// Mutual exclusion: rambox XOR bricon. Enabling one side disables the other;
// disabling never auto-enables the other side.
struct ModuleState { bool unikon; bool bricon; };

static ModuleState resolveModuleEnable(ModuleState cur, bool wantUnikon, bool wantBricon) {
    // Last explicit enable wins; explicit disable only clears its own side.
    if (wantUnikon && !wantBricon) { cur.unikon = true; cur.bricon = false; }
    else if (wantBricon && !wantUnikon) { cur.bricon = true; cur.unikon = false; }
    else if (!wantUnikon && !wantBricon) { cur.unikon = false; cur.bricon = false; }
    // Both requested: bricon wins loudly (same tiebreak as firmware).
    else { cur.bricon = true; cur.unikon = false; }
    return cur;
}

static const char* activeModuleName(ModuleState s) {
    if (s.bricon && !s.unikon) return "bricon";
    if (s.unikon && !s.bricon) return "rambox";
    if (s.bricon && s.unikon) return "bricon";  // tiebreak, must not happen
    return "none";
}

// RS232 profile validation (mirrors briconSetConfig clamps).
struct SerialProfile { int baud; int data; const char* parity; int stop; };

static SerialProfile clampSerialProfile(int baud, int data, const char* parity, int stop) {
    SerialProfile p;
    p.baud = (baud > 0) ? baud : 9600;
    p.data = (data >= 5 && data <= 8) ? data : 8;
    p.parity = (!parity || parity[0] == '\0') ? "none" : parity;
    if (strcmp(p.parity, "even") != 0 && strcmp(p.parity, "odd") != 0) p.parity = "none";
    // REA: Bricon builds SerialPort(..., StopBits.Two) => default 2 (Unikon is 1).
    p.stop = (stop == 1) ? 1 : 2;
    return p;
}

static const int BAUDS[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
static int baudIndex(int baud) {
    for (size_t i = 0; i < sizeof(BAUDS) / sizeof(BAUDS[0]); i++) {
        if (BAUDS[i] == baud) return (int)i;
    }
    return 3;  // 9600 slot, same fallback as firmware
}

TEST_CASE("briconExclusion: enabling bricon disables rambox") {
    ModuleState s = resolveModuleEnable({ true, false }, false, true);
    CHECK(!s.unikon);
    CHECK(s.bricon);
    CHECK(std::string(activeModuleName(s)) == "bricon");
}

TEST_CASE("briconExclusion: enabling rambox disables bricon") {
    ModuleState s = resolveModuleEnable({ false, true }, true, false);
    CHECK(s.unikon);
    CHECK(!s.bricon);
    CHECK(std::string(activeModuleName(s)) == "rambox");
}

TEST_CASE("briconExclusion: disabling never auto-enables the other side") {
    ModuleState s = resolveModuleEnable({ false, true }, false, false);
    CHECK(!s.unikon);
    CHECK(!s.bricon);
    CHECK(std::string(activeModuleName(s)) == "none");
}

TEST_CASE("briconExclusion: both requested resolves to bricon") {
    ModuleState s = resolveModuleEnable({ false, false }, true, true);
    CHECK(!s.unikon);
    CHECK(s.bricon);
}

TEST_CASE("briconProfile: valid values pass through") {
    SerialProfile p = clampSerialProfile(38400, 8, "none", 2);
    CHECK(p.baud == 38400);
    CHECK(p.data == 8);
    CHECK(std::string(p.parity) == "none");
    CHECK(p.stop == 2);
    CHECK(baudIndex(38400) == 5);
}

TEST_CASE("briconProfile: Bricon default is 9600 8N2 (REA: StopBits.Two)") {
    SerialProfile p = clampSerialProfile(-1, 99, "mark", 3);
    CHECK(p.baud == 9600);
    CHECK(p.data == 8);
    CHECK(std::string(p.parity) == "none");
    CHECK(p.stop == 2);              // 8N2, not Unikon's 8N1
    CHECK(baudIndex(12345) == 3);
}

TEST_CASE("briconProfile: explicit 1 stop bit honoured (probe fallback)") {
    SerialProfile p = clampSerialProfile(9600, 8, "none", 1);
    CHECK(p.stop == 1);
}

// ====== Poll interval: fixed minute steps, 3 min default (page + loadConfig) ======
// Mirror of the /rambox selector (rmIntervalOption) and of the firmware defaults
// (unikon loadConfig 180; bricon loadConfig 180 since the 2026-10-24 request).
static const int POLL_MIN[] = { 1, 3, 5, 10, 15, 30, 60 };

static uint16_t pollIntervalDefaultSec(bool keyPresent, uint16_t stored) {
    return keyPresent ? stored : (uint16_t)180;   // missing key => 3 minutes
}

static int pollOptionMin(int sec) {
    if (sec <= 0) return 3;                       // 0/absent => 3 min
    for (size_t i = 0; i < sizeof(POLL_MIN) / sizeof(POLL_MIN[0]); i++)
        if (POLL_MIN[i] * 60 == sec) return POLL_MIN[i];
    for (size_t i = 0; i < sizeof(POLL_MIN) / sizeof(POLL_MIN[0]); i++)
        if (POLL_MIN[i] * 60 > sec) return POLL_MIN[i];
    return POLL_MIN[sizeof(POLL_MIN) / sizeof(POLL_MIN[0]) - 1];
}

TEST_CASE("pollInterval: both clocks default to 3 minutes") {
    CHECK(pollIntervalDefaultSec(false, 0) == 180);
    CHECK(pollIntervalDefaultSec(true, 180) == 180);
    CHECK(pollIntervalDefaultSec(true, 900) == 900);   // explicit value kept
    CHECK(pollIntervalDefaultSec(true, 0) == 0);       // explicit manual kept
}

TEST_CASE("pollInterval: page selector maps seconds to 1/3/5/10/15/30/60 min") {
    CHECK(pollOptionMin(0) == 3);         // manual/absent -> default
    CHECK(pollOptionMin(60) == 1);
    CHECK(pollOptionMin(180) == 3);
    CHECK(pollOptionMin(120) == 3);       // legacy 2 min snaps up
    CHECK(pollOptionMin(900) == 15);
    CHECK(pollOptionMin(3600) == 60);
    CHECK(pollOptionMin(7200) == 60);     // above the last step -> cap
}

// ====== Bench mode: the RS232 page profile vs the module's own profile ======
// Mirror of the module apply logic: with manual_serial the runtime uses the page
// settings; the module's stored profile (own) is never rewritten and comes back
// as soon as manual mode is switched off.
struct ModuleProfile { int baud; int data; const char* parity; int stop; };

static ModuleProfile resolveActiveProfile(bool manualSerial, ModuleProfile own, ModuleProfile page) {
    return manualSerial ? page : own;
}

TEST_CASE("benchMode: manual uses the RS232 page profile, own profile untouched") {
    ModuleProfile own  = { 9600, 8, "none", 2 };   // Bricon default (8N2)
    ModuleProfile page = { 19200, 8, "even", 1 };
    ModuleProfile active = resolveActiveProfile(true, own, page);
    CHECK(active.baud == 19200);
    CHECK(active.stop == 1);
    CHECK(active.parity == std::string("even"));
    CHECK(own.baud == 9600);                       // default preserved
    CHECK(own.stop == 2);
    CHECK(own.parity == std::string("none"));
}

TEST_CASE("benchMode: leaving manual restores the module default") {
    ModuleProfile own  = { 9600, 8, "none", 1 };   // Unikon default (8N1)
    ModuleProfile page = { 38400, 7, "odd", 2 };
    CHECK(resolveActiveProfile(false, own, page).baud == 9600);
    CHECK(resolveActiveProfile(false, own, page).stop == 1);
    CHECK(resolveActiveProfile(false, own, page).data == 8);
    // and the page profile is still there for the next bench session
    CHECK(page.baud == 38400);
}

TEST_CASE("briconLayout: country decides the FedBand (GetPigeonCommand)") {
    // Mirrors the firmware/simulator rule: PT/ES/NL/UA/PL/FR/KSA => 14 B, else 22 B.
    auto fed = [](const char* c) {
        const char* shortFed[] = { "PT", "ES", "NL", "UA", "PL", "FR", "KSA" };
        for (size_t i = 0; i < sizeof(shortFed) / sizeof(shortFed[0]); i++)
            if (strcasecmp(c, shortFed[i]) == 0) return 14;
        return 22;
    };
    CHECK(fed("PT") == 14);          // our case: 14 B FedBand => 53-byte arrival
    CHECK(fed("ES") == 14);
    CHECK(fed("FR") == 14);
    CHECK(fed("UK") == 22);
    CHECK(fed("BE") == 22);
    CHECK((39 + fed("PT")) == 53);
    CHECK((39 + fed("BE")) == 61);
}

// ====== Ingest funnel tests (mirror briconIngestArrival dedup/update/cap) ======
struct BArr { char ring[16]; char time[7]; bool isTraining; };

static const int BCAP = 5;
static BArr s_store[BCAP];
static int s_n = 0;

static bool hasT(const BArr& a) {
    return a.time[0] != '\0' && !(a.time[0]=='0'&&a.time[1]=='0'&&a.time[2]=='0'&&a.time[3]=='0'&&a.time[4]=='0'&&a.time[5]=='0');
}

static int findByRing(const char* ring) {
    for (int i = 0; i < s_n; i++) {
        if (strncmp(s_store[i].ring, ring, 16) == 0) return i;
    }
    return -1;
}

static bool ingestReimpl(const BArr& src) {
    if (!hasT(src)) return false;
    int idx = findByRing(src.ring);
    if (idx >= 0) {
        bool hadRaceTime = hasT(s_store[idx]) && !s_store[idx].isTraining;
        if (hadRaceTime && src.isTraining) return true;
        s_store[idx] = src;
        return true;
    }
    if (s_n >= BCAP) return false;
    s_store[s_n++] = src;
    return true;
}

static void resetStore() { s_n = 0; memset(s_store, 0, sizeof(s_store)); }

TEST_CASE("briconIngest: new ring appended") {
    resetStore();
    BArr a = {"RING00000000001", "162444", false};
    CHECK(ingestReimpl(a));
    CHECK(s_n == 1);
}

TEST_CASE("briconIngest: no time rejected, cap respected") {
    resetStore();
    BArr a = {"RING00000000001", "000000", false};
    CHECK(!ingestReimpl(a));
    CHECK(s_n == 0);
    for (int i = 0; i < BCAP + 2; i++) {
        char r[16];
        snprintf(r, sizeof(r), "R%014d", i);
        BArr b;
        memcpy(b.ring, r, 16);
        memcpy(b.time, "162444", 7);
        b.isTraining = false;
        ingestReimpl(b);
    }
    CHECK(s_n == BCAP);
}

TEST_CASE("briconIngest: race time keeps precedence over training") {
    resetStore();
    BArr race = {"RING00000000001", "162444", false};
    BArr train = {"RING00000000001", "170000", true};
    CHECK(ingestReimpl(race));
    CHECK(ingestReimpl(train));
    CHECK(s_n == 1);
    CHECK(!s_store[0].isTraining);
    CHECK(strncmp(s_store[0].time, "162444", 6) == 0);
}

TEST_CASE("briconIngest: training updates empty slot, email counts split") {
    resetStore();
    BArr t1 = {"RING00000000001", "162444", true};
    BArr r1 = {"RING00000000002", "170000", false};
    CHECK(ingestReimpl(t1));
    CHECK(ingestReimpl(r1));
    int tr = 0, ra = 0;
    for (int i = 0; i < s_n; i++) {
        if (s_store[i].isTraining) tr++;
        else ra++;
    }
    CHECK(tr == 1);
    CHECK(ra == 1);
}

// ====== Arrival email table (phone-friendly: No / Ring / Chip / Time) ======
// Mirror of appendBriconArrivalSection()/appendArrivalSectionEmail() width logic:
// every line of the ASCII table must have the same length, otherwise it wraps on
// a smartphone and becomes unreadable.
#include <vector>

struct EmailRow { std::string no, ring, chip, time; };

static void emailCell(std::string& out, const std::string& s, size_t width) {
    out += ' ';
    std::string v = (s.size() > width) ? s.substr(0, width) : s;
    out += v;
    for (size_t i = v.size(); i < width; i++) out += ' ';
    out += ' ';
    out += '|';
}

static std::string buildArrivalEmailTable(const std::vector<EmailRow>& rows) {
    const char* HDR[4] = { "No", "Ring", "Chip", "Time" };
    size_t w[4];
    for (int c = 0; c < 4; c++) w[c] = strlen(HDR[c]);
    if (w[1] < 11) w[1] = 11;
    if (w[3] < 8)  w[3] = 8;
    for (size_t i = 0; i < rows.size(); i++) {
        if (rows[i].no.size()   > w[0]) w[0] = rows[i].no.size();
        if (rows[i].ring.size() > w[1]) w[1] = rows[i].ring.size();
        if (rows[i].chip.size() > w[2]) w[2] = rows[i].chip.size();
    }
    if (w[1] > 15) w[1] = 15;
    if (w[2] > 11) w[2] = 11;

    std::string sep = "+";
    for (int c = 0; c < 4; c++) {
        for (size_t k = 0; k < w[c] + 2; k++) sep += '-';
        sep += '+';
    }
    std::string hdr = "|";
    for (int c = 0; c < 4; c++) emailCell(hdr, HDR[c], w[c]);

    std::string out = sep + "\n" + hdr + "\n" + sep + "\n";
    for (size_t i = 0; i < rows.size(); i++) {
        std::string line = "|";
        emailCell(line, rows[i].no,   w[0]);
        emailCell(line, rows[i].ring, w[1]);
        emailCell(line, rows[i].chip, w[2]);
        emailCell(line, rows[i].time, w[3]);
        out += line + "\n";
    }
    out += sep + "\n";
    return out;
}

TEST_CASE("emailTable: only No/Ring/Chip/Time columns (phone-friendly)") {
    std::vector<EmailRow> rows;
    rows.push_back({ "1.", "23123450", "C54A7B9A", "14:25:00" });
    rows.push_back({ "2.", "23123451", "C54A7B9B", "14:25:01" });
    std::string t = buildArrivalEmailTable(rows);
    CHECK(t.find("| No ") != std::string::npos);
    bool hasRing = (t.find("| Ring ") != std::string::npos) || (t.find("Anilha") != std::string::npos);
    CHECK(hasRing);
    CHECK(t.find("| Chip ") != std::string::npos);
    CHECK(t.find("| Time ") != std::string::npos);
    CHECK(t.find("| Date ") == std::string::npos);
    CHECK(t.find("| Race ") == std::string::npos);
    CHECK(t.find("Pos") == std::string::npos);
    CHECK(t.find("Eval") == std::string::npos);
}

TEST_CASE("emailTable: every line has the same width, even with a long chip") {
    std::vector<EmailRow> rows;
    rows.push_back({ "1.", "23123450", "C54A7B9AEXTRA", "14:25:00" });   // chip longer than header
    rows.push_back({ "12.", "123456789012345678", "", "08:00:00" });     // ring above the cap
    std::string t = buildArrivalEmailTable(rows);
    size_t first = t.find('\n');
    size_t width = first;
    size_t pos = 0;
    int lines = 0;
    while (pos < t.size()) {
        size_t eol = t.find('\n', pos);
        if (eol == std::string::npos) eol = t.size();
        if (eol > pos) {
            CHECK((eol - pos) == width);
            lines++;
        }
        pos = eol + 1;
    }
    CHECK(lines == 6);   // sep, header, sep, 2 rows, sep
}
