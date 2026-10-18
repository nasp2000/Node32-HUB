// grbl_sender_internal.h
// Internal shared declarations for the GRBL sender split compilation units.
// This header is included ONLY by grbl_sender*.cpp files — never by user code.
#pragma once
#include "grbl_sender.h"

#if ENABLE_GRBL == 1

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "direct_log.h"
#include "usb_host_manager.h"

// Shared extern declarations for all grbl_sender_*.cpp files
extern void sendVirtualMessage(const String& msg, int msgType,
                                  bool alreadyFormatted, bool bypassDedup);
extern void grblWebPublishConsoleLine(const String& line) __attribute__((weak));
extern void grblWebPublishProductionEvent(const char* type, const String& message) __attribute__((weak));
extern void grblDebugLog(const char* fmt, ...);
extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity = true);

// GRBL transport selection. S3 CNC: USB Host only (lazy start, UART disabled);
// P4 CNC: USB Host only. Session-only — host is off after every reboot.
extern HardwareSerial* s_grblUart;
extern GrblTransportMode s_grblTransport;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define GRBL_INFLIGHT_FIFO_SIZE   64
#define GRBL_RX_BUFFER_STRICT_LIMIT 127
// Sustained streaming allowance used by the PSRAM feeder for the WHOLE job.
// It must exceed GRBL's 128-byte RX buffer so the sender can pre-fill GRBL's
// planner block buffer (BLOCK_BUFFER_SIZE=18) and keep lookahead full. GRBL
// acks "ok" as soon as a line drains RX->planner, so s_inflight (RX + planner
// resident, unacked bytes) safely exceeds 127 without overflowing the RX ring.
// A 127-byte cap starves the planner: when s_inflight nears 120 and the next
// line does not fit in the ~7 leftover bytes, the feeder deadlocks because GRBL
// stops acking during real motion. 200 bytes keeps the planner fed.
#define GRBL_RX_STREAM_INFLIGHT_LIMIT 200
// Startup burst: identifier kept for compatibility in the cold-start prefill.
#define GRBL_RX_STARTUP_BURST_LIMIT GRBL_RX_STREAM_INFLIGHT_LIMIT
#define GRBL_RX_SAFETY_MARGIN_FILE_STREAM 0
#define GRBL_RX_SAFETY_MARGIN_FILE_STREAM_STABLE 0
#define GRBL_RX_SAFETY_MARGIN_LOCAL_QUEUE 10

#define GRBL_STATUS_POLL_DEFAULT_MS  500UL
#define GRBL_STATUS_POLL_MIN_MS      100UL
#define GRBL_STATUS_POLL_STREAMING_MS 500UL
#define GRBL_STATUS_POLL_ADAPTIVE_SOFT_MS 1000UL
#define GRBL_STATUS_POLL_ADAPTIVE_HARD_MS 2000UL
#define GRBL_STATUS_POLL_ADAPTIVE_SOFT_CPM 60U
#define GRBL_STATUS_POLL_ADAPTIVE_HARD_CPM 30U
#define GRBL_STATUS_POLL_MAX_OUTSTANDING 4U
#define GRBL_STATUS_FRESH_WINDOW_MS 1200UL
#define GRBL_POSITION_VALID_WINDOW_MS 180000UL

#define GRBL_RX_STALL_TIMEOUT_MS 30000UL
#define GRBL_ACK_STALL_TIMEOUT_MS 5000UL
#define GRBL_ACK_STALL_BLOCK_WINDOW_MS 3000UL
#define GRBL_DONE_STABLE_WINDOW_MS 250UL

#define GRBL_PSRAM_BURST_MIN_LINES      3U
#define GRBL_PSRAM_BURST_MAX_LINES     128U
#define GRBL_PSRAM_BURST_START_LINES    24U
#define GRBL_PSRAM_TARGET_BURST_BYTES   120U
#define GRBL_PSRAM_INFLIGHT_TARGET      100U
#define GRBL_PSRAM_AVG_LINE_ALPHA       4U
#define GRBL_PSRAM_FEED_NOTIFY_TIMEOUT_MS  1U
#define GRBL_PSRAM_BURST_YIELD_TICKS    1U

#define GRBL_STREAM_REFILL_CHUNK_BYTES   65536UL
#define GRBL_STREAM_LOW_WATERMARK_BYTES  262144UL
#define GRBL_STREAM_STAGE_MIN_BYTES       1048576UL
#define GRBL_STREAM_STAGE_MAX_BYTES       8388608UL
#define GRBL_STREAM_STAGE_ALIGN_BYTES     65536UL
#define GRBL_STREAM_STAGE_TARGET_DIV      4UL
#define GRBL_STREAM_LOW_WATERMARK_MIN_BYTES 262144UL
#define GRBL_STREAM_LOW_WATERMARK_MAX_BYTES 2097152UL
#define GRBL_STREAM_REFILL_MIN_BYTES      65536UL
#define GRBL_STREAM_REFILL_MAX_BYTES      1048576UL
#define GRBL_PREPARED_LINE_QUEUE_CAP     128
#define GRBL_FILE_STREAM_RETRY_MAX        6
#define GRBL_SD_READ_STALL_TIMEOUT_MS    5000UL
#define GRBL_FEED_STALL_TIMEOUT_MS       5000UL
#define GRBL_USB_SEND_RETRY_MAX_ATTEMPTS   12U
#define GRBL_STATUS_POLL_STARTUP_MS      1200UL

#define GRBL_STREAM_BURST_START_ACK_LINES    1200UL
#define GRBL_STREAM_BURST_RECOVERY_ACK_LINES 400UL

// ---------------------------------------------------------------------------
// Internal enums
// ---------------------------------------------------------------------------
enum class GrblJobSource : uint8_t {
    NONE,
    FILE_STREAM,
    LOCAL_QUEUE
};

enum class GrblWorkflowState : uint8_t {
    IDLE,
    RUNNING,
    PAUSED,
    HOLD,
    CANCELLING,
    DONE,
    ERROR
};

// ---------------------------------------------------------------------------
// Runtime buffers struct
// ---------------------------------------------------------------------------
struct GrblRuntimeBuffers {
    char filePath[GRBL_MAX_PATH_LEN];
    char alarmMessage[GRBL_MAX_LINE_LEN + 4];
    char machineStateName[16];
    char lastStatusRaw[GRBL_MAX_LINE_LEN + 1];
    char jobOrigin[48];
    char pauseReason[48];
    char errorReason[96];
    char lastSentLine[GRBL_MAX_LINE_LEN + 1];
    char localCurrentLine[GRBL_MAX_LINE_LEN + 1];
    char fileRetryLine[GRBL_MAX_LINE_LEN + 1];
    char localQueueBuf[GRBL_LOCAL_QUEUE_MAX_BYTES];
    GrblRecentError recentErrors[GRBL_RECENT_ERROR_HISTORY];
    uint8_t fifo[GRBL_INFLIGHT_FIFO_SIZE];
    char fifoLines[GRBL_INFLIGHT_FIFO_SIZE][GRBL_MAX_LINE_LEN + 1];
    char rxBuf[GRBL_MAX_LINE_LEN + 4];
    char preparedLines[GRBL_PREPARED_LINE_QUEUE_CAP][GRBL_MAX_LINE_LEN + 1];
    uint16_t preparedLens[GRBL_PREPARED_LINE_QUEUE_CAP];
    uint8_t fileRetryCount;
};

// ---------------------------------------------------------------------------
// Extern state declarations — defined in grbl_sender.cpp
// ---------------------------------------------------------------------------
extern GrblStreamState s_state;
extern GrblJobSource   s_jobSource;
extern uint8_t         s_slot;
extern uint16_t        s_inflight;

extern char*           s_streamPsramBuf;
extern size_t          s_streamPsramCap;
extern size_t          s_streamPsramLen;
extern size_t          s_streamPsramPos;
extern bool            s_streamPsramEof;

extern TaskHandle_t    s_psramFeedTask;
extern bool            s_psramFeedTaskStop;

extern uint32_t        s_linesSent;
extern uint32_t        s_linesTotal;
extern uint32_t        s_linesAcked;
extern uint32_t        s_errorCount;
extern bool            s_alarmActive;
extern bool            s_hasMachineStatus;
extern bool            s_hasMachinePosition;
extern bool            s_hasWorkPosition;
extern bool            s_doorOpen;
extern bool            s_probeTriggered;
extern uint16_t        s_plannerBufferFree;
extern uint16_t        s_rxBufferFree;
extern float           s_machinePos[3];
extern float           s_workPos[3];
extern GrblWorkflowState s_workflowState;
extern uint32_t        s_cancellingSinceMs;
extern uint32_t        s_jobStartMs;
extern uint32_t        s_jobEndMs;
extern uint32_t        s_holdAccumMs;
extern uint32_t        s_holdStartMs;
extern uint32_t        s_rateLastMs;
extern uint32_t        s_rateLastAck;
extern uint16_t        s_currentCmdPerMin;
extern uint32_t        s_blockedByInflight;
extern uint32_t        s_blockedByReportedRx;
extern uint32_t        s_blockedByFifoFull;
extern uint32_t        s_blockedByCriticalCommand;
extern uint32_t        s_blockedBySafetyMargin;
extern char            s_lastBlockReason[32];
extern char            s_lastBlockLine[GRBL_MAX_LINE_LEN + 1];
extern uint32_t        s_lastBlockMs;
extern uint16_t        s_avgLineBytes;
extern uint8_t         s_burstLinesSent;
extern uint32_t        s_burstCycles;
extern bool            s_queueContinueOnError;
extern uint8_t         s_queueRetryMax;
extern bool            s_localCurrentLineActive;
extern uint8_t         s_localCurrentRetry;
extern size_t          s_localQueueLen;
extern size_t          s_localQueuePos;
extern uint32_t        s_fileRecoveryAckUntil;
extern uint32_t        s_lastUsbTxZeroMs;
extern uint32_t        s_lastAckProgressMs;
// Alarm resume ("restart from line N") — defined in grbl_sender.cpp.
extern bool            s_resumeAvailable;
extern GrblJobSource   s_resumeSource;
extern char            s_resumePath[128];
extern uint32_t        s_resumeTotalLines;
extern uint32_t        s_resumeAckedLines;
extern uint32_t        s_resumeFileSize;
extern bool            s_resumeFromPsram;
extern char            s_resumeAlarm[64];
extern bool            s_resumeSkipArmed;
extern char            s_resumeSkipPath[128];
extern uint32_t        s_resumeDropLines;
extern bool            s_resumeDropArmed;
extern bool            s_resumeBusy;
extern bool            s_resumeAbort;
extern char            s_resumeError[96];

// Alarm resume ("restart from line N"). Snapshot: call with s_mutex HELD,
// before the alarm wipe in processGrblResponseLine. Consume: call inside the
// (re)starts (mutex held there) with the start's source path; applies the
// pending line skip when it matches, else fully clears stale resume state.
void grblCaptureResumeSnapshotLocked();
bool grblConsumeResumeSkipLocked(const char* startPath);
extern uint32_t        s_lastCompletionLog;
extern size_t          s_lastFeedPos;
extern uint32_t        s_lastFeedProgressMs;
extern bool            s_jobSawNonZeroMachinePos;
extern bool            s_jobSawNonZeroWorkPos;
extern uint8_t         s_detectedAxisCount;

extern bool            s_autoDetectActive;
extern bool            s_autoDetectFound;
extern TaskHandle_t    s_autoDetectTask;

extern uint8_t         s_lastConnectedMask;

// ── Tool change (M6 pause) ───────────────────────────────────────────────
extern bool s_toolChangePending;

// ── Probe grid state ─────────────────────────────────────────────────────
extern ProbeGridConfig  s_probeGridConfig;
extern ProbeGridPhase   s_probeGridPhase;
extern ProbeGridPoint   s_probeGridPoints[PROBE_GRID_MAX_POINTS];
extern uint16_t         s_probeGridTotal;
extern uint16_t         s_probeGridIndex;
extern uint16_t         s_probeGridCols;
extern uint8_t          s_probeGridSlot;
extern bool             s_heightMapEnabled;
extern uint32_t         s_probeGridWaitStartMs;

// Apply height-map compensation to one G-code line in place. Only G0/G1/G2/G3
// motion commands that carry a Z word are touched; returns the new line length
// (unchanged when nothing was modified). `line` must be a buffer of at least
// GRBL_MAX_LINE_LEN+1 bytes. Must be called with s_mutex held (it tracks the
// last commanded XY across the streamed path).
size_t grblApplyHeightMapToLine(char* line, size_t lineLen);
// Forget the tracked last-commanded XY (call at job start so a new file does
// not inherit the previous job's position).
void grblHeightMapResetTracker();

#if ENABLE_SDCARD == 1
#include "sd_lib.h"
extern File s_file;
#endif

extern GrblRuntimeBuffers* s_runtimeBuffers;
extern bool s_runtimeBuffersReady;
extern bool s_runtimeBuffersErrorNotified;
extern portMUX_TYPE s_runtimeAllocMux;
extern char* s_streamStageBuf;
extern bool s_streamStageReady;
extern bool s_streamStageErrorNotified;
extern size_t s_streamStageFill;
extern size_t s_streamStageReadPos;
extern bool s_streamStageEof;
extern uint32_t s_streamStageLastProgressMs;
extern size_t s_streamStageCapacity;
extern size_t s_streamRefillChunkBytes;
extern size_t s_streamLowWatermarkBytes;
extern uint16_t s_preparedHead;
extern uint16_t s_preparedTail;
extern uint16_t s_preparedCount;

extern uint8_t s_fifoHead;
extern uint8_t s_fifoTail;
extern uint8_t s_fifoCount;
extern uint32_t s_pollIntervalMs;
extern uint32_t s_lastPollMs;
extern uint8_t  s_pollOutstanding;
extern SemaphoreHandle_t s_mutex;
extern uint8_t s_rxLen;
extern uint8_t s_recentErrorHead;
extern uint8_t s_recentErrorCount;
extern uint32_t s_lastStatusMs;
extern uint32_t s_lastPositionMs;
extern uint32_t s_lastRxMs;
extern uint32_t s_doneStableSinceMs;

// Runtime buffer field macros
#define s_filePath         (s_runtimeBuffers->filePath)
#define s_alarmMessage     (s_runtimeBuffers->alarmMessage)
#define s_machineStateName (s_runtimeBuffers->machineStateName)
#define s_lastStatusRaw    (s_runtimeBuffers->lastStatusRaw)
#define s_jobOrigin        (s_runtimeBuffers->jobOrigin)
#define s_pauseReason      (s_runtimeBuffers->pauseReason)
#define s_errorReason      (s_runtimeBuffers->errorReason)
#define s_lastSentLine     (s_runtimeBuffers->lastSentLine)
#define s_localCurrentLine (s_runtimeBuffers->localCurrentLine)
#define s_fileRetryLine    (s_runtimeBuffers->fileRetryLine)
#define s_localQueueBuf    (s_runtimeBuffers->localQueueBuf)
#define s_recentErrors     (s_runtimeBuffers->recentErrors)
#define s_fifo             (s_runtimeBuffers->fifo)
#define s_fifoLines        (s_runtimeBuffers->fifoLines)
#define s_rxBuf            (s_runtimeBuffers->rxBuf)
#define s_preparedLines    (s_runtimeBuffers->preparedLines)
#define s_preparedLens     (s_runtimeBuffers->preparedLens)
#define s_fileRetryCount   (s_runtimeBuffers->fileRetryCount)

// ---------------------------------------------------------------------------
// Transport layer (grbl_sender_transport.cpp)
// ---------------------------------------------------------------------------
size_t grblTransportWrite(uint8_t slot, const uint8_t* data, size_t len);
size_t grblTransportWriteStr(uint8_t slot, const char* str);
bool grblTransportSetBaud(uint8_t slot, uint32_t baud);
UsbCdcDevice grblTransportGetDevice(uint8_t slot);
bool grblTransportIsRunning();
void grblTransportPollRx();

// ---------------------------------------------------------------------------
// Internal function prototypes
// ---------------------------------------------------------------------------

// Stream stage helpers
size_t clampSizeT(size_t value, size_t minValue, size_t maxValue);
size_t alignDownSizeT(size_t value, size_t align);
void configureStreamStageWatermarksLocked();
void clearPreparedQueue();
bool preparedQueueIsFull();
bool preparedQueueIsEmpty();
bool preparedQueuePush(const char* line, size_t len);
bool preparedQueuePeek(const char** line, size_t* len);
bool preparedQueuePop();
bool ensureRuntimeBuffers();
bool ensureStreamStageBuffer();
void resetStreamStageState();
size_t streamStageBytesAvailable();
void compactStreamStageIfNeeded();
bool peekStreamStageRawLine(char* out, size_t outSize, size_t* consumedBytes, bool* needMoreData, bool* lineTooLong);
void consumeStreamStageBytes(size_t consumedBytes);
#if ENABLE_SDCARD == 1
size_t fillStreamStageFromFile();
int fillPreparedQueueFromStage();
#endif

// FIFO
void fifoPush(uint8_t lineBytes, const char* line);
uint8_t fifoPop(char* lineOut, size_t lineOutSize);
bool firstConnectedUsbSlot(uint8_t* outSlot);
uint8_t resolveConnectedUsbSlot(uint8_t preferred);

// Misc helpers
bool isTripletNearZero(float x, float y, float z);
bool hasFreshPositionLocked();
bool isSyntheticZeroPositionLocked(bool machinePos, float x, float y, float z);
void pushRecentErrorLocked(const char* message, const char* source, const char* preview);
const char* workflowStateName(GrblWorkflowState state);
const char* jobSourceName(GrblJobSource source);
void publishConsoleLine(const String& msg);
void publishProductionEvent(const char* type, const String& msg);
#include "grbl_line_parser.h"

// Line sending
bool sendLineLocked(const char* line, size_t len);
bool sendRealtimeLocked(char c);
size_t sendUsbPayloadWithRetry(uint8_t useSlot, const uint8_t* data, size_t len);

// Local queue
uint32_t localQueueCountLines(const char* text);
bool localQueueReadNextRawLine(char* out, size_t outSize, size_t* lineStart_pos);

// Workflow
bool isJobRuntimeActiveLocked();
uint32_t jobEffectiveElapsedMsLocked(uint32_t nowMs);
void recordBlockReasonLocked(const char* reason, const char* line);
bool isWorkflowTransitionAllowed(GrblWorkflowState from, GrblWorkflowState to);
void setWorkflowStateLocked(GrblWorkflowState nextState, const char* reason);

// Status parsing
bool extractTripletFieldCI(const char* line, const char* tokenNoColon, float* x, float* y, float* z);
void parseMachineStatusLine(const char* line);

// RX processing
void processGrblResponseLine(const char* line);

// Job logging (grbl_sender_joblog.cpp)
#if ENABLE_SDCARD == 1
void jobLogEventLocked(const char* ev, const char* detail);
bool jobLogOpenLocked(const char* kind, const char* sourcePath);
void jobLogCloseLocked(const char* finalEvent, const char* reason);
void jobLogTickLocked();
void jobLogFlush(bool force);
#else
static inline void jobLogEventLocked(const char*, const char*) {}
static inline void jobLogTickLocked() {}
static inline bool jobLogOpenLocked(const char*, const char*) { return false; }
static inline void jobLogCloseLocked(const char*, const char*) {}
#endif

// PSRAM streaming
void psramFeedNotify();

// Auto baud
uint32_t grblAutoDetectBaud(uint8_t slot);

// Probe grid executor (grbl_sender_probe.cpp)
void probeGridUpdate();

// Job-log state (defined in grbl_sender_joblog.cpp)
#define GRBL_JOB_LOG_BLOCK_THROTTLE_MS 250UL
extern bool s_jobLogActive;
extern bool s_jobLogEnabled;
extern bool s_jobStartWorkPosValid;
extern float s_jobStartWorkPos[3];
extern uint32_t s_jobLogLastBlockMs;
extern char s_jobLogLastBlockReason[32];

#endif // ENABLE_GRBL == 1
