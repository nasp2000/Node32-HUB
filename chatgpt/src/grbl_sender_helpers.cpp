// grbl_sender_helpers.cpp
// Internal helper functions for the GRBL sender.
#include "feature_flags.h"
#include "grbl_sender_internal.h"

#if ENABLE_GRBL == 1

#include <string.h>
#include <stdio.h>
#include "commands.h"

static void directLogPrintf(const char* fmt, ...) {
    char buffer[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    directLogPrint(buffer);
}

bool isTripletNearZero(float x, float y, float z) {
    const float eps = 0.0005f;
    const float ax = (x >= 0.0f) ? x : -x;
    const float ay = (y >= 0.0f) ? y : -y;
    const float az = (z >= 0.0f) ? z : -z;
    return (ax <= eps) && (ay <= eps) && (az <= eps);
}

bool hasFreshPositionLocked() {
    if (!s_hasMachineStatus) return false;
    if (s_lastPositionMs == 0) return false;
    const uint32_t nowMs = millis();
    return (nowMs >= s_lastPositionMs) && ((nowMs - s_lastPositionMs) <= GRBL_POSITION_VALID_WINDOW_MS);
}

bool isSyntheticZeroPositionLocked(bool machinePos, float x, float y, float z) {
    if (s_state != GrblStreamState::STREAMING) return false;
    if (s_linesAcked < 20U) return false;
    const bool sawNonZero = machinePos ? s_jobSawNonZeroMachinePos : s_jobSawNonZeroWorkPos;
    if (sawNonZero) return false;
    return isTripletNearZero(x, y, z);
}

void pushRecentErrorLocked(const char* message, const char* source, const char* preview) {
    if (!s_runtimeBuffers) return;
    GrblRecentError& slot = s_recentErrors[s_recentErrorHead];
    slot.timestamp_ms = millis();
    slot.line_sent = s_linesSent;
    slot.line_acked = s_linesAcked;
    slot.error_count = s_errorCount;
    strlcpy(slot.source, (source && source[0]) ? source : "unknown", sizeof(slot.source));
    strlcpy(slot.message, (message && message[0]) ? message : "error", sizeof(slot.message));
    strlcpy(slot.line_preview, (preview && preview[0]) ? preview : "", sizeof(slot.line_preview));
    s_recentErrorHead = (uint8_t)((s_recentErrorHead + 1U) % (uint8_t)GRBL_RECENT_ERROR_HISTORY);
    if (s_recentErrorCount < (uint8_t)GRBL_RECENT_ERROR_HISTORY) s_recentErrorCount++;
}

void publishConsoleLine(const String& msg) {
        if (grblWebPublishConsoleLine) {
            grblDebugLog("[DBG-RX] publishConsoleLine -> grblWebPublishConsoleLine: '%.80s'", msg.c_str());
            grblWebPublishConsoleLine(msg);
    } else {
        // Weak symbol not linked — publish via monitor pipeline as fallback
        // so console events still appear in the web UI status polling.
        sendVirtualMessage(msg, COMMAND_RESPONSE);
    }
}

void publishProductionEvent(const char* type, const String& msg) {
    if (grblWebPublishProductionEvent) grblWebPublishProductionEvent(type, msg);
}



bool isJobRuntimeActiveLocked() {
    return s_jobSource != GrblJobSource::NONE &&
           (s_state == GrblStreamState::STREAMING || s_state == GrblStreamState::PAUSED);
}

void recordBlockReasonLocked(const char* reason, const char* line) {
    strlcpy(s_lastBlockReason, (reason && reason[0]) ? reason : "unknown", sizeof(s_lastBlockReason));
    if (line && line[0]) strlcpy(s_lastBlockLine, line, sizeof(s_lastBlockLine));
    else s_lastBlockLine[0] = '\0';
    s_lastBlockMs = millis();
#if ENABLE_SDCARD == 1
    if (s_jobLogActive) {
        const uint32_t nowMs = s_lastBlockMs;
        const bool reasonChanged = (strncmp(s_jobLogLastBlockReason, s_lastBlockReason, sizeof(s_jobLogLastBlockReason)) != 0);
        if (reasonChanged || (nowMs - s_jobLogLastBlockMs) >= GRBL_JOB_LOG_BLOCK_THROTTLE_MS) {
            strlcpy(s_jobLogLastBlockReason, s_lastBlockReason, sizeof(s_jobLogLastBlockReason));
            s_jobLogLastBlockMs = nowMs;
            char detail[140];
            if (line && line[0]) snprintf(detail, sizeof(detail), "%s|%s", s_lastBlockReason, line);
            else snprintf(detail, sizeof(detail), "%s", s_lastBlockReason);
            jobLogEventLocked("BLOCK", detail);
        }
    }
#endif
}

size_t sendUsbPayloadWithRetry(uint8_t useSlot, const uint8_t* data, size_t len) {
    if (!data || len == 0) return 0;

    size_t totalWritten = 0;
    uint8_t attempts = 0;
    while (totalWritten < len && attempts < (uint8_t)GRBL_USB_SEND_RETRY_MAX_ATTEMPTS) {
        attempts++;
        const size_t written = grblTransportWrite(useSlot, data + totalWritten, len - totalWritten);
        if (written == 0) {
            // USB TX buffer full — must let USB Host tasks run to drain.
            // Use vTaskDelay(1) not taskYIELD() for same reason as burst yield.
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        totalWritten += written;
    }

    return totalWritten;
}

bool isWorkflowTransitionAllowed(GrblWorkflowState from, GrblWorkflowState to) {
    if (from == to) return true;
    switch (from) {
        case GrblWorkflowState::IDLE:
            return to == GrblWorkflowState::RUNNING || to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::RUNNING:
            return to == GrblWorkflowState::HOLD || to == GrblWorkflowState::PAUSED ||
                   to == GrblWorkflowState::CANCELLING || to == GrblWorkflowState::DONE ||
                   to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::PAUSED:
            return to == GrblWorkflowState::RUNNING || to == GrblWorkflowState::CANCELLING ||
                   to == GrblWorkflowState::ERROR || to == GrblWorkflowState::DONE;
        case GrblWorkflowState::HOLD:
            return to == GrblWorkflowState::PAUSED || to == GrblWorkflowState::RUNNING ||
                   to == GrblWorkflowState::CANCELLING || to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::CANCELLING:
            return to == GrblWorkflowState::IDLE || to == GrblWorkflowState::ERROR;
        case GrblWorkflowState::DONE:
            return to == GrblWorkflowState::IDLE || to == GrblWorkflowState::RUNNING;
        case GrblWorkflowState::ERROR:
            return to == GrblWorkflowState::IDLE || to == GrblWorkflowState::RUNNING;
        default:
            return false;
    }
}

void setWorkflowStateLocked(GrblWorkflowState nextState, const char* reason) {
    const GrblWorkflowState prevState = s_workflowState;
    if (s_workflowState == nextState) return;
    if (!isWorkflowTransitionAllowed(s_workflowState, nextState)) {
        directLogPrintf("[GRBL] workflow transition blocked: %s -> %s (%s)\n",
                        workflowStateName(s_workflowState), workflowStateName(nextState),
                        reason ? reason : "-");
        return;
    }
    directLogPrintf("[GRBL] workflow transition: %s -> %s (%s)\n",
                    workflowStateName(s_workflowState), workflowStateName(nextState),
                    reason ? reason : "-");
    String line = String("workflow ") + workflowStateName(s_workflowState) +
                  String(" -> ") + workflowStateName(nextState);
    if (reason && reason[0]) line += String(" (") + String(reason) + String(")");
    publishConsoleLine(line);
    s_workflowState = nextState;

    const char* eventType = nullptr;
    if (nextState == GrblWorkflowState::RUNNING && prevState == GrblWorkflowState::HOLD) {
        eventType = "resume";
    } else if (nextState == GrblWorkflowState::RUNNING) {
        eventType = "start";
    } else if (nextState == GrblWorkflowState::HOLD || nextState == GrblWorkflowState::PAUSED) {
        eventType = "hold";
    } else if (nextState == GrblWorkflowState::DONE) {
        eventType = "done";
    } else if (nextState == GrblWorkflowState::ERROR) {
        eventType = "error";
    } else if (nextState == GrblWorkflowState::CANCELLING) {
        eventType = "cancel";
    }
    if (eventType) publishProductionEvent(eventType, line);
    if (nextState == GrblWorkflowState::DONE) {
        jobLogCloseLocked("JOB_DONE", reason ? reason : "done");
    } else if (nextState == GrblWorkflowState::ERROR) {
        jobLogCloseLocked("JOB_ERROR", reason ? reason : "error");
    }
}

bool extractTripletFieldCI(const char* line, const char* tokenNoColon, float* x, float* y, float* z) {
    if (!line || !tokenNoColon || !*tokenNoColon || !x || !y || !z) return false;

    const size_t tokenLen = strlen(tokenNoColon);
    const char* p = line;
    while (*p) {
        // Compare token case-insensitively.
        size_t i = 0;
        while (i < tokenLen && p[i] && toupper((unsigned char)p[i]) == toupper((unsigned char)tokenNoColon[i])) {
            i++;
        }
        if (i == tokenLen) {
            const char* c = p + tokenLen;
            while (*c == ' ' || *c == '\t') c++;
            if (*c != ':') {
                p++;
                continue;
            }
            c++;
            while (*c == ' ' || *c == '\t') c++;

            // Copy numeric payload until field separator.
            char buf[96];
            size_t n = 0;
            while (*c && *c != '|' && *c != '>' && n < sizeof(buf) - 1) {
                buf[n++] = *c++;
            }
            buf[n] = '\0';

            float tx = 0.0f, ty = 0.0f, tz = 0.0f;
            // Accept 2 or 3 axis values — laser runs XY only.
            int matched = sscanf(buf, " %f , %f , %f", &tx, &ty, &tz);
            if (matched >= 2) {
                *x = tx;
                *y = ty;
                *z = (matched == 2) ? 0.0f : tz;
                // Update detected axis count from actual observed values.
                s_detectedAxisCount = (uint8_t)matched;
                return true;
            }
        }
        p++;
    }
    return false;
}

void parseMachineStatusLine(const char* line) {
    if (!line || line[0] != '<') return;
    if (!s_mutex) return;

    const char* end = strchr(line, '>');
    if (!end) return;

    char stateToken[16] = {};
    size_t n = 0;
    const char* p = line + 1;
    while (*p && *p != '|' && *p != '>' && n < sizeof(stateToken) - 1) {
        stateToken[n++] = (char)toupper((unsigned char)*p);
        p++;
    }
    stateToken[n] = '\0';

    bool door = false;
    bool probeTriggered = false;
    if (strcmp(stateToken, "DOOR") == 0) {
        door = true;
    }

    const char* pn = strstr(line, "|Pn:");
    if (pn) {
        pn += 4;
        while (*pn && *pn != '|' && *pn != '>') {
            if (*pn == 'D') {
                door = true;
            } else if ((char)toupper((unsigned char)*pn) == 'P') {
                probeTriggered = true;
            }
            pn++;
        }
    }

    uint16_t plannerFree = s_plannerBufferFree;
    uint16_t rxFree = s_rxBufferFree;
    float machinePos[3] = {0.0f, 0.0f, 0.0f};
    float workPos[3] = {0.0f, 0.0f, 0.0f};
    float workOffset[3] = {0.0f, 0.0f, 0.0f};
    bool hasMachinePos = false;
    bool hasWorkPos = false;
    bool hasWorkOffset = false;

    hasMachinePos = extractTripletFieldCI(line, "MPOS", &machinePos[0], &machinePos[1], &machinePos[2]);
    hasWorkPos = extractTripletFieldCI(line, "WPOS", &workPos[0], &workPos[1], &workPos[2]);
    hasWorkOffset = extractTripletFieldCI(line, "WCO", &workOffset[0], &workOffset[1], &workOffset[2]);
    if (hasMachinePos && !hasWorkPos && hasWorkOffset) {
        workPos[0] = machinePos[0] - workOffset[0];
        workPos[1] = machinePos[1] - workOffset[1];
        workPos[2] = machinePos[2] - workOffset[2];
        hasWorkPos = true;
    } else if (hasWorkPos && !hasMachinePos && hasWorkOffset) {
        machinePos[0] = workPos[0] + workOffset[0];
        machinePos[1] = workPos[1] + workOffset[1];
        machinePos[2] = workPos[2] + workOffset[2];
        hasMachinePos = true;
    }

    const char* bf = strstr(line, "|Bf:");
    if (bf) {
        bf += 4;
        int pVal = -1;
        int rVal = -1;
        if (sscanf(bf, "%d,%d", &pVal, &rVal) == 2) {
            if (pVal >= 0) plannerFree = (uint16_t)pVal;
            if (rVal >= 0) rxFree = (uint16_t)rVal;
        }
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    // A status frame confirms GRBL answered a realtime query.
    s_pollOutstanding = 0;
    s_lastStatusMs = millis();
    s_hasMachineStatus = true;
    strlcpy(s_lastStatusRaw, line, sizeof(s_lastStatusRaw));
    strlcpy(s_machineStateName, stateToken[0] ? stateToken : "UNKNOWN", sizeof(s_machineStateName));
    s_doorOpen = door;
    s_probeTriggered = probeTriggered;
    s_plannerBufferFree = plannerFree;
    s_rxBufferFree = rxFree;
    if (hasMachinePos) {
        s_machinePos[0] = machinePos[0];
        s_machinePos[1] = machinePos[1];
        s_machinePos[2] = machinePos[2];
        s_hasMachinePosition = true;
        if (s_jobSource != GrblJobSource::NONE && !isTripletNearZero(machinePos[0], machinePos[1], machinePos[2])) {
            s_jobSawNonZeroMachinePos = true;
        }
    }
    if (hasWorkPos) {
        s_workPos[0] = workPos[0];
        s_workPos[1] = workPos[1];
        s_workPos[2] = workPos[2];
        s_hasWorkPosition = true;
        if (s_jobSource != GrblJobSource::NONE && !isTripletNearZero(workPos[0], workPos[1], workPos[2])) {
            s_jobSawNonZeroWorkPos = true;
        }
    }
    if (hasMachinePos || hasWorkPos) {
        s_lastPositionMs = s_lastStatusMs;
    }
    if (strcmp(s_machineStateName, "ALARM") == 0 && !s_alarmActive) {
        s_alarmActive = true;
        strlcpy(s_alarmMessage, "ALARM: machine state", sizeof(s_alarmMessage));
        strlcpy(s_errorReason, s_alarmMessage, sizeof(s_errorReason));
        publishProductionEvent("alarm", String(s_alarmMessage));
    }
    xSemaphoreGive(s_mutex);
}

#endif // ENABLE_GRBL == 1
