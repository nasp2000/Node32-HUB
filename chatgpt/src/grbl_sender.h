// grbl_sender.h
// GRBL G-code sender with character-counting flow control.
//
// GRBL uses a 127-byte serial RX buffer (not ACK-per-line).
// The safe sender tracks bytes in-flight: only sends a new line when
// (bytes_in_flight + line_length + 1) <= GRBL_RX_BUFFER_SIZE.
// Each 'ok' or 'error' response from GRBL frees the bytes of the
// corresponding sent line from the counter.
//
// Two send modes:
//   Manual:  grblSend() — send a single command string immediately.
//   Stream:  grblStreamFile() — open a .nc/.gcode file from SD and
//            stream it line by line using the flow-control protocol.
//
// Depends on: feature_flags.h, usb_host_manager.h
// Used by: setup_impl.h, loop_impl.h, chatgpt.ino

#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include "feature_flags.h"

#ifndef ENABLE_GRBL
  #define ENABLE_GRBL 0
#endif

// Keep size limits available even when ENABLE_GRBL=0.
// grbl_web_api.cpp references these constants in shared preflight helpers,
// and non-P4 targets still compile that unit with stubbed sender functions.
#ifndef GRBL_MAX_PATH_LEN
  #define GRBL_MAX_PATH_LEN 128
#endif

#ifndef GRBL_MAX_LINE_LEN
  #define GRBL_MAX_LINE_LEN 96
#endif

#ifndef GRBL_LOCAL_QUEUE_MAX_BYTES
  #define GRBL_LOCAL_QUEUE_MAX_BYTES 8192
#endif

#ifndef GRBL_STREAM_STAGE_BYTES
  #define GRBL_STREAM_STAGE_BYTES 1048576UL
#endif

#ifndef GRBL_RECENT_ERROR_HISTORY
  #define GRBL_RECENT_ERROR_HISTORY 8
#endif

// Maximum bytes to pre-load into PSRAM for streaming. Files larger than this
// are refilled incrementally from SD (or rejected in PSRAM-only mode).
#ifndef GRBL_STREAM_PSRAM_MAX_BYTES
  #define GRBL_STREAM_PSRAM_MAX_BYTES (4 * 1024 * 1024)  // 4MB (default, S3 N16R8 with 5MB VFS)
#endif

// Enforce PSRAM-only allocation for GRBL runtime/web buffers by default.
// Set to 0 only for emergency bring-up on boards without functional PSRAM.
#ifndef GRBL_PSRAM_STRICT
  #define GRBL_PSRAM_STRICT 1
#endif

typedef struct GrblRecentError {
  uint32_t timestamp_ms;
  uint32_t line_sent;
  uint32_t line_acked;
  uint32_t error_count;
  char     source[16];
  char     message[96];
  char     line_preview[GRBL_MAX_LINE_LEN + 1];
} GrblRecentError;

// Global runtime snapshot of the active or last GRBL job session.
// Kept unconditional so stub signatures remain valid when ENABLE_GRBL=0.
typedef struct GrblJobSession {
  bool            active;
  char            type[16];          // none | file_stream | local_queue
  char            state[16];         // idle | running | paused | hold | cancelling | done | error
  char            origin[48];        // Session or source that started the job
  uint32_t        current_line;      // Current processed line (acked)
  uint32_t        total_lines;       // Declared job total lines
  char            pause_reason[48];
  char            error_reason[96];
} GrblJobSession;

// Launch result for assisted resume after ALARM (shared by real + stub API).
enum class GrblResumeLaunch : uint8_t {
    OK,         // background flow launched
    NO_RESUME,  // no alarm snapshot available
    BUSY,       // a resume flow is already running (or launcher busy)
    NOT_IDLE    // a job is streaming/paused — stop it first
};

#if ENABLE_GRBL == 1

// GRBL transport selection. Both transports are compiled in the S3 CNC pack
// and selected at runtime: UART (Serial2 GPIO16/17, the boot default) or USB
// Host (OTG). The selection is session-only — it resets to UART on reboot so
// a board can boot with an OTG adapter plugged and activate it later.
enum class GrblTransportMode : uint8_t {
    UART = 0,
    USB  = 1
};

// GRBL serial RX buffer size (hard-coded in GRBL firmware, do not change).
#define GRBL_RX_BUFFER_SIZE   127

// States of the streaming state machine.
enum class GrblStreamState : uint8_t {
    IDLE,           // No streaming in progress.
    STREAMING,      // Sending lines, waiting for 'ok' to release buffer.
    PAUSED,         // User-requested pause — stops sending but keeps file open.
    DONE,           // All lines sent and all 'ok' received.
    ERROR,          // GRBL reported an error — streaming stopped.
    CANCELLED       // User cancelled mid-stream.
};

// Snapshot of sender state — read-only, safe to call from any context.
struct GrblStatus {
    GrblStreamState state;
    uint32_t        lines_sent;       // Lines sent so far.
    uint32_t        lines_total;      // Total lines in the file (0 if not yet counted).
    uint32_t        lines_acked;      // Lines acknowledged with 'ok'.
    uint32_t        error_count;      // 'error:' responses received.
    uint8_t         bytes_in_flight;  // Bytes currently unacknowledged in GRBL RX buffer.
    uint8_t         usb_slot;         // USB Host slot this sender is bound to.
    char            file_path[GRBL_MAX_PATH_LEN]; // Current or last streamed file.
    // Alarm resume ("restart from line N") snapshot.
    bool            resume_available; // An alarm interrupted a job; resume possible.
    uint32_t        resume_line;      // First unexecuted line (0-based acked count).
    uint32_t        resume_total;     // Total lines of the interrupted job.
    bool            resume_busy;      // A resume flow is running in background.
    char            resume_file[GRBL_MAX_PATH_LEN]; // Job file/label of the snapshot.
    char            resume_alarm[64];               // Alarm text that stopped the job.
    char            resume_error[96];               // Last resume-flow error (empty = none).
};

// --- Public API ---

// Initialise the sender (clears state, no SD or USB operation).
void grblSenderInit();

// Called from loop() — drives the streaming state machine and processes
// incoming GRBL responses ('ok', 'error:', '<Idle|...>' status).
void grblSenderUpdate();

// Notify the sender that new data arrived from the USB CDC RX callback.
// The grbl_sender processes 'ok'/'error' tokens to advance the flow-control counter.
// Call this from the USB Host RX callback INSTEAD of injectDataToRXBuffer
// when ENABLE_GRBL=1 — grblOnRxData internally decides what to forward.
void grblOnRxData(const uint8_t* data, size_t len, uint8_t usb_slot);

// Send a single G-code command to the GRBL machine (manual jog, home, etc.).
// The command is queued if the in-flight buffer is full; returns false if the
// internal command queue is also full (use the return value for feedback).
// Appends '\n' automatically if not present.
bool grblSend(uint8_t usb_slot, const char* cmd);

// Apply a feed/spindle override percentage using GRBL real-time override
// bytes (feed: 0x90/0x93/0x94, spindle: 0x99/0x9C/0x9D). Unlike M220/M221
// g-code, real-time bytes are processed out-of-band by GRBL and never enter
// the stream line/ack counting, so they are safe to send while a file is
// streaming. feedPct/spinPct in [10,200]; pass -1 to leave a channel unchanged.
bool grblSetOverride(uint8_t usb_slot, int feedPct, int spinPct);

// Start streaming a G-code file from SD card.
// path must be an absolute SD path, e.g. "/gcode/part.nc".
// Returns false if already streaming or file cannot be opened.
bool grblStreamFile(uint8_t usb_slot, const char* path);

// Start streaming from pre-loaded PSRAM buffer (no SD needed).
// Data must already be in the PSRAM G-code storage (grblPsramGcodeStore).
// Returns false if already streaming or no data loaded.
bool grblStreamPsramData(uint8_t usb_slot, const char* label);

// Start a firmware-backed local queue job from raw multi-line G-code text.
// This job is executed by the ESP32 and continues even if web pages are closed.
// Returns false if queue is empty, too large, or another job is running.
bool grblQueueStart(uint8_t usb_slot, const char* gcodeText);

// Stop/cancel only the firmware-backed local queue job.
// Returns false if there is no active local queue job.
bool grblQueueStop();

// True when current active job source is the firmware local queue.
bool grblQueueIsActive();

// Returns true when current active job source is SD file stream.
bool grblStreamIsActive();

// Runtime metadata helpers for UI state rehydration.
const char* grblGetJobSourceName();
const char* grblGetWorkflowStateName();
uint32_t grblGetStatusPollInterval();
uint8_t grblGetStatusPollOutstanding();
uint32_t grblGetStatusLagMs();
uint32_t grblGetBlockedByInflightCount();
uint32_t grblGetBlockedByReportedRxCount();
uint32_t grblGetBlockedByFifoFullCount();
uint32_t grblGetBlockedByCriticalCommandCount();
uint32_t grblGetBlockedBySafetyMarginCount();
uint16_t grblGetAvgLineBytes();
uint8_t grblGetBurstLinesSent();
uint32_t grblGetBurstCycles();
const char* grblGetLastBlockReason();
const char* grblGetLastBlockLine();
void grblClearBlockReason();
uint32_t grblGetLastBlockMs();
uint8_t grblGetRecentErrorCount();
bool grblGetRecentErrorAt(uint8_t index, GrblRecentError* out);

// Alarm snapshot helpers.
bool grblIsAlarmActive();
const char* grblGetAlarmMessage();

// Last parsed machine-status report helpers (from '<...>' lines).
bool grblHasMachineStatus();
bool grblIsDoorOpen();
bool grblIsProbeTriggered();
const char* grblGetMachineStateName();
bool grblCopyLastStatusRaw(char* out, size_t outSize);
uint16_t grblGetPlannerBufferFree();
uint16_t grblGetRxBufferFree();
uint8_t grblGetAxisCount();
bool grblGetMachinePosition(float* x, float* y, float* z);
bool grblGetMaxTravel(float* x, float* y, float* z);
bool grblGetWorkPosition(float* x, float* y, float* z);

// Runtime telemetry helpers.
uint16_t grblGetCurrentCmdPerMin();
uint16_t grblGetAverageCmdPerMin();
uint32_t grblGetEtaSeconds();
uint32_t grblGetActiveElapsedMs();
const char* grblGetPauseReason();

// Local queue execution controls.
void grblSetQueueExecutionOptions(bool continueOnError, uint8_t retryMax);

// Auto-detect GRBL baud rate for a connected USB-CDC slot.
// Cycles through common rates (115200, 57600, 38400, 19200, 9600, …),
// sends '?' at each rate, and returns the first rate that yields a valid
// GRBL status frame.  Returns 0 when no rate responds.
uint32_t grblAutoDetectBaud(uint8_t slot);

// Configure GRBL to use a HardwareSerial UART port (e.g. Serial2 on
// GPIO16/17) instead of USB Host CDC-ACM.
void grblSetUartTransport(HardwareSerial& port, int rxPin = 16, int txPin = 17, int baud = 115200);

// Select which compiled-in transport GRBL talks through (UART default, USB =
// OTG host). Switching mid-job is rejected (returns false); the selection is
// session-only and resets to UART on every boot.
bool grblSetTransportMode(GrblTransportMode mode);
GrblTransportMode grblGetTransportMode();

// Set the logical origin of the current job session (e.g., UI session id).
void grblSetJobOrigin(const char* origin);

// Return global job session snapshot.
GrblJobSession grblGetJobSession();

// Return the WCS (work coordinate) position captured at job start.
// Returns true and fills x/y/z if a valid position was recorded; false otherwise.
bool grblGetJobStartWorkPos(float* x, float* y, float* z);

// Pause the current stream (STREAMING → PAUSED).
void grblPause();

// Resume a paused stream (PAUSED → STREAMING).
// Returns false when resume is blocked by a safety hold.
bool grblResume();
GrblResumeLaunch grblResumeFromAlarm();
// Last resume-flow error text (empty when none). Read-only snapshot.
const char* grblResumeErrorText();

// Cancel the current stream immediately and close the file.
void grblCancel();

// Return a read-only snapshot of the current sender state.
GrblStatus grblGetStatus();

// Feed a '?' status query to GRBL every N ms to get machine position.
// Interval 0 disables polling. Default: 250 ms while streaming.
void grblSetStatusPollInterval(uint32_t ms);

// ── Tool Height Map / Probe Grid ─────────────────────────────────────────

struct ProbeGridConfig {
    float startX = 0;       // Grid start X (mm)
    float startY = 0;       // Grid start Y (mm)
    float endX   = 100;     // Grid end X (mm)
    float endY   = 100;     // Grid end Y (mm)
    float spacing = 10;     // Step between points (mm)
    float probeFeed  = 50;  // Probe feed rate (mm/min)
    float probeMaxDepth = 20; // Max Z depth for probe (mm)
    float safeZ = 5;        // Safe Z height for XY moves (mm)
    float clearHeight = 10; // Height to retract to after probe (mm)
};

#define PROBE_GRID_MAX_POINTS 256

struct ProbeGridPoint {
    float x; // Grid X position (mm)
    float y; // Grid Y position (mm)
    float z; // Measured Z surface height (mm)
    bool  valid; // true if probe completed successfully
};

enum class ProbeGridPhase : uint8_t {
    IDLE,
    MOVING_XY,      // G0 to next grid XY
    PROBING,        // G38.2 Z probe sent, waiting for trigger
    RETRACTING,     // G0 Z to safe height after probe
    COMPLETE,       // All points probed
    CANCELLED,
    ERROR
};

// Configure and start a probe grid cycle. Returns false if grid has no points
// or a probe cycle is already running. The probing runs asynchronously via
// grblSenderUpdate() and does not block.
bool grblProbeGridStart(uint8_t usbSlot, const ProbeGridConfig& config);

// Cancel an in-progress probe grid cycle.
void grblProbeGridCancel();

// Returns the current probe grid phase and progress info.
ProbeGridPhase grblGetProbeGridPhase();
uint16_t grblGetProbeGridTotal();
uint16_t grblGetProbeGridCompleted();
const ProbeGridConfig* grblGetProbeGridConfig();
const ProbeGridPoint* grblGetProbeGridPoints(uint16_t* count);

// Enable/disable Z compensation based on the height map.
// When enabled, every G1 Z move is adjusted by the interpolated surface height.
void grblSetHeightMapEnabled(bool enabled);
bool grblGetHeightMapEnabled();

// Clear all probed points (reset height map).
void grblClearHeightMap();

// PSRAM feeder task — high-priority dedicated task that keeps the GRBL
// RX buffer full at all times. Automatically started/stopped by
// grblStreamFile() / grblCancel(). Safe to call manually for tuning.
void grblStartPsramFeedTask();
void grblStopPsramFeedTask();

// Job trace log control. Enable/disable via GRBLLOG command.
// Returns current enabled state after the call.
bool grblJobLogSetEnabled(bool enable);
bool grblJobLogIsEnabled();

#else // ENABLE_GRBL == 0 — compile-time stubs

enum class GrblTransportMode : uint8_t { UART = 0, USB = 1 };
enum class GrblStreamState : uint8_t { IDLE, STREAMING, PAUSED, DONE, ERROR, CANCELLED };
struct GrblStatus { GrblStreamState state; uint32_t lines_sent; uint32_t lines_total;
                    uint32_t lines_acked; uint32_t error_count;
                    uint8_t bytes_in_flight; uint8_t usb_slot;
                     char file_path[128];
                    bool resume_available; uint32_t resume_line; uint32_t resume_total;
                    bool resume_busy; char resume_file[128]; char resume_alarm[64];
                    char resume_error[96]; };

// Stub definitions for types used by ENABLE_GRBL=0 callers.
struct ProbeGridConfig {
    float startX = 0, startY = 0, endX = 100, endY = 100;
    float spacing = 10, probeFeed = 50, probeMaxDepth = 20;
    float safeZ = 5, clearHeight = 10;
};
struct ProbeGridPoint { float x, y, z; bool valid; };
enum class ProbeGridPhase : uint8_t { IDLE, MOVING_XY, PROBING, RETRACTING, COMPLETE, CANCELLED, ERROR };

static inline void        grblSenderInit()                               {}
static inline void        grblSenderUpdate()                             {}
static inline void        grblOnRxData(const uint8_t*, size_t, uint8_t) {}
static inline bool        grblSend(uint8_t, const char*)                 { return false; }
static inline bool        grblSetOverride(uint8_t, int, int)             { return false; }
static inline bool        grblStreamFile(uint8_t, const char*)           { return false; }
static inline bool        grblStreamPsramData(uint8_t, const char*)      { return false; }
static inline bool        grblQueueStart(uint8_t, const char*)           { return false; }
static inline GrblResumeLaunch grblResumeFromAlarm()                     { return GrblResumeLaunch::NO_RESUME; }
static inline const char* grblResumeErrorText()                          { return ""; }
static inline bool        grblQueueStop()                                { return false; }
static inline bool        grblQueueIsActive()                            { return false; }
static inline bool        grblStreamIsActive()                           { return false; }
static inline const char* grblGetJobSourceName()                         { return "none"; }
static inline const char* grblGetWorkflowStateName()                     { return "idle"; }
static inline uint32_t    grblGetStatusPollInterval()                    { return 0; }
static inline uint8_t     grblGetStatusPollOutstanding()                 { return 0; }
static inline uint32_t    grblGetStatusLagMs()                           { return 0; }
static inline uint32_t    grblGetBlockedByInflightCount()                { return 0; }
static inline uint32_t    grblGetBlockedByReportedRxCount()              { return 0; }
static inline uint32_t    grblGetBlockedByFifoFullCount()                { return 0; }
static inline uint32_t    grblGetBlockedByCriticalCommandCount()         { return 0; }
static inline uint32_t    grblGetBlockedBySafetyMarginCount()            { return 0; }
static inline const char* grblGetLastBlockReason()                       { return ""; }
static inline const char* grblGetLastBlockLine()                         { return ""; }
static inline void        grblClearBlockReason()                         {}
static inline uint32_t    grblGetLastBlockMs()                           { return 0; }
static inline uint8_t     grblGetRecentErrorCount()                      { return 0; }
static inline bool        grblGetRecentErrorAt(uint8_t, GrblRecentError*) { return false; }
static inline bool        grblJobLogSetEnabled(bool)                     { return false; }
static inline bool        grblJobLogIsEnabled()                          { return false; }
static inline bool        grblIsAlarmActive()                            { return false; }
static inline const char* grblGetAlarmMessage()                          { return ""; }
static inline bool        grblHasMachineStatus()                         { return false; }
static inline bool        grblIsDoorOpen()                               { return false; }
static inline bool        grblIsProbeTriggered()                         { return false; }
static inline const char* grblGetMachineStateName()                      { return "unknown"; }
static inline bool        grblCopyLastStatusRaw(char*, size_t)           { return false; }
static inline uint16_t    grblGetPlannerBufferFree()                     { return 0; }
static inline uint16_t    grblGetRxBufferFree()                          { return 0; }
static inline bool        grblGetMachinePosition(float*, float*, float*)  { return false; }
static inline uint8_t     grblGetAxisCount()                              { return 3; }
static inline bool        grblGetMaxTravel(float*, float*, float*)         { return false; }
static inline bool        grblGetWorkPosition(float*, float*, float*)     { return false; }
static inline bool        grblGetJobStartWorkPos(float*, float*, float*)  { return false; }
static inline uint16_t    grblGetCurrentCmdPerMin()                      { return 0; }
static inline uint16_t    grblGetAverageCmdPerMin()                      { return 0; }
static inline uint32_t    grblGetEtaSeconds()                            { return 0; }
static inline uint32_t    grblGetActiveElapsedMs()                       { return 0; }
static inline const char* grblGetPauseReason()                           { return ""; }
static inline void        grblSetQueueExecutionOptions(bool, uint8_t)    {}
static inline void        grblSetJobOrigin(const char*)                  {}
static inline GrblJobSession grblGetJobSession()                         { return {}; }
static inline void        grblPause()                                    {}
static inline bool        grblResume()                                   { return false; }
static inline void        grblCancel()                                   {}
static inline GrblStatus  grblGetStatus()                                { return {}; }
static inline void        grblSetStatusPollInterval(uint32_t)            {}
static inline uint32_t    grblAutoDetectBaud(uint8_t)                    { return 0; }
static inline void        grblSetUartTransport(HardwareSerial&, int = 16, int = 17, int = 115200) {}
static inline bool        grblSetTransportMode(GrblTransportMode)        { return false; }
static inline GrblTransportMode grblGetTransportMode()                   { return GrblTransportMode::UART; }
static inline void        grblStartPsramFeedTask()                       {}
static inline void        grblStopPsramFeedTask()                        {}
static inline bool        grblProbeGridStart(uint8_t, const ProbeGridConfig&) { return false; }
static inline void        grblProbeGridCancel()                          {}
static inline ProbeGridPhase grblGetProbeGridPhase()                     { return ProbeGridPhase::IDLE; }
static inline uint16_t    grblGetProbeGridTotal()                        { return 0; }
static inline uint16_t    grblGetProbeGridCompleted()                    { return 0; }
static inline const ProbeGridConfig* grblGetProbeGridConfig()            { return nullptr; }
static inline const ProbeGridPoint* grblGetProbeGridPoints(uint16_t*)    { return nullptr; }
static inline void        grblSetHeightMapEnabled(bool)                  {}
static inline bool        grblGetHeightMapEnabled()                      { return false; }
static inline void        grblClearHeightMap()                           {}

#endif // ENABLE_GRBL
