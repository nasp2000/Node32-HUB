// grbl_sender_send.cpp
// FIFO tracking and G-code line sending for the GRBL sender.
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
// FIFO helpers
// ---------------------------------------------------------------------------

void fifoPush(uint8_t lineBytes, const char* line) {
    if (s_fifoCount >= GRBL_INFLIGHT_FIFO_SIZE) return; // overflow guard
    s_fifo[s_fifoHead] = lineBytes;
    if (line) strlcpy(s_fifoLines[s_fifoHead], line, (size_t)GRBL_MAX_LINE_LEN + 1U);
    else s_fifoLines[s_fifoHead][0] = '\0';
    s_fifoHead = (s_fifoHead + 1) % GRBL_INFLIGHT_FIFO_SIZE;
    s_fifoCount++;
}

bool firstConnectedUsbSlot(uint8_t* outSlot) {
    if (!outSlot) return false;
    for (uint8_t i = 0; i < USB_HOST_MAX_CDC_DEVICES; i++) {
        const UsbCdcDevice dev = grblTransportGetDevice(i);
        if (dev.connected) {
            *outSlot = i;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Internal send — takes mutex, checks buffer, sends, updates counters.
// Returns true if the line was sent.
// ---------------------------------------------------------------------------

bool sendLineLocked(const char* line, size_t len) {
    // len does NOT include '\n'. Wire size = len + 1.
    uint8_t wireBytes = (uint8_t)(len + 1);
    const uint8_t hardLimit = (GRBL_RX_BUFFER_SIZE < GRBL_RX_BUFFER_STRICT_LIMIT)
        ? (uint8_t)GRBL_RX_BUFFER_SIZE
        : (uint8_t)GRBL_RX_BUFFER_STRICT_LIMIT;
    uint8_t safetyMargin = 0U;
    if (s_jobSource == GrblJobSource::FILE_STREAM) {
        const bool linkLooksStable = (s_errorCount == 0) &&
                                     (s_pollOutstanding < 2) &&
                                     (s_lastStatusMs > 0) &&
                                     ((millis() - s_lastStatusMs) <= GRBL_STATUS_FRESH_WINDOW_MS);
        safetyMargin = (uint8_t)(linkLooksStable
            ? GRBL_RX_SAFETY_MARGIN_FILE_STREAM_STABLE
            : GRBL_RX_SAFETY_MARGIN_FILE_STREAM);
            
        // During PSRAM streaming, eliminate safety margins completely
        if (s_streamPsramBuf != nullptr) {
            safetyMargin = 0;
        }
    } else if (s_jobSource == GrblJobSource::LOCAL_QUEUE) {
        safetyMargin = (uint8_t)GRBL_RX_SAFETY_MARGIN_LOCAL_QUEUE;
    }
    const uint8_t softLimit = (hardLimit > safetyMargin) ? (uint8_t)(hardLimit - safetyMargin) : hardLimit;

    // Serialize critical persistent commands (UGS-style conservative behavior).
    // Allow during manual mode (NONE source) — user should always be able
    // to send $$, $H, $X, etc. regardless of stale inflight bytes.
    if (isCriticalPersistentCommand(line, len)) {
        if (s_jobSource != GrblJobSource::NONE && (s_inflight > 0 || s_fifoCount > 0)) {
            s_blockedByCriticalCommand++;
            recordBlockReasonLocked("critical_command", line);
            return false;
        }
    }

        // Guard 1: internal character-counting budget must stay within active limit.
    if (s_inflight >= softLimit) {
        s_blockedByInflight++;
        if (safetyMargin > 0) s_blockedBySafetyMargin++;
        recordBlockReasonLocked(safetyMargin > 0 ? "inflight_soft_limit" : "inflight_limit", line);
        return false;
    }
    const uint8_t remainingByInflight = (uint8_t)(softLimit - s_inflight);
    if (wireBytes > remainingByInflight) {
        s_blockedByInflight++;
        if (safetyMargin > 0) s_blockedBySafetyMargin++;
        recordBlockReasonLocked(safetyMargin > 0 ? "inflight_soft_limit" : "inflight_limit", line);
        return false;
    }



    // Guard 2: reported RX free (|Bf:planner,rx|).
    // Skip this guard when PSRAM streaming — it is too conservative and causes stalls.
    // The internal s_inflight counter is the reliable throttle with PSRAM.
    const uint32_t nowMs = millis();
    if (s_jobSource != GrblJobSource::FILE_STREAM) {
        const bool hasFreshStatus = s_hasMachineStatus && (s_lastStatusMs > 0) &&
                                    ((nowMs - s_lastStatusMs) <= GRBL_STATUS_FRESH_WINDOW_MS);
        if (hasFreshStatus && s_rxBufferFree > 0) {
            const uint8_t reportedFree = (s_rxBufferFree > softLimit) ? softLimit : (uint8_t)s_rxBufferFree;
            if (wireBytes > reportedFree) {
                s_blockedByReportedRx++;
                recordBlockReasonLocked("reported_rx_limit", line);
                return false;
            }
        }
    }

    // Final saturation guard (kept explicit for auditability).
    if ((uint16_t)s_inflight + (uint16_t)wireBytes > (uint16_t)softLimit) {
        s_blockedByInflight++;
        if (safetyMargin > 0) s_blockedBySafetyMargin++;
        recordBlockReasonLocked(safetyMargin > 0 ? "inflight_soft_limit" : "inflight_limit", line);
        return false;
    }
    if (s_fifoCount >= GRBL_INFLIGHT_FIFO_SIZE) {
        s_blockedByFifoFull++;
        recordBlockReasonLocked("fifo_full", line);
        return false;
    }

    // Build buffer with '\n'.
    char buf[GRBL_MAX_LINE_LEN + 2];
    memcpy(buf, line, len);
    buf[len]     = '\n';
    buf[len + 1] = '\0';
    strlcpy(s_lastSentLine, line, sizeof(s_lastSentLine));

    const bool allowFallbackSlot = (s_jobSource == GrblJobSource::NONE);
    const uint8_t useSlot = allowFallbackSlot ? resolveConnectedUsbSlot(s_slot) : s_slot;
    if (!grblTransportGetDevice(useSlot).connected) {
        recordBlockReasonLocked("usb_disconnected", line);
        return false;
    }
    s_slot = useSlot;
    const size_t expected = len + 1;
    const size_t sent = sendUsbPayloadWithRetry(useSlot, (const uint8_t*)buf, expected);
    if (sent == 0) {
        // USB TX failed — log but don't block the feeder.
        // The PSRAM task will retry on next notification.
        s_lastUsbTxZeroMs = millis();
        directLogPrintf("[GRBL] USB TX failed (will retry): %s\n", line);
        return false;
    }
    if (sent != expected) {
        directLogPrintf("[GRBL] USB TX incomplete after retries (%u/%u bytes). Entering ERROR to avoid corrupted job.\n",
                        (unsigned)sent, (unsigned)expected);
        recordBlockReasonLocked("usb_partial_tx_retry_exhausted", line);
        strlcpy(s_errorReason, "usb_partial_tx_retry_exhausted", sizeof(s_errorReason));
        s_state = GrblStreamState::ERROR;
        s_jobSource = GrblJobSource::NONE;
        s_pauseReason[0] = '\0';
        s_holdStartMs = 0;
        s_jobEndMs = millis();
        setWorkflowStateLocked(GrblWorkflowState::ERROR, "usb_partial_tx_retry_exhausted");
    #if ENABLE_SDCARD == 1
        if (s_file) s_file.close();
    #endif
        resetStreamStageState();
        return false;
    }

    s_inflight += wireBytes;
    fifoPush(wireBytes, line);

    // Detect M6/M06 tool change command — set pause-request flag.
    // Match "M6" or "M06" but not "M60", "M600", etc.
    if (!s_toolChangePending) {
        bool isM6 = false;
        for (const char* p = line; *p; ++p) {
            if (*p == 'M' || *p == 'm') {
                if (*(p+1) == '6' || *(p+1) == '6') {
                    if (*(p+1) == '6') {
                        // M6 — check next char is not digit
                        char a = *(p+2);
                        if (a == '\0' || a == ' ' || a == '\t' || a == '(' || a == ';') isM6 = true;
                    }
                } else if (*(p+1) == '0' && (*(p+2) == '6' || *(p+2) == '6')) {
                    // M06
                    char a = *(p+3);
                    if (a == '\0' || a == ' ' || a == '\t' || a == '(' || a == ';') isM6 = true;
                }
            }
            if (isM6) break;
        }
        if (isM6) {
            s_toolChangePending = true;
            directLogPrintf("[GRBL] M6 tool change detected, will pause on ack\n");
        }
    }

    jobLogEventLocked("TX", line);
    return true;
}

bool sendRealtimeLocked(char c) {
    const uint8_t useSlot = resolveConnectedUsbSlot(s_slot);
    s_slot = useSlot;
    return grblTransportWrite(useSlot, (const uint8_t*)&c, 1) == 1;
}

bool localQueueReadNextRawLine(char* out, size_t outSize, size_t* lineStart_pos) {
    if (!out || outSize < 2) return false;
    if (s_localQueuePos >= s_localQueueLen) return false;

    if (lineStart_pos) *lineStart_pos = s_localQueuePos;

    size_t n = 0;
    while (s_localQueuePos < s_localQueueLen && s_localQueueBuf[s_localQueuePos] != '\n') {
        char c = s_localQueueBuf[s_localQueuePos++];
        if (c != '\r' && n < outSize - 1) out[n++] = c;
    }
    if (s_localQueuePos < s_localQueueLen && s_localQueueBuf[s_localQueuePos] == '\n') s_localQueuePos++;
    out[n] = '\0';
    return true;
}

#endif // ENABLE_GRBL == 1
