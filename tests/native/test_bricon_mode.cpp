#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>

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
    p.stop = (stop == 2) ? 2 : 1;
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
    SerialProfile p = clampSerialProfile(38400, 8, "none", 1);
    CHECK(p.baud == 38400);
    CHECK(p.data == 8);
    CHECK(std::string(p.parity) == "none");
    CHECK(p.stop == 1);
    CHECK(baudIndex(38400) == 5);
}

TEST_CASE("briconProfile: garbage clamps to 9600 8N1") {
    SerialProfile p = clampSerialProfile(-1, 99, "mark", 3);
    CHECK(p.baud == 9600);
    CHECK(p.data == 8);
    CHECK(std::string(p.parity) == "none");
    CHECK(p.stop == 1);
    CHECK(baudIndex(12345) == 3);
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
