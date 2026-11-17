#pragma once

// diag_trace.h — a tiny breadcrumb ring in RTC RAM (survives a panic/WDT reboot).
//
// Why: TASK_WDT on an ESP32 reports the task that did NOT run (IDLE0), and the
// core dump kept in flash therefore shows IDLE0's frames — useless for finding
// the task that hogged the CPU or the flash op that stalled. This ring records
// "what the firmware was doing" with a timestamp, in memory the reset does not
// wipe (RTC_DATA_ATTR), so the next boot can print the last ~24 steps of the
// previous run into the daily log (SD → survives power cuts too).
//
// Cost: ~600 B of RTC RAM, one 32-bit read per mark and a 24-byte store.
// Rules: mark coarse phases only (a cycle, a flash write, an upload), never a
// hot loop; tags are literals so they are copied as-is.

#include <Arduino.h>

#define DIAG_TRACE_SLOTS   24
#define DIAG_TRACE_TAG_LEN 20

struct DiagTraceEntry {
    uint32_t ms;                    // millis() when the mark was taken
    char     tag[DIAG_TRACE_TAG_LEN];
};

struct DiagTraceRing {
    uint32_t       magic;
    uint32_t       head;            // next slot to write
    uint32_t       count;           // entries written (saturates at SLOTS)
    DiagTraceEntry e[DIAG_TRACE_SLOTS];
};

// Record one step ("ftp:scan", "lfs:save", ...). Cheap enough for a 30 s tick.
void diagTraceMark(const char* tag);

// True when the RTC ring holds a run from before this boot (magic + non-empty).
bool diagTracePreviousValid();

// Print the PREVIOUS run's ring into the daily log (and Serial). Call once per
// boot after the log storage is ready; it clears the ring so the next crash
// records its own steps.
void diagTraceDumpToLog();

// Previous run's steps as one string ("" when there was nothing), served by
// /api/status as `diag_trace_prev` so a crash can be read over HTTP even if the
// SD log is unreachable.
const char* diagTracePreviousText();

// ---------------------------------------------------------------------------
// Stage + progress snapshot: one record, overwritten in place, also in RTC RAM.
// The ring above answers "which phase"; this answers "how far inside it" — the
// question a TASK_WDT leaves unanswered (it reports the task that did NOT run,
// IDLE0, so the core dump is useless). A transfer that dies at 94 % of the bytes
// is a different bug from one that dies at 0.
// ---------------------------------------------------------------------------
void diagStage(uint8_t stage, uint32_t a, uint32_t b);

// Previous run's stage line ("" when the previous run never set one), served by
// /api/status as `diag_stage_prev`.
const char* diagStagePreviousText();

// ---------------------------------------------------------------------------
// CPU-hog sampler ("who starved IDLE0?").
//
// CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS is on, so every task carries a runtime
// counter. While armed, a priority-20 task samples the table every 250 ms and
// records into RTC RAM the task that consumed >=80 % of the interval — the one
// that keeps IDLE0 from running and makes the watchdog blame IDLE0 instead of
// the culprit. Priority 20 preempts the web task (10) and the FTP monitor (1),
// i.e. it still runs while they spin.
//
// Armed only around an FTP cycle (cost is a table walk twice a second).
// ---------------------------------------------------------------------------
void diagHogArm(bool on);

// Previous run's hog line ("" when nothing was recorded), served by
// /api/status as `diag_hog_prev`.
const char* diagHogPreviousText();
