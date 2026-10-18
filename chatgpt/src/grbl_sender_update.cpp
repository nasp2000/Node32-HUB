// grbl_sender_update.cpp
// Main GRBL sender update loop — called from loop().
#include "feature_flags.h"
#include "grbl_sender_internal.h"

#if ENABLE_GRBL == 1

#include <string.h>
#include <stdio.h>
#include <esp_task_wdt.h>
#include "commands.h"

extern void wsGrblLoop() __attribute__((weak));

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

// ---------------------------------------------------------------------------
// grblSenderUpdate — called from loop(), drives the SD streaming pump.
// ---------------------------------------------------------------------------

void grblSenderUpdate() {
    esp_task_wdt_reset();
    // Drain UART RX before anything else — otherwise GRBL responses pile up unread.
    grblTransportPollRx();

    if (!ensureRuntimeBuffers()) return;
    if (!s_mutex) return;

    // Use short timeouts throughout — never block the web handler or feeder task.
    // The feeder task yields between bursts, so we will succeed on the next attempt.
    static const TickType_t MUTEX_TIMEOUT = pdMS_TO_TICKS(20);

    // Keep a lightweight periodic '?' status poll active so DRO (M/W axis)
    // updates in manual and streaming workflows.
    if (s_pollIntervalMs > 0 && grblTransportIsRunning()) {
        bool shouldPoll = false;
        uint16_t cpmNow = 0;
        GrblStreamState streamStateNow = GrblStreamState::IDLE;
        uint8_t pollOutstandingNow = 0;
        if (xSemaphoreTake(s_mutex, MUTEX_TIMEOUT) == pdTRUE) {
            if (s_streamPsramBuf && s_state == GrblStreamState::STREAMING) {
                shouldPoll = true;
                s_pollIntervalMs = GRBL_STATUS_POLL_STREAMING_MS;
            } else {
                shouldPoll = true;
            }
            cpmNow = s_currentCmdPerMin;
            streamStateNow = s_state;
            pollOutstandingNow = s_pollOutstanding;
            xSemaphoreGive(s_mutex);
        }
        if (shouldPoll) {
        uint32_t effectivePollMs = s_pollIntervalMs;
        if (streamStateNow == GrblStreamState::STREAMING) {
            if (xSemaphoreTake(s_mutex, MUTEX_TIMEOUT) == pdTRUE) {
                const bool startupWindow = (s_jobSource == GrblJobSource::FILE_STREAM) &&
                                           (s_linesAcked < (uint32_t)GRBL_STREAM_BURST_START_ACK_LINES);
                xSemaphoreGive(s_mutex);
                if (startupWindow && effectivePollMs < GRBL_STATUS_POLL_STARTUP_MS) {
                    effectivePollMs = GRBL_STATUS_POLL_STARTUP_MS;
                }
            }
            if (cpmNow > 0 && cpmNow <= GRBL_STATUS_POLL_ADAPTIVE_HARD_CPM &&
                effectivePollMs < GRBL_STATUS_POLL_ADAPTIVE_HARD_MS) {
                effectivePollMs = GRBL_STATUS_POLL_ADAPTIVE_HARD_MS;
            } else if (cpmNow > 0 && cpmNow <= GRBL_STATUS_POLL_ADAPTIVE_SOFT_CPM &&
                       effectivePollMs < GRBL_STATUS_POLL_ADAPTIVE_SOFT_MS) {
                effectivePollMs = GRBL_STATUS_POLL_ADAPTIVE_SOFT_MS;
            }
        }
        uint32_t now = millis();
        if (now - s_lastPollMs >= effectivePollMs &&
            pollOutstandingNow < (uint8_t)GRBL_STATUS_POLL_MAX_OUTSTANDING) {
            s_lastPollMs = now;
            const uint8_t useSlot = resolveConnectedUsbSlot(s_slot);
            s_slot = useSlot;
            // Retry up to 5 times (~5ms total) — the high-priority PSRAM feeder
            // task may hold s_ioMutex during streaming, starving the '?' poll.
            bool pollSent = false;
            for (int retry = 0; retry < 5; retry++) {
                if (grblTransportWrite(useSlot, (const uint8_t*)"?", 1) == 1) {
                    pollSent = true;
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            if (pollSent) {
                if (xSemaphoreTake(s_mutex, MUTEX_TIMEOUT) == pdTRUE) {
                    if (s_pollOutstanding < (uint8_t)GRBL_STATUS_POLL_MAX_OUTSTANDING) {
                        s_pollOutstanding++;
                    }
                    xSemaphoreGive(s_mutex);
                }
            }
        }
        }
    }

    // Drain UART RX bytes (S3) or USB bytes (handled by callback on P4).
    grblTransportPollRx();

#if ENABLE_USB_HOST
    // Track USB connection state changes to reset status on reconnect.
    {
        uint8_t curMask = 0;
        for (uint8_t i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
            const UsbCdcDevice d = grblTransportGetDevice(i);
            if (d.connected) curMask |= (1U << i);
        }
        if (s_lastConnectedMask != curMask) {
            const bool anyNew = ((curMask & ~s_lastConnectedMask) != 0);
            s_lastConnectedMask = curMask;
            if (anyNew) {
                directLogPrintf("[GRBL] new USB device connected, resetting status\n");
                s_hasMachineStatus = false;
                s_lastStatusMs = 0;
            }
        }
    }
#endif // ENABLE_USB_HOST

    // Auto-detect disabled: GRBL defaults to USB_HOST_DEFAULT_BAUD (115200).
    // Baud rate can be changed manually via web UI "Aplicar" button.

    bool notifyUsbDisconnect = false;
    bool notifyRxStall = false;
    bool notifyAckStall = false;
    bool notifySdReadStall = false;
    bool notifyFeedStall = false;
    bool notifyStreamDone = false;
    uint32_t doneLinesAcked = 0;

    if (xSemaphoreTake(s_mutex, MUTEX_TIMEOUT) != pdTRUE) return;
    jobLogTickLocked();
    if (s_workflowState == GrblWorkflowState::CANCELLING && s_cancellingSinceMs > 0) {
        uint32_t nowMs = millis();
        if (nowMs - s_cancellingSinceMs >= 350) {
            s_cancellingSinceMs = 0;
            setWorkflowStateLocked(GrblWorkflowState::IDLE, "cancel_complete_timeout");
        }
    }
    if (s_state == GrblStreamState::PAUSED && s_workflowState == GrblWorkflowState::HOLD && s_inflight == 0) {
        setWorkflowStateLocked(GrblWorkflowState::PAUSED, "pause_stable_no_inflight");
    }

    // M6 Tool Change: pause streaming when the M6 command has been acknowledged
    // (inflight == 0) so the operator can change the tool manually.
    if (s_toolChangePending && s_inflight == 0 && s_fifoCount == 0 &&
        (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED)) {
        s_toolChangePending = false;
        s_state = GrblStreamState::PAUSED;
        s_pauseReason[0] = '\0';
        strlcpy(s_pauseReason, "M6 Tool Change", sizeof(s_pauseReason));
        setWorkflowStateLocked(GrblWorkflowState::HOLD, "m6_tool_change");
        publishConsoleLine(F(">>> M6 Tool Change — press Resume when done"));
        sendVirtualMessage(F("M6 Tool Change — press Resume to continue"), 0, true, false);
    }

    // Inflight stall recovery for manual mode: if no bytes have been acknowledged
    // for >5s and we are not actively streaming, reset inflight so subsequent
    // manual commands ($$, $H, etc.) are not blocked by stale fifo state.
    const uint32_t stallNowMs = millis();
    if (s_inflight > 0 && s_jobSource == GrblJobSource::NONE && s_lastRxMs > 0) {
        if ((stallNowMs - s_lastRxMs) >= 5000UL) {
            directLogPrintf("[GRBL] inflight stall recovery: resetting inflight=%u fifoCount=%u (lastRx=%lums ago)\n",
                            (unsigned)s_inflight, (unsigned)s_fifoCount, (unsigned long)(stallNowMs - s_lastRxMs));
            s_inflight = 0;
            s_fifoCount = 0;
            s_fifoHead = 0;
            s_fifoTail = 0;
            s_blockedByInflight = 0;
            s_blockedByReportedRx = 0;
            s_blockedByCriticalCommand = 0;
            s_blockedBySafetyMargin = 0;
            s_blockedByFifoFull = 0;
            if (grblWebPublishConsoleLine) {
                grblWebPublishConsoleLine(F("system: inflight stall recovered — cleared stale state"));
            }
        }
    }

    const uint32_t nowRateMs = millis();
    if (nowRateMs - s_rateLastMs >= 1000UL) {
        const uint32_t deltaAck = s_linesAcked - s_rateLastAck;
        const uint32_t deltaMs = nowRateMs - s_rateLastMs;
        s_currentCmdPerMin = (deltaMs > 0) ? (uint16_t)((deltaAck * 60000UL) / deltaMs) : 0;
        s_rateLastAck = s_linesAcked;
        s_rateLastMs = nowRateMs;
    }

    if ((s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED) &&
        s_jobSource != GrblJobSource::NONE) {
        const UsbCdcDevice activeDev = grblTransportGetDevice(s_slot);
        if (!activeDev.connected) {
            if (strcmp(s_pauseReason, "usb_disconnected") != 0) {
                notifyUsbDisconnect = true;
            }
            s_state = GrblStreamState::PAUSED;
            strlcpy(s_pauseReason, "usb_disconnected", sizeof(s_pauseReason));
            if (s_holdStartMs == 0) s_holdStartMs = nowRateMs;
            setWorkflowStateLocked(GrblWorkflowState::HOLD, "usb_disconnected");
        }
    }

    if (s_state == GrblStreamState::STREAMING && s_inflight > 0 && s_lastRxMs > 0 &&
        (nowRateMs - s_lastRxMs) >= GRBL_RX_STALL_TIMEOUT_MS) {
        if (strcmp(s_pauseReason, "rx_stall") != 0) {
            notifyRxStall = true;
        }
        s_state = GrblStreamState::PAUSED;
        strlcpy(s_pauseReason, "rx_stall", sizeof(s_pauseReason));
        if (s_holdStartMs == 0) s_holdStartMs = nowRateMs;
        setWorkflowStateLocked(GrblWorkflowState::HOLD, "rx_stall");
    }

    const bool ackBlockedByFlowControl =
        (strcmp(s_lastBlockReason, "inflight_limit") == 0) ||
        (strcmp(s_lastBlockReason, "inflight_soft_limit") == 0) ||
        (strcmp(s_lastBlockReason, "reported_rx_limit") == 0);
    if (s_state == GrblStreamState::STREAMING && s_inflight > 0 && s_linesSent > s_linesAcked &&
        s_lastAckProgressMs > 0 && (nowRateMs - s_lastAckProgressMs) >= GRBL_ACK_STALL_TIMEOUT_MS &&
        ackBlockedByFlowControl && s_lastBlockMs > 0 &&
        (nowRateMs - s_lastBlockMs) <= GRBL_ACK_STALL_BLOCK_WINDOW_MS) {
        if (strcmp(s_pauseReason, "ack_stall_safe_stop") != 0) {
            notifyAckStall = true;
        }
        s_state = GrblStreamState::PAUSED;
        strlcpy(s_pauseReason, "ack_stall_safe_stop", sizeof(s_pauseReason));
        strlcpy(s_errorReason, "ack_progress_timeout", sizeof(s_errorReason));
        if (s_holdStartMs == 0) s_holdStartMs = nowRateMs;
        setWorkflowStateLocked(GrblWorkflowState::HOLD, "ack_stall_safe_stop");
        jobLogEventLocked("HOLD_ACK_STALL", s_lastBlockLine[0] ? s_lastBlockLine : "ack_progress_timeout");
    }

    // Finalize only after all in-flight lines are acknowledged.
    if (s_state == GrblStreamState::STREAMING && s_jobSource == GrblJobSource::NONE && s_inflight == 0) {
        s_state = GrblStreamState::DONE;
        s_doneStableSinceMs = 0;
    }

    // Safety timeout: if file is fully read (jobSource==NONE) but inflight lines
    // never get acknowledged within the ACK stall window, force DONE so the
    // operator can start a new job instead of being permanently stuck.
    if (s_state == GrblStreamState::STREAMING && s_jobSource == GrblJobSource::NONE &&
        s_inflight > 0 && s_lastAckProgressMs > 0 &&
        (nowRateMs - s_lastAckProgressMs) >= GRBL_ACK_STALL_TIMEOUT_MS) {
        s_state = GrblStreamState::DONE;
        s_doneStableSinceMs = 0;
        jobLogEventLocked("FORCE_DONE", "inflight_timeout");
        directLogPrintf("[GRBL] force DONE: inflight=%u never acked after %lu ms\n",
                        (unsigned)s_inflight, (unsigned long)GRBL_ACK_STALL_TIMEOUT_MS);
    }

    if (s_state == GrblStreamState::DONE && s_jobSource == GrblJobSource::NONE && s_inflight == 0) {
        if (s_doneStableSinceMs == 0) s_doneStableSinceMs = nowRateMs;
        const bool stable = (nowRateMs - s_doneStableSinceMs) >= GRBL_DONE_STABLE_WINDOW_MS;
        if (stable && s_workflowState != GrblWorkflowState::DONE) {
            const bool allAcked = (s_linesAcked >= s_linesSent);
            if (allAcked) {
                setWorkflowStateLocked(GrblWorkflowState::DONE, "all_acks_stable");
            } else {
                char mismatchReason[80];
                snprintf(mismatchReason, sizeof(mismatchReason),
                         "mismatch_sent_%lu_acked_%lu", (unsigned long)s_linesSent,
                         (unsigned long)s_linesAcked);
                setWorkflowStateLocked(GrblWorkflowState::DONE, mismatchReason);
            }
            s_jobEndMs = nowRateMs;
            doneLinesAcked = s_linesAcked;
            notifyStreamDone = true;
        }
    } else {
        s_doneStableSinceMs = 0;
    }
    xSemaphoreGive(s_mutex);

    if (notifyStreamDone) {
        // Always close the job log when stream completes — even if ack != sent.
        if (xSemaphoreTake(s_mutex, MUTEX_TIMEOUT) == pdTRUE) {
            if (s_jobLogActive) {
                const bool allAcked = (s_linesAcked >= s_linesSent);
                jobLogCloseLocked(allAcked ? "JOB_DONE" : "JOB_MISMATCH",
                                  allAcked ? "all_acks" : "partial_ack");
            }
            xSemaphoreGive(s_mutex);
        }
    }

    if (notifyUsbDisconnect) {
        directLogPrintf("[GRBL] stream paused: active USB slot %u disconnected\n", (unsigned)s_slot);
        sendVirtualMessage(F("⚠️ GRBL: stream paused (USB disconnected)"), WARN);
        publishProductionEvent("hold", "usb_disconnected");
    }

    if (notifyRxStall) {
        directLogPrintf("[GRBL] stream paused by RX stall watchdog (>%lu ms without response, inflight=%u)\n",
                        (unsigned long)GRBL_RX_STALL_TIMEOUT_MS, (unsigned)s_inflight);
        sendVirtualMessage(F("⚠️ GRBL: stream paused (no controller response)"), WARN);
        publishProductionEvent("hold", "rx_stall");
    }

    if (notifyAckStall) {
        directLogPrintf("[GRBL] stream paused by ACK stall watchdog (>%lu ms without ACK progress, inflight=%u, last_block=%s)\n",
                        (unsigned long)GRBL_ACK_STALL_TIMEOUT_MS,
                        (unsigned)s_inflight,
                        s_lastBlockReason[0] ? s_lastBlockReason : "none");
        sendVirtualMessage(F("🛑 GRBL: safety pause (ACK without progress). Resume blocked to prevent line loss."), WARN);
        publishProductionEvent("hold", "ack_stall_safe_stop");
    }

    if (notifyStreamDone) {
        directLogPrintf("[GRBL] stream complete: %lu lines sent, %lu acked\n",
                        (unsigned long)s_linesSent, (unsigned long)doneLinesAcked);
        sendVirtualMessage(("✅ GRBL stream completed: " + String(doneLinesAcked) +
                               " lines").c_str(), INFO);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool shouldPump = (s_state == GrblStreamState::STREAMING);
    const GrblJobSource source = s_jobSource;
    xSemaphoreGive(s_mutex);

    // Run WS push before the streaming pump so broadcasts are never starved
    // by the drain-ACK loop or long SD reads.
    if (wsGrblLoop) wsGrblLoop();

    if (!shouldPump || source == GrblJobSource::NONE) return;
    // (wsGrblLoop also runs here on non-streaming loops via the call above)

    static uint32_t lastPumpLog = 0;
    if (millis() - lastPumpLog > 5000) {
        directLogPrintf("[GRBL] shouldPump=%d, source=%d, state=%d, inflight=%u\n",
                        shouldPump, (int)source, (int)s_state, (unsigned)s_inflight);
        lastPumpLog = millis();
    }

    if (source == GrblJobSource::LOCAL_QUEUE) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        const bool busy = (s_inflight > 0);
        xSemaphoreGive(s_mutex);
        if (busy) return;

        if (!s_localCurrentLineActive) {
            while (s_localQueuePos < s_localQueueLen) {
                char raw[GRBL_MAX_LINE_LEN + 2];
                if (!localQueueReadNextRawLine(raw, sizeof(raw), nullptr)) break;

                char stripped[GRBL_MAX_LINE_LEN + 1];
                size_t slen = stripLine(raw, stripped, sizeof(stripped));
                if (slen == 0 || stripped[0] == ';') continue;

                // Alarm-resume offset: same rule as the other feeders.
                if (s_resumeDropArmed && s_resumeDropLines > 0) {
                    if (--s_resumeDropLines == 0) s_resumeDropArmed = false;
                    continue;
                }

                xSemaphoreTake(s_mutex, portMAX_DELAY);
                strlcpy(s_localCurrentLine, stripped, sizeof(s_localCurrentLine));
                if (s_heightMapEnabled && s_probeGridTotal > 0) {
                    size_t clen = strlen(s_localCurrentLine);
                    clen = grblApplyHeightMapToLine(s_localCurrentLine, clen);
                    s_localCurrentLine[clen] = '\0';
                }
                s_localCurrentLineActive = true;
                s_localCurrentRetry = 0;
                xSemaphoreGive(s_mutex);
                break;
            }
        }

        if (s_localCurrentLineActive) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            const size_t slen = strlen(s_localCurrentLine);
            bool sent = false;
            if (slen > 0) sent = sendLineLocked(s_localCurrentLine, slen);
            if (sent) s_linesSent++;
            xSemaphoreGive(s_mutex);
        }

        if (!s_localCurrentLineActive && s_localQueuePos >= s_localQueueLen) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if (s_state == GrblStreamState::STREAMING && s_jobSource == GrblJobSource::LOCAL_QUEUE) {
                // Keep STREAMING while draining remaining acknowledgements.
                s_jobSource = GrblJobSource::NONE;
                s_doneStableSinceMs = 0;
                directLogPrintf("[GRBL] local queue read complete, waiting for final acks (inflight=%u)\n",
                                (unsigned)s_inflight);
            }
            xSemaphoreGive(s_mutex);
        }
        return;
    }

#if ENABLE_SDCARD == 1
    if (source == GrblJobSource::FILE_STREAM) {
        // PSRAM mode: file is already closed (entire file loaded into PSRAM).
        // Staged mode: file must be open. Skip this guard if PSRAM is active.
        if (!s_file && !s_streamPsramBuf) return;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        const bool retryPendingPause = (s_state == GrblStreamState::PAUSED &&
                                        strcmp(s_pauseReason, "grbl_error_retry_pending") == 0 &&
                                        s_fileRetryLine[0] != '\0');
        const bool retryCanSendNow = retryPendingPause && (s_inflight == 0);
        xSemaphoreGive(s_mutex);

        if (retryCanSendNow) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            const size_t retryLen = strlen(s_fileRetryLine);
            bool retrySent = (retryLen > 0) ? sendLineLocked(s_fileRetryLine, retryLen) : false;
            if (retrySent) {
                s_linesSent++;
                s_fileRecoveryAckUntil = s_linesAcked + (uint32_t)GRBL_STREAM_BURST_RECOVERY_ACK_LINES;
                s_state = GrblStreamState::STREAMING;
                if (s_holdStartMs > 0) {
                    uint32_t nowMs = millis();
                    if (nowMs > s_holdStartMs) s_holdAccumMs += (nowMs - s_holdStartMs);
                    s_holdStartMs = 0;
                }
                s_pauseReason[0] = '\0';
                setWorkflowStateLocked(GrblWorkflowState::RUNNING, "file_retry_sent");
                jobLogEventLocked("RETRY_FILE_SENT", s_fileRetryLine);
            }
            xSemaphoreGive(s_mutex);

            if (retrySent) {
                directLogPrintf("[GRBL] file retry sent (%u/%u): %s\n",
                                (unsigned)s_fileRetryCount,
                                (unsigned)GRBL_FILE_STREAM_RETRY_MAX,
                                s_fileRetryLine);
            }
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        const bool streamNowActive = (s_state == GrblStreamState::STREAMING && s_jobSource == GrblJobSource::FILE_STREAM);
        xSemaphoreGive(s_mutex);
        if (!streamNowActive) return;

        // If PSRAM has the entire file, SD/staging fill is unnecessary.
        // For files >4MB, PSRAM is refilled below in the "Refill PSRAM" block.
        if (!s_streamPsramBuf || s_streamPsramLen == 0) {
            fillStreamStageFromFile();
        } else {
            // Mark stage as EOF so the stall watchdog does not fire.
            s_streamStageEof = true;
        }

        // If SD feeding makes no progress for too long and staging is empty,
        // pause explicitly instead of appearing stuck.
        // Skip this watchdog when PSRAM is active (file already in RAM, no SD reads).
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        const bool streamActive = (s_state == GrblStreamState::STREAMING && s_jobSource == GrblJobSource::FILE_STREAM);
        const bool stageEmpty = (streamStageBytesAvailable() == 0);
        const bool noInflight = (s_inflight == 0);
        const bool notEof = !s_streamStageEof;
        const bool stalled = (nowRateMs - s_streamStageLastProgressMs) >= GRBL_SD_READ_STALL_TIMEOUT_MS;
        const bool usingPsram = (s_streamPsramBuf != nullptr && s_streamPsramLen > 0);
        if (streamActive && stageEmpty && noInflight && notEof && stalled && !usingPsram) {
            if (strcmp(s_pauseReason, "sd_read_stall") != 0) {
                notifySdReadStall = true;
            }
            s_state = GrblStreamState::PAUSED;
            strlcpy(s_pauseReason, "sd_read_stall", sizeof(s_pauseReason));
            strlcpy(s_errorReason, "sd_read_stall", sizeof(s_errorReason));
            if (s_holdStartMs == 0) s_holdStartMs = nowRateMs;
            setWorkflowStateLocked(GrblWorkflowState::HOLD, "sd_read_stall");
        }
        xSemaphoreGive(s_mutex);

        if (notifySdReadStall) {
            directLogPrintf("[GRBL] stream paused by SD read stall watchdog (>%lu ms without SD progress)\n",
                            (unsigned long)GRBL_SD_READ_STALL_TIMEOUT_MS);
            sendVirtualMessage(F("⚠️ GRBL: stream paused (no SD read progress)"), WARN);
            publishProductionEvent("hold", "sd_read_stall");
            return;
        }

        // Fill the PSRAM staging buffer (producer side).
        // The dedicated grblStreamTask (consumer) handles the actual sending to GRBL.
        // This separation prevents planner starvation for high-density engraving.
        // --- Refill PSRAM buffer if file >4MB and PSRAM is exhausted ---
        if (s_streamPsramBuf && !s_streamPsramEof && s_file) {
            if (s_streamPsramPos >= s_streamPsramLen) {
                // All bytes consumed — reset buffer for next chunk.
                s_streamPsramLen = 0;
                s_streamPsramPos = 0;
            }
            // Read next chunk from SD into the free space.
            const size_t freeSpace = s_streamPsramCap - s_streamPsramLen;
            if (freeSpace > 0) {
                const size_t n = s_file.read((uint8_t*)(s_streamPsramBuf + s_streamPsramLen), freeSpace);
                if (n > 0) {
                    s_streamPsramLen += n;
                    s_streamPsramEof = !s_file.available();
                    directLogPrintf("[GRBL] Refilled PSRAM: +%lu bytes (total %lu, eof=%d)\n",
                                    (unsigned long)n, (unsigned long)s_streamPsramLen, (int)s_streamPsramEof);
                    if (s_streamPsramEof) {
                        s_file.close();
                        sendVirtualMessage("🔍 GRBL: PSRAM refill complete, file closed", INFO);
                    }
                } else {
                    // Read returned 0 — treat as EOF.
                    s_streamPsramEof = true;
                    if (s_file) s_file.close();
                    directLogPrintf("[GRBL] PSRAM refill got 0 bytes, treating as EOF\n");
                }
            }
        }

        // --- Mid-file feed watchdog ---
        // Catches the PSRAM-mode stall that sd_read_stall skips: file not
        // finished, nothing in flight and the PSRAM position stops advancing
        // (SD refill stuck OR USB TX dead). Pause explicitly instead of
        // leaving the job silently "running" with the machine idle.
        // s_linesSent > 0: never fire during startup — before the first batch
        // is sent the position is 0 by definition, so a stale progress timer
        // (e.g. from boot or a previous stream) must not abort the new job.
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        const bool feedStreamActive = (s_state == GrblStreamState::STREAMING && s_jobSource == GrblJobSource::FILE_STREAM);
        const bool feedPsramActive = (s_streamPsramBuf != nullptr && s_streamPsramLen > 0);
        const bool feedNotDone = !(s_streamPsramBuf && s_streamPsramPos >= s_streamPsramLen && s_streamPsramEof);
        if (feedStreamActive && feedPsramActive && feedNotDone && s_inflight == 0 && s_linesSent > 0) {
            if (s_streamPsramPos != s_lastFeedPos) {
                s_lastFeedPos = s_streamPsramPos;
                s_lastFeedProgressMs = nowRateMs;
            } else if ((nowRateMs - s_lastFeedProgressMs) >= GRBL_FEED_STALL_TIMEOUT_MS) {
                if (strcmp(s_pauseReason, "feed_stall") != 0) {
                    notifyFeedStall = true;
                }
                s_state = GrblStreamState::PAUSED;
                strlcpy(s_pauseReason, "feed_stall", sizeof(s_pauseReason));
                strlcpy(s_errorReason, "feed_stall", sizeof(s_errorReason));
                if (s_holdStartMs == 0) s_holdStartMs = nowRateMs;
                setWorkflowStateLocked(GrblWorkflowState::HOLD, "feed_stall");
            }
        } else {
            s_lastFeedPos = s_streamPsramPos;
            s_lastFeedProgressMs = nowRateMs;
        }
        xSemaphoreGive(s_mutex);

        if (notifyFeedStall) {
            directLogPrintf("[GRBL] stream paused by feed stall watchdog (>%lu ms, pos=%lu/%lu, inflight=0)\n",
                            (unsigned long)GRBL_FEED_STALL_TIMEOUT_MS,
                            (unsigned long)s_streamPsramPos, (unsigned long)s_streamPsramLen);
            sendVirtualMessage(F("⚠️ GRBL: stream paused (feeder stalled — no data flowing)"), WARN);
            publishProductionEvent("hold", "feed_stall");
            return;
        }

        // Fill GRBL buffer from PSRAM — CONSUMER IS psramFeedTask() ONLY.
        // grblSenderUpdate() is the PRODUCER (refill from SD).
        // This guard prevents two feeders competing for the same TX buffer.
        // s_inflight is already bytes (not lines * avg), so any burst budget
        // calculation that multiplies inflight by avg_line/256 would be wrong.
        // We let the dedicated high-priority task handle all TX.
        (void)0;

        const int preparedResult = fillPreparedQueueFromStage();
        if (preparedResult < 0) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            strlcpy(s_errorReason, "line_too_long", sizeof(s_errorReason));
            s_state = GrblStreamState::ERROR;
            s_jobSource = GrblJobSource::NONE;
            s_pauseReason[0] = '\0';
            s_holdStartMs = 0;
            s_jobEndMs = millis();
            setWorkflowStateLocked(GrblWorkflowState::ERROR, "line_too_long");
#if ENABLE_SDCARD == 1
            if (s_file) s_file.close();
#endif
            resetStreamStageState();
            xSemaphoreGive(s_mutex);
            sendVirtualMessage(("❌ GRBL: linha demasiado longa durante stream (max " +
                                   String((unsigned)GRBL_MAX_LINE_LEN) + ")").c_str(), ERROR);
            return;
        }

        // Staged mode (PSRAM allocation failed / file larger than the PSRAM
        // cap): the prepared queue is filled from the SD stage buffer above.
        // Drain it to GRBL via sendLineLocked (which enforces the in-flight
        // budget); lines it cannot accept now stay queued for the next cycle.
        // When PSRAM is active the dedicated feed task owns TX, so skip here.
        if (!s_streamPsramBuf) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            while (!preparedQueueIsEmpty()) {
                const char* pline = nullptr;
                size_t plen = 0;
                if (!preparedQueuePeek(&pline, &plen) || !pline || plen == 0) {
                    preparedQueuePop();
                    continue;
                }
                char comp[GRBL_MAX_LINE_LEN + 1];
                memcpy(comp, pline, plen);
                comp[plen] = '\0';
                if (s_heightMapEnabled && s_probeGridTotal > 0) {
                    plen = grblApplyHeightMapToLine(comp, plen);
                    pline = comp;
                }
                const bool sent = sendLineLocked(pline, plen);
                if (!sent) break; // RX buffer full — retry next cycle
                preparedQueuePop();
                s_linesSent++;
            }
            xSemaphoreGive(s_mutex);
        }

        // Check if we have finished reading, parsing and sending all prepared lines.
        // When PSRAM is active, only psramDone matters — the staged pipeline is unused.
        const bool psramDone = (s_streamPsramBuf && s_streamPsramPos >= s_streamPsramLen && s_streamPsramEof);
        const bool stageDone = !s_streamPsramBuf && s_streamStageEof && streamStageBytesAvailable() == 0 && preparedQueueIsEmpty();
        
        // Log PSRAM completion status
        if (s_streamPsramBuf && (millis() - s_lastCompletionLog > 3000)) {
            uint32_t progress = (s_streamPsramPos * 100) / s_streamPsramLen;
            directLogPrintf("[GRBL] PSRAM status: pos=%lu/%lu (%lu%%), done=%d, eof=%d, inflight=%u\n",
                            (unsigned long)s_streamPsramPos, (unsigned long)s_streamPsramLen,
                            (unsigned long)progress, (int)psramDone, (int)s_streamPsramEof,
                            (unsigned)s_inflight);
            s_lastCompletionLog = millis();
        }
        
        if (psramDone || stageDone) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if (s_state == GrblStreamState::STREAMING) {
                // Drain remaining ACKs: wait for GRBL to acknowledge all lines.
                // Send '?' status queries to keep the link alive and flush ACKs.
                // If GRBL seems stuck (no ACK progress), send '~' cycle start
                // to wake it up without pausing the machine.
                uint32_t drainStartMs = millis();
                const uint32_t drainTimeoutMs = 5000UL; // max 5s wait
                uint32_t lastAcked = s_linesAcked;
                uint32_t stuckSinceMs = 0;
                while (s_inflight > 0 && s_state == GrblStreamState::STREAMING) {
                    uint32_t now = millis();
                    if (now - drainStartMs >= drainTimeoutMs) {
                        directLogPrintf("[GRBL] drain timeout after %lu ms, inflight=%u\n",
                                        (unsigned long)drainTimeoutMs, (unsigned)s_inflight);
                        break;
                    }
                    // Check if ACKs are progressing
                    if (s_linesAcked > lastAcked) {
                        lastAcked = s_linesAcked;
                        stuckSinceMs = 0;
                    } else if (stuckSinceMs == 0) {
                        stuckSinceMs = now;
                    }
                    // If stuck for >1s, send cycle start to wake GRBL
                    if (stuckSinceMs > 0 && (now - stuckSinceMs) > 1000UL) {
                        grblTransportWrite(s_slot, (const uint8_t*)"~", 1);
                        stuckSinceMs = 0; // reset to avoid spamming
                        directLogPrintf("[GRBL] drain: sent '~' to wake GRBL (inflight=%u)\n",
                                        (unsigned)s_inflight);
                    }
                    // Send status query to keep link alive
                    grblTransportWrite(s_slot, (const uint8_t*)"?", 1);
                    xSemaphoreGive(s_mutex);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    xSemaphoreTake(s_mutex, portMAX_DELAY);
                }
                directLogPrintf("[GRBL] file read complete, drained ACKs "
                                "(inflight=%u, lines_sent=%lu, lines_acked=%lu)\n",
                                (unsigned)s_inflight, (unsigned long)s_linesSent, (unsigned long)s_linesAcked);
                if (s_inflight > 0) {
                    s_inflight = 0;
                    s_fifoHead = 0;
                    s_fifoTail = 0;
                    s_fifoCount = 0;
                    directLogPrintf("[GRBL] drain timeout: forced inflight=0, fifo cleared\n");
                }
                // Keep STREAMING while draining remaining acknowledgements.
                s_jobSource = GrblJobSource::NONE;
                s_doneStableSinceMs = 0;
                if (s_file) s_file.close();
                s_streamPsramPos = 0;
                s_streamPsramLen = 0;
                s_streamPsramEof = false;
                resetStreamStageState();
            }
            xSemaphoreGive(s_mutex);
        }
    }
#endif // ENABLE_SDCARD

    // Probe grid executor — non-blocking state machine running probe cycles
    probeGridUpdate();

}

#endif // ENABLE_GRBL == 1
