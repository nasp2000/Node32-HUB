// diag_trace.cpp — RTC-RAM breadcrumb ring, see diag_trace.h.
//
// RTC_DATA_ATTR is not re-initialised by a software/WDT/panic reset, so the
// ring keeps what the previous run was doing. A power-on gives arbitrary bytes,
// hence the magic check. Kept in its own translation unit so exactly one copy
// exists in the app image.

#include "diag_trace.h"
#include "log.h"

#include <cstring>
#include <esp_attr.h>

namespace {
constexpr uint32_t kDiagTraceMagic = 0x44545231;   // "DTR1"
RTC_DATA_ATTR DiagTraceRing gDiagTrace;             // zero-filled on cold boot only
}  // namespace

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
    if (r.magic != kDiagTraceMagic || r.count == 0) {
        r.magic = kDiagTraceMagic;      // fresh ring for this run
        r.head = 0;
        r.count = 0;
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
    Serial.println(line);
    // CRASH_DETAIL type (its own 60 s bucket, not shared with PANIC_REPORT).
    logSystemEvent(String(F("CRASH_DETAIL")), String(line));
    // Start clean for this run so the next crash records only its own steps.
    r.head = 0;
    r.count = 0;
    r.magic = kDiagTraceMagic;
}
