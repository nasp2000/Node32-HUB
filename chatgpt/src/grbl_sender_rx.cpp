// grbl_sender_rx.cpp
// GRBL response line processing.
#include "feature_flags.h"
#include "grbl_sender_internal.h"

#if ENABLE_GRBL == 1

#include <string.h>
#include "commands.h"

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

// ---------------------------------------------------------------------------
// Process one complete line received from GRBL.
// ---------------------------------------------------------------------------

void processGrblResponseLine(const char* line) {
    const uint32_t nowMs = millis();

    // Fast-path for "ok" acks during PSRAM streaming — skip console/monitor
    // overhead when processing 1000+ "ok"/sec. Outside streaming, fall through
    // to slow path so manual commands (e.g. $$) show responses in console.
    if (line[0] == 'o' && line[1] == 'k' && (line[2] == '\0' || line[2] == '\r' || line[2] == ' ')) {
        bool isStreaming = false;
        if (s_mutex) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            isStreaming = (s_jobSource == GrblJobSource::FILE_STREAM && s_streamPsramBuf);
            xSemaphoreGive(s_mutex);
        }
        if (isStreaming) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if ((s_linesAcked < 20) || ((s_linesAcked % 100U) == 0U)) {
                grblDebugLog("[DBG-RX] fast path ok (streaming) acked=%lu", (unsigned long)s_linesAcked);
            }
            s_lastRxMs = nowMs;
            char ackedLine[GRBL_MAX_LINE_LEN + 1];
            uint8_t freed = fifoPop(ackedLine, sizeof(ackedLine));
            if (freed > 0) {
                if (s_inflight >= freed) s_inflight -= freed;
                else                     s_inflight  = 0;
                s_linesAcked++;
                s_lastAckProgressMs = nowMs;
                if (s_fileRetryLine[0] != '\0' && ackedLine[0] != '\0' && strcmp(s_fileRetryLine, ackedLine) == 0) {
                    s_fileRetryLine[0] = '\0';
                    s_fileRetryCount = 0;
                }
                if ((s_linesAcked <= 10) || ((s_linesAcked % 50U) == 0U)) {
                    jobLogEventLocked("ACK", ackedLine[0] ? ackedLine : "ok");
                }
                s_avgLineBytes = (uint16_t)(((uint32_t)GRBL_PSRAM_AVG_LINE_ALPHA * freed +
                                             (uint32_t)s_avgLineBytes) / ((uint32_t)GRBL_PSRAM_AVG_LINE_ALPHA + 1U));
            }
            const uint32_t progressAck = s_linesAcked;
            psramFeedNotify();
            xSemaphoreGive(s_mutex);

            // Publish every 50th "ok" during streaming so the web UI shows
            // job progress even though individual acks are suppressed.
            if ((progressAck % 50U) == 0U) {
                char buf[32];
                snprintf(buf, sizeof(buf), "ok [%lu]", (unsigned long)progressAck);
                publishConsoleLine(String(buf));
            }

            return;
        }
        // Non-streaming "ok" — fall through to slow path for console publish.
    }

    // Slow path — non-"ok" responses (status, error, alarm, hold, etc.).
    // These are infrequent and deserve full processing.
    grblDebugLog("[DBG-RX] slow path: '%s'", line);
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_lastRxMs = nowMs;
        xSemaphoreGive(s_mutex);
    } else {
        s_lastRxMs = nowMs;
    }

    // Publish to console first (lightweight, no mutex needed).
    if (line[0] == '<') {
        parseMachineStatusLine(line);
        publishConsoleLine(String(line));
        // Notify auto-detect task only when a valid GRBL status frame was parsed.
        if (s_autoDetectActive && s_hasMachineStatus) {
            s_autoDetectFound = true;
            TaskHandle_t h = s_autoDetectTask;
            if (h) xTaskNotifyGive(h);
        }
    } else if (line[0] == 'o' && line[1] == 'k') {
        // Non-streaming "ok" — publish to console AND process FIFO/inflight
        // so manual commands clean up properly (sendLineLocked pushes to FIFO).
        publishConsoleLine(F("ok"));
    } else {
        // All other responses (config $, error:, alarm, etc.) are published
        // individually by the type-specific branches below.
    }

    // Inject to monitor pipeline for non-trivial responses (after console).
    if (line[0] != 'o' || line[1] != 'k') {
        injectDataToRXBuffer(String(line), String("GRBL"));
    }

    // Process FIFO/inflight for non-streaming "ok".
    if (line[0] == 'o' && line[1] == 'k') {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        char ackedLine[GRBL_MAX_LINE_LEN + 1];
        uint8_t freed = fifoPop(ackedLine, sizeof(ackedLine));
        if (freed > 0) {
            if (s_inflight >= freed) s_inflight -= freed;
            else                     s_inflight  = 0;
        }
        s_lastRxMs = nowMs;
        xSemaphoreGive(s_mutex);
    } else if (strncmp(line, "ALARM:", 6) == 0) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_alarmActive = true;
        strlcpy(s_alarmMessage, line, sizeof(s_alarmMessage));
        strlcpy(s_errorReason, line, sizeof(s_errorReason));
        s_jobEndMs = millis();
        const bool wasActive = (s_state == GrblStreamState::STREAMING ||
                                s_state == GrblStreamState::PAUSED);
        if (wasActive) {
            // Snapshot first: path/counters are still intact here; the wipe
            // below clears the live job. Enables "resume from line N".
            grblCaptureResumeSnapshotLocked();
            s_state = GrblStreamState::ERROR;
            s_jobSource = GrblJobSource::NONE;
            s_inflight = 0;
            s_fifoHead = 0;
            s_fifoTail = 0;
            s_fifoCount = 0;
            s_holdStartMs = 0;
            s_cancellingSinceMs = 0;
            setWorkflowStateLocked(GrblWorkflowState::ERROR, "alarm");
        }
        jobLogEventLocked("ALARM", line);
        xSemaphoreGive(s_mutex);
        if (wasActive) {
            grblStopPsramFeedTask();
        }
        directLogPrintf("[GRBL] alarm response: %s\n", line);
        publishConsoleLine(String("alarm ") + String(line));
        publishProductionEvent("alarm", String(line));
    } else if (strncmp(line, "error:", 6) == 0) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        char failedLine[GRBL_MAX_LINE_LEN + 1];
        uint8_t freed = fifoPop(failedLine, sizeof(failedLine));
        if (s_inflight >= freed) s_inflight -= freed;
        else                     s_inflight  = 0;
        s_errorCount++;
        strlcpy(s_errorReason, line, sizeof(s_errorReason));
        jobLogEventLocked("ERR", line);

        const char* errSource = (s_jobSource == GrblJobSource::LOCAL_QUEUE) ? "local_queue"
                              : ((s_jobSource == GrblJobSource::FILE_STREAM) ? "file_stream"
                                                                           : "manual");
        const char* errPreview = "";
        if (s_jobSource == GrblJobSource::LOCAL_QUEUE && s_localCurrentLineActive && s_localCurrentLine[0]) {
            errPreview = s_localCurrentLine;
        } else if (s_lastSentLine[0]) {
            errPreview = s_lastSentLine;
        }
        pushRecentErrorLocked(line, errSource, errPreview);

        bool handledLocalQueue = false;
        bool handledFileStreamRetry = false;
        bool fileStreamOrderRisk = false;
        if (s_jobSource == GrblJobSource::LOCAL_QUEUE && s_localCurrentLineActive) {
            if (s_localCurrentRetry < s_queueRetryMax) {
                const size_t retryLen = strlen(s_localCurrentLine);
                if (retryLen > 0 && sendLineLocked(s_localCurrentLine, retryLen)) {
                    s_linesSent++;
                    s_localCurrentRetry++;
                    handledLocalQueue = true;
                    directLogPrintf("[GRBL] local queue retry %u/%u for line: %s\n",
                                    (unsigned)s_localCurrentRetry,
                                    (unsigned)s_queueRetryMax,
                                    s_localCurrentLine);
                    publishConsoleLine(String("retry line ") + String(s_localCurrentRetry) +
                                       String("/") + String(s_queueRetryMax));
                    publishProductionEvent("retry", String(s_localCurrentLine));
                    jobLogEventLocked("RETRY_LOCAL", s_localCurrentLine);
                }
            } else if (s_queueContinueOnError) {
                s_localCurrentLineActive = false;
                s_localCurrentRetry = 0;
                handledLocalQueue = true;
                directLogPrintf("[GRBL] local queue continue-on-error, skipping failed line\n");
                publishConsoleLine(String("skip failed line: ") + String(line));
                publishProductionEvent("line_error_skipped", String(line));
                jobLogEventLocked("SKIP_LOCAL", line);
            }
        }

        if (!handledLocalQueue && s_jobSource == GrblJobSource::FILE_STREAM && failedLine[0] != '\0') {
            // If there are younger lines still pending, retrying this failed line would
            // execute it out of order. In this case we stop automatic retry and keep the
            // job on hold for operator action.
            const bool hasYoungerInFlight = (s_fifoCount > 0 || s_inflight > 0);
            if (hasYoungerInFlight) {
                fileStreamOrderRisk = true;
            }

            const bool sameRetryLine = (s_fileRetryLine[0] != '\0' && strcmp(s_fileRetryLine, failedLine) == 0);
            if (!sameRetryLine) {
                strlcpy(s_fileRetryLine, failedLine, sizeof(s_fileRetryLine));
                s_fileRetryCount = 0;
            }

            if (!fileStreamOrderRisk && s_fileRetryCount < (uint8_t)GRBL_FILE_STREAM_RETRY_MAX) {
                s_fileRetryCount++;
                handledFileStreamRetry = true;
                s_state = GrblStreamState::PAUSED;
                strlcpy(s_pauseReason, "grbl_error_retry_pending", sizeof(s_pauseReason));
                if (s_holdStartMs == 0) s_holdStartMs = millis();
                setWorkflowStateLocked(GrblWorkflowState::HOLD, "grbl_error_retry_pending");
                directLogPrintf("[GRBL] file stream retry scheduled %u/%u for line: %s\n",
                                (unsigned)s_fileRetryCount,
                                (unsigned)GRBL_FILE_STREAM_RETRY_MAX,
                                failedLine);
                publishConsoleLine(String("retry file line ") + String(s_fileRetryCount) +
                                   String("/") + String((unsigned)GRBL_FILE_STREAM_RETRY_MAX));
                publishProductionEvent("retry", String(failedLine));
                jobLogEventLocked("RETRY_FILE", failedLine);
            } else if (fileStreamOrderRisk) {
                s_state = GrblStreamState::PAUSED;
                strlcpy(s_pauseReason, "grbl_error_out_of_order_risk", sizeof(s_pauseReason));
                if (s_holdStartMs == 0) s_holdStartMs = millis();
                setWorkflowStateLocked(GrblWorkflowState::HOLD, "grbl_error_out_of_order_risk");
                directLogPrintf("[GRBL] file stream retry blocked to avoid out-of-order execution (inflight=%u fifo=%u)\n",
                                (unsigned)s_inflight,
                                (unsigned)s_fifoCount);
                publishConsoleLine(F("hold: out-of-order retry risk"));
                publishProductionEvent("hold", "grbl_error_out_of_order_risk");
                jobLogEventLocked("HOLD_ORDER", failedLine);
            }
        }

        // UGS-style behavior: pause on error only when there are pending lines.
        // If there is nothing left to send, do not force a pause/abort transition.
        if (!handledLocalQueue && !handledFileStreamRetry && s_state == GrblStreamState::STREAMING) {
            bool hasPendingLines = false;
            if (s_linesSent < s_linesTotal) {
                hasPendingLines = true;
            }
            if (s_jobSource == GrblJobSource::LOCAL_QUEUE) {
                if (s_localCurrentLineActive || s_localQueuePos < s_localQueueLen) {
                    hasPendingLines = true;
                }
            }
            if (s_jobSource == GrblJobSource::FILE_STREAM) {
                if (!preparedQueueIsEmpty() || streamStageBytesAvailable() > 0 || !s_streamStageEof) {
                    hasPendingLines = true;
                }
            }

            if (hasPendingLines) {
                s_state = GrblStreamState::PAUSED;
                strlcpy(s_pauseReason, "grbl_error", sizeof(s_pauseReason));
                if (s_holdStartMs == 0) s_holdStartMs = millis();
                setWorkflowStateLocked(GrblWorkflowState::HOLD, "grbl_error_pause");
                if (s_jobSource == GrblJobSource::LOCAL_QUEUE) {
                    s_localCurrentLineActive = false;
                    s_localCurrentRetry = 0;
                    s_localCurrentLine[0] = '\0';
                }
            }
        }
        xSemaphoreGive(s_mutex);
        directLogPrintf("[GRBL] error response: %s\n", line);
        sendVirtualMessage(("❌ GRBL error: " + String(line)).c_str(), ERROR);
        if (!handledLocalQueue && !handledFileStreamRetry) {
            const bool pausedByError = (s_state == GrblStreamState::PAUSED);
            if (pausedByError) {
                if (fileStreamOrderRisk) {
                    sendVirtualMessage(F("⏸️ GRBL safe pause: error on a line with subsequent commands in-flight. Out-of-order resend avoided."), WARN);
                } else {
                    sendVirtualMessage(F("⏸️ GRBL paused due to error. Fix and resume when ready."), WARN);
                }
            }
        }
        publishConsoleLine(String("error ") + String(line));
        if (!handledLocalQueue && !handledFileStreamRetry) publishProductionEvent("error", String(line));
    } else if (line[0] != '<') {
        publishConsoleLine(String(line));
    }
    // Status reports '<...>' are forwarded to monitor but need no buffer action.
}

#endif // ENABLE_GRBL == 1
