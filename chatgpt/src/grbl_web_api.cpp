#include "feature_flags.h"

#if ENABLE_GRBL==1

#include "grbl_web_api.h"

#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "psram_helpers.h"
#include "grbl_sender_internal.h"
#include "grbl_psram_gcode.h"

extern void executeCommand(const String &command);

extern void wsGrblBroadcastConsole(const String& text) __attribute__((weak));
extern void wsGrblBroadcastProduction(const char* type, const String& message) __attribute__((weak));

// ---------------------------------------------------------------------------
// Debug ring buffer (PSRAM) — no Serial, no SD, no mutex
// ---------------------------------------------------------------------------
#define DBG_BUF_SIZE (64 * 1024)

static char* s_dbgBuf = nullptr;
static size_t s_dbgWritePos = 0;
static size_t s_dbgReadPos = 0;

static void ensureDebugBuf() {
    if (s_dbgBuf) return;
    if (psramFound()) {
        s_dbgBuf = (char*)heap_caps_malloc(DBG_BUF_SIZE, MALLOC_CAP_SPIRAM);
    }
    if (!s_dbgBuf) {
        s_dbgBuf = (char*)malloc(DBG_BUF_SIZE);
    }
    if (s_dbgBuf) {
        memset(s_dbgBuf, 0, DBG_BUF_SIZE);
    }
}

void grblDebugLog(const char* fmt, ...) {
    ensureDebugBuf();
    if (!s_dbgBuf) return;
    char line[192];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, args);
    va_end(args);
    if (n <= 0) return;
    if (n > (int)sizeof(line) - 2) n = (int)sizeof(line) - 2;
    line[n] = '\n';
    n++;

    size_t pos = s_dbgWritePos;
    size_t idx = pos % DBG_BUF_SIZE;
    size_t avail = DBG_BUF_SIZE - idx;

    if ((size_t)n <= avail) {
        memcpy(s_dbgBuf + idx, line, n);
    } else {
        memcpy(s_dbgBuf + idx, line, avail);
        memcpy(s_dbgBuf, line + avail, n - avail);
    }
    s_dbgWritePos = pos + n;
}

String grblDebugDump() {
    if (!s_dbgBuf) return String();
    size_t start = s_dbgReadPos;
    size_t end = s_dbgWritePos;
    if (start == end) return String();

    size_t len = end - start;
    if (len > DBG_BUF_SIZE) {
        len = DBG_BUF_SIZE;
        start = end - DBG_BUF_SIZE;
    }

    size_t startIdx = start % DBG_BUF_SIZE;
    String result;
    if (startIdx + len <= DBG_BUF_SIZE) {
        result = String(s_dbgBuf + startIdx, len);
    } else {
        size_t first = DBG_BUF_SIZE - startIdx;
        result = String(s_dbgBuf + startIdx, first);
        result += String(s_dbgBuf, len - first);
    }
    s_dbgReadPos = end;
    return result;
}

#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_P4_CNC || SELECTED_PACK == PACK_S3_CNC)

namespace {

static const uint8_t kFallbackUsbSlots = 4;
static const uint32_t kControlLeaseMs = 90000UL;

#if ENABLE_USB_HOST == 1 && USB_HOST_NATIVE_AVAILABLE == 1
static const uint8_t kUsbSlots = USB_HOST_MAX_CDC_DEVICES;
#else
static const uint8_t kUsbSlots = kFallbackUsbSlots;
#endif

static String s_operatorSession;
static uint32_t s_operatorLeaseUntilMs = 0;

struct GrblConsoleEvent {
    uint32_t id;
    char text[160];
};

struct GrblProductionEvent {
    uint32_t id;
    char type[24];
    char text[160];
};

struct SessionControlToken {
    char sid[48];
    char token[40];
    uint32_t expiresMs;
};

static const uint8_t kConsoleEventHistory = 120;
static const uint8_t kProductionEventHistory = 12;

static uint8_t s_consoleHead = 0;
static uint8_t s_consoleCount = 0;
static uint8_t s_prodHead = 0;
static uint8_t s_prodCount = 0;
static const uint32_t kTokenLeaseMs = 15UL * 60UL * 1000UL;
static uint32_t s_eventSeq = 0;

struct GrblWebRuntimeBuffers {
    GrblConsoleEvent consoleEvents[kConsoleEventHistory];
    GrblProductionEvent prodEvents[kProductionEventHistory];
    SessionControlToken sessionTokens[8];
};

static GrblWebRuntimeBuffers* s_webBuffers = nullptr;
static bool s_webBuffersReady = false;
static portMUX_TYPE s_webAllocMux = portMUX_INITIALIZER_UNLOCKED;

#ifndef GRBL_PSRAM_STRICT
#define GRBL_PSRAM_STRICT 1
#endif

#define s_consoleEvents (s_webBuffers->consoleEvents)
#define s_prodEvents (s_webBuffers->prodEvents)
#define s_sessionTokens (s_webBuffers->sessionTokens)

bool ensureWebBuffers() {
    if (s_webBuffersReady && s_webBuffers) return true;

    taskENTER_CRITICAL(&s_webAllocMux);
    if (s_webBuffersReady && s_webBuffers) {
        taskEXIT_CRITICAL(&s_webAllocMux);
        return true;
    }

    // Try PSRAM first (always — no compile-time guards), fall back to heap.
    if (psramFound()) {
        s_webBuffers = (GrblWebRuntimeBuffers*)heap_caps_malloc(sizeof(GrblWebRuntimeBuffers), MALLOC_CAP_SPIRAM);
        grblDebugLog("[DBG-WEB] PSRAM alloc: %p\n", s_webBuffers);
    }
    if (!s_webBuffers) {
        s_webBuffers = (GrblWebRuntimeBuffers*)heap_caps_malloc(sizeof(GrblWebRuntimeBuffers), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        grblDebugLog("[DBG-WEB] heap fallback alloc: %p\n", s_webBuffers);
    }

    if (!s_webBuffers) {
        grblDebugLog("[DBG-WEB] BOTH PSRAM and heap ALLOC FAILED\n");
        taskEXIT_CRITICAL(&s_webAllocMux);
        return false;
    }
    memset(s_webBuffers, 0, sizeof(GrblWebRuntimeBuffers));
    s_webBuffersReady = true;
    grblDebugLog("[DBG-WEB] ensureWebBuffers OK (ready=1 buf=%p)\n", s_webBuffers);
    taskEXIT_CRITICAL(&s_webAllocMux);
    return true;
}

String jsonEscape(const String& input) {
    String out;
    out.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); i++) {
        const char c = input[i];
        switch (c) {
            case '\\': out += F("\\\\"); break;
            case '"':  out += F("\\\""); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default:
                if ((uint8_t)c < 0x20) {
                    out += ' ';
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

bool parseStatusTripletFieldCI(const String& rawStatus, const char* tokenNoColon, float* x, float* y, float* z) {
    if (!tokenNoColon || !*tokenNoColon || !x || !y || !z) return false;
    if (rawStatus.length() == 0) return false;

    String upper = rawStatus;
    upper.toUpperCase();
    String needle = String(tokenNoColon);
    needle.toUpperCase();
    needle += ':';

    const int start = upper.indexOf(needle);
    if (start < 0) return false;

    int valStart = start + needle.length();
    while (valStart < (int)rawStatus.length()) {
        const char c = rawStatus[(size_t)valStart];
        if (c != ' ' && c != '\t') break;
        valStart++;
    }
    if (valStart >= (int)rawStatus.length()) return false;

    int valEnd = rawStatus.indexOf('|', valStart);
    if (valEnd < 0) valEnd = rawStatus.indexOf('>', valStart);
    if (valEnd < 0) valEnd = (int)rawStatus.length();
    if (valEnd <= valStart) return false;

    const String payload = rawStatus.substring((unsigned)valStart, (unsigned)valEnd);
    float tx = 0.0f;
    float ty = 0.0f;
    float tz = 0.0f;
    // Accept 2 or 3 axis values — laser runs XY only.
    int matched = sscanf(payload.c_str(), " %f , %f , %f", &tx, &ty, &tz);
    if (matched < 2) return false;
    *x = tx;
    *y = ty;
    *z = (matched == 2) ? 0.0f : tz;
    return true;
}

String statusToJsonBool(bool v) {
    return v ? String(F("true")) : String(F("false"));
}

String buildStatusStateName(GrblStreamState state) {
    return String(grblWebStateToString(state));
}

bool millisReached(uint32_t nowMs, uint32_t targetMs) {
    return (int32_t)(nowMs - targetMs) >= 0;
}

uint32_t nextEventId() {
    s_eventSeq++;
    if (s_eventSeq == 0) s_eventSeq = 1;
    return s_eventSeq;
}

void pushConsoleEvent(const String& text) {
    if (!ensureWebBuffers()) {
        grblDebugLog("[DBG-WEB] pushConsoleEvent: ensureWebBuffers FAILED\n");
        return;
    }
    const uint32_t id = nextEventId();
    GrblConsoleEvent& slot = s_consoleEvents[s_consoleHead];
    slot.id = id;
    strlcpy(slot.text, text.c_str(), sizeof(slot.text));
    grblDebugLog("[DBG-WEB] pushConsoleEvent id=%lu text='%.80s' head=%u count=%u\n",
                    (unsigned long)id, text.c_str(), (unsigned)s_consoleHead, (unsigned)s_consoleCount);
    s_consoleHead = (uint8_t)((s_consoleHead + 1) % (sizeof(s_consoleEvents) / sizeof(s_consoleEvents[0])));
    if (s_consoleCount < (sizeof(s_consoleEvents) / sizeof(s_consoleEvents[0]))) {
        s_consoleCount++;
    }
    // Console events are pushed to WS clients via the periodic status broadcast
    // (wsGrblLoop → wsGrblBroadcastStatus, which includes console_events in the
    // JSON).  Real-time broadcast from here is intentionally skipped to avoid
    // calling AsyncWebSocket::textAll() from the TinyUSB callback context,
    // which races with wsGrblLoop() on the main loop task.
}

void pushProductionEvent(const char* type, const String& text) {
    if (!ensureWebBuffers()) return;
    const uint32_t id = nextEventId();
    GrblProductionEvent& slot = s_prodEvents[s_prodHead];
    slot.id = id;
    strlcpy(slot.type, (type && type[0]) ? type : "event", sizeof(slot.type));
    strlcpy(slot.text, text.c_str(), sizeof(slot.text));
    s_prodHead = (uint8_t)((s_prodHead + 1) % (sizeof(s_prodEvents) / sizeof(s_prodEvents[0])));
    if (s_prodCount < (sizeof(s_prodEvents) / sizeof(s_prodEvents[0]))) s_prodCount++;
    // Production events are pushed via the periodic status broadcast
    // (included in console_events / status JSON). Same thread-safety reasoning
    // as pushConsoleEvent — no direct WS call from producer context.
}

void appendSseEvent(String& out, uint32_t id, const __FlashStringHelper* eventName, const String& data) {
    out += F("id: ");
    out += String((unsigned long)id);
    out += '\n';
    out += F("event: ");
    out += String(eventName);
    out += '\n';
    out += F("data: ");
    out += data;
    out += F("\n\n");
}

uint8_t consoleEventCapacity() {
    if (!ensureWebBuffers()) return 0;
    return (uint8_t)(sizeof(s_consoleEvents) / sizeof(s_consoleEvents[0]));
}

uint8_t productionEventCapacity() {
    if (!ensureWebBuffers()) return 0;
    return (uint8_t)(sizeof(s_prodEvents) / sizeof(s_prodEvents[0]));
}

void clearExpiredTokens() {
    if (!ensureWebBuffers()) return;
    const uint32_t nowMs = millis();
    for (uint8_t i = 0; i < (sizeof(s_sessionTokens) / sizeof(s_sessionTokens[0])); i++) {
        if (s_sessionTokens[i].sid[0] == '\0') continue;
        if (s_sessionTokens[i].expiresMs == 0 || millisReached(nowMs, s_sessionTokens[i].expiresMs)) {
            s_sessionTokens[i].sid[0] = '\0';
            s_sessionTokens[i].token[0] = '\0';
            s_sessionTokens[i].expiresMs = 0;
        }
    }
}

int findTokenSlot(const String& sid) {
    if (!ensureWebBuffers()) return -1;
    for (uint8_t i = 0; i < (sizeof(s_sessionTokens) / sizeof(s_sessionTokens[0])); i++) {
        if (s_sessionTokens[i].sid[0] == '\0') continue;
        if (sid.equals(s_sessionTokens[i].sid)) return (int)i;
    }
    return -1;
}

int findFreeTokenSlot() {
    if (!ensureWebBuffers()) return -1;
    for (uint8_t i = 0; i < (sizeof(s_sessionTokens) / sizeof(s_sessionTokens[0])); i++) {
        if (s_sessionTokens[i].sid[0] == '\0') return (int)i;
    }
    return -1;
}

String makeToken(const String& sid) {
    uint32_t seed = (uint32_t)micros() ^ (uint32_t)millis() ^ nextEventId();
    for (size_t i = 0; i < sid.length(); i++) seed = (seed * 131U) ^ (uint8_t)sid[i];
    char buf[33];
    uint32_t a = seed ^ 0xA5F0C39DU;
    uint32_t b = (seed * 2654435761UL) ^ 0x3C6EF372U;
    uint32_t c = (seed << 5) ^ (seed >> 3) ^ 0x9E3779B9U;
    uint32_t d = a ^ b ^ c ^ 0x7F4A7C15U;
    snprintf(buf, sizeof(buf), "%08lx%08lx%08lx%08lx",
             (unsigned long)a, (unsigned long)b, (unsigned long)c, (unsigned long)d);
    return String(buf);
}

String issueTokenForSession(const String& sid) {
    if (!ensureWebBuffers()) return String();
    clearExpiredTokens();

    int idx = findTokenSlot(sid);
    if (idx < 0) idx = findFreeTokenSlot();
    if (idx < 0) idx = 0;

    const String token = makeToken(sid);
    strlcpy(s_sessionTokens[idx].sid, sid.c_str(), sizeof(s_sessionTokens[idx].sid));
    strlcpy(s_sessionTokens[idx].token, token.c_str(), sizeof(s_sessionTokens[idx].token));
    s_sessionTokens[idx].expiresMs = millis() + kTokenLeaseMs;
    return token;
}

bool validateSessionToken(const String& sid, const String& token) {
    if (!ensureWebBuffers()) return false;
    if (sid.length() == 0 || token.length() == 0) return false;
    clearExpiredTokens();
    const int idx = findTokenSlot(sid);
    if (idx < 0) return false;
    if (!token.equals(s_sessionTokens[idx].token)) return false;
    s_sessionTokens[idx].expiresMs = millis() + kTokenLeaseMs;
    return true;
}

String sanitizeSessionId(const String& raw) {
    String session;
    session.reserve(48);
    for (size_t i = 0; i < raw.length() && session.length() < 48; i++) {
        const char c = raw[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == ':' || c == '@') {
            session += c;
        }
    }
    if (session.length() == 0) session = F("anon");
    return session;
}

void refreshControlLease() {
    if (s_operatorSession.length() == 0) {
        s_operatorLeaseUntilMs = 0;
        return;
    }
    const uint32_t nowMs = millis();
    if (s_operatorLeaseUntilMs == 0 || millisReached(nowMs, s_operatorLeaseUntilMs)) {
        s_operatorSession = String();
        s_operatorLeaseUntilMs = 0;
    }
}

void touchControlLease(const String& sessionId) {
    if (sessionId.length() == 0) return;
    s_operatorSession = sessionId;
    s_operatorLeaseUntilMs = millis() + kControlLeaseMs;
}

bool canWriteForSession(const String& sessionId, bool acquireIfFree, bool* lockedByOther) {
    refreshControlLease();
    if (lockedByOther) *lockedByOther = false;

    if (s_operatorSession.length() == 0) {
        if (acquireIfFree) touchControlLease(sessionId);
        return true;
    }
    if (s_operatorSession == sessionId) {
        touchControlLease(sessionId);
        return true;
    }
    if (lockedByOther) *lockedByOther = true;
    return false;
}

bool activeJobHasBoundOwner(String* ownerSession) {
    const GrblJobSession job = grblGetJobSession();
    if (!job.active) return false;

    String owner = sanitizeSessionId(String(job.origin));
    owner.trim();
    if (owner.length() == 0 || owner == F("anon")) return false;

    if (ownerSession) *ownerSession = owner;
    return true;
}

String controlModeForSession(const String& sessionId, bool* canWrite, bool* isLocked) {
    String ownerSession;
    if (activeJobHasBoundOwner(&ownerSession)) {
        if (isLocked) *isLocked = true;
        if (ownerSession == sessionId) {
            touchControlLease(sessionId);
            if (canWrite) *canWrite = true;
            return F("operator");
        }
        if (canWrite) *canWrite = false;
        return F("observer");
    }

    refreshControlLease();
    if (isLocked) *isLocked = (s_operatorSession.length() > 0);

    if (s_operatorSession.length() == 0) {
        if (canWrite) *canWrite = true;
        return F("free");
    }
    if (s_operatorSession == sessionId) {
        touchControlLease(sessionId);
        if (canWrite) *canWrite = true;
        return F("operator");
    }
    if (canWrite) *canWrite = false;
    return F("observer");
}

uint32_t controlLeaseRemainingMs() {
    refreshControlLease();
    if (s_operatorSession.length() == 0 || s_operatorLeaseUntilMs == 0) return 0;
    const uint32_t nowMs = millis();
    if (millisReached(nowMs, s_operatorLeaseUntilMs)) return 0;
    return (s_operatorLeaseUntilMs - nowMs);
}

bool workflowBlocksStart(const char* workflowState) {
    if (!workflowState) return false;
    return strcmp(workflowState, "running") == 0 || strcmp(workflowState, "paused") == 0 ||
           strcmp(workflowState, "hold") == 0 || strcmp(workflowState, "cancelling") == 0;
}

bool hasSupportedGcodeExtension(const String& path) {
    String lower = path;
    lower.toLowerCase();
    return lower.endsWith(F(".nc")) || lower.endsWith(F(".gcode")) ||
           lower.endsWith(F(".tap")) || lower.endsWith(F(".txt")) ||
           lower.endsWith(F(".gc")) || lower.endsWith(F(".ngc")) ||
           lower.endsWith(F(".cnc")) || lower.endsWith(F(".code")) ||
           lower.endsWith(F(".cg")) || lower.endsWith(F(".cgo")) ||
           lower.endsWith(F(".cgode"));
}

bool queueHasUsableLine(const String& queueText) {
    const char* p = queueText.c_str();
    while (*p) {
        while (*p == '\r' || *p == '\n') p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';') {
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p && *p != '\r' && *p != '\n') return true;
        while (*p && *p != '\n') p++;
    }
    return false;
}

bool queueHasLineTooLong(const String& queueText) {
    const char* p = queueText.c_str();
    size_t len = 0;
    while (*p) {
        if (*p == '\r') {
            p++;
            continue;
        }
        if (*p == '\n') {
            if (len > GRBL_MAX_LINE_LEN) return true;
            len = 0;
            p++;
            continue;
        }
        len++;
        p++;
    }
    return len > GRBL_MAX_LINE_LEN;
}

uint8_t resolveUsbSlotForAction(uint8_t requested) {
    const UsbCdcDevice preferred = usbHostGetDevice(requested);
    if (preferred.connected) return requested;

    uint8_t foundSlot = requested;
    uint8_t connectedCount = 0;
    for (uint8_t i = 0; i < kUsbSlots; i++) {
        const UsbCdcDevice dev = usbHostGetDevice(i);
        if (!dev.connected) continue;
        connectedCount++;
        foundSlot = i;
        if (connectedCount > 1) break;
    }

    // Safe fallback for the common single-device scenario.
    if (connectedCount == 1) return foundSlot;
    return requested;
}

GrblWebActionResult runStartPreflight(uint8_t usbSlot, bool queueMode, const String& payload) {
    if (!usbHostIsRunning()) return {false, F("preflight_usb_not_running")};

    const UsbCdcDevice device = usbHostGetDevice(usbSlot);
    if (!device.connected) return {false, F("preflight_slot_not_connected")};

    if (workflowBlocksStart(grblGetWorkflowStateName())) {
        return {false, F("preflight_workflow_busy")};
    }

    if (grblIsAlarmActive()) {
        const String alarm = String(grblGetAlarmMessage());
        if (alarm.indexOf(F("ALARM:9")) >= 0) return {false, F("preflight_homing_required")};
        // Allow stream to proceed — grblStreamFile() auto-sends $X to clear alarm.
        // This ensures the PSRAM feeder task can run without manual unlock.
        if (queueMode) return {false, F("preflight_alarm_active")};
    }

    if (grblHasMachineStatus() && grblIsDoorOpen()) {
        return {false, F("preflight_door_open")};
    }

    if (!queueMode) {
        if (payload.length() == 0) return {false, F("preflight_empty_path")};
        if (payload.length() >= GRBL_MAX_PATH_LEN) return {false, F("preflight_path_too_long")};
        if (!hasSupportedGcodeExtension(payload)) return {false, F("preflight_invalid_extension")};
        return {true, F("ok")};
    }

    if (payload.length() == 0) return {false, F("preflight_empty_queue")};
    if (payload.length() >= GRBL_LOCAL_QUEUE_MAX_BYTES) return {false, F("preflight_queue_too_large")};
    if (!queueHasUsableLine(payload)) return {false, F("preflight_empty_queue")};
    if (queueHasLineTooLong(payload)) return {false, F("preflight_line_too_long")};
    return {true, F("ok")};
}

GrblWebActionResult requireWriteControl(const String& sessionId) {
    String ownerSession;
    if (activeJobHasBoundOwner(&ownerSession)) {
        if (ownerSession == sessionId) {
            touchControlLease(sessionId);
            return {true, F("ok")};
        }
        return {false, F("control_locked")};
    }

    bool lockedByOther = false;
    if (canWriteForSession(sessionId, true, &lockedByOther)) return {true, F("ok")};
    (void)lockedByOther;
    return {false, F("control_locked")};
}

} // namespace

const char* grblWebStateToString(GrblStreamState state) {
    switch (state) {
        case GrblStreamState::IDLE: return "IDLE";
        case GrblStreamState::STREAMING: return "STREAMING";
        case GrblStreamState::PAUSED: return "PAUSED";
        case GrblStreamState::DONE: return "DONE";
        case GrblStreamState::ERROR: return "ERROR";
        case GrblStreamState::CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

String grblWebBuildStatusJson(const String& sessionIdRaw, uint32_t lastConsoleEventId) {
    static String s_cache;
    static uint32_t s_cacheAge = 0;
    if (lastConsoleEventId == 0 && s_cache.length() > 0 && millis() - s_cacheAge < 100) {
        return s_cache;
    }
    grblDebugLog("[DBG-WEB] buildStatusJson called, lastConsoleEventId=%lu\n", (unsigned long)lastConsoleEventId);
    const bool webPsramReady = ensureWebBuffers();
    const uint8_t consoleCap = consoleEventCapacity();
    const uint8_t consoleCount = s_consoleCount;
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblStatus status = grblGetStatus();
    const GrblJobSession jobSession = grblGetJobSession();
    const UsbHostStats usbStats = usbHostGetStats();
#if ENABLE_USB_HOST == 1
    const UsbHostDiag usbDiag = usbHostGetDiag();
#endif
    const bool queueActive = grblQueueIsActive();
    const bool streamActive = grblStreamIsActive();
    const bool jobActive = queueActive || streamActive;
    const bool alarmActive = grblIsAlarmActive();
    const char* alarmMessage = grblGetAlarmMessage();
    const char* workflowState = grblGetWorkflowStateName();
    const char* jobSource = grblGetJobSourceName();
    const uint32_t pollInterval = grblGetStatusPollInterval();
    const uint8_t pollOutstanding = grblGetStatusPollOutstanding();
    const uint32_t statusLagMs = grblGetStatusLagMs();
    const uint32_t blockedByInflight = grblGetBlockedByInflightCount();
    const uint32_t blockedByReportedRx = grblGetBlockedByReportedRxCount();
    const uint32_t blockedByFifoFull = grblGetBlockedByFifoFullCount();
    const uint32_t blockedByCriticalCommand = grblGetBlockedByCriticalCommandCount();
    const uint32_t blockedBySafetyMargin = grblGetBlockedBySafetyMarginCount();
    const char* lastBlockReason = grblGetLastBlockReason();
    const char* lastBlockLine = grblGetLastBlockLine();
    const uint32_t lastBlockMs = grblGetLastBlockMs();
    const uint8_t recentErrorCount = grblGetRecentErrorCount();
    const uint16_t avgCmdPerMin = grblGetAverageCmdPerMin();
    const uint16_t curCmdPerMin = grblGetCurrentCmdPerMin();
    const uint32_t etaSec = grblGetEtaSeconds();
    const uint32_t elapsedMs = grblGetActiveElapsedMs();
    const uint16_t plannerFree = grblGetPlannerBufferFree();
    const uint16_t rxFree = grblGetRxBufferFree();
    const uint16_t avgLineBytes = grblGetAvgLineBytes();
    const uint8_t burstLinesSent = grblGetBurstLinesSent();
    const uint32_t burstCycles = grblGetBurstCycles();
    const char* holdReason = grblGetPauseReason();
    const bool hasMachineStatus = grblHasMachineStatus();
    const bool doorOpen = grblIsDoorOpen();
    const bool probeTriggered = grblIsProbeTriggered();
    const char* machineState = grblGetMachineStateName();
    float mposX = 0.0f, mposY = 0.0f, mposZ = 0.0f;
    float wposX = 0.0f, wposY = 0.0f, wposZ = 0.0f;
    char statusRaw[GRBL_MAX_LINE_LEN + 1] = {0};
    const bool hasStatusRaw = grblCopyLastStatusRaw(statusRaw, sizeof(statusRaw));
    String statusRawText = hasStatusRaw ? String(statusRaw) : String();
    statusRawText.trim();
    String statusRawUpper = statusRawText;
    statusRawUpper.toUpperCase();
    const bool rawFrameValid = statusRawText.length() > 0 && statusRawText.startsWith("<") && (statusRawText.indexOf('>') >= 0);
    const bool rawHasMpos = rawFrameValid && (statusRawUpper.indexOf(F("MPOS:")) >= 0);
    const bool rawHasWpos = rawFrameValid && (statusRawUpper.indexOf(F("WPOS:")) >= 0);
    const bool rawHasWco = rawFrameValid && (statusRawUpper.indexOf(F("WCO:")) >= 0);
    bool hasMpos = grblGetMachinePosition(&mposX, &mposY, &mposZ);
    bool hasWpos = grblGetWorkPosition(&wposX, &wposY, &wposZ);
    bool usedRawPositionFallback = false;
    if (rawFrameValid && (!hasMpos || !hasWpos)) {
        float fx = 0.0f;
        float fy = 0.0f;
        float fz = 0.0f;
        if (!hasMpos && parseStatusTripletFieldCI(statusRawText, "MPOS", &fx, &fy, &fz)) {
            mposX = fx;
            mposY = fy;
            mposZ = fz;
            hasMpos = true;
            usedRawPositionFallback = true;
        }
        if (!hasWpos && parseStatusTripletFieldCI(statusRawText, "WPOS", &fx, &fy, &fz)) {
            wposX = fx;
            wposY = fy;
            wposZ = fz;
            hasWpos = true;
            usedRawPositionFallback = true;
        }
    }
    const char* positionTelemetryHint = "ok";
    if (!hasMachineStatus) {
        positionTelemetryHint = "no_status_frames";
    } else if (!rawFrameValid) {
        positionTelemetryHint = "invalid_or_empty_status_frame";
    } else if (usedRawPositionFallback) {
        positionTelemetryHint = "raw_status_fallback_used";
    } else if (!rawHasMpos && !rawHasWpos) {
        positionTelemetryHint = "status_without_coordinates_check_$10";
    } else if (!hasMpos && !hasWpos) {
        positionTelemetryHint = "coordinates_present_but_not_accepted";
    }
    bool controlCanWrite = false;
    bool controlLocked = false;
    const String controlMode = controlModeForSession(sessionId, &controlCanWrite, &controlLocked);
    const uint32_t leaseRemainingMs = controlLeaseRemainingMs();
    bool homingRequired = false;
    if (alarmActive) {
        const String alarmText = String(alarmMessage);
        homingRequired = alarmText.indexOf(F("ALARM:9")) >= 0;
    }

    String json;
    json.reserve(4096);
    json += F("{\"success\":true,\"message\":\"ok\"");
    json += F(",\"state\":\"");
    json += buildStatusStateName(status.state);
    json += F("\"");
    json += F(",\"workflow_state\":\"");
    json += jsonEscape(String(workflowState));
    json += F("\"");
    json += F(",\"job_source\":\"");
    json += jsonEscape(String(jobSource));
    json += F("\"");
    json += F(",\"job_active\":");
    json += statusToJsonBool(jobActive);
    json += F(",\"queue_active\":");
    json += statusToJsonBool(queueActive);
    json += F(",\"stream_active\":");
    json += statusToJsonBool(streamActive);
    json += F(",\"lines_sent\":");
    json += String((unsigned long)status.lines_sent);
    json += F(",\"lines_total\":");
    json += String((unsigned long)status.lines_total);
    json += F(",\"lines_acked\":");
    json += String((unsigned long)status.lines_acked);
    json += F(",\"error_count\":");
    json += String((unsigned long)status.error_count);
    json += F(",\"bytes_in_flight\":");
    json += String((unsigned long)status.bytes_in_flight);
    json += F(",\"usb_slot\":");
    json += String((unsigned long)status.usb_slot);
    json += F(",\"file_path\":\"");
    json += jsonEscape(String(status.file_path));
    json += F("\"");
    json += F(",\"poll_interval_ms\":");
    json += String((unsigned long)pollInterval);
    json += F(",\"poll_outstanding\":");
    json += String((unsigned long)pollOutstanding);
    json += F(",\"poll_health\":\"");
    if (pollOutstanding == 0) json += F("ok");
    else if (pollOutstanding < 10) json += F("degraded");
    else json += F("stalled");
    json += F("\"");
    json += F(",\"status_lag_ms\":");
    json += String((unsigned long)statusLagMs);
    json += F(",\"blocked_by_inflight\":");
    json += String((unsigned long)blockedByInflight);
    json += F(",\"blocked_by_reported_rx\":");
    json += String((unsigned long)blockedByReportedRx);
    json += F(",\"blocked_by_fifo_full\":");
    json += String((unsigned long)blockedByFifoFull);
    json += F(",\"blocked_by_critical_command\":");
    json += String((unsigned long)blockedByCriticalCommand);
    json += F(",\"blocked_by_safety_margin\":");
    json += String((unsigned long)blockedBySafetyMargin);
    json += F(",\"last_block_reason\":\"");
    json += jsonEscape(String(lastBlockReason));
    json += F("\"");
    json += F(",\"last_block_line\":\"");
    json += jsonEscape(String(lastBlockLine));
    json += F("\"");
    json += F(",\"last_block_ms\":");
    json += String((unsigned long)lastBlockMs);
    json += F(",\"status_health\":\"");
    if (!hasMachineStatus || statusLagMs == 0) json += F("unknown");
    else if (statusLagMs <= 1500UL) json += F("ok");
    else if (statusLagMs <= 4000UL) json += F("degraded");
    else json += F("stale");
    json += F("\"");
    json += F(",\"avg_cmd_per_min\":");
    json += String((unsigned long)avgCmdPerMin);
    json += F(",\"cmd_per_min\":");
    json += String((unsigned long)curCmdPerMin);
    json += F(",\"eta_sec\":");
    json += String((unsigned long)etaSec);
    json += F(",\"elapsed_ms\":");
    json += String((unsigned long)elapsedMs);
    json += F(",\"planner_buffer_free\":");
    json += String((unsigned long)plannerFree);
    json += F(",\"rx_buffer_free\":");
    json += String((unsigned long)rxFree);
    json += F(",\"avg_line_bytes\":");
    json += String((unsigned long)avgLineBytes);
    json += F(",\"burst_lines_sent\":");
    json += String((unsigned long)burstLinesSent);
    json += F(",\"burst_cycles\":");
    json += String((unsigned long)burstCycles);
    json += F(",\"hold_reason\":\"");
    json += jsonEscape(String(holdReason));
    json += F("\"");
    json += F(",\"alarm_active\":");
    json += statusToJsonBool(alarmActive);
    json += F(",\"alarm_message\":\"");
    json += jsonEscape(String(alarmMessage));
    json += F("\"");
    grblDebugLog("[DBG-WEB] machine_status_known=%d machine_state='%s' hasStatusRaw=%d\n",
                    (int)hasMachineStatus, machineState ? machineState : "null", (int)hasStatusRaw);
    json += F(",\"machine_status_known\":");
    json += statusToJsonBool(hasMachineStatus);
    json += F(",\"machine_state\":\"");
    json += jsonEscape(String(machineState));
    json += F("\"");
    json += F(",\"status_raw\":\"");
    json += jsonEscape(statusRawText);
    json += F("\"");
    json += F(",\"status_raw_len\":");
    json += String((unsigned long)statusRawText.length());
    json += F(",\"status_raw_frame_valid\":");
    json += statusToJsonBool(rawFrameValid);
    json += F(",\"status_raw_has_mpos\":");
    json += statusToJsonBool(rawHasMpos);
    json += F(",\"status_raw_has_wpos\":");
    json += statusToJsonBool(rawHasWpos);
    json += F(",\"status_raw_has_wco\":");
    json += statusToJsonBool(rawHasWco);
    json += F(",\"position_telemetry_hint\":\"");
    json += jsonEscape(String(positionTelemetryHint));
    json += F("\"");
    json += F(",\"axis_count\":");
    json += String(grblGetAxisCount());
    {
        float mtX = 0.0f, mtY = 0.0f, mtZ = 0.0f;
        if (grblGetMaxTravel(&mtX, &mtY, &mtZ)) {
            json += F(",\"max_travel_x\":");
            json += String(mtX, 1);
            json += F(",\"max_travel_y\":");
            json += String(mtY, 1);
            json += F(",\"max_travel_z\":");
            json += String(mtZ, 1);
        }
    }
    json += F(",\"mpos_valid\":");
    json += statusToJsonBool(hasMpos);
    json += F(",\"wpos_valid\":");
    json += statusToJsonBool(hasWpos);
    if (hasMpos) {
        json += F(",\"mpos_x\":");
        json += String(mposX, 3);
        json += F(",\"mpos_y\":");
        json += String(mposY, 3);
        json += F(",\"mpos_z\":");
        json += String(mposZ, 3);
    }
    if (hasWpos) {
        json += F(",\"wpos_x\":");
        json += String(wposX, 3);
        json += F(",\"wpos_y\":");
        json += String(wposY, 3);
        json += F(",\"wpos_z\":");
        json += String(wposZ, 3);
    }
    json += F(",\"door_open\":");
    json += statusToJsonBool(doorOpen);
    json += F(",\"probe_triggered\":");
    json += statusToJsonBool(probeTriggered);
    json += F(",\"height_map\":{");
    json += F("\"enabled\":");
    json += statusToJsonBool(grblGetHeightMapEnabled());
    json += F(",\"phase\":\"");
    switch (grblGetProbeGridPhase()) {
        case ProbeGridPhase::IDLE:       json += F("idle"); break;
        case ProbeGridPhase::MOVING_XY:  json += F("moving_xy"); break;
        case ProbeGridPhase::PROBING:    json += F("probing"); break;
        case ProbeGridPhase::RETRACTING: json += F("retracting"); break;
        case ProbeGridPhase::COMPLETE:   json += F("complete"); break;
        case ProbeGridPhase::CANCELLED:  json += F("cancelled"); break;
        case ProbeGridPhase::ERROR:      json += F("error"); break;
    }
    json += F("\",\"total\":");
    json += String(grblGetProbeGridTotal());
    json += F(",\"completed\":");
    json += String(grblGetProbeGridCompleted());
    json += F("}");
    json += F(",\"homing_required\":");
    json += statusToJsonBool(homingRequired);
    json += F(",\"control_mode\":\"");
    json += jsonEscape(controlMode);
    json += F("\"");
    json += F(",\"control_locked\":");
    json += statusToJsonBool(controlLocked);
    json += F(",\"control_can_write\":");
    json += statusToJsonBool(controlCanWrite);
    json += F(",\"control_lease_ms\":");
    json += String((unsigned long)leaseRemainingMs);
    json += F(",\"web_psram_ready\":");
    json += statusToJsonBool(webPsramReady);
    json += F(",\"job_session\":{");
    json += F("\"active\":");
    json += statusToJsonBool(jobSession.active);
    json += F(",\"type\":\"");
    json += jsonEscape(String(jobSession.type));
    json += F("\"");
    json += F(",\"state\":\"");
    json += jsonEscape(String(jobSession.state));
    json += F("\"");
    json += F(",\"origin\":\"");
    json += jsonEscape(String(jobSession.origin));
    json += F("\"");
    json += F(",\"current_line\":");
    json += String((unsigned long)jobSession.current_line);
    json += F(",\"total_lines\":");
    json += String((unsigned long)jobSession.total_lines);
    json += F(",\"pause_reason\":\"");
    json += jsonEscape(String(jobSession.pause_reason));
    json += F("\"");
    json += F(",\"tool_change_active\":");
    json += statusToJsonBool(strcmp(jobSession.pause_reason, "M6 Tool Change") == 0);
    json += F(",\"error_reason\":\"");
    json += jsonEscape(String(jobSession.error_reason));
    json += F("\"");
    {
        float swx = 0.0f, swy = 0.0f, swz = 0.0f;
        const bool swValid = grblGetJobStartWorkPos(&swx, &swy, &swz);
        json += F(",\"start_wpos_valid\":");
        json += swValid ? F("true") : F("false");
        if (swValid) {
            json += F(",\"start_wx\":");
            json += String(swx, 3);
            json += F(",\"start_wy\":");
            json += String(swy, 3);
            json += F(",\"start_wz\":");
            json += String(swz, 3);
        }
    }
    json += F("}");
    json += F(",\"console_events\":[");
    grblDebugLog("[DBG-WEB] buildStatus: webPsramReady=%d consoleCap=%u consoleCount=%u lastEventId=%lu\n",
                    (int)webPsramReady, (unsigned)consoleCap, (unsigned)consoleCount, (unsigned long)lastConsoleEventId);
    if (webPsramReady && consoleCap > 0 && consoleCount > 0) {
        uint8_t start = 0;
        if (consoleCount == consoleCap) {
            start = s_consoleHead;
        }

        bool firstConsole = true;
        bool lastEmittedWasStatusFrame = false;
        int emittedCount = 0;
        for (uint8_t i = 0; i < consoleCount; i++) {
            const uint8_t idx = (uint8_t)((start + i) % consoleCap);
            const GrblConsoleEvent& ev = s_consoleEvents[idx];
            if (ev.id == 0) continue;
            if (lastConsoleEventId > 0 && ev.id <= lastConsoleEventId) continue;

            const String evText = String(ev.text);
            // Dedup consecutive status frames of ANY machine state, not just Idle.
            // During streaming, GRBL sends "<Run|MPos:...>" every 500ms, flooding
            // the 120-slot buffer so real content ($$ responses, errors) is lost.
            const bool isStatusFrame = evText.length() > 0 && evText[0] == '<';
            if (isStatusFrame && lastEmittedWasStatusFrame) {
                continue;
            }

            if (!firstConsole) json += ',';
            firstConsole = false;
            json += F("{\"id\":");
            json += String((unsigned long)ev.id);
            json += F(",\"text\":\"");
            json += jsonEscape(evText);
            json += F("\"}");
            lastEmittedWasStatusFrame = isStatusFrame;
            emittedCount++;
        }
        grblDebugLog("[DBG-WEB] buildStatus: emitted %d console events\n", emittedCount);
    }
    json += F("]");
    json += F(",\"recent_errors\":[");
    for (uint8_t i = 0; i < recentErrorCount; i++) {
        GrblRecentError item = {};
        if (!grblGetRecentErrorAt(i, &item)) continue;
        if (i > 0) json += ',';
        json += F("{\"timestamp_ms\":");
        json += String((unsigned long)item.timestamp_ms);
        json += F(",\"line_sent\":");
        json += String((unsigned long)item.line_sent);
        json += F(",\"line_acked\":");
        json += String((unsigned long)item.line_acked);
        json += F(",\"error_count\":");
        json += String((unsigned long)item.error_count);
        json += F(",\"source\":\"");
        json += jsonEscape(String(item.source));
        json += F("\",\"message\":\"");
        json += jsonEscape(String(item.message));
        json += F("\",\"line_preview\":\"");
        json += jsonEscape(String(item.line_preview));
        json += F("\"}");
    }
    json += F("]");
    json += F(",\"grbl_transport\":\"");
    json += (grblGetTransportMode() == GrblTransportMode::UART) ? F("uart") : F("usb");
    json += F("\"");
    json += F(",\"usb_running\":");
    json += statusToJsonBool(usbHostIsRunning());
    json += F(",\"usb_backend\":\"");
    json += jsonEscape(String(usbHostBackendName()));
    json += F("\"");
    json += F(",\"usb_error\":\"");
    json += jsonEscape(String(usbHostLastError()));
    json += F("\"");
    json += F(",\"usb_begin_stage\":\"");
    json += jsonEscape(String(usbHostBeginStage()));
    json += F("\"");
    json += F(",\"usb_begin_calls\":");
    json += String((unsigned long)usbHostBeginCalls());
    json += F(",\"usb_begin_runs\":");
    json += String((unsigned long)usbHostBeginRuns());
    json += F(",\"usb_connected\":");
    json += String((unsigned long)usbStats.devices_connected);
    json += F(",\"resume_available\":");
    json += statusToJsonBool(status.resume_available);
    json += F(",\"resume_line\":");
    json += String((unsigned long)status.resume_line);
    json += F(",\"resume_total\":");
    json += String((unsigned long)status.resume_total);
    json += F(",\"resume_busy\":");
    json += statusToJsonBool(status.resume_busy);
    json += F(",\"resume_file\":\"");
    json += jsonEscape(String(status.resume_file));
    json += F("\",\"resume_alarm\":\"");
    json += jsonEscape(String(status.resume_alarm));
    json += F("\",\"resume_error\":\"");
    json += jsonEscape(String(status.resume_error));
    json += F("\"");
    json += F(",\"grbl_log_enabled\":");
    json += statusToJsonBool(grblJobLogIsEnabled());
#if ENABLE_USB_HOST == 1
    json += F(",\"usb_diag\":{");
    json += F("\"peripheral_map\":");
    json += String((unsigned long)usbDiag.peripheral_map);
    json += F(",\"phy_route_idx\":");
    json += String((unsigned long)usbDiag.phy_route_idx);
    json += F(",\"phy_fallback_attempted\":");
    json += statusToJsonBool(usbDiag.phy_fallback_attempted);
    json += F(",\"phy_route_switches\":");
    json += String((unsigned long)usbDiag.phy_route_switches);
    json += F(",\"pin_exchg_enabled\":");
    json += statusToJsonBool(usbDiag.pin_exchg_enabled);
    json += F(",\"pin_exchg_fallback_attempted\":");
    json += statusToJsonBool(usbDiag.pin_exchg_fallback_attempted);
    json += F(",\"auto_restart_attempted\":");
    json += statusToJsonBool(usbDiag.auto_restart_attempted);
    json += F(",\"new_dev_events\":");
    json += String((unsigned long)usbDiag.new_dev_events);
    json += F(",\"dev_gone_events\":");
    json += String((unsigned long)usbDiag.dev_gone_events);
    json += F(",\"scan_cycles\":");
    json += String((unsigned long)usbDiag.scan_cycles);
    json += F(",\"scan_no_device_cycles\":");
    json += String((unsigned long)usbDiag.scan_no_device_cycles);
    json += F(",\"scan_addr_failures\":");
    json += String((unsigned long)usbDiag.scan_addr_failures);
    json += F(",\"last_addr_count\":");
    json += String((unsigned long)usbDiag.last_addr_count);
    json += F(",\"lib_num_devices\":");
    json += String((long)usbDiag.lib_num_devices);
    json += F(",\"lib_num_clients\":");
    json += String((long)usbDiag.lib_num_clients);
    json += F(",\"hprt0_raw\":\"0x");
    {
        char hexbuf[9];
        snprintf(hexbuf, sizeof(hexbuf), "%08lX", (unsigned long)usbDiag.hprt0_raw);
        json += hexbuf;
    }
    json += F("\"");
    json += F(",\"hprt0_ppwr\":");
    json += String((unsigned long)((usbDiag.hprt0_raw >> 12) & 1U));
    json += F(",\"hprt0_pcsts\":");
    json += String((unsigned long)(usbDiag.hprt0_raw & 1U));
    json += F(",\"hprt0_pena\":");
    json += String((unsigned long)((usbDiag.hprt0_raw >> 2) & 1U));
    json += F(",\"hprt0_plsts\":");
    json += String((unsigned long)((usbDiag.hprt0_raw >> 10) & 3U));
    json += F(",\"hprt0_fs_raw\":\"0x");
    {
        char hexbuf2[9];
        snprintf(hexbuf2, sizeof(hexbuf2), "%08lX", (unsigned long)usbDiag.hprt0_fs_raw);
        json += hexbuf2;
    }
    json += F("\"");
    json += F(",\"hprt0_fs_pcsts\":");
    json += String((unsigned long)(usbDiag.hprt0_fs_raw & 1U));
    json += F(",\"ghwcfg2_raw\":\"0x");
    {
        char hexbuf3[9];
        snprintf(hexbuf3, sizeof(hexbuf3), "%08lX", (unsigned long)usbDiag.ghwcfg2_raw);
        json += hexbuf3;
    }
    json += F("\"");
    json += F(",\"ghwcfg2_hsphytype\":");
    json += String((unsigned long)((usbDiag.ghwcfg2_raw >> 6) & 3U));
    json += F(",\"root_pwr_on_err\":");
    json += String((long)usbDiag.root_pwr_on_err);
    json += F(",\"gpio27_level\":");
    json += String((int)usbDiag.gpio27_level);
    json += F(",\"gpio26_level\":");
    json += String((int)usbDiag.gpio26_level);
    json += F("}");
#endif // ENABLE_USB_HOST

    json += F(",\"usb_slot_count\":");
    json += String((unsigned long)kUsbSlots);
    json += F(",\"usb_slots\":[");

    for (uint8_t i = 0; i < kUsbSlots; i++) {
        if (i > 0) json += ',';
        const UsbCdcDevice dev = grblTransportGetDevice(i);
        json += F("{\"slot\":");
        json += String((unsigned long)i);
        json += F(",\"connected\":");
        json += statusToJsonBool(dev.connected);
        json += F(",\"rx\":");
        json += String((unsigned long)dev.rx_bytes);
        json += F(",\"tx\":");
        json += String((unsigned long)dev.tx_bytes);
        json += F(",\"baud\":");
        json += String((unsigned long)dev.baud);
        json += F(",\"vid\":");
        json += String((unsigned long)dev.vid);
        json += F(",\"pid\":");
        json += String((unsigned long)dev.pid);
        json += F(",\"dev_addr\":");
        json += String((unsigned long)dev.dev_addr);
        json += F(",\"name\":\"");
        json += jsonEscape(String(dev.description));
        json += F("\",\"chip\":\"");
        json += jsonEscape(usbChipName(dev.vid, dev.pid));
        json += F("\"}");
    }

    json += F("]}");
    if (lastConsoleEventId == 0) {
        s_cache = json;
        s_cacheAge = millis();
    }
    return json;
}

String grblWebBuildEventsSse(const String& sessionIdRaw, uint32_t lastEventId) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    String out;
    out.reserve(3072);
    out += F("retry: 1200\n\n");

    if (!ensureWebBuffers()) {
        const uint32_t statusId = nextEventId();
        appendSseEvent(out, statusId, F("status"), grblWebBuildStatusJson(sessionId));
        return out;
    }

    const uint8_t cap = consoleEventCapacity();
    uint8_t emitCount = s_consoleCount;
    if (emitCount > cap) emitCount = cap;

    uint8_t start = 0;
    if (s_consoleCount == cap) start = s_consoleHead;

    for (uint8_t i = 0; i < emitCount; i++) {
        const uint8_t idx = (uint8_t)((start + i) % cap);
        const GrblConsoleEvent& ev = s_consoleEvents[idx];
        if (ev.id == 0 || ev.id <= lastEventId) continue;

        String data;
        data.reserve(220);
        data += F("{\"text\":\"");
        data += jsonEscape(String(ev.text));
        data += F("\"}");
        appendSseEvent(out, ev.id, F("console"), data);
    }

    const uint8_t pCap = productionEventCapacity();
    uint8_t pCount = s_prodCount;
    if (pCount > pCap) pCount = pCap;
    uint8_t pStart = 0;
    if (s_prodCount == pCap) pStart = s_prodHead;

    for (uint8_t i = 0; i < pCount; i++) {
        const uint8_t idx = (uint8_t)((pStart + i) % pCap);
        const GrblProductionEvent& ev = s_prodEvents[idx];
        if (ev.id == 0 || ev.id <= lastEventId) continue;

        String data;
        data.reserve(260);
        data += F("{\"type\":\"");
        data += jsonEscape(String(ev.type));
        data += F("\",\"message\":\"");
        data += jsonEscape(String(ev.text));
        data += F("\"}");
        appendSseEvent(out, ev.id, F("production"), data);
    }

    const uint32_t statusId = nextEventId();
    appendSseEvent(out, statusId, F("status"), grblWebBuildStatusJson(sessionId));
    return out;
}

String grblWebBuildActionJson(const GrblWebActionResult& result) {
    String json;
    json.reserve(96 + result.message.length());
    json += F("{\"success\":");
    json += result.success ? F("true") : F("false");
    json += F(",\"message\":\"");
    json += jsonEscape(result.message);
    json += F("\"}");
    return json;
}

String grblWebBuildControlTokenJson(const String& sessionIdRaw) {
    if (!ensureWebBuffers()) {
        return F("{\"success\":false,\"message\":\"psram_unavailable\"}");
    }
    const String sid = sanitizeSessionId(sessionIdRaw);
    bool canWrite = false;
    const String mode = controlModeForSession(sid, &canWrite, nullptr);
    const String token = issueTokenForSession(sid);

    String json;
    json.reserve(260);
    json += F("{\"success\":true,\"message\":\"ok\",\"token\":\"");
    json += jsonEscape(token);
    json += F("\",\"permission\":\"");
    json += jsonEscape(mode);
    json += F("\",\"can_write\":");
    json += statusToJsonBool(canWrite);
    json += F(",\"expires_ms\":");
    json += String((unsigned long)kTokenLeaseMs);
    json += F("}");
    return json;
}

GrblWebActionResult grblWebAuthorizeWrite(const String& sessionIdRaw, const String& tokenRaw) {
    if (!ensureWebBuffers()) return {false, F("psram_unavailable")};
    const String sid = sanitizeSessionId(sessionIdRaw);
    const String token = sanitizeSessionId(tokenRaw);
    if (!validateSessionToken(sid, token)) return {false, F("auth_token_invalid")};
    return requireWriteControl(sid);
}

void grblWebPublishConsoleLine(const String& line) {
    String clean = line;
    clean.trim();
    if (clean.length() == 0) {
        grblDebugLog("[DBG-WEB] grblWebPublishConsoleLine: empty after trim, line='%s'\n", line.c_str());
        return;
    }
    if (clean.length() > 0 && clean[0] == '<') {
        grblDebugLog("[DBG-WEB] grblWebPublishConsoleLine: status frame '%.80s'\n", clean.c_str());
    }
    pushConsoleEvent(clean);
}

void grblWebPublishProductionEvent(const char* type, const String& message) {
    String clean = message;
    clean.trim();
    if (clean.length() == 0) clean = F("event");
    pushProductionEvent(type, clean);
}

uint8_t grblWebNormalizeSlot(int slot) {
    if (slot < 0) return 0;
    if (slot >= (int)kUsbSlots) return (uint8_t)(kUsbSlots - 1);
    return (uint8_t)slot;
}

GrblWebActionResult grblWebSendCommand(uint8_t usbSlot, const String& command, const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    if (command.length() == 0) return {false, F("empty_command")};
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;

    // Intercept system commands before sending to GRBL.
    // GRBLLOG, STATUS, etc. are ESP32 commands, not GRBL G-code.
    String upperCmd = command;
    upperCmd.trim();
    upperCmd.toUpperCase();
    if (upperCmd.startsWith(F("GRBLLOG"))) {
        executeCommand(command);
        grblWebPublishConsoleLine(String("system: ") + command);
        return {true, F("ok")};
    }

    const uint8_t resolvedSlot = resolveUsbSlotForAction(usbSlot);
    const bool ok = grblSend(resolvedSlot, command.c_str());
    if (!ok) return {false, F("send_failed")};
    grblWebPublishConsoleLine(String("send ") + command);
    return {true, F("ok")};
}

GrblWebActionResult grblWebSetOverride(uint8_t usbSlot, int feedPct, int spinPct, const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;

    const uint8_t resolvedSlot = resolveUsbSlotForAction(usbSlot);
    const bool ok = grblSetOverride(resolvedSlot, feedPct, spinPct);
    if (!ok) return {false, F("override_failed")};
    char line[48];
    if (feedPct >= 0 && spinPct >= 0) {
        snprintf(line, sizeof(line), "override feed %d%% spin %d%%", feedPct, spinPct);
    } else if (feedPct >= 0) {
        snprintf(line, sizeof(line), "override feed %d%%", feedPct);
    } else {
        snprintf(line, sizeof(line), "override spin %d%%", spinPct);
    }
    grblWebPublishConsoleLine(String(line));
    return {true, F("ok")};
}

GrblWebActionResult grblWebStartStream(uint8_t usbSlot, const String& filePath, const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    if (filePath.length() == 0) return {false, F("empty_path")};
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;

    String path = filePath;
    if (!path.startsWith("/")) path = "/" + path;

    const uint8_t resolvedSlot = resolveUsbSlotForAction(usbSlot);
    const GrblWebActionResult preflight = runStartPreflight(resolvedSlot, false, path);
    if (!preflight.success) return preflight;

    const bool ok = grblStreamFile(resolvedSlot, path.c_str());
    if (!ok) return {false, F("stream_failed")};
    grblSetJobOrigin(sessionId.c_str());
    grblWebPublishConsoleLine(String("start stream ") + path);
    return {true, F("ok")};
}

GrblWebActionResult grblWebStartPsramStream(uint8_t usbSlot, const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;

    const char* name = grblPsramGcodeGetName();
    if (!name || !*name) return {false, F("no_file")};
    const size_t size = grblPsramGcodeGetSize();
    if (size == 0) return {false, F("empty_file")};

    const uint8_t resolvedSlot = resolveUsbSlotForAction(usbSlot);
    const GrblWebActionResult preflight = runStartPreflight(resolvedSlot, false, name);
    if (!preflight.success) return preflight;

    const bool ok = grblStreamPsramData(resolvedSlot, name);
    if (!ok) return {false, F("psram_stream_failed")};
    grblSetJobOrigin(sessionId.c_str());
    grblWebPublishConsoleLine(String("start PSRAM stream ") + name);
    return {true, F("ok")};
}

String grblWebBuildPsramFileInfoJson() {
    const char* name = grblPsramGcodeGetName();
    const size_t size = grblPsramGcodeGetSize();
    const bool hasFile = (name && *name && size > 0);
    String json = String("{\"success\":true,\"has_file\":") + (hasFile ? "true" : "false");
    if (hasFile) {
        json += ",\"name\":\"" + String(name) + "\"";
        json += ",\"size\":" + String((unsigned long)size);
    }
    json += "}";
    return json;
}

GrblWebActionResult grblWebStartQueue(uint8_t usbSlot, const String& queueText, const String& sessionIdRaw,
                                      const String& queueErrorMode, uint8_t retryMax) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    if (queueText.length() == 0) return {false, F("empty_queue")};
    if (queueText.length() >= GRBL_LOCAL_QUEUE_MAX_BYTES) return {false, F("queue_too_large")};
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;

    const uint8_t resolvedSlot = resolveUsbSlotForAction(usbSlot);
    const GrblWebActionResult preflight = runStartPreflight(resolvedSlot, true, queueText);
    if (!preflight.success) return preflight;

    String mode = queueErrorMode;
    mode.toLowerCase();
    const bool continueOnError = (mode == F("continue"));
    if (retryMax > 10) retryMax = 10;
    grblSetQueueExecutionOptions(continueOnError, retryMax);

    const bool ok = grblQueueStart(resolvedSlot, queueText.c_str());
    if (!ok) return {false, F("queue_start_failed")};
    grblSetJobOrigin(sessionId.c_str());
    grblWebPublishConsoleLine(F("start local queue"));
    return {true, F("ok")};
}

GrblWebActionResult grblWebStopQueue(const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;
    const bool ok = grblQueueStop();
    if (!ok) return {false, F("queue_not_running")};
    grblWebPublishConsoleLine(F("stop local queue"));
    return {true, F("ok")};
}

GrblWebActionResult grblWebPauseStream(const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;
    const GrblStatus status = grblGetStatus();
    if (status.state != GrblStreamState::STREAMING) return {false, F("not_streaming")};
    grblPause();
    grblWebPublishConsoleLine(F("pause stream"));
    return {true, F("ok")};
}

GrblWebActionResult grblWebResumeStream(const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;
    const GrblStatus status = grblGetStatus();
    if (status.state != GrblStreamState::PAUSED) return {false, F("not_paused")};
    const bool resumed = grblResume();
    if (!resumed) return {false, F("resume_blocked_ack_stall")};
    grblWebPublishConsoleLine(F("resume stream"));
    return {true, F("ok")};
}

GrblWebActionResult grblWebCancelStream(const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;
    const GrblStatus status = grblGetStatus();
    if (status.state == GrblStreamState::IDLE || status.state == GrblStreamState::DONE ||
        status.state == GrblStreamState::ERROR || status.state == GrblStreamState::CANCELLED) {
        return {false, F("nothing_to_cancel")};
    }
    grblCancel();
    grblWebPublishConsoleLine(F("cancel stream"));
    return {true, F("ok")};
}

GrblWebActionResult grblWebSetPollInterval(uint32_t ms, const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;
    if (ms == 0UL) ms = 500UL;
    if (ms > 10000UL) ms = 10000UL;
    grblSetStatusPollInterval(ms);
    return {true, F("ok")};
}

GrblWebActionResult grblWebAutoDetectBaud(uint8_t slot, const String& sessionIdRaw) {
    const String sessionId = sanitizeSessionId(sessionIdRaw);
    const GrblWebActionResult control = requireWriteControl(sessionId);
    if (!control.success) return control;

    // Verify the slot is connected before cycling rates.
    const UsbCdcDevice dev = usbHostGetDevice(slot);
    if (!dev.connected) {
        return {false, F("usb_slot_not_connected")};
    }

    const uint32_t detected = grblAutoDetectBaud(slot);
    if (detected == 0) {
        return {false, F("no_response")};
    }

    char msg[48];
    snprintf(msg, sizeof(msg), "detected %lu baud", (unsigned long)detected);
    return {true, msg};
}

#else

const char* grblWebStateToString(GrblStreamState state) {
    switch (state) {
        case GrblStreamState::IDLE: return "IDLE";
        case GrblStreamState::STREAMING: return "STREAMING";
        case GrblStreamState::PAUSED: return "PAUSED";
        case GrblStreamState::DONE: return "DONE";
        case GrblStreamState::ERROR: return "ERROR";
        case GrblStreamState::CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

String grblWebBuildStatusJson(const String&, uint32_t) {
    return F("{\"success\":false,\"message\":\"grbl_not_available\"}");
}

String grblWebBuildEventsSse(const String&, uint32_t) {
    return F("retry: 1200\n\n");
}

String grblWebBuildActionJson(const GrblWebActionResult& result) {
    String json;
    json.reserve(96 + result.message.length());
    json += F("{\"success\":");
    json += result.success ? F("true") : F("false");
    json += F(",\"message\":\"");
    json += result.message;
    json += F("\"}");
    return json;
}

String grblWebBuildControlTokenJson(const String&) {
    return F("{\"success\":false,\"message\":\"grbl_not_available\"}");
}

void grblWebPublishConsoleLine(const String&) {}

void grblWebPublishProductionEvent(const char*, const String&) {}

uint8_t grblWebNormalizeSlot(int slot) {
    if (slot < 0) return 0;
    return (uint8_t)slot;
}

GrblWebActionResult grblWebAuthorizeWrite(const String&, const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebSendCommand(uint8_t, const String&, const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebStartStream(uint8_t, const String&, const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebStartPsramStream(uint8_t, const String&) {
    return {false, F("grbl_not_available")};
}

String grblWebBuildPsramFileInfoJson() {
    return F("{\"success\":false,\"message\":\"grbl_not_available\"}");
}

GrblWebActionResult grblWebStartQueue(uint8_t, const String&, const String&, const String&, uint8_t) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebStopQueue(const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebPauseStream(const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebResumeStream(const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebCancelStream(const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebSetPollInterval(uint32_t, const String&) {
    return {false, F("grbl_not_available")};
}

GrblWebActionResult grblWebAutoDetectBaud(uint8_t, const String&) {
    return {false, F("grbl_not_available")};
}

#endif // PACK_CNC
#endif // ENABLE_GRBL
