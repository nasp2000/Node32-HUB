// grbl_sender_api.cpp
// Public API for the GRBL sender.
#include "feature_flags.h"
#include "grbl_sender_internal.h"
#include "grbl_resume_scan.h"

#if ENABLE_GRBL == 1

#include <string.h>
#include <stdio.h>
#include "psram_helpers.h"
#include "ConfigManager.h"
#include "grbl_psram_gcode.h"
#include <Preferences.h>
#include "commands.h"

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

extern ConfigManager configManager;

// Public API
// ---------------------------------------------------------------------------

void grblSenderInit() {
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC)
    // S3 CNC pack: USB OTG only. The USB Host stack is compiled in
    // (ENABLE_USB_HOST==1) but NOT started at boot — it starts lazily when
    // the user presses the USB OTG toggle in the web transport selector.
    // This avoids the ~30 s reboot loop that occurred when usbHostBegin()
    // ran alongside Serial2 from boot. UART (Serial2) is disabled; the
    // native USB port (GPIO19/20) stays idle until the user activates OTG.
    s_grblTransport = GrblTransportMode::USB;
#else
    // P4 CNC pack: USB Host (OTG) only — no UART transport is wired (pins are
    // reserved for SDIO/C6), so the transport is always USB and the selector
    // is hidden in the web UI.
    s_grblTransport = GrblTransportMode::USB;
#endif
    if (!ensureRuntimeBuffers()) {
        s_state = GrblStreamState::ERROR;
        s_jobSource = GrblJobSource::NONE;
        s_workflowState = GrblWorkflowState::ERROR;
        return;
    }
    s_state      = GrblStreamState::IDLE;
    s_jobSource  = GrblJobSource::NONE;
    s_workflowState = GrblWorkflowState::IDLE;
    s_cancellingSinceMs = 0;
    s_inflight   = 0;
    s_linesSent  = 0;
    s_linesTotal = 0;
    s_linesAcked = 0;
    s_errorCount = 0;
    s_recentErrorHead = 0;
    s_recentErrorCount = 0;
    s_pollOutstanding = 0;
    s_lastStatusMs = 0;
    s_lastPositionMs = 0;
    s_lastRxMs = 0;
    s_doneStableSinceMs = 0;
    s_fifoHead   = 0;
    s_fifoTail   = 0;
    s_fifoCount  = 0;
    s_rxLen      = 0;
    s_filePath[0]= '\0';
    s_alarmActive = false;
    s_alarmMessage[0] = '\0';
    s_lastConnectedMask = 0;
    s_jobOrigin[0] = '\0';
    strlcpy(s_jobOrigin, "unknown", sizeof(s_jobOrigin));
    s_pauseReason[0] = '\0';
    s_errorReason[0] = '\0';
    s_lastSentLine[0] = '\0';
    s_jobStartMs = 0;
    s_jobEndMs = 0;
    s_holdAccumMs = 0;
    s_holdStartMs = 0;
    s_rateLastMs = millis();
    s_rateLastAck = 0;
    s_currentCmdPerMin = 0;
    s_blockedByInflight = 0;
    s_blockedByReportedRx = 0;
    s_blockedByFifoFull = 0;
    s_blockedByCriticalCommand = 0;
    s_blockedBySafetyMargin = 0;
    s_lastBlockReason[0] = '\0';
    s_lastBlockLine[0] = '\0';
    s_lastBlockMs = 0;
    s_queueContinueOnError = false;
    s_queueRetryMax = 0;
    s_localCurrentLine[0] = '\0';
    s_fileRetryLine[0] = '\0';
    s_fileRetryCount = 0;
    s_localCurrentLineActive = false;
    s_localCurrentRetry = 0;
    s_hasMachineStatus = false;
    s_hasMachinePosition = false;
    s_hasWorkPosition = false;
    s_doorOpen = false;
    s_probeTriggered = false;
    strlcpy(s_machineStateName, "UNKNOWN", sizeof(s_machineStateName));
    s_lastStatusRaw[0] = '\0';
    s_plannerBufferFree = 0;
    s_rxBufferFree = 0;
    s_localQueueLen = 0;
    s_localQueuePos = 0;
    s_fileRecoveryAckUntil = 0;
    s_lastUsbTxZeroMs = 0;
    s_lastAckProgressMs = 0;
    s_jobSawNonZeroMachinePos = false;
    s_jobSawNonZeroWorkPos = false;
    s_detectedAxisCount = 3;
    s_avgLineBytes = 30U;
    s_burstLinesSent = 0;
    s_burstCycles = 0;
    resetStreamStageState();

    {
        Preferences p;
        p.begin("esp32_config", true);
        s_jobLogEnabled = p.getBool("grbl_log", false);
        p.end();
        directLogPrintf("[GRBL] job log: %s (persisted)\n", s_jobLogEnabled ? "ON" : "OFF");
    }
}

void grblOnRxData(const uint8_t* data, size_t len, uint8_t usb_slot) {
    if (!ensureRuntimeBuffers()) {
        (void)usb_slot;
        grblDebugLog("[DBG-RX] grblOnRxData FAILED: ensureRuntimeBuffers");
        return;
    }
    if (len > 0) {
        grblDebugLog("[DBG-RX] grblOnRxData: %u bytes first=0x%02X", (unsigned)len, (unsigned)data[0]);
    }
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            if (s_rxLen > 0) {
                s_rxBuf[s_rxLen] = '\0';
                processGrblResponseLine(s_rxBuf);
                s_rxLen = 0;
            }
        } else if (s_rxLen < GRBL_MAX_LINE_LEN) {
            s_rxBuf[s_rxLen++] = c;
        }
    }
    (void)usb_slot;
}

bool grblSend(uint8_t usb_slot, const char* cmd) {
    if (!ensureRuntimeBuffers()) return false;
    if (!cmd || !*cmd) return false;
    char stripped[GRBL_MAX_LINE_LEN + 1];
    size_t len = stripLine(cmd, stripped, sizeof(stripped));
    if (len == 0) return true;

    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        vTaskDelay(pdMS_TO_TICKS(5));
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            return false;
        }
    }
    s_slot = resolveConnectedUsbSlot(usb_slot);
    if (s_workflowState == GrblWorkflowState::DONE || s_workflowState == GrblWorkflowState::ERROR) {
        setWorkflowStateLocked(GrblWorkflowState::IDLE, "manual_command_after_terminal_state");
    }
    if (strcmp(stripped, "$X") == 0 || strcmp(stripped, "$H") == 0 || strcmp(stripped, "\x18") == 0) {
        s_alarmActive = false;
        s_alarmMessage[0] = '\0';
        s_errorReason[0] = '\0';
    }
    bool ok = false;
    if (len == 1 && (stripped[0] == '?' || stripped[0] == '!' || stripped[0] == '~' || stripped[0] == '\x18')) {
        ok = sendRealtimeLocked(stripped[0]);
    } else if (isRepeatedRealtimeStatusQuery(stripped, len)) {
        ok = true;
        size_t i;
        for (i = 0; i < len; i++) {
            if (!sendRealtimeLocked('?')) {
                ok = false;
                break;
            }
        }
    } else {
        ok = sendLineLocked(stripped, len);
        if (ok) {
            s_linesSent++;
            (void)sendRealtimeLocked('?');
        }
    }
    xSemaphoreGive(s_mutex);
    return ok;
}

bool grblSetOverride(uint8_t usb_slot, int feedPct, int spinPct) {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (feedPct < 0 && spinPct < 0) return true;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const uint8_t useSlot = resolveConnectedUsbSlot(usb_slot);
    s_slot = useSlot;
    bool ok = true;
    if (feedPct >= 0) {
        if (feedPct < 10) feedPct = 10;
        if (feedPct > 200) feedPct = 200;
        // GRBL v1.1 real-time override bytes:
        //   0x90 feed 100%, 0x93 feed +1%, 0x94 feed -1%.
        // Reset to 100% then step by +1%/-1% so the result is exact regardless
        // of the current internal override state. GRBL clamps 10%-200%.
        ok = sendRealtimeLocked((char)0x90) && ok; // feed 100%
        int i;
        if (feedPct > 100) {
            for (i = 0; i < feedPct - 100 && ok; i++) ok = sendRealtimeLocked((char)0x93); // feed +1%
        } else if (feedPct < 100) {
            for (i = 0; i < 100 - feedPct && ok; i++) ok = sendRealtimeLocked((char)0x94); // feed -1%
        }
    }
    if (spinPct >= 0) {
        if (spinPct < 10) spinPct = 10;
        if (spinPct > 200) spinPct = 200;
        // GRBL v1.1 real-time override bytes:
        //   0x99 spindle 100%, 0x9C spindle +1%, 0x9D spindle -1%.
        ok = sendRealtimeLocked((char)0x99) && ok; // spindle 100%
        int j;
        if (spinPct > 100) {
            for (j = 0; j < spinPct - 100 && ok; j++) ok = sendRealtimeLocked((char)0x9C); // spindle +1%
        } else if (spinPct < 100) {
            for (j = 0; j < 100 - spinPct && ok; j++) ok = sendRealtimeLocked((char)0x9D); // spindle -1%
        }
    }
    xSemaphoreGive(s_mutex);
    return ok;
}

bool grblStreamFile(uint8_t usb_slot, const char* path) {
    sendVirtualMessage(("🔍 GRBL: loading file " + String(path ? path : "(null)")).c_str(), INFO);
    if (!ensureRuntimeBuffers()) return false;
#if ENABLE_SDCARD == 0
    (void)usb_slot; (void)path;
    sendVirtualMessage(F("❌ GRBL stream: SD not available"), ERROR);
    return false;
#else
    if (!path || !*path) return false;
    if (!ensureStreamStageBuffer()) return false;
    if (!s_mutex) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_resumeBusy) {
        xSemaphoreGive(s_mutex);
        directLogPrintf("[GRBL] streamFile: rejected, resume flow running\n");
        return false;
    }
    if (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED) {
        xSemaphoreGive(s_mutex);
        directLogPrintf("[GRBL] streamFile: already streaming\n");
        return false;
    }
    xSemaphoreGive(s_mutex);

    grblStopPsramFeedTask();
    vTaskDelay(pdMS_TO_TICKS(10));

    if (s_jobLogActive) {
        jobLogFlush(true);
    }

#if SDCARD_USE_SPI_MODE == 1
    s_file = SD.open(path, FILE_READ);
#else
    s_file = SD_MMC.open(path, FILE_READ);
#endif
    if (!s_file) {
        directLogPrintf("[GRBL] streamFile: cannot open %s\n", path);
        sendVirtualMessage(("❌ GRBL: file not found: " + String(path)).c_str(), ERROR);
        return false;
    }

    uint32_t totalLines = 0;

    const size_t fileBytes = s_file.size();
    const size_t allocSize = (fileBytes < GRBL_STREAM_PSRAM_MAX_BYTES) ? fileBytes : GRBL_STREAM_PSRAM_MAX_BYTES;
    if (s_streamPsramBuf == nullptr || s_streamPsramCap < allocSize) {
        if (s_streamPsramBuf) { heap_caps_free(s_streamPsramBuf); s_streamPsramBuf = nullptr; }
        s_streamPsramBuf = (char*)heap_caps_calloc(allocSize, 1, MALLOC_CAP_SPIRAM);
        if (s_streamPsramBuf) {
            s_streamPsramCap = allocSize;
            directLogPrintf("[GRBL] PSRAM alloc success: %lu bytes at %p\n", (unsigned long)allocSize, s_streamPsramBuf);
        } else {
            s_streamPsramCap = 0;
            directLogPrintf("[GRBL] ERROR: PSRAM alloc %lu failed! Fallback to staged pipeline.\n", (unsigned long)allocSize);
            multi_heap_info_t info;
            heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
            directLogPrintf("[GRBL] PSRAM info: total_free=%lu, largest_free=%lu\n",
                            (unsigned long)info.total_free_bytes, (unsigned long)info.largest_free_block);
        }
    }

    if (s_streamPsramBuf) {
        directLogPrintf("[GRBL] PSRAM buf=%p, allocSize=%lu, fileBytes=%lu\n",
                        s_streamPsramBuf, (unsigned long)allocSize, (unsigned long)fileBytes);
        const size_t readBytes = s_file.read((uint8_t*)s_streamPsramBuf, allocSize);
        directLogPrintf("[GRBL] PSRAM readBytes=%lu, file.position=%lu\n",
                        (unsigned long)readBytes, (unsigned long)s_file.position());
        if (readBytes == 0) {
            directLogPrintf("[GRBL] ERROR: PSRAM read failed! file.available=%d, file.size=%lu\n",
                            s_file.available(), (unsigned long)s_file.size());
        } else {
            directLogPrintf("[GRBL] PSRAM loaded %lu bytes\n", (unsigned long)readBytes);
        }
        s_streamPsramLen = readBytes;
        s_streamPsramPos = 0;
        const size_t beforePreprocess = readBytes;
        s_streamPsramLen = preprocessGcodeInPsram(s_streamPsramBuf, readBytes);
        directLogPrintf("[GRBL] Preprocessed G-code: %lu -> %lu bytes (%lu%% of original)\n",
                        (unsigned long)beforePreprocess, (unsigned long)s_streamPsramLen,
                        s_streamPsramLen > 0 ? (unsigned long)((s_streamPsramLen * 100) / beforePreprocess) : 0UL);
        totalLines = 0;
        for (size_t i = 0; i < s_streamPsramLen; i++) {
            if (s_streamPsramBuf[i] == '\n') totalLines++;
        }
        s_streamPsramEof = (readBytes >= fileBytes);
        if (s_streamPsramEof) {
            s_file.close();
            directLogPrintf("[GRBL] Loaded %lu bytes into PSRAM (entire file)\n", (unsigned long)readBytes);
        } else {
            directLogPrintf("[GRBL] Loaded %lu bytes into PSRAM (%lu left on SD, file kept open)\n",
                            (unsigned long)readBytes, (unsigned long)(fileBytes - readBytes));
        }
    } else {
        directLogPrintf("[GRBL] PSRAM alloc failed, using staged pipeline\n");
    }

    resetStreamStageState();
    if (!s_streamPsramBuf || !s_streamPsramEof) {
        fillStreamStageFromFile();
    }
    if (!s_streamPsramBuf || !s_streamPsramEof) {
        fillStreamStageFromFile();
    }

    if (totalLines == 0 && s_file) {
        s_file.seek(0);
        while (s_file.available()) {
            int c = s_file.read();
            if (c == '\n') totalLines++;
        }
        s_file.seek(0);
        directLogPrintf("[GRBL] Fallback line count: %lu\n", (unsigned long)totalLines);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(s_filePath, path, sizeof(s_filePath));
    s_slot       = resolveConnectedUsbSlot(usb_slot);
    s_linesTotal = totalLines;
    s_linesSent  = 0;
    s_linesAcked = 0;
    s_errorCount = 0;
    s_inflight   = 0;
    s_fifoHead   = 0;
    s_fifoTail   = 0;
    s_fifoCount  = 0;
    s_state      = GrblStreamState::STREAMING;
    s_jobSource  = GrblJobSource::FILE_STREAM;
    // Resume with offset: consume a pending skip when this is the same
    // source, else drop stale resume state (fresh job supersedes it).
    grblConsumeResumeSkipLocked(path);
    grblHeightMapResetTracker(); // new job — do not inherit the previous position
    setWorkflowStateLocked(GrblWorkflowState::RUNNING, "file_stream_start");
    s_alarmActive = false;
    s_alarmMessage[0] = '\0';
    s_pauseReason[0] = '\0';
    s_errorReason[0] = '\0';
    s_jobStartMs = millis();
    s_jobEndMs = 0;
    s_lastRxMs = s_jobStartMs;
    s_doneStableSinceMs = 0;
    s_holdAccumMs = 0;
    s_holdStartMs = 0;
    s_rateLastMs = s_jobStartMs;
    s_rateLastAck = 0;
    s_currentCmdPerMin = 0;
    s_localCurrentLine[0] = '\0';
    s_fileRetryLine[0] = '\0';
    s_fileRetryCount = 0;
    s_fileRecoveryAckUntil = 0;
    s_lastUsbTxZeroMs = 0;
    s_lastAckProgressMs = s_jobStartMs;
    s_jobSawNonZeroMachinePos = false;
    s_jobSawNonZeroWorkPos = false;
    s_detectedAxisCount = 3;
    s_avgLineBytes = 30U;
    s_burstLinesSent = 0;
    s_burstCycles = 0;
    s_localCurrentLineActive = false;
    s_localCurrentRetry = 0;
    const bool qlogOpened = jobLogOpenLocked("file_stream", path);
    if (!qlogOpened) {
        directLogPrintf("[GRBL] job log unavailable for file stream: %s\n", path);
    }
    xSemaphoreGive(s_mutex);

    directLogPrintf("[GRBL] streaming %s (%lu lines)\n", path, (unsigned long)totalLines);
    sendVirtualMessage(("▶️ GRBL stream started: " + String(path) +
                           " (" + String(totalLines) + " lines)").c_str(), INFO);

    {
        const uint32_t guardStart = millis();
        bool gotStatus = false;
        while ((millis() - guardStart) < 2000UL) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            gotStatus = s_hasMachineStatus && (s_lastStatusMs > 0) &&
                        ((millis() - s_lastStatusMs) <= 2000UL);
            xSemaphoreGive(s_mutex);
            if (gotStatus) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (gotStatus) {
            directLogPrintf("[GRBL] startup guard: GRBL ready after %lu ms\n",
                            (unsigned long)(millis() - guardStart));
        } else {
            directLogPrintf("[GRBL] startup guard: no GRBL status after %lu ms, starting anyway\n",
                            (unsigned long)(millis() - guardStart));
        }
    }

    bool alarmClearSent = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool alarm = s_alarmActive;
    xSemaphoreGive(s_mutex);
    if (alarm) {
        sendVirtualMessage(F("⚠️ GRBL: alarm detectado a enviar $X unlock"), WARN);
        const char* unlock = "$X\n";
        grblTransportWrite(s_slot, (const uint8_t*)unlock, strlen(unlock));
        vTaskDelay(pdMS_TO_TICKS(100));
        alarmClearSent = true;
    }

    {
        const uint32_t idleWaitStart = millis();
        bool grblIdle = false;
        while ((millis() - idleWaitStart) < 5000UL) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            const bool isIdle = (strcasecmp(s_machineStateName, "IDLE") == 0);
            const bool hasStatus = s_hasMachineStatus && (s_lastStatusMs > 0);
            xSemaphoreGive(s_mutex);
            if (hasStatus && isIdle) {
                grblIdle = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (!grblIdle) {
            directLogPrintf("[GRBL] WARNING: GRBL not idle after 5s wait (state=%s), starting feeder anyway\n",
                            s_machineStateName);
            sendVirtualMessage(F("⚠️ GRBL: state is not Idle — starting anyway"), WARN);
        } else {
            directLogPrintf("[GRBL] GRBL is idle, starting feeder\n");
        }
    }

    {
        // Short planner flush: 0.2s dwell to clear any residual motion queue.
        // We already waited for Idle state before this point, so 200ms is sufficient.
        static const char PLANNER_PREFILL_DWELL[] = "G4 P0.2\n";
        const uint8_t dwellBytes = sizeof(PLANNER_PREFILL_DWELL) - 1;

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_inflight += dwellBytes;
        fifoPush(dwellBytes, nullptr);
        xSemaphoreGive(s_mutex);

        sendUsbPayloadWithRetry(s_slot, (const uint8_t*)PLANNER_PREFILL_DWELL, dwellBytes);

        const uint32_t g4WaitStart = millis();
        while ((millis() - g4WaitStart) < 3000UL) {
            bool done = false;
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            done = (s_inflight == 0);
            xSemaphoreGive(s_mutex);
            if (done) break;
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        // Cold-start prefill is handled by psramFeedTask (grbl_sender_stream.cpp):
        // its startup-burst logic sends the first batch at the startup burst limit
        // (200 bytes) when s_linesSent == 0, then refills continuously on every
        // ACK — the same mechanism that keeps the buffer full in steady state.
        // The previous 6-burst prefill here waited for the GRBL buffer to fully
        // drain between bursts, which starved micro-step lines and caused visible
        // first-piece stops.
    }

    if (s_streamPsramBuf) {
        grblStartPsramFeedTask();
        psramFeedNotify();
    }

    {
        // Diagnostic for the P4 async_tcp crash: report peak stack usage of the
        // task that ran the stream-start (async_tcp when called via the HTTP
        // handler). A value below ~1KB means the task is close to overflowing.
        const UBaseType_t hwmBytes = uxTaskGetStackHighWaterMark(NULL);
        directLogPrintf("[GRBL] streamFile done: task=%s stackHighWater=%u bytes\n",
                        pcTaskGetTaskName(NULL), (unsigned)hwmBytes);
    }

    return true;
#endif
}

bool grblStreamPsramData(uint8_t usb_slot, const char* label) {
    if (!ensureRuntimeBuffers()) return false;
    if (!ensureStreamStageBuffer()) return false;
    if (!s_mutex) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_resumeBusy) {
        xSemaphoreGive(s_mutex);
        directLogPrintf("[GRBL] streamPsramData: rejected, resume flow running\n");
        return false;
    }
    if (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED) {
        xSemaphoreGive(s_mutex);
        directLogPrintf("[GRBL] streamPsramData: already streaming\n");
        return false;
    }
    xSemaphoreGive(s_mutex);

    grblStopPsramFeedTask();
    vTaskDelay(pdMS_TO_TICKS(10));

    if (s_jobLogActive) jobLogFlush(true);

    const size_t psramSize = grblPsramGcodeGetSize();
    if (!grblPsramGcodeGetData() || psramSize == 0) {
        sendVirtualMessage(F("❌ GRBL: PSRAM G-code buffer empty"), ERROR);
        return false;
    }

    char psramName[128];
    {
        const char* src = grblPsramGcodeGetName();
        strncpy(psramName, src ? src : "", sizeof(psramName) - 1);
        psramName[sizeof(psramName) - 1] = '\0';
    }

    const size_t allocSize = (psramSize < GRBL_STREAM_PSRAM_MAX_BYTES) ? psramSize : GRBL_STREAM_PSRAM_MAX_BYTES;
    if (s_streamPsramBuf) heap_caps_free(s_streamPsramBuf);
    s_streamPsramBuf = grblPsramGcodeRelease();
    s_streamPsramCap = allocSize;
    s_streamPsramLen = allocSize;
    s_streamPsramPos = 0;

    const size_t beforePreprocess = s_streamPsramLen;
    s_streamPsramLen = preprocessGcodeInPsram(s_streamPsramBuf, s_streamPsramLen);
    s_streamPsramEof = true;
    directLogPrintf("[GRBL] PSRAM data stream: %lu -> %lu bytes after preprocess\n",
                    (unsigned long)beforePreprocess, (unsigned long)s_streamPsramLen);

    uint32_t totalLines = 0;
    for (size_t i = 0; i < s_streamPsramLen; i++) {
        if (s_streamPsramBuf[i] == '\n') totalLines++;
    }

    resetStreamStageState();

    const char* displayPath = (label && *label) ? label :
                              (psramName[0]) ? psramName : "[PSRAM]";

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    strlcpy(s_filePath, displayPath, sizeof(s_filePath));
    s_slot       = resolveConnectedUsbSlot(usb_slot);
    s_linesTotal = totalLines;
    s_linesSent  = 0;
    s_linesAcked = 0;
    s_errorCount = 0;
    s_inflight   = 0;
    s_fifoHead   = 0;
    s_fifoTail   = 0;
    s_fifoCount  = 0;
    s_state      = GrblStreamState::STREAMING;
    s_jobSource  = GrblJobSource::FILE_STREAM;
    grblConsumeResumeSkipLocked(displayPath);
    grblHeightMapResetTracker();
    setWorkflowStateLocked(GrblWorkflowState::RUNNING, "psram_stream_start");
    s_alarmActive = false;
    s_alarmMessage[0] = '\0';
    s_pauseReason[0] = '\0';
    s_errorReason[0] = '\0';
    s_jobStartMs = millis();
    s_jobEndMs = 0;
    s_lastRxMs = s_jobStartMs;
    s_doneStableSinceMs = 0;
    s_holdAccumMs = 0;
    s_holdStartMs = 0;
    s_rateLastMs = s_jobStartMs;
    s_rateLastAck = 0;
    s_currentCmdPerMin = 0;
    s_localCurrentLine[0] = '\0';
    s_fileRetryLine[0] = '\0';
    s_fileRetryCount = 0;
    s_fileRecoveryAckUntil = 0;
    s_lastUsbTxZeroMs = 0;
    s_lastAckProgressMs = s_jobStartMs;
    s_jobSawNonZeroMachinePos = false;
    s_jobSawNonZeroWorkPos = false;
    s_detectedAxisCount = 3;
    s_avgLineBytes = 30U;
    s_burstLinesSent = 0;
    s_burstCycles = 0;
    s_localCurrentLineActive = false;
    s_localCurrentRetry = 0;
    const bool qlogOpened = jobLogOpenLocked("file_stream_psram", displayPath);
    if (!qlogOpened) {
        directLogPrintf("[GRBL] job log unavailable for PSRAM stream: %s\n", displayPath);
    }
    xSemaphoreGive(s_mutex);

    directLogPrintf("[GRBL] PSRAM streaming %s (%lu lines)\n", displayPath, (unsigned long)totalLines);
    sendVirtualMessage(("▶️ GRBL PSRAM stream: " + String(displayPath) +
                           " (" + String(totalLines) + " lines)").c_str(), INFO);

    {
        const uint32_t guardStart = millis();
        bool gotStatus = false;
        while ((millis() - guardStart) < 2000UL) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            gotStatus = s_hasMachineStatus && (s_lastStatusMs > 0) &&
                        ((millis() - s_lastStatusMs) <= 2000UL);
            xSemaphoreGive(s_mutex);
            if (gotStatus) break;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (gotStatus) {
            directLogPrintf("[GRBL] startup guard: GRBL ready after %lu ms\n",
                            (unsigned long)(millis() - guardStart));
        } else {
            directLogPrintf("[GRBL] startup guard: no GRBL status after %lu ms\n",
                            (unsigned long)(millis() - guardStart));
        }
    }

    bool alarmClearSent = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool alarm = s_alarmActive;
    xSemaphoreGive(s_mutex);
    if (alarm) {
        sendVirtualMessage(F("⚠️ GRBL: alarm detectado a enviar $X unlock"), WARN);
        const char* unlock = "$X\n";
        grblTransportWrite(s_slot, (const uint8_t*)unlock, strlen(unlock));
        vTaskDelay(pdMS_TO_TICKS(100));
        alarmClearSent = true;
    }

    {
        const uint32_t idleWaitStart = millis();
        bool grblIdle = false;
        while ((millis() - idleWaitStart) < 5000UL) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            const char* stateName = s_machineStateName;
            grblIdle = (strcmp(stateName, "Idle") == 0);
            xSemaphoreGive(s_mutex);
            if (grblIdle) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        directLogPrintf("[GRBL] idle wait %s after %lu ms\n",
                        grblIdle ? "PASSED" : "TIMEOUT",
                        (unsigned long)(millis() - idleWaitStart));
    }

    {
        // Cold-start prefill: more aggressive than the single 120-byte batch.
        // Use the startup burst limit (200 bytes) for the first burst, then
        // smaller follow-up bursts (120 bytes). Total prefill ~440 bytes.
        const uint16_t burstLimits[] = {
            (uint16_t)GRBL_RX_STARTUP_BURST_LIMIT,  // 200 bytes
            (uint16_t)GRBL_PSRAM_TARGET_BURST_BYTES, // 120 bytes
            (uint16_t)GRBL_PSRAM_TARGET_BURST_BYTES  // 120 bytes
        };
        for (uint8_t burst = 0; burst < 3; burst++) {
            char prefillBuf[GRBL_RX_STARTUP_BURST_LIMIT + 32];
            size_t prefillBytes = 0;
            uint8_t prefillCount = 0;
            uint8_t prefillLineBytes[32];
            size_t pos = s_streamPsramPos;
            const uint16_t limit = burstLimits[burst];

            while (pos < s_streamPsramLen
                   && prefillBytes < limit
                   && prefillCount < sizeof(prefillLineBytes)) {
                const size_t start = pos;
                while (pos < s_streamPsramLen && s_streamPsramBuf[pos] != '\n') pos++;
                size_t lineLen = pos - start;
                if (lineLen > 0 && s_streamPsramBuf[pos - 1] == '\r') lineLen--;
                const size_t nextPos = (pos < s_streamPsramLen) ? pos + 1 : s_streamPsramLen;
                if (lineLen == 0 || s_streamPsramBuf[start] == ';') {
                    pos = nextPos;
                    continue;
                }
                if (lineLen > GRBL_MAX_LINE_LEN) lineLen = GRBL_MAX_LINE_LEN;
                const uint8_t wb = (uint8_t)(lineLen + 1U);
                if ((size_t)prefillBytes + wb > (size_t)limit) break;
                memcpy(&prefillBuf[prefillBytes], &s_streamPsramBuf[start], lineLen);
                prefillBuf[prefillBytes + lineLen] = '\n';
                prefillBytes += wb;
                prefillLineBytes[prefillCount++] = wb;
                pos = nextPos;
            }

            if (prefillCount > 0) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_streamPsramPos = pos;
                for (uint8_t i = 0; i < prefillCount; i++) {
                    fifoPush(prefillLineBytes[i], nullptr);
                }
                s_inflight += (uint8_t)prefillBytes;
                s_linesSent += prefillCount;
                xSemaphoreGive(s_mutex);
                sendUsbPayloadWithRetry(s_slot, (const uint8_t*)prefillBuf, prefillBytes);
            } else {
                break;
            }
        }
    }

    if (s_streamPsramBuf) {
        grblStartPsramFeedTask();
        psramFeedNotify();
    }

    return true;
}

bool grblQueueStart(uint8_t usb_slot, const char* gcodeText) {
    if (!ensureRuntimeBuffers()) return false;
    if (!gcodeText || !*gcodeText) return false;
    if (!s_mutex) return false;

    const size_t rawLen = strlen(gcodeText);
    if (rawLen == 0 || rawLen >= GRBL_LOCAL_QUEUE_MAX_BYTES) return false;

    const uint32_t totalLines = localQueueCountLines(gcodeText);
    if (totalLines == 0) return false;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_resumeBusy) {
        xSemaphoreGive(s_mutex);
        directLogPrintf("[GRBL] queueStart: rejected, resume flow running\n");
        return false;
    }
    if (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    memcpy(s_localQueueBuf, gcodeText, rawLen);
    s_localQueueBuf[rawLen] = '\0';
    s_localQueueLen = rawLen;
    s_localQueuePos = 0;

    strlcpy(s_filePath, "[LOCAL_QUEUE]", sizeof(s_filePath));
    s_slot       = resolveConnectedUsbSlot(usb_slot);
    s_linesTotal = totalLines;
    s_linesSent  = 0;
    s_linesAcked = 0;
    s_errorCount = 0;
    s_inflight   = 0;
    s_fifoHead   = 0;
    s_fifoTail   = 0;
    s_fifoCount  = 0;
    s_state      = GrblStreamState::STREAMING;
    s_jobSource  = GrblJobSource::LOCAL_QUEUE;
    grblConsumeResumeSkipLocked("[LOCAL_QUEUE]");
    grblHeightMapResetTracker();
    setWorkflowStateLocked(GrblWorkflowState::RUNNING, "local_queue_start");
    s_alarmActive = false;
    s_alarmMessage[0] = '\0';
    s_pauseReason[0] = '\0';
    s_errorReason[0] = '\0';
    s_jobStartMs = millis();
    s_jobEndMs = 0;
    s_lastRxMs = s_jobStartMs;
    s_doneStableSinceMs = 0;
    s_holdAccumMs = 0;
    s_holdStartMs = 0;
    s_rateLastMs = s_jobStartMs;
    s_rateLastAck = 0;
    s_currentCmdPerMin = 0;
    s_localCurrentLine[0] = '\0';
    s_fileRetryLine[0] = '\0';
    s_fileRetryCount = 0;
    s_fileRecoveryAckUntil = 0;
    s_lastUsbTxZeroMs = 0;
    s_lastAckProgressMs = s_jobStartMs;
    s_jobSawNonZeroMachinePos = false;
    s_jobSawNonZeroWorkPos = false;
    s_detectedAxisCount = 3;
    s_avgLineBytes = 30U;
    s_burstLinesSent = 0;
    s_burstCycles = 0;
    s_localCurrentLineActive = false;
    s_localCurrentRetry = 0;
    resetStreamStageState();
    const bool qlogOpened = jobLogOpenLocked("local_queue", "[LOCAL_QUEUE]");
    if (!qlogOpened) {
        directLogPrintf("[GRBL] job log unavailable for local queue\n");
    }
    xSemaphoreGive(s_mutex);

    directLogPrintf("[GRBL] local queue started (%lu lines)\n", (unsigned long)totalLines);
    sendVirtualMessage(("▶️ GRBL local queue started (" + String(totalLines) + " lines)").c_str(), INFO);
    return true;
}

bool grblQueueStop() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;

    bool isLocalQueue = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    isLocalQueue = (s_jobSource == GrblJobSource::LOCAL_QUEUE) &&
                   (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED);
    xSemaphoreGive(s_mutex);

    if (!isLocalQueue) return false;
    grblCancel();
    return true;
}

bool grblQueueIsActive() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool active = (s_jobSource == GrblJobSource::LOCAL_QUEUE) &&
                        (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED);
    xSemaphoreGive(s_mutex);
    return active;
}

bool grblStreamIsActive() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool active = (s_jobSource == GrblJobSource::FILE_STREAM) &&
                        (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED);
    xSemaphoreGive(s_mutex);
    return active;
}

const char* grblGetJobSourceName() {
    if (!ensureRuntimeBuffers()) return "none";
    if (!s_mutex) return "none";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "none";
    const GrblJobSource source = s_jobSource;
    xSemaphoreGive(s_mutex);

    return jobSourceName(source);
}

const char* grblGetWorkflowStateName() {
    if (!ensureRuntimeBuffers()) return "error";
    if (!s_mutex) return "idle";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "idle";
    const GrblWorkflowState state = s_workflowState;
    xSemaphoreGive(s_mutex);
    return workflowStateName(state);
}

uint32_t grblGetStatusPollInterval() {
    return s_pollIntervalMs;
}

uint8_t grblGetStatusPollOutstanding() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint8_t v = s_pollOutstanding;
    xSemaphoreGive(s_mutex);
    return v;
}

uint32_t grblGetStatusLagMs() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool hasStatus = s_hasMachineStatus && (s_lastStatusMs > 0);
    const uint32_t lastStatusMs = s_lastStatusMs;
    const bool isStreaming = (s_state == GrblStreamState::STREAMING ||
                              s_state == GrblStreamState::PAUSED) &&
                             s_jobSource != GrblJobSource::NONE;
    const uint32_t effectiveLastMs = isStreaming ? s_lastRxMs : lastStatusMs;
    xSemaphoreGive(s_mutex);
    if (!hasStatus && !isStreaming) return 0;
    if (effectiveLastMs == 0) return 0;
    const uint32_t nowMs = millis();
    return (nowMs >= effectiveLastMs) ? (nowMs - effectiveLastMs) : 0;
}

uint32_t grblGetBlockedByInflightCount() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t v = s_blockedByInflight;
    xSemaphoreGive(s_mutex);
    return v;
}

uint32_t grblGetBlockedByReportedRxCount() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t v = s_blockedByReportedRx;
    xSemaphoreGive(s_mutex);
    return v;
}

uint32_t grblGetBlockedByFifoFullCount() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t v = s_blockedByFifoFull;
    xSemaphoreGive(s_mutex);
    return v;
}

uint32_t grblGetBlockedByCriticalCommandCount() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t v = s_blockedByCriticalCommand;
    xSemaphoreGive(s_mutex);
    return v;
}

uint32_t grblGetBlockedBySafetyMarginCount() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t v = s_blockedBySafetyMargin;
    xSemaphoreGive(s_mutex);
    return v;
}

const char* grblGetLastBlockReason() {
    if (!ensureRuntimeBuffers()) return "";
    if (!s_mutex) return "";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "";
    const bool hasReason = s_lastBlockReason[0] != '\0';
    xSemaphoreGive(s_mutex);
    return hasReason ? s_lastBlockReason : "";
}

void grblClearBlockReason() {
    if (!s_mutex) return;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    s_lastBlockReason[0] = '\0';
    s_lastBlockLine[0] = '\0';
    s_lastBlockMs = 0;
    xSemaphoreGive(s_mutex);
}

const char* grblGetLastBlockLine() {
    if (!ensureRuntimeBuffers()) return "";
    if (!s_mutex) return "";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "";
    const bool hasLine = s_lastBlockLine[0] != '\0';
    xSemaphoreGive(s_mutex);
    return hasLine ? s_lastBlockLine : "";
}

uint32_t grblGetLastBlockMs() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t value = s_lastBlockMs;
    xSemaphoreGive(s_mutex);
    return value;
}

uint8_t grblGetRecentErrorCount() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint8_t count = s_recentErrorCount;
    xSemaphoreGive(s_mutex);
    return count;
}

bool grblGetRecentErrorAt(uint8_t index, GrblRecentError* out) {
    if (!out) return false;
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (index >= s_recentErrorCount) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    const uint8_t cap = (uint8_t)GRBL_RECENT_ERROR_HISTORY;
    const uint8_t newest = (uint8_t)((s_recentErrorHead + cap - 1U) % cap);
    const uint8_t slot = (uint8_t)((newest + cap - (index % cap)) % cap);
    *out = s_recentErrors[slot];
    xSemaphoreGive(s_mutex);
    return true;
}

bool grblIsAlarmActive() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool active = s_alarmActive;
    xSemaphoreGive(s_mutex);
    return active;
}

const char* grblGetAlarmMessage() {
    if (!ensureRuntimeBuffers()) return "";
    if (!s_mutex) return "";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "";
    const bool active = s_alarmActive;
    xSemaphoreGive(s_mutex);
    return active ? s_alarmMessage : "";
}

bool grblHasMachineStatus() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool hasStatus = s_hasMachineStatus;
    xSemaphoreGive(s_mutex);
    return hasStatus;
}

bool grblIsDoorOpen() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool open = s_doorOpen;
    xSemaphoreGive(s_mutex);
    return open;
}

bool grblIsProbeTriggered() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool triggered = s_probeTriggered;
    xSemaphoreGive(s_mutex);
    return triggered;
}

const char* grblGetMachineStateName() {
    if (!ensureRuntimeBuffers()) return "unknown";
    if (!s_mutex) return "unknown";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "unknown";
    const bool hasStatus = s_hasMachineStatus;
    xSemaphoreGive(s_mutex);
    return hasStatus ? s_machineStateName : "unknown";
}

// ---------------------------------------------------------------------------
// Alarm resume ("restart from line N").
// A GRBL ALARM resets the controller: position, feed, spindle, coolant and
// modal groups are lost. The assisted resume replays: $X unlock -> $H home ->
// modal preamble (scanned from the job start) -> restart with line offset.
// The long pole is homing (up to ~2 min), so the flow runs in a background
// task; /api/grbl/status exposes resume_busy/resume_error. Nothing here
// disables or changes normal job behavior.
// ---------------------------------------------------------------------------

// Raw clear (no locking): caller holds s_mutex, or no feeder can run
// (orchestrator before streaming starts).
static void resumeClearStateRaw() {
    s_resumeAvailable = false;
    s_resumeSource = GrblJobSource::NONE;
    s_resumePath[0] = '\0';
    s_resumeTotalLines = 0;
    s_resumeAckedLines = 0;
    s_resumeFileSize = 0;
    s_resumeFromPsram = false;
    s_resumeAlarm[0] = '\0';
    s_resumeSkipArmed = false;
    s_resumeSkipPath[0] = '\0';
    s_resumeDropLines = 0;
    s_resumeDropArmed = false;
    s_resumeError[0] = '\0';
    // s_resumeBusy / s_resumeAbort owned by the orchestrator lifecycle.
}

// Call with s_mutex HELD, before the alarm wipe in processGrblResponseLine.
void grblCaptureResumeSnapshotLocked() {
    resumeClearStateRaw();
    if (s_jobSource != GrblJobSource::FILE_STREAM &&
        s_jobSource != GrblJobSource::LOCAL_QUEUE) return;
    if (s_linesTotal == 0 || s_linesAcked >= s_linesTotal) return;
    s_resumeSource = s_jobSource;
    strlcpy(s_resumePath, s_filePath, sizeof(s_resumePath));
    s_resumeTotalLines = s_linesTotal;
    s_resumeAckedLines = s_linesAcked;
    strlcpy(s_resumeAlarm, s_alarmMessage, sizeof(s_resumeAlarm));
    if (s_jobSource == GrblJobSource::LOCAL_QUEUE) {
        s_resumeFromPsram = false;
        s_resumeFileSize = (uint32_t)s_localQueueLen;
    } else if (s_streamPsramBuf && s_streamPsramLen > 0) {
        s_resumeFromPsram = true;
        s_resumeFileSize = (uint32_t)s_streamPsramLen;
    } else {
#if ENABLE_SDCARD == 1
        s_resumeFromPsram = false;
        s_resumeFileSize = s_file ? (uint32_t)s_file.size() : 0;
#else
        return;
#endif
    }
    s_resumeAvailable = true;
    directLogPrintf("[GRBL] resume snapshot: %s line %lu/%lu\n",
                    s_resumePath, (unsigned long)s_resumeAckedLines,
                    (unsigned long)s_resumeTotalLines);
}

// Consume a pending skip inside the (re)start functions (mutex held there).
// Applies only when the armed path matches this start's source; any other
// (fresh) job fully clears stale resume state. Presets counters to the acked
// line and arms the feeder drop counter; returns true when consumed.
bool grblConsumeResumeSkipLocked(const char* startPath) {
    if (s_resumeSkipArmed && startPath && s_resumeSkipPath[0] &&
        strcmp(startPath, s_resumeSkipPath) == 0 && s_resumeAckedLines > 0) {
        s_linesSent = s_resumeAckedLines;
        s_linesAcked = s_resumeAckedLines;
        s_resumeDropLines = s_resumeAckedLines;
        s_resumeDropArmed = true;
        s_resumeSkipArmed = false;
        s_resumeAvailable = false;
        s_resumeError[0] = '\0';
        directLogPrintf("[GRBL] resume skip consumed: %lu lines\n",
                        (unsigned long)s_resumeAckedLines);
        return true;
    }
    resumeClearStateRaw();
    return false;
}

// Poll until the machine reports Idle with no alarm (mutex never held across
// the wait). Returns false on abort, timeout, or re-alarm.
static bool resumeWaitIdleNoAlarm(uint32_t timeoutMs) {
    const uint32_t start = millis();
    for (;;) {
        if (s_resumeAbort) return false;
        const bool alarm = grblIsAlarmActive();
        const char* st = grblGetMachineStateName();
        char stateCopy[16];
        strncpy(stateCopy, st ? st : "", sizeof(stateCopy) - 1);
        stateCopy[sizeof(stateCopy) - 1] = '\0';
        if (!alarm && strcmp(stateCopy, "Idle") == 0) return true;
        if (millis() - start > timeoutMs) return false;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Fold modal state from the first `acked` SENDABLE lines of an open SD file.
// Sendable == non-empty after strip and not a ';' comment (feeder rule).
static bool resumeScanSdPrefix(File& f, uint32_t acked, GrblResumeModals* out) {
    char line[128];
    size_t lineLen = 0;
    uint32_t sendable = 0;
    if (!f) return false;
    while (sendable < acked && f.available()) {
        const int c = f.read();
        if (c < 0) break;
        if (c == '\n') {
            line[lineLen] = '\0';
            char stripped[128];
            if (grblResumeStripLine(line, stripped, sizeof(stripped)) > 0 &&
                grblResumeIsSendable(stripped)) {
                grblResumeFoldLine(stripped, out);
                sendable++;
            }
            lineLen = 0;
        } else if (c != '\r' && lineLen < sizeof(line) - 1) {
            line[lineLen++] = (char)c;
        }
    }
    return true;  // readable source is enough; empty modals => empty preamble
}

// Fold modal state from the first `acked` sendable lines of a memory buffer.
static bool resumeScanBuffer(const char* buf, size_t len, uint32_t acked,
                             GrblResumeModals* out) {
    size_t i = 0;
    uint32_t sendable = 0;
    char line[128];
    if (!buf || !out) return false;
    while (i < len && sendable < acked) {
        size_t start = i;
        while (i < len && buf[i] != '\n') i++;
        size_t segLen = (i > start) ? (i - start) : 0;
        size_t cp = (segLen < sizeof(line) - 1) ? segLen : (sizeof(line) - 1);
        for (size_t k = 0; k < cp; k++) line[k] = buf[start + k];
        line[cp] = '\0';
        char stripped[128];
        if (grblResumeStripLine(line, stripped, sizeof(stripped)) > 0 &&
            grblResumeIsSendable(stripped)) {
            grblResumeFoldLine(stripped, out);
            sendable++;
        }
        if (i < len && buf[i] == '\n') i++;
    }
    return true;
}

// Restart a PSRAM stream on the CURRENT buffer (which survived the alarm).
// Unlike grblStreamPsramData() this does not re-acquire from the store
// (ownership was already transferred). Mirrors its tail: recount, reset,
// stream. Returns false when the buffer is gone.
static bool grblRestartPsramOnBuffer(uint8_t slot) {
    if (!s_streamPsramBuf || s_streamPsramLen == 0) return false;
    uint32_t totalLines = 0;
    for (size_t i = 0; i < s_streamPsramLen; i++) {
        if (s_streamPsramBuf[i] == '\n') totalLines++;
    }
    if (totalLines == 0) return false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;
    s_streamPsramPos = 0;
    s_streamPsramEof = true;
    resetStreamStageState();
    s_slot       = resolveConnectedUsbSlot(slot);
    s_linesTotal = totalLines;
    s_linesSent  = 0;
    s_linesAcked = 0;
    s_errorCount = 0;
    s_inflight   = 0;
    s_fifoHead   = 0;
    s_fifoTail   = 0;
    s_fifoCount  = 0;
    s_state      = GrblStreamState::STREAMING;
    s_jobSource  = GrblJobSource::FILE_STREAM;
    grblConsumeResumeSkipLocked(s_filePath);
    grblHeightMapResetTracker();
    setWorkflowStateLocked(GrblWorkflowState::RUNNING, "resume_psram_restart");
    s_alarmActive = false;
    s_alarmMessage[0] = '\0';
    s_pauseReason[0] = '\0';
    s_errorReason[0] = '\0';
    s_jobStartMs = millis();
    s_jobEndMs = 0;
    s_lastRxMs = s_jobStartMs;
    // Consume a pending resume skip (or clear stale resume on mismatch).
    grblConsumeResumeSkipLocked(s_filePath);
    grblStartPsramFeedTask();
    xSemaphoreGive(s_mutex);
    return true;
}

static void grblResumeFail(const char* msg) {
    // Task context only (never called with s_mutex held): serialize the
    // error write + busy clear so status readers never see a torn state.
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (msg && msg[0]) {
            strncpy(s_resumeError, msg, sizeof(s_resumeError) - 1);
            s_resumeError[sizeof(s_resumeError) - 1] = '\0';
        }
        s_resumeSkipArmed = false;
        s_resumeDropArmed = false;
        s_resumeDropLines = 0;
        s_resumeBusy = false;
        xSemaphoreGive(s_mutex);
    }
    sendVirtualMessage((String(F("❌ GRBL resume: ")) + msg).c_str(), ERROR);
    directLogPrintf("[GRBL] resume failed: %s\n", msg ? msg : "?");
}

// The background resume flow. Reads a local copy of the snapshot, then:
// $X unlock -> $H home -> verify source -> scan modals -> preamble ->
// arm skip -> restart with offset. Any failure keeps the snapshot (retryable)
// except a fresh user job/cancel, which clears it.
static void grblResumeTaskFn(void* /*arg*/) {
    char path[128] = "";
    char alarm[64] = "";
    uint32_t acked = 0, total = 0, fileSize = 0;
    GrblJobSource src = GrblJobSource::NONE;
    bool fromPsram = false;
    uint8_t slot = 0;
    // Snapshot under a brief take; everything below runs unlocked.
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        grblResumeFail("mutex busy");
        vTaskDelete(nullptr);
        return;
    }
    if (!s_resumeAvailable || s_resumeAbort ||
        s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED) {
        xSemaphoreGive(s_mutex);
        grblResumeFail(s_resumeAbort ? "cancelled" : "not resumable now");
        vTaskDelete(nullptr);
        return;
    }
    strlcpy(path, s_resumePath, sizeof(path));
    strlcpy(alarm, s_resumeAlarm, sizeof(alarm));
    acked = s_resumeAckedLines;
    total = s_resumeTotalLines;
    fileSize = s_resumeFileSize;
    src = s_resumeSource;
    fromPsram = s_resumeFromPsram;
    slot = s_slot;
    xSemaphoreGive(s_mutex);
    directLogPrintf("[GRBL] resume begin: %s line %lu/%lu (%s)\n",
                    path, (unsigned long)acked, (unsigned long)total, alarm);
    sendVirtualMessage((String(F("▶️ GRBL resume from line ")) + String((unsigned long)acked)).c_str(), INFO);

    // 1. Unlock: $X, then require alarm-clear + Idle.
    if (s_resumeAbort) { grblResumeFail("cancelled"); vTaskDelete(nullptr); return; }
    grblSend(slot, "$X");
    if (!resumeWaitIdleNoAlarm(15000)) {
        grblResumeFail("unlock failed (still in alarm)");
        vTaskDelete(nullptr);
        return;
    }
    // 2. Home: $H, then require Idle. Abort (no silent skip): without a known
    // position, resuming motion is unsafe.
    if (s_resumeAbort) { grblResumeFail("cancelled"); vTaskDelete(nullptr); return; }
    sendVirtualMessage(F("🏠 GRBL resume: homing..."), INFO);
    grblSend(slot, "$H");
    if (!resumeWaitIdleNoAlarm(150000)) {
        grblResumeFail("homing failed — home manually, then retry");
        vTaskDelete(nullptr);
        return;
    }
    // 3. Verify the source is still there and unchanged.
    if (s_resumeAbort) { grblResumeFail("cancelled"); vTaskDelete(nullptr); return; }
    GrblResumeModals modals;
    grblResumeModalsInit(&modals);
    if (src == GrblJobSource::LOCAL_QUEUE) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            grblResumeFail("mutex busy"); vTaskDelete(nullptr); return;
        }
        const bool okBuf = (s_localQueueLen > 0);
        if (okBuf) resumeScanBuffer(s_localQueueBuf, s_localQueueLen, acked, &modals);
        xSemaphoreGive(s_mutex);
        if (!okBuf) { grblResumeFail("queue content lost"); vTaskDelete(nullptr); return; }
    } else if (fromPsram) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            grblResumeFail("mutex busy"); vTaskDelete(nullptr); return;
        }
        const bool okBuf = (s_streamPsramBuf && s_streamPsramLen > 0);
        if (okBuf) resumeScanBuffer(s_streamPsramBuf, s_streamPsramLen, acked, &modals);
        xSemaphoreGive(s_mutex);
        if (!okBuf) { grblResumeFail("psram buffer lost"); vTaskDelete(nullptr); return; }
    } else {
#if ENABLE_SDCARD == 1
#if SDCARD_USE_SPI_MODE == 1
        File f = SD.open(path, FILE_READ);
#else
        File f = SD_MMC.open(path, FILE_READ);
#endif
        if (!f) { grblResumeFail("file not found"); vTaskDelete(nullptr); return; }
        if (fileSize > 0 && f.size() != fileSize) {
            f.close();
            grblResumeFail("file changed since alarm");
            vTaskDelete(nullptr);
            return;
        }
        resumeScanSdPrefix(f, acked, &modals);
        f.close();
#else
        grblResumeFail("no SD support");
        vTaskDelete(nullptr);
        return;
#endif
    }
    // 4. Modal preamble (units/distance/WCS/plane/feed/spindle+dwell/coolant).
    if (s_resumeAbort) { grblResumeFail("cancelled"); vTaskDelete(nullptr); return; }
    {
        char pre[192];
        const size_t preLen = grblResumeEmitPreamble(&modals, pre, sizeof(pre));
        size_t pos = 0;
        char line[GRBL_MAX_LINE_LEN + 1];
        while (pos < preLen) {
            size_t e = pos;
            while (e < preLen && pre[e] != '\n') e++;
            size_t ll = (e > pos) ? (e - pos) : 0;
            if (ll >= sizeof(line)) ll = sizeof(line) - 1;
            memcpy(line, pre + pos, ll);
            line[ll] = '\0';
            if (ll > 0 && !grblSend(slot, line)) {
                grblResumeFail("preamble send failed");
                vTaskDelete(nullptr);
                return;
            }
            pos = (e < preLen) ? (e + 1) : preLen;
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
    // 5. Arm the skip and restart the same source with offset.
    if (s_resumeAbort) { grblResumeFail("cancelled"); vTaskDelete(nullptr); return; }
    bool started = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_resumeSkipArmed = true;
        strlcpy(s_resumeSkipPath, path, sizeof(s_resumeSkipPath));
        xSemaphoreGive(s_mutex);
    }
    if (src == GrblJobSource::LOCAL_QUEUE) {
        // Re-copy current content (start overwrites the buffer in place).
        size_t qlen = 0;
        char* qcopy = nullptr;
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            qlen = s_localQueueLen;
            xSemaphoreGive(s_mutex);
        }
        if (qlen > 0 && qlen < GRBL_LOCAL_QUEUE_MAX_BYTES) {
            qcopy = (char*)malloc(qlen + 1);
            if (qcopy && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                memcpy(qcopy, s_localQueueBuf, qlen);
                qcopy[qlen] = '\0';
                xSemaphoreGive(s_mutex);
            } else if (qcopy) {
                free(qcopy);
                qcopy = nullptr;
            }
        }
        if (qcopy) {
            started = grblQueueStart(slot, qcopy);
            free(qcopy);
        } else {
            grblResumeFail("queue copy failed");
        }
    } else if (fromPsram) {
        started = grblRestartPsramOnBuffer(slot);
        if (!started) grblResumeFail("psram restart failed");
    } else {
        started = grblStreamFile(slot, path);
        if (!started) grblResumeFail("file restart failed");
    }
    if (!started) {
        // Keep the snapshot for retry; disarm the one-shot skip.
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            s_resumeSkipArmed = false;
            s_resumeDropArmed = false;
            s_resumeDropLines = 0;
            xSemaphoreGive(s_mutex);
        }
        vTaskDelete(nullptr);
        return;
    }
    sendVirtualMessage((String(F("✅ GRBL resume streaming from line ")) + String((unsigned long)acked)).c_str(), INFO);
    directLogPrintf("[GRBL] resume streaming from line %lu\n", (unsigned long)acked);
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        s_resumeBusy = false;
        xSemaphoreGive(s_mutex);
    }
    vTaskDelete(nullptr);
}

// Validate fast (no waits) and launch the background flow.
GrblResumeLaunch grblResumeFromAlarm() {
    if (!ensureRuntimeBuffers() || !s_mutex) return GrblResumeLaunch::NO_RESUME;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) return GrblResumeLaunch::BUSY;
    GrblResumeLaunch rc = GrblResumeLaunch::OK;
    if (!s_resumeAvailable) rc = GrblResumeLaunch::NO_RESUME;
    else if (s_resumeBusy) rc = GrblResumeLaunch::BUSY;
    else if (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED) {
        rc = GrblResumeLaunch::NOT_IDLE;
    } else {
        s_resumeBusy = true;
        s_resumeAbort = false;
        s_resumeError[0] = '\0';
    }
    xSemaphoreGive(s_mutex);
    if (rc != GrblResumeLaunch::OK) return rc;
    if (xTaskCreate(grblResumeTaskFn, "grbl_resume", 8192, nullptr, 3, nullptr) != pdPASS) {
        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            s_resumeBusy = false;
            strlcpy(s_resumeError, "task create failed", sizeof(s_resumeError));
            xSemaphoreGive(s_mutex);
        }
        return GrblResumeLaunch::BUSY;
    }
    return GrblResumeLaunch::OK;
}

const char* grblResumeErrorText() {
    if (!ensureRuntimeBuffers() || !s_mutex) return "";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "";
    static char buf[96];  // copy out: caller must not hold the mutex
    strlcpy(buf, s_resumeError, sizeof(buf));
    xSemaphoreGive(s_mutex);
    return buf;
}

bool grblCopyLastStatusRaw(char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool hasRaw = s_lastStatusRaw[0] != '\0';
    if (hasRaw) {
        strlcpy(out, s_lastStatusRaw, outSize);
    }
    xSemaphoreGive(s_mutex);
    return hasRaw;
}

uint16_t grblGetPlannerBufferFree() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint16_t v = s_plannerBufferFree;
    xSemaphoreGive(s_mutex);
    return v;
}

uint16_t grblGetRxBufferFree() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint16_t v = s_rxBufferFree;
    xSemaphoreGive(s_mutex);
    return v;
}

uint16_t grblGetAvgLineBytes() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint16_t v = s_avgLineBytes;
    xSemaphoreGive(s_mutex);
    return v;
}

uint8_t grblGetBurstLinesSent() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint8_t v = s_burstLinesSent;
    xSemaphoreGive(s_mutex);
    return v;
}

uint32_t grblGetBurstCycles() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t v = s_burstCycles;
    xSemaphoreGive(s_mutex);
    return v;
}

bool grblGetMachinePosition(float* x, float* y, float* z) {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool hasPos = s_hasMachinePosition;
    const bool hasFreshPos = hasFreshPositionLocked();
    const float px = s_machinePos[0];
    const float py = s_machinePos[1];
    const float pz = s_machinePos[2];
    const bool syntheticZero = isSyntheticZeroPositionLocked(true, px, py, pz);
    xSemaphoreGive(s_mutex);
    if (!hasPos || !hasFreshPos || syntheticZero) return false;
    if (x) *x = px;
    if (y) *y = py;
    if (z) *z = pz;
    return true;
}

bool grblGetWorkPosition(float* x, float* y, float* z) {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool hasPos = s_hasWorkPosition;
    const bool hasFreshPos = hasFreshPositionLocked();
    const float px = s_workPos[0];
    const float py = s_workPos[1];
    const float pz = s_workPos[2];
    const bool syntheticZero = isSyntheticZeroPositionLocked(false, px, py, pz);
    xSemaphoreGive(s_mutex);
    if (!hasPos || !hasFreshPos || syntheticZero) return false;
    if (x) *x = px;
    if (y) *y = py;
    if (z) *z = pz;
    return true;
}

uint8_t grblGetAxisCount() {
    if (!ensureRuntimeBuffers()) return 3;
    if (!s_mutex) return 3;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const uint8_t axes = s_detectedAxisCount;
    xSemaphoreGive(s_mutex);
    return axes;
}

bool grblGetMaxTravel(float* x, float* y, float* z) {
    (void)x; (void)y; (void)z;
    return false;
}

bool grblGetJobStartWorkPos(float* x, float* y, float* z) {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    const bool valid = s_jobStartWorkPosValid;
    const float px = s_jobStartWorkPos[0];
    const float py = s_jobStartWorkPos[1];
    const float pz = s_jobStartWorkPos[2];
    xSemaphoreGive(s_mutex);
    if (!valid) return false;
    if (x) *x = px;
    if (y) *y = py;
    if (z) *z = pz;
    return true;
}

uint32_t grblGetActiveElapsedMs() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    const uint32_t nowMs = millis();
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t elapsed = jobEffectiveElapsedMsLocked(nowMs);
    xSemaphoreGive(s_mutex);
    return elapsed;
}

uint16_t grblGetAverageCmdPerMin() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    const uint32_t nowMs = millis();
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint32_t elapsed = jobEffectiveElapsedMsLocked(nowMs);
    const uint16_t cpm = (elapsed > 0) ? (uint16_t)((s_linesAcked * 60000UL) / elapsed) : 0;
    xSemaphoreGive(s_mutex);
    return cpm;
}

uint16_t grblGetCurrentCmdPerMin() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const uint16_t cpm = s_currentCmdPerMin;
    xSemaphoreGive(s_mutex);
    return cpm;
}

uint32_t grblGetEtaSeconds() {
    if (!ensureRuntimeBuffers()) return 0;
    if (!s_mutex) return 0;
    const uint32_t nowMs = millis();
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    const bool active = isJobRuntimeActiveLocked();
    const uint32_t elapsed = jobEffectiveElapsedMsLocked(nowMs);
    const uint32_t rateCpm = (elapsed > 0) ? ((s_linesAcked * 60000UL) / elapsed) : 0;
    uint32_t etaSec = 0;
    if (active && rateCpm > 0 && s_linesTotal > s_linesAcked) {
        const uint32_t remaining = s_linesTotal - s_linesAcked;
        etaSec = (remaining * 60UL) / rateCpm;
    }
    xSemaphoreGive(s_mutex);
    return etaSec;
}

const char* grblGetPauseReason() {
    if (!ensureRuntimeBuffers()) return "";
    if (!s_mutex) return "";
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return "";
    const bool hasReason = s_pauseReason[0] != '\0';
    xSemaphoreGive(s_mutex);
    return hasReason ? s_pauseReason : "";
}

void grblSetQueueExecutionOptions(bool continueOnError, uint8_t retryMax) {
    if (!ensureRuntimeBuffers()) return;
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_queueContinueOnError = continueOnError;
    s_queueRetryMax = retryMax;
    xSemaphoreGive(s_mutex);
}

void grblPause() {
    if (!ensureRuntimeBuffers()) return;
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state == GrblStreamState::STREAMING) {
        s_state = GrblStreamState::PAUSED;
        strlcpy(s_pauseReason, "user_hold", sizeof(s_pauseReason));
        if (s_holdStartMs == 0) s_holdStartMs = millis();
        setWorkflowStateLocked(GrblWorkflowState::HOLD, "pause_requested");
        jobLogEventLocked("USER_PAUSE", "pause_requested");
    }
    xSemaphoreGive(s_mutex);
}

bool grblResume() {
    if (!ensureRuntimeBuffers()) return false;
    if (!s_mutex) return false;
    bool resumed = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_state == GrblStreamState::PAUSED) {
        if (strcmp(s_pauseReason, "ack_stall_safe_stop") == 0) {
            jobLogEventLocked("RESUME_ACK_STALL", "operator_override");
            sendVirtualMessage(F("⚠️ GRBL: resume after ACK stall — GRBL may have lost sync. Check the job."), WARN);
        }
        s_state = GrblStreamState::STREAMING;
        if (s_holdStartMs > 0) {
            uint32_t nowMs = millis();
            if (nowMs > s_holdStartMs) s_holdAccumMs += (nowMs - s_holdStartMs);
            s_holdStartMs = 0;
        }
        s_pauseReason[0] = '\0';
        setWorkflowStateLocked(GrblWorkflowState::RUNNING, "resume_requested");
        jobLogEventLocked("USER_RESUME", "resume_requested");
        resumed = true;
    }
    xSemaphoreGive(s_mutex);
    return resumed;
}

void grblCancel() {
    if (!ensureRuntimeBuffers()) return;
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_resumeAbort = true;  // stop a background resume flow, if any
    if (!s_resumeBusy) resumeClearStateRaw();  // mutex held; busy flow keeps its snapshot
    setWorkflowStateLocked(GrblWorkflowState::CANCELLING, "cancel_requested");
    s_cancellingSinceMs = millis();
    s_state = GrblStreamState::CANCELLED;
    s_jobSource = GrblJobSource::NONE;
    s_pauseReason[0] = '\0';
    s_holdStartMs = 0;
    s_jobEndMs = millis();
#if ENABLE_SDCARD == 1
    if (s_file) s_file.close();
#endif
    s_inflight   = 0;
    s_toolChangePending = false;
    s_fifoHead  = 0;
    s_fifoTail  = 0;
    s_fifoCount = 0;
    s_localQueueLen = 0;
    s_localQueuePos = 0;
    s_localCurrentLine[0] = '\0';
    s_localCurrentLineActive = false;
    s_localCurrentRetry = 0;
    s_fileRetryLine[0] = '\0';
    s_fileRetryCount = 0;
    s_fileRecoveryAckUntil = 0;
    s_lastUsbTxZeroMs = 0;
    resetStreamStageState();
    jobLogCloseLocked("JOB_CANCEL", "user_cancel");
    xSemaphoreGive(s_mutex);
    grblStopPsramFeedTask();
    grblTransportWrite(s_slot, (const uint8_t*)"!\n", 2);
    vTaskDelay(pdMS_TO_TICKS(100));
    grblTransportWrite(s_slot, (const uint8_t*)"\x18\n", 2);

    sendVirtualMessage(F("⏹️ GRBL stream cancelado"), WARN);
    publishConsoleLine(F("cancel requested"));
}

void grblSetStatusPollInterval(uint32_t ms) {
    if (ms == 0) {
        ms = GRBL_STATUS_POLL_DEFAULT_MS;
    }
    if (ms > 0 && ms < GRBL_STATUS_POLL_MIN_MS) {
        ms = GRBL_STATUS_POLL_MIN_MS;
    }
    s_pollIntervalMs = ms;
}

bool grblJobLogSetEnabled(bool enable) {
    s_jobLogEnabled = enable;
    {
        Preferences p;
        p.begin("esp32_config", false);
        p.putBool("grbl_log", enable);
        p.end();
    }
    configManager.setGrblLogEnabled(enable);
    sendVirtualMessage(enable
        ? F("📝 GRBL job trace log: ENABLED (will write to SD on next stream)")
        : F("📝 GRBL job trace log: DISABLED"),
        INFO);
    return s_jobLogEnabled;
}

bool grblJobLogIsEnabled() {
    return s_jobLogEnabled;
}

GrblStatus grblGetStatus() {
    GrblStatus st = {};
    if (!ensureRuntimeBuffers()) return st;
    if (!s_mutex) return st;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return st;
    st.state           = s_state;
    st.lines_sent      = s_linesSent;
    st.lines_total     = s_linesTotal;
    st.lines_acked     = s_linesAcked;
    st.error_count     = s_errorCount;
    st.bytes_in_flight = s_inflight;
    st.usb_slot        = s_slot;
    strlcpy(st.file_path, s_filePath, sizeof(st.file_path));
    st.resume_available = s_resumeAvailable;
    st.resume_line      = s_resumeAckedLines;
    st.resume_total     = s_resumeTotalLines;
    st.resume_busy      = s_resumeBusy;
    strlcpy(st.resume_file, s_resumePath, sizeof(st.resume_file));
    strlcpy(st.resume_alarm, s_resumeAlarm, sizeof(st.resume_alarm));
    strlcpy(st.resume_error, s_resumeError, sizeof(st.resume_error));
    xSemaphoreGive(s_mutex);
    return st;
}

void grblSetJobOrigin(const char* origin) {
    if (!ensureRuntimeBuffers()) return;
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!origin || origin[0] == '\0') {
        strlcpy(s_jobOrigin, "unknown", sizeof(s_jobOrigin));
    } else {
        strlcpy(s_jobOrigin, origin, sizeof(s_jobOrigin));
    }
    xSemaphoreGive(s_mutex);
}

GrblJobSession grblGetJobSession() {
    GrblJobSession js = {};
    if (!ensureRuntimeBuffers()) return js;
    if (!s_mutex) return js;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) return js;
    js.active = (s_jobSource != GrblJobSource::NONE) &&
                (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED);
    strlcpy(js.type, jobSourceName(s_jobSource), sizeof(js.type));
    strlcpy(js.state, workflowStateName(s_workflowState), sizeof(js.state));
    strlcpy(js.origin, s_jobOrigin, sizeof(js.origin));
    js.current_line = s_linesAcked;
    js.total_lines = s_linesTotal;
    strlcpy(js.pause_reason, s_pauseReason, sizeof(js.pause_reason));
    strlcpy(js.error_reason, s_errorReason, sizeof(js.error_reason));
    xSemaphoreGive(s_mutex);
    return js;
}

#endif // ENABLE_GRBL == 1
