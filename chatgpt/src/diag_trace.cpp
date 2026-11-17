// diag_trace.cpp — RTC-RAM breadcrumb ring, see diag_trace.h.
//
// RTC_DATA_ATTR is not re-initialised by a software/WDT/panic reset, so the
// ring keeps what the previous run was doing. A power-on gives arbitrary bytes,
// hence the magic check. Kept in its own translation unit so exactly one copy
// exists in the app image.

#include "diag_trace.h"
#include "log.h"

#include <cstring>
#include <cstdio>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>   // uxTaskGetSystemState (CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y)

namespace {
constexpr uint32_t kDiagTraceMagic = 0x44545231;   // "DTR1"
// RTC_NOINIT_ATTR, not RTC_DATA_ATTR: ESP-IDF re-initialises .rtc.data from the
// app image on every reset except a deep-sleep wake, so a WDT/panic reset would
// wipe the ring. .rtc.noinit is left untouched by any reset (garbage only on a
// power-on, which the magic check rejects).
RTC_NOINIT_ATTR DiagTraceRing gDiagTrace;
char s_prevText[900];                               // previous run, for /api/status
bool s_prevValid = false;

// Stage/progress snapshot (see diag_trace.h). Own magic so a power-on cannot be
// mistaken for a real record.
constexpr uint32_t kDiagStageMagic = 0x44535431;    // "DST1"
struct DiagStageRec {
    uint32_t magic;
    uint8_t  stage;
    uint32_t a;
    uint32_t b;
    uint32_t ms;
};
RTC_NOINIT_ATTR DiagStageRec gDiagStage;
char s_prevStage[120];
bool s_prevStageValid = false;
}  // namespace

const char* diagTracePreviousText() {
    return s_prevValid ? s_prevText : "";
}

const char* diagStagePreviousText() {
    return s_prevStageValid ? s_prevStage : "";
}

void diagStage(uint8_t stage, uint32_t a, uint32_t b) {
    gDiagStage.magic = kDiagStageMagic;
    gDiagStage.stage = stage;
    gDiagStage.a = a;
    gDiagStage.b = b;
    gDiagStage.ms = (uint32_t)millis();
}

// ---------------------------------------------------------------------------
// CPU-hog sampler (see diag_trace.h).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kDiagHogMagic = 0x44484731;   // "DHG1"
constexpr uint32_t kDiagHogMinPct = 80;          // a task owning >=80 % is the hog
constexpr uint32_t kDiagHogPeriodMs = 250;
constexpr UBaseType_t kDiagHogMaxTasks = 32;

struct DiagHogRec {
    uint32_t magic;
    char     name[16];
    uint16_t pct;          // CPU share of the sampled interval
    uint16_t idle0Pct;     // IDLE0's share over the same interval
    uint8_t  core;         // xCoreID (0xFF = either)
    uint8_t  streak;       // consecutive samples above kDiagHogMinPct
    uint32_t ms;           // millis() at the sample
    uint32_t uptimeS;
};
RTC_NOINIT_ATTR DiagHogRec gDiagHog;
char s_prevHog[180];
bool s_prevHogValid = false;
volatile bool s_hogArmed = false;
TaskHandle_t s_hogTask = nullptr;
}  // namespace

const char* diagHogPreviousText() {
    return s_prevHogValid ? s_prevHog : "";
}

// One sample: per-task runtime deltas since the previous sample. Runtime stats
// are cumulative and 32-bit, so the unsigned delta is wrap-safe.
static void diagHogSample() {
    static TaskStatus_t snap[kDiagHogMaxTasks];
    static TaskHandle_t prevH[kDiagHogMaxTasks];
    static uint32_t     prevR[kDiagHogMaxTasks];
    static uint32_t     prevN = 0;

    UBaseType_t n = uxTaskGetSystemState(snap, kDiagHogMaxTasks, nullptr);
    if (!n) return;

    uint32_t total = 0;
    uint32_t bestDelta = 0;
    uint32_t idle0Delta = 0;
    int best = -1;
    bool idle0Seen = false;
    for (UBaseType_t i = 0; i < n; i++) {
        uint32_t delta = 0;
        bool known = false;
        for (uint32_t k = 0; k < prevN; k++) {
            if (prevH[k] == snap[i].xHandle) {
                delta = (uint32_t)snap[i].ulRunTimeCounter - prevR[k];
                known = true;
                break;
            }
        }
        if (snap[i].pcTaskName && strcmp(snap[i].pcTaskName, "IDLE0") == 0) {
            idle0Seen = true;
            idle0Delta = delta;
        }
        if (known) {
            total += delta;
            if (delta > bestDelta) { bestDelta = delta; best = (int)i; }
        }
    }
    // Remember this snapshot for the next delta.
    for (UBaseType_t i = 0; i < n; i++) {
        prevH[i] = snap[i].xHandle;
        prevR[i] = (uint32_t)snap[i].ulRunTimeCounter;
    }
    prevN = n;

    if (!total || best < 0) return;
    uint32_t pct = (uint32_t)((uint64_t)bestDelta * 100u / total);
    if (pct < kDiagHogMinPct) {
        // Normal work: forget the streak so only a sustained hog is reported.
        if (gDiagHog.magic == kDiagHogMagic) gDiagHog.streak = 0;
        return;
    }
    DiagHogRec& r = gDiagHog;
    uint32_t streak = (r.magic == kDiagHogMagic && r.streak < 0xFF) ? (uint32_t)r.streak + 1 : 1;
    r.magic = kDiagHogMagic;
    snprintf(r.name, sizeof(r.name), "%.15s", snap[best].pcTaskName ? snap[best].pcTaskName : "?");
    r.pct = (uint16_t)pct;
    r.idle0Pct = idle0Seen ? (uint16_t)((uint64_t)idle0Delta * 100u / total) : 0;
    r.core = (uint8_t)snap[best].xCoreID;
    r.streak = (uint8_t)streak;
    r.ms = (uint32_t)millis();
    r.uptimeS = (uint32_t)(millis() / 1000UL);
}

static void diagHogTask(void* p) {
    (void)p;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kDiagHogPeriodMs));
        if (!s_hogArmed) continue;
        diagHogSample();
    }
}

void diagHogArm(bool on) {
    s_hogArmed = on;
    if (on) {
        if (!s_hogTask) {
            xTaskCreate(diagHogTask, "diagHog", 3072, nullptr, 20, &s_hogTask);
        }
        // Fresh baseline: a record from an earlier transfer must not be mistaken
        // for this one.
        gDiagHog.magic = kDiagHogMagic;
        gDiagHog.streak = 0;
    }
}

void diagTraceMark(const char* tag) {
    if (!tag || !tag[0]) return;
    DiagTraceRing& r = gDiagTrace;
    if (r.magic != kDiagTraceMagic) {
        // First mark of this power cycle: start a fresh ring.
        memset(&r, 0, sizeof(r));
        r.magic = kDiagTraceMagic;
    }
    DiagTraceEntry& s = r.e[r.head % DIAG_TRACE_SLOTS];
    s.ms = (uint32_t)millis();
    size_t i = 0;
    for (; tag[i] && i + 1 < DIAG_TRACE_TAG_LEN; i++) s.tag[i] = tag[i];
    s.tag[i] = '\0';
    r.head = (r.head + 1) % DIAG_TRACE_SLOTS;
    if (r.count < DIAG_TRACE_SLOTS) r.count++;
}

bool diagTracePreviousValid() {
    const DiagTraceRing& r = gDiagTrace;
    return r.magic == kDiagTraceMagic && r.count > 0;
}

void diagTraceDumpToLog() {
    DiagTraceRing& r = gDiagTrace;
    // Stage snapshot of the previous run, independent of the ring: read it
    // first (it survives even when the ring was never written), then clear.
    if (gDiagStage.magic == kDiagStageMagic) {
        snprintf(s_prevStage, sizeof(s_prevStage),
                 "PREV-RUN-STAGE %u: a=%lu b=%lu @%lums",
                 (unsigned)gDiagStage.stage, (unsigned long)gDiagStage.a,
                 (unsigned long)gDiagStage.b, (unsigned long)gDiagStage.ms);
        s_prevStageValid = true;
        Serial.println(s_prevStage);
        logSystemEvent(String(F("CRASH_DETAIL")), String(s_prevStage), true, /*bypassThrottle=*/true);
        gDiagStage.magic = kDiagStageMagic;
        gDiagStage.stage = 0;
        gDiagStage.a = 0;
        gDiagStage.b = 0;
        gDiagStage.ms = 0;
    } else {
        s_prevStageValid = false;
        s_prevStage[0] = '\0';
    }
    // CPU-hog record: the task that owned the CPU while IDLE0 was not running.
    if (gDiagHog.magic == kDiagHogMagic && gDiagHog.streak >= 2) {
        snprintf(s_prevHog, sizeof(s_prevHog),
                 "PREV-RUN-HOG %s core=%u cpu=%u%% idle0=%u%% streak=%u @%lums (up %lus)",
                 gDiagHog.name, (unsigned)gDiagHog.core, (unsigned)gDiagHog.pct,
                 (unsigned)gDiagHog.idle0Pct, (unsigned)gDiagHog.streak,
                 (unsigned long)gDiagHog.ms, (unsigned long)gDiagHog.uptimeS);
        s_prevHogValid = true;
        Serial.println(s_prevHog);
        logSystemEvent(String(F("CRASH_DETAIL")), String(s_prevHog), true, /*bypassThrottle=*/true);
    } else {
        s_prevHogValid = false;
        s_prevHog[0] = '\0';
    }
    gDiagHog.magic = kDiagHogMagic;
    gDiagHog.streak = 0;
    if (r.magic != kDiagTraceMagic || r.count == 0) {
        r.magic = kDiagTraceMagic;      // fresh ring for this run
        r.head = 0;
        r.count = 0;
        s_prevValid = false;
        s_prevText[0] = '\0';
        return;
    }
    // Oldest → newest, all in ONE log event: logSystemEvent() throttles per
    // event type (60 s), so one line per step would lose everything but the first.
    const uint32_t n = (r.count < DIAG_TRACE_SLOTS) ? r.count : DIAG_TRACE_SLOTS;
    char line[900];
    int used = snprintf(line, sizeof(line), "PREV-RUN-TRACE %u steps:", (unsigned)n);
    for (uint32_t i = 0; i < n && used > 0 && used < (int)sizeof(line) - 40; i++) {
        const uint32_t idx = (r.head + DIAG_TRACE_SLOTS - n + i) % DIAG_TRACE_SLOTS;
        used += snprintf(line + used, sizeof(line) - (size_t)used, " %lums:%s",
                         (unsigned long)r.e[idx].ms, r.e[idx].tag);
    }
    if (used < 0) used = 0;
    line[sizeof(line) - 1] = '\0';
    snprintf(s_prevText, sizeof(s_prevText), "%s", line);
    s_prevValid = true;
    Serial.println(line);
    // CRASH_DETAIL type (its own 60 s bucket, not shared with PANIC_REPORT).
    logSystemEvent(String(F("CRASH_DETAIL")), String(line), true, /*bypassThrottle=*/true);
    // Start clean for this run so the next crash records only its own steps.
    r.head = 0;
    r.count = 0;
    r.magic = kDiagTraceMagic;    // note: s_prevText keeps the copy served by /api/status
}
