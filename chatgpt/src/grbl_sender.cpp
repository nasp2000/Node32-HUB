// grbl_sender.cpp
// GRBL G-code sender — residual file: state definitions + helpers not extracted.
#include "feature_flags.h"
#include "grbl_sender_internal.h"

#if ENABLE_GRBL == 1

#include <Arduino.h>
#include <stdarg.h>
#include <ctype.h>

#include "usb_host_manager.h"
#include "direct_log.h"
#include "commands.h"
#include <HardwareSerial.h>

#include "psram_helpers.h"
#include "ConfigManager.h"
#include "grbl_psram_gcode.h"
#include <Preferences.h>

extern ConfigManager configManager;
extern void sendVirtualMessage(const String& msg, int msgType,
                                  bool alreadyFormatted, bool bypassDedup);
extern void injectDataToRXBuffer(const String& data, const String& source, bool resetInactivity);
extern void grblWebPublishConsoleLine(const String& line) __attribute__((weak));
extern void grblWebPublishProductionEvent(const char* type, const String& message) __attribute__((weak));
extern void wsGrblLoop() __attribute__((weak));
extern void grblDebugLog(const char* fmt, ...);

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

// ---------------------------------------------------------------------------
// State definitions — declared extern in grbl_sender_internal.h
// ---------------------------------------------------------------------------

GrblStreamState s_state     = GrblStreamState::IDLE;
GrblJobSource   s_jobSource = GrblJobSource::NONE;
uint8_t         s_slot      = 0;
uint16_t        s_inflight  = 0;

char*           s_streamPsramBuf  = nullptr;
size_t          s_streamPsramCap  = 0;
size_t          s_streamPsramLen  = 0;
size_t          s_streamPsramPos  = 0;
bool            s_streamPsramEof  = false;

TaskHandle_t    s_psramFeedTask   = nullptr;
bool            s_psramFeedTaskStop = false;

uint32_t        s_linesSent = 0;
uint32_t        s_linesTotal= 0;
uint32_t        s_linesAcked= 0;
uint32_t        s_errorCount= 0;
bool            s_alarmActive = false;
bool            s_hasMachineStatus = false;
bool            s_hasMachinePosition = false;
bool            s_hasWorkPosition = false;
bool            s_doorOpen = false;
bool            s_probeTriggered = false;
bool            s_toolChangePending = false;
uint16_t        s_plannerBufferFree = 0;
uint16_t        s_rxBufferFree = 0;
float           s_machinePos[3] = {0.0f, 0.0f, 0.0f};
float           s_workPos[3] = {0.0f, 0.0f, 0.0f};
GrblWorkflowState s_workflowState = GrblWorkflowState::IDLE;
uint32_t        s_cancellingSinceMs = 0;
uint32_t        s_jobStartMs = 0;
uint32_t        s_jobEndMs = 0;
uint32_t        s_holdAccumMs = 0;
uint32_t        s_holdStartMs = 0;
uint32_t        s_rateLastMs = 0;
uint32_t        s_rateLastAck = 0;
uint16_t        s_currentCmdPerMin = 0;
uint32_t        s_blockedByInflight = 0;
uint32_t        s_blockedByReportedRx = 0;
uint32_t        s_blockedByFifoFull = 0;
uint32_t        s_blockedByCriticalCommand = 0;
uint32_t        s_blockedBySafetyMargin = 0;
char            s_lastBlockReason[32] = "";
char            s_lastBlockLine[GRBL_MAX_LINE_LEN + 1] = "";
uint32_t        s_lastBlockMs = 0;
uint16_t        s_avgLineBytes = 30U;
uint8_t         s_burstLinesSent = 0;
uint32_t        s_burstCycles = 0;
bool            s_queueContinueOnError = false;
uint8_t         s_queueRetryMax = 0;
bool            s_localCurrentLineActive = false;
uint8_t         s_localCurrentRetry = 0;
size_t          s_localQueueLen = 0;
size_t          s_localQueuePos = 0;
uint32_t        s_fileRecoveryAckUntil = 0;
uint32_t        s_lastUsbTxZeroMs = 0;
uint32_t        s_lastAckProgressMs = 0;
uint32_t        s_lastCompletionLog = 0;
size_t          s_lastFeedPos = 0;
uint32_t        s_lastFeedProgressMs = 0;
bool            s_jobSawNonZeroMachinePos = false;
bool            s_jobSawNonZeroWorkPos = false;
uint8_t         s_detectedAxisCount = 3;

// ---- Alarm resume ("restart from line N") ----
// Snapshot taken on ALARM while a job was active; consumed by the assisted
// resume flow (unlock -> home -> modal preamble -> restart with line offset).
// Plain statics (small); cleared on new job start / cancel.
bool            s_resumeAvailable = false;
GrblJobSource   s_resumeSource = GrblJobSource::NONE;
char            s_resumePath[128] = "";
uint32_t        s_resumeTotalLines = 0;
uint32_t        s_resumeAckedLines = 0;
uint32_t        s_resumeFileSize = 0;
bool            s_resumeFromPsram = false;
char            s_resumeAlarm[64] = "";
// Skip machinery, armed by the resume orchestrator just before (re)starting:
// s_resumeSkipArmed + path gate the one-shot; s_resumeDropLines counts down
// the already-executed lines inside the feeders.
bool            s_resumeSkipArmed = false;
char            s_resumeSkipPath[128] = "";
uint32_t        s_resumeDropLines = 0;
bool            s_resumeDropArmed = false;
// Orchestrator state (background task).
bool            s_resumeBusy = false;
bool            s_resumeAbort = false;
char            s_resumeError[96] = "";

bool            s_autoDetectActive = false;
bool            s_autoDetectFound = false;
TaskHandle_t    s_autoDetectTask = nullptr;

uint8_t         s_lastConnectedMask = 0;

GrblRuntimeBuffers* s_runtimeBuffers = nullptr;
bool s_runtimeBuffersReady = false;
bool s_runtimeBuffersErrorNotified = false;
portMUX_TYPE s_runtimeAllocMux = portMUX_INITIALIZER_UNLOCKED;
char* s_streamStageBuf = nullptr;
bool s_streamStageReady = false;
bool s_streamStageErrorNotified = false;
size_t s_streamStageFill = 0;
size_t s_streamStageReadPos = 0;
bool s_streamStageEof = false;
uint32_t s_streamStageLastProgressMs = 0;
size_t s_streamStageCapacity = 0;
size_t s_streamRefillChunkBytes = GRBL_STREAM_REFILL_CHUNK_BYTES;
size_t s_streamLowWatermarkBytes = GRBL_STREAM_LOW_WATERMARK_BYTES;
uint16_t s_preparedHead = 0;
uint16_t s_preparedTail = 0;
uint16_t s_preparedCount = 0;

#if ENABLE_SDCARD == 1
File s_file;
#endif

uint8_t s_fifoHead = 0;
uint8_t s_fifoTail = 0;
uint8_t s_fifoCount= 0;

uint32_t s_pollIntervalMs = GRBL_STATUS_POLL_DEFAULT_MS;
uint32_t s_lastPollMs     = 0;
uint8_t  s_pollOutstanding = 0;

SemaphoreHandle_t s_mutex = nullptr;

uint8_t s_rxLen = 0;
uint8_t s_recentErrorHead = 0;
uint8_t s_recentErrorCount = 0;
uint32_t s_lastStatusMs = 0;
uint32_t s_lastPositionMs = 0;
uint32_t s_lastRxMs = 0;
uint32_t s_doneStableSinceMs = 0;

// ---------------------------------------------------------------------------
// Internal helpers — kept here because they are used across split units
// and would create circular dependencies if extracted.
// ---------------------------------------------------------------------------

size_t clampSizeT(size_t value, size_t minValue, size_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

size_t alignDownSizeT(size_t value, size_t align) {
    if (align == 0) return value;
    return value - (value % align);
}

bool ensureRuntimeBuffers() {
    if (s_runtimeBuffersReady && s_runtimeBuffers) return true;

    taskENTER_CRITICAL(&s_runtimeAllocMux);
    if (s_runtimeBuffersReady && s_runtimeBuffers) {
        taskEXIT_CRITICAL(&s_runtimeAllocMux);
        return true;
    }

    if (psramFound()) {
        s_runtimeBuffers = (GrblRuntimeBuffers*)heap_caps_malloc(sizeof(GrblRuntimeBuffers), MALLOC_CAP_SPIRAM);
    }
    if (!s_runtimeBuffers) {
        s_runtimeBuffers = (GrblRuntimeBuffers*)heap_caps_malloc(sizeof(GrblRuntimeBuffers), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    if (!s_runtimeBuffers) {
        if (!s_runtimeBuffersErrorNotified) {
            s_runtimeBuffersErrorNotified = true;
            directLogPrintf("[GRBL] runtime buffer allocation failed (both PSRAM and heap)\n");
            sendVirtualMessage(F("❌ GRBL runtime disabled: out of memory"), ERROR);
        }
        taskEXIT_CRITICAL(&s_runtimeAllocMux);
        return false;
    }

    memset(s_runtimeBuffers, 0, sizeof(GrblRuntimeBuffers));
    strlcpy(s_machineStateName, "UNKNOWN", sizeof(s_machineStateName));
    strlcpy(s_jobOrigin, "unknown", sizeof(s_jobOrigin));
    s_runtimeBuffersReady = true;
    taskEXIT_CRITICAL(&s_runtimeAllocMux);
    return true;
}

bool ensureStreamStageBuffer() {
    if (s_streamStageReady && s_streamStageBuf) return true;

    taskENTER_CRITICAL(&s_runtimeAllocMux);
    if (s_streamStageReady && s_streamStageBuf) {
        taskEXIT_CRITICAL(&s_runtimeAllocMux);
        return true;
    }

    if (psramFound()) {
        size_t desired = (size_t)GRBL_STREAM_STAGE_BYTES;
        const uint32_t freePsram = ESP.getFreePsram();
        if (freePsram > 0) {
            size_t adaptive = (size_t)freePsram / (size_t)GRBL_STREAM_STAGE_TARGET_DIV;
            desired = clampSizeT(adaptive,
                                (size_t)GRBL_STREAM_STAGE_MIN_BYTES,
                                (size_t)GRBL_STREAM_STAGE_MAX_BYTES);
            desired = alignDownSizeT(desired, (size_t)GRBL_STREAM_STAGE_ALIGN_BYTES);
            if (desired < (size_t)GRBL_STREAM_STAGE_MIN_BYTES) {
                desired = (size_t)GRBL_STREAM_STAGE_MIN_BYTES;
            }
        }

        size_t trySize = desired;
        while (trySize >= (size_t)GRBL_STREAM_STAGE_MIN_BYTES) {
            s_streamStageBuf = (char*)heap_caps_malloc(trySize, MALLOC_CAP_SPIRAM);
            if (s_streamStageBuf) {
                s_streamStageCapacity = trySize;
                break;
            }
            if (trySize == (size_t)GRBL_STREAM_STAGE_MIN_BYTES) break;
            if (trySize > (size_t)GRBL_STREAM_STAGE_ALIGN_BYTES) {
                trySize -= (size_t)GRBL_STREAM_STAGE_ALIGN_BYTES;
            } else {
                break;
            }
            if (trySize < (size_t)GRBL_STREAM_STAGE_MIN_BYTES) {
                trySize = (size_t)GRBL_STREAM_STAGE_MIN_BYTES;
            }
        }
    }

    if (!s_streamStageBuf) {
        size_t heapDesired = (size_t)GRBL_STREAM_STAGE_MIN_BYTES;
        s_streamStageBuf = (char*)heap_caps_malloc(heapDesired, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_streamStageBuf) {
            s_streamStageCapacity = heapDesired;
        }
    }

    if (!s_streamStageBuf) {
        if (!s_streamStageErrorNotified) {
            s_streamStageErrorNotified = true;
            directLogPrintf("[GRBL] stream staging allocation failed (%lu bytes)\n",
                            (unsigned long)GRBL_STREAM_STAGE_BYTES);
            sendVirtualMessage(F("❌ GRBL stream staging disabled: out of memory"), ERROR);
        }
        taskEXIT_CRITICAL(&s_runtimeAllocMux);
        return false;
    }

    if (s_streamStageCapacity == 0) s_streamStageCapacity = (size_t)GRBL_STREAM_STAGE_BYTES;
    memset(s_streamStageBuf, 0, s_streamStageCapacity);
    s_streamStageFill = 0;
    s_streamStageReadPos = 0;
    s_streamStageEof = false;
    configureStreamStageWatermarksLocked();
    directLogPrintf("[GRBL] stream staging ready: cap=%lu low=%lu refill=%lu\n",
                    (unsigned long)s_streamStageCapacity,
                    (unsigned long)s_streamLowWatermarkBytes,
                    (unsigned long)s_streamRefillChunkBytes);
    s_streamStageReady = true;
    taskEXIT_CRITICAL(&s_runtimeAllocMux);
    return true;
}

const char* workflowStateName(GrblWorkflowState state) {
    switch (state) {
        case GrblWorkflowState::IDLE: return "idle";
        case GrblWorkflowState::RUNNING: return "running";
        case GrblWorkflowState::PAUSED: return "paused";
        case GrblWorkflowState::HOLD: return "hold";
        case GrblWorkflowState::CANCELLING: return "cancelling";
        case GrblWorkflowState::DONE: return "done";
        case GrblWorkflowState::ERROR: return "error";
        default: return "idle";
    }
}

const char* jobSourceName(GrblJobSource source) {
    switch (source) {
        case GrblJobSource::FILE_STREAM: return "file_stream";
        case GrblJobSource::LOCAL_QUEUE: return "local_queue";
        default: return "none";
    }
}

uint32_t jobEffectiveElapsedMsLocked(uint32_t nowMs) {
    if (s_jobStartMs == 0) return 0;

    uint32_t endMs = nowMs;
    if (!isJobRuntimeActiveLocked() && s_jobEndMs > 0) endMs = s_jobEndMs;
    if (endMs < s_jobStartMs) return 0;

    uint32_t total = endMs - s_jobStartMs;
    uint32_t hold = s_holdAccumMs;
    if (s_holdStartMs > 0 && endMs > s_holdStartMs) hold += (endMs - s_holdStartMs);
    if (hold >= total) return 0;
    return total - hold;
}

uint8_t fifoPop(char* lineOut, size_t lineOutSize) {
    if (lineOut && lineOutSize > 0) lineOut[0] = '\0';
    if (s_fifoCount == 0) return 0;

    uint8_t bytes = s_fifo[s_fifoTail];
    if (lineOut && lineOutSize > 0) {
        strlcpy(lineOut, s_fifoLines[s_fifoTail], lineOutSize);
    }
    s_fifoLines[s_fifoTail][0] = '\0';
    s_fifoTail = (s_fifoTail + 1) % GRBL_INFLIGHT_FIFO_SIZE;
    s_fifoCount--;
    return bytes;
}

uint8_t resolveConnectedUsbSlot(uint8_t preferred) {
    const UsbCdcDevice pref = grblTransportGetDevice(preferred);
    if (pref.connected) return preferred;

    uint8_t fallback = preferred;
    if (firstConnectedUsbSlot(&fallback)) return fallback;
    return preferred;
}

uint32_t localQueueCountLines(const char* text) {
    if (!text || !*text) return 0;
    uint32_t total = 0;
    const char* p = text;
    while (*p) {
        char raw[GRBL_MAX_LINE_LEN + 2];
        size_t n = 0;
        while (*p && *p != '\n') {
            if (*p != '\r' && n < sizeof(raw) - 1) raw[n++] = *p;
            p++;
        }
        if (*p == '\n') p++;
        raw[n] = '\0';

        char stripped[GRBL_MAX_LINE_LEN + 1];
        size_t slen = stripLine(raw, stripped, sizeof(stripped));
        if (slen == 0 || stripped[0] == ';') continue;
        total++;
    }
    return total;
}

#endif // ENABLE_GRBL == 1
