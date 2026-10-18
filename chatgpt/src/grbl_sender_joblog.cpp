// grbl_sender_joblog.cpp
// GRBL job trace log — writes per-job diagnostic CSV to SD.
// Extracted from grbl_sender.cpp for parallel compilation.
#include "grbl_sender_internal.h"

#include "feature_flags.h"
// Daily storage stats (defined in sd_tasks_impl.h)
void storNoteSdWrite(uint64_t bytes);
#if ENABLE_GRBL == 1 && ENABLE_SDCARD == 1

#include <time.h>
#include <stdarg.h>
#include "direct_log.h"
#include "commands.h"
#include "feature_flags.h"

extern void sendVirtualMessage(const String& msg, int msgType,
                                  bool alreadyFormatted, bool bypassDedup);

// Per-job trace log for GRBL tuning and line-loss diagnostics.
#define GRBL_JOB_LOG_DIR "/GRBL_JOBS"
#define GRBL_JOB_LOG_BUF_SIZE 24576U
#define GRBL_JOB_LOG_FLUSH_INTERVAL_MS 700UL
#define GRBL_JOB_LOG_STATUS_INTERVAL_MS 2000UL
#define GRBL_JOB_LOG_BLOCK_THROTTLE_MS 250UL

static File s_jobLogFile;
bool s_jobLogActive = false;
bool s_jobLogEnabled = false;
static char s_jobLogPath[128] = "";
static char* s_jobLogBuf = nullptr;
static size_t s_jobLogBufLen = 0;
static uint32_t s_jobLogSeq = 0;
static uint32_t s_jobLogDropped = 0;
static uint32_t s_jobLogLastFlushMs = 0;
static uint32_t s_jobLogLastStatusMs = 0;
uint32_t s_jobLogLastBlockMs = 0;
char s_jobLogLastBlockReason[32] = "";
static bool s_jobStartMachinePosValid = false;
bool s_jobStartWorkPosValid = false;
static float s_jobStartMachinePos[3] = {0.0f, 0.0f, 0.0f};
float s_jobStartWorkPos[3] = {0.0f, 0.0f, 0.0f};

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

static void buildPositionPairDetail(char* out, size_t outSize,
                                    bool hasMachinePos, const float machinePos[3],
                                    bool hasWorkPos, const float workPos[3]) {
    if (!out || outSize == 0) return;
    const int n = snprintf(
        out,
        outSize,
        "M=%s%.3f,%.3f,%.3f|W=%s%.3f,%.3f,%.3f",
        hasMachinePos ? "" : "NA,",
        hasMachinePos ? machinePos[0] : 0.0f,
        hasMachinePos ? machinePos[1] : 0.0f,
        hasMachinePos ? machinePos[2] : 0.0f,
        hasWorkPos ? "" : "NA,",
        hasWorkPos ? workPos[0] : 0.0f,
        hasWorkPos ? workPos[1] : 0.0f,
        hasWorkPos ? workPos[2] : 0.0f
    );
    if (n <= 0) {
        out[0] = '\0';
    }
}

static void buildPositionDeltaDetail(char* out, size_t outSize,
                                     bool hasStartMachine, const float startMachine[3],
                                     bool hasEndMachine, const float endMachine[3],
                                     bool hasStartWork, const float startWork[3],
                                     bool hasEndWork, const float endWork[3]) {
    if (!out || outSize == 0) return;
    const bool hasDeltaMachine = hasStartMachine && hasEndMachine;
    const bool hasDeltaWork = hasStartWork && hasEndWork;
    const int n = snprintf(
        out,
        outSize,
        "DM=%s%.3f,%.3f,%.3f|DW=%s%.3f,%.3f,%.3f",
        hasDeltaMachine ? "" : "NA,",
        hasDeltaMachine ? (endMachine[0] - startMachine[0]) : 0.0f,
        hasDeltaMachine ? (endMachine[1] - startMachine[1]) : 0.0f,
        hasDeltaMachine ? (endMachine[2] - startMachine[2]) : 0.0f,
        hasDeltaWork ? "" : "NA,",
        hasDeltaWork ? (endWork[0] - startWork[0]) : 0.0f,
        hasDeltaWork ? (endWork[1] - startWork[1]) : 0.0f,
        hasDeltaWork ? (endWork[2] - startWork[2]) : 0.0f
    );
    if (n <= 0) {
        out[0] = '\0';
    }
}

static void jobLogSanitizeInplace(char* s) {
    if (!s) return;
    while (*s) {
        if (*s == '\r' || *s == '\n') *s = ' ';
        s++;
    }
}

static void jobLogAppendRaw(const char* data, size_t len) {
    if (!s_jobLogActive || !data || len == 0) return;
    if (len >= GRBL_JOB_LOG_BUF_SIZE) {
        s_jobLogDropped++;
        return;
    }
    if ((s_jobLogBufLen + len) >= GRBL_JOB_LOG_BUF_SIZE) {
        if (s_jobLogFile && s_jobLogBufLen > 0) {
            const size_t written = s_jobLogFile.write((const uint8_t*)s_jobLogBuf, s_jobLogBufLen);
            if (written == s_jobLogBufLen) {
                s_jobLogBufLen = 0;
                s_jobLogLastFlushMs = millis();
            } else {
                s_jobLogDropped++;
                return;
            }
        } else {
            s_jobLogDropped++;
            return;
        }
    }
    memcpy(s_jobLogBuf + s_jobLogBufLen, data, len);
    s_jobLogBufLen += len;
}

void jobLogEventLocked(const char* ev, const char* detail) {
    if (!s_jobLogActive) return;
    char safeDetail[140];
    safeDetail[0] = '\0';
    if (detail && detail[0]) {
        strlcpy(safeDetail, detail, sizeof(safeDetail));
        jobLogSanitizeInplace(safeDetail);
    }
    char line[280];
    const int n = snprintf(
        line,
        sizeof(line),
        "T=%lu;SEQ=%lu;EV=%s;SRC=%s;WF=%s;STATE=%u;SENT=%lu;ACK=%lu;TOT=%lu;INFLIGHT=%u;FIFO=%u;ERR=%lu;POLL=%u;DETAIL=%s\n",
        (unsigned long)millis(),
        (unsigned long)(++s_jobLogSeq),
        ev ? ev : "EV",
        jobSourceName(s_jobSource),
        workflowStateName(s_workflowState),
        (unsigned)s_state,
        (unsigned long)s_linesSent,
        (unsigned long)s_linesAcked,
        (unsigned long)s_linesTotal,
        (unsigned)s_inflight,
        (unsigned)s_fifoCount,
        (unsigned long)s_errorCount,
        (unsigned)s_pollOutstanding,
        safeDetail[0] ? safeDetail : "-"
    );
    if (n > 0) jobLogAppendRaw(line, (size_t)n);
}

void jobLogFlush(bool force) {
    if (!s_jobLogActive || !s_jobLogFile) return;
    const uint32_t nowMs = millis();
    if (!force && (nowMs - s_jobLogLastFlushMs) < GRBL_JOB_LOG_FLUSH_INTERVAL_MS && s_jobLogBufLen < 1024U) {
        return;
    }
    if (s_jobLogBufLen > 0) {
        const size_t written = s_jobLogFile.write((const uint8_t*)s_jobLogBuf, s_jobLogBufLen);
        if (written == s_jobLogBufLen) {
            s_jobLogBufLen = 0;
        } else {
            s_jobLogDropped++;
        }
    }
    if (force) s_jobLogFile.flush();
    s_jobLogLastFlushMs = nowMs;
}

bool jobLogOpenLocked(const char* kind, const char* sourcePath) {
    if (!s_jobLogEnabled) return false;

    if (s_jobLogActive) {
        jobLogEventLocked("JOB_CLOSE", "new_job_overwrite");
        jobLogFlush(true);
        if (s_jobLogFile) {
            size_t jlSize = s_jobLogFile.size();
            s_jobLogFile.close();
            storNoteSdWrite(jlSize);
        }
    }

    s_jobLogActive = false;
    s_jobLogBufLen = 0;
    s_jobLogSeq = 0;
    s_jobLogDropped = 0;
    s_jobLogLastFlushMs = millis();
    s_jobLogLastStatusMs = 0;
    s_jobLogLastBlockMs = 0;
    s_jobLogLastBlockReason[0] = '\0';
    s_jobLogPath[0] = '\0';
    s_jobStartMachinePosValid = false;
    s_jobStartWorkPosValid = false;

    if (!s_jobLogBuf) {
        s_jobLogBuf = (char*)heap_caps_malloc(GRBL_JOB_LOG_BUF_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_jobLogBuf) return false;
    }

    if (!SD_MMC.exists(GRBL_JOB_LOG_DIR)) {
        if (!SD_MMC.mkdir(GRBL_JOB_LOG_DIR)) return false;
    }

    const time_t now = time(nullptr);
    struct tm ti;
    localtime_r(&now, &ti);
    char ts[32];
    if (now > 100000) strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &ti);
    else snprintf(ts, sizeof(ts), "MS_%lu", (unsigned long)millis());

    char filename[128];
    snprintf(filename, sizeof(filename), "%s/%s_%s.log", GRBL_JOB_LOG_DIR, ts,
             (kind && kind[0]) ? kind : "job");
    strlcpy(s_jobLogPath, filename, sizeof(s_jobLogPath));

    s_jobLogFile = SD_MMC.open(s_jobLogPath, FILE_WRITE);
    if (!s_jobLogFile) {
        s_jobLogPath[0] = '\0';
        return false;
    }

    s_jobLogActive = true;
    char hdr[220];
    const int hn = snprintf(hdr, sizeof(hdr),
                            "# GRBL JOB TRACE\n# PATH=%s\n# KIND=%s\n# SOURCE=%s\n# ORIGIN=%s\n",
                            s_jobLogPath,
                            (kind && kind[0]) ? kind : "job",
                            (sourcePath && sourcePath[0]) ? sourcePath : "-",
                            s_jobOrigin[0] ? s_jobOrigin : "unknown");
    if (hn > 0) jobLogAppendRaw(hdr, (size_t)hn);
    jobLogEventLocked("JOB_START", sourcePath ? sourcePath : "-");

    const bool hasFreshPosition = hasFreshPositionLocked();
    s_jobStartMachinePosValid = s_hasMachinePosition && hasFreshPosition &&
                                !isSyntheticZeroPositionLocked(true, s_machinePos[0], s_machinePos[1], s_machinePos[2]);
    s_jobStartWorkPosValid = s_hasWorkPosition && hasFreshPosition &&
                             !isSyntheticZeroPositionLocked(false, s_workPos[0], s_workPos[1], s_workPos[2]);
    if (s_jobStartMachinePosValid) {
        s_jobStartMachinePos[0] = s_machinePos[0];
        s_jobStartMachinePos[1] = s_machinePos[1];
        s_jobStartMachinePos[2] = s_machinePos[2];
    }
    if (s_jobStartWorkPosValid) {
        s_jobStartWorkPos[0] = s_workPos[0];
        s_jobStartWorkPos[1] = s_workPos[1];
        s_jobStartWorkPos[2] = s_workPos[2];
    }

    char startDetail[140];
    buildPositionPairDetail(startDetail, sizeof(startDetail),
                            s_jobStartMachinePosValid, s_jobStartMachinePos,
                            s_jobStartWorkPosValid, s_jobStartWorkPos);
    jobLogEventLocked("POS_START", startDetail[0] ? startDetail : "-");
    return true;
}

void jobLogCloseLocked(const char* finalEvent, const char* reason) {
    if (!s_jobLogActive) return;

    const bool hasFreshPosition = hasFreshPositionLocked();
    const bool hasEndMachinePos = s_hasMachinePosition && hasFreshPosition &&
                                  !isSyntheticZeroPositionLocked(true, s_machinePos[0], s_machinePos[1], s_machinePos[2]);
    const bool hasEndWorkPos = s_hasWorkPosition && hasFreshPosition &&
                               !isSyntheticZeroPositionLocked(false, s_workPos[0], s_workPos[1], s_workPos[2]);
    float endMachinePos[3] = {0.0f, 0.0f, 0.0f};
    float endWorkPos[3] = {0.0f, 0.0f, 0.0f};
    if (hasEndMachinePos) {
        endMachinePos[0] = s_machinePos[0];
        endMachinePos[1] = s_machinePos[1];
        endMachinePos[2] = s_machinePos[2];
    }
    if (hasEndWorkPos) {
        endWorkPos[0] = s_workPos[0];
        endWorkPos[1] = s_workPos[1];
        endWorkPos[2] = s_workPos[2];
    }

    char endDetail[140];
    buildPositionPairDetail(endDetail, sizeof(endDetail),
                            hasEndMachinePos, endMachinePos,
                            hasEndWorkPos, endWorkPos);
    jobLogEventLocked("POS_END", endDetail[0] ? endDetail : "-");

    char deltaDetail[140];
    buildPositionDeltaDetail(deltaDetail, sizeof(deltaDetail),
                             s_jobStartMachinePosValid, s_jobStartMachinePos,
                             hasEndMachinePos, endMachinePos,
                             s_jobStartWorkPosValid, s_jobStartWorkPos,
                             hasEndWorkPos, endWorkPos);
    jobLogEventLocked("POS_DELTA", deltaDetail[0] ? deltaDetail : "-");

    jobLogEventLocked(finalEvent ? finalEvent : "JOB_END", reason ? reason : "-");

    {
        const bool allAcked = (s_linesAcked >= s_linesTotal);
        const bool zeroInflight = (s_inflight == 0 && s_fifoCount == 0);
        char verify[120];
        snprintf(verify, sizeof(verify),
                 "VERIFY: ack=%lu sent=%lu tot=%lu inflight=%u fifo=%u %s",
                 (unsigned long)s_linesAcked, (unsigned long)s_linesSent,
                 (unsigned long)s_linesTotal, (unsigned)s_inflight,
                 (unsigned)s_fifoCount,
                 allAcked ? "OK" : "MISMATCH");
        jobLogEventLocked("VERIFY", verify);
        if (allAcked && zeroInflight) {
            String msgOk = String("GRBL: todas as linhas reconhecidas (ack=sent)");
            sendVirtualMessage(msgOk, INFO, false, false);
        } else {
            uint32_t missing = s_linesSent - s_linesAcked;
            String msgWarn = String("GRBL: ") + String(missing) + " linhas por reconhecer (sent=" +
                String(s_linesSent) + " ack=" + String(s_linesAcked) +
                "). Adicione G4 P0.5 no final do G-code.";
            sendVirtualMessage(msgWarn, WARN, false, false);
        }
    }

    if (s_jobLogDropped > 0) {
        char meta[96];
        const int n = snprintf(meta, sizeof(meta), "# DROPPED=%lu\n", (unsigned long)s_jobLogDropped);
        if (n > 0) jobLogAppendRaw(meta, (size_t)n);
    }
    jobLogFlush(true);
    if (s_jobLogFile) {
        size_t jlSize = s_jobLogFile.size();
        s_jobLogFile.close();
        storNoteSdWrite(jlSize);
    }
    s_jobLogActive = false;
    s_jobLogPath[0] = '\0';
}

void jobLogTickLocked() {
    if (!s_jobLogActive) return;
    const uint32_t nowMs = millis();
    if ((nowMs - s_jobLogLastStatusMs) >= GRBL_JOB_LOG_STATUS_INTERVAL_MS) {
        s_jobLogLastStatusMs = nowMs;
        jobLogEventLocked("STAT", "periodic");
    }
    jobLogFlush(false);
}

#endif // ENABLE_GRBL == 1 && ENABLE_SDCARD == 1
