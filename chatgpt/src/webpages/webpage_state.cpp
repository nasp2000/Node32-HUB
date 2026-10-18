#include <Arduino.h>
#include "../arduinojson_fix.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../ConfigManager.h"
#include "../commands.h"
#include "../psram_helpers.h"
#include <time.h>

extern ConfigManager configManager;

// Session event types recorded in the in-memory ring buffer.
enum SessionEventType : uint8_t { SE_CONNECT = 0, SE_EXPIRE = 1, SE_KICK = 2 };

struct SessionEvent {
    time_t  ts;
    char    ip[16];
    char    ua[48]; // parsed User-Agent string (OS + browser)
    uint8_t type;   // SessionEventType
};

static const int SESSION_HIST_MAX    = 64;
static SessionEvent*     sessionHist     = nullptr;
static int               sessionHistHead = 0;
static int               sessionHistSize = 0;
static SemaphoreHandle_t sessionHistMx   = nullptr;

static void sessionHistEnsureInit() {
    if (sessionHist) return;
    size_t sz = sizeof(SessionEvent) * SESSION_HIST_MAX;
    if (psramFound())
        sessionHist = (SessionEvent*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
    if (!sessionHist)
        sessionHist = (SessionEvent*)malloc(sz);
    if (sessionHist) memset(sessionHist, 0, sz);
    sessionHistMx = xSemaphoreCreateMutex();
}

// Extracts a compact OS + browser label from a raw User-Agent string.
static void _parseUA(const char* raw, char* out, size_t outLen) {
    if (!raw || !out || outLen == 0) { if (out) out[0] = '\0'; return; }
    const char* os = "?";
    if      (strstr(raw, "Android"))  os = "Android";
    else if (strstr(raw, "iPhone"))   os = "iPhone";
    else if (strstr(raw, "iPad"))     os = "iPad";
    else if (strstr(raw, "Windows"))  os = "Windows";
    else if (strstr(raw, "Mac OS X")) os = "macOS";
    else if (strstr(raw, "Linux"))    os = "Linux";
    const char* br = "";
    if      (strstr(raw, "Edg/"))     br = " - Edge";
    else if (strstr(raw, "OPR/"))     br = " - Opera";
    else if (strstr(raw, "Chrome/"))  br = " - Chrome";
    else if (strstr(raw, "Firefox/")) br = " - Firefox";
    else if (strstr(raw, "Safari/"))  br = " - Safari";
    snprintf(out, outLen, "%s%s", os, br);
}

static void sessionHistRecord(const char* ip, SessionEventType type, const char* ua = "") {
    sessionHistEnsureInit();
    if (!sessionHist) return;
    if (sessionHistMx) xSemaphoreTake(sessionHistMx, portMAX_DELAY);
    SessionEvent& ev = sessionHist[sessionHistHead];
    ev.ts   = time(nullptr);
    ev.type = (uint8_t)type;
    strncpy(ev.ip, ip ? ip : "?", sizeof(ev.ip) - 1);
    ev.ip[sizeof(ev.ip) - 1] = '\0';
    strncpy(ev.ua, ua ? ua : "", sizeof(ev.ua) - 1);
    ev.ua[sizeof(ev.ua) - 1] = '\0';
    sessionHistHead = (sessionHistHead + 1) % SESSION_HIST_MAX;
    if (sessionHistSize < SESSION_HIST_MAX) sessionHistSize++;
    if (sessionHistMx) xSemaphoreGive(sessionHistMx);
}

// Builds a JSON array of all recorded session events; called from access_handler.
String buildSessionHistoryJson() {
    sessionHistEnsureInit();
    if (!sessionHist || sessionHistSize == 0) return F("{\"events\":[]}");
    if (sessionHistMx) xSemaphoreTake(sessionHistMx, portMAX_DELAY);
    int count = sessionHistSize;
    int start = (count < SESSION_HIST_MAX) ? 0 : sessionHistHead;
    String out;
    out.reserve(count * 80);
    out = F("{\"events\":[");
    bool first = true;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % SESSION_HIST_MAX;
        const SessionEvent& ev = sessionHist[idx];
        if (!first) out += ',';
        first = false;
        char tsBuf[20] = "---";
        if (ev.ts > 0) {
            struct tm tmInfo;
            localtime_r(&ev.ts, &tmInfo);
            strftime(tsBuf, sizeof(tsBuf), "%Y-%m-%d %H:%M:%S", &tmInfo);
        }
        const char* typeStr = ev.type == (uint8_t)SE_CONNECT ? "connect"
                            : ev.type == (uint8_t)SE_KICK    ? "kick"
                            : "expired";
        out += F("{\"ts\":");
        out += (long)ev.ts;
        out += F(",\"ip\":\"");   out += ev.ip;
        out += F("\",\"type\":\""); out += typeStr;
        out += F("\",\"when\":\""); out += tsBuf;
        out += F("\"}" );
    }
    out += F("]}");
    if (sessionHistMx) xSemaphoreGive(sessionHistMx);
    return out;
}

// Fills arr with the last maxCount unique client IPs (most recent first).
// Each entry: {"ip":"...", "when":"HH:MM:SS"}
void fillRecentUniqueIPs(JsonArray& arr, int maxCount) {
    sessionHistEnsureInit();
    if (!sessionHist || sessionHistSize == 0) return;
    if (sessionHistMx) xSemaphoreTake(sessionHistMx, portMAX_DELAY);

    char seen[4][16] = {};
    int seenCount = 0;
    int limit = (maxCount < 4) ? maxCount : 4;

    for (int i = 0; i < sessionHistSize && seenCount < limit; i++) {
        int idx = ((sessionHistHead - 1 - i) % SESSION_HIST_MAX + SESSION_HIST_MAX) % SESSION_HIST_MAX;
        const SessionEvent& ev = sessionHist[idx];
        if (ev.ip[0] == '\0') continue;

        bool dup = false;
        for (int s = 0; s < seenCount; s++) {
            if (strncmp(seen[s], ev.ip, 16) == 0) { dup = true; break; }
        }
        if (dup) continue;

        strncpy(seen[seenCount], ev.ip, 15);
        seen[seenCount][15] = '\0';
        seenCount++;

        char timeBuf[10] = "---";
        if (ev.ts > 0) {
            struct tm tmInfo;
            localtime_r(&ev.ts, &tmInfo);
            strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &tmInfo);
        }
        JsonObject o = arr.createNestedObject();
        o["ip"] = ev.ip;
        o["when"] = timeBuf;
        if (ev.ua[0] != '\0') o["ua"] = ev.ua;
    }

    if (sessionHistMx) xSemaphoreGive(sessionHistMx);
}

volatile int activeHtmlSessions = 0;

// Timestamp of the last successful allowHtmlClient() call in multi-client mode.
// Used by webclientCleanup() to detect leaked session counters (onDisconnect not called).
static volatile unsigned long multiClientLastActivityMs = 0;

// Multi-client session tracking by IP (request-level counting caused false 429 on fast navigation).
static const uint8_t MULTI_TRACK_MAX = 8;
static const unsigned long MULTI_TRACK_TTL_MS = 15000UL;
struct MultiClientTracker {
	IPAddress ip;
	unsigned long lastSeenMs;
	bool used;
};
static MultiClientTracker multiTrack[MULTI_TRACK_MAX] = {};

static void multiTrackPrune(unsigned long nowMs) {
	for (uint8_t i = 0; i < MULTI_TRACK_MAX; i++) {
		if (!multiTrack[i].used) continue;
		if (nowMs - multiTrack[i].lastSeenMs > MULTI_TRACK_TTL_MS) {
			multiTrack[i].used = false;
		}
	}
}

static int multiTrackCount() {
	int c = 0;
	for (uint8_t i = 0; i < MULTI_TRACK_MAX; i++) if (multiTrack[i].used) c++;
	return c;
}

static bool multiTrackContains(const IPAddress& ip) {
	for (uint8_t i = 0; i < MULTI_TRACK_MAX; i++) {
		if (multiTrack[i].used && multiTrack[i].ip == ip) return true;
	}
	return false;
}

static bool multiTrackTouch(const IPAddress& ip, unsigned long nowMs, bool& isNewClient) {
	for (uint8_t i = 0; i < MULTI_TRACK_MAX; i++) {
		if (multiTrack[i].used && multiTrack[i].ip == ip) {
			multiTrack[i].lastSeenMs = nowMs;
			isNewClient = false;
			return true;
		}
	}
	for (uint8_t i = 0; i < MULTI_TRACK_MAX; i++) {
		if (!multiTrack[i].used) {
			multiTrack[i].used = true;
			multiTrack[i].ip = ip;
			multiTrack[i].lastSeenMs = nowMs;
			isNewClient = true;
			return true;
		}
	}
	isNewClient = true;
	return false;
}

static IPAddress singleClientIP = (uint32_t)0;
static volatile unsigned long singleClientConnectedAt = 0;
static volatile unsigned long singleClientLastActivity = 0;

// Read session limits from config at call time so changes take effect without reboot.
static inline int cfgMaxSessions()    { return configManager.getWebMaxSessions(); }
static inline unsigned long cfgIdleMs()   { return (unsigned long)configManager.getWebIdleTimeoutMin() * 60UL * 1000UL; }
static inline unsigned long cfgAbsMs()    { return (unsigned long)configManager.getWebAbsTimeoutMin() * 60UL * 1000UL; }

// Returns true if the authenticated user is the viewer (read-only) account.
static bool isViewerRequest(AsyncWebServerRequest* request) {
#include "feature_flags.h"
#if ENABLE_HTTP_BASIC_AUTH==1
	if (!configManager.getWebViewerEnabled()) return false;
	String vPass = configManager.getWebViewerPassword();
	if (vPass.length() == 0) return false;
	String vUser = configManager.getWebViewerUsername();
	return request->authenticate(vUser.c_str(), vPass.c_str());
#else
	(void)request;
	return false;
#endif
}

bool allowHtmlClient(AsyncWebServerRequest* request) {
	if (request == nullptr) return false;

	// Viewer user: restrict to read-only pages only.
	if (isViewerRequest(request)) {
		String url = request->url();
		if (url != "/" && url != "/status" && url != "/sensor-charts" && url != "/monitor" && url != "/files") {
			request->send(403, "text/plain", "Acesso negado: utilizador de visualizacao");
			return false;
		}
	}

	if (!configManager.getWebSingleClientOnly()) {
		IPAddress remote = request->client()->remoteIP();
		const unsigned long nowMs = millis();
		multiTrackPrune(nowMs);

		const bool knownClient = multiTrackContains(remote);
		if (!knownClient && multiTrackCount() >= cfgMaxSessions()) {
			request->send(429, "text/plain", "Too many viewers - try later");
			return false;
		}

		bool isNewClient = false;
		if (!multiTrackTouch(remote, nowMs, isNewClient)) {
			request->send(429, "text/plain", "Too many viewers - try later");
			return false;
		}

		if (isNewClient) {
			String ipStr = remote.toString();
			char uaBuf[48] = "";
			const AsyncWebHeader* uaHdr = request->getHeader("User-Agent");
			if (uaHdr) _parseUA(uaHdr->value().c_str(), uaBuf, sizeof(uaBuf));
			sessionHistRecord(ipStr.c_str(), SE_CONNECT, uaBuf);
		}

		activeHtmlSessions = multiTrackCount();
		multiClientLastActivityMs = nowMs;
		return true;
	}

	IPAddress remote = request->client()->remoteIP();
	unsigned long now = millis();

	if (singleClientConnectedAt != 0) {
		unsigned long idleTime = now - singleClientLastActivity;
		unsigned long totalTime = now - singleClientConnectedAt;

		if (!configManager.getChartsExemptSessions()) {
			if (idleTime >= cfgIdleMs()) {
				singleClientIP = (uint32_t)0;
				singleClientConnectedAt = 0;
				singleClientLastActivity = 0;
				activeHtmlSessions = 0;
				request->send(503, "text/plain", "Session expired due to inactivity. Please reload to reconnect.");
				return false;
			}

			if (totalTime >= cfgAbsMs()) {
				singleClientIP = (uint32_t)0;
				singleClientConnectedAt = 0;
				singleClientLastActivity = 0;
				activeHtmlSessions = 0;
				request->send(503, "text/plain", "Session expired: time limit reached. Please reload to reconnect.");
				return false;
			}
		}

		if (singleClientIP == remote) {
			singleClientLastActivity = now;
			if (activeHtmlSessions == 0) {
				activeHtmlSessions = 1;
				request->onDisconnect([]() {
					if (activeHtmlSessions > 0) activeHtmlSessions = activeHtmlSessions - 1;
				});
			}
			return true;
		}

		request->send(429, "text/plain", "Only one web client allowed at a time - try later");
		return false;
	}

	{
		String ipStr = remote.toString();
		char uaBuf[48] = "";
		const AsyncWebHeader* uaHdr = request->getHeader("User-Agent");
		if (uaHdr) _parseUA(uaHdr->value().c_str(), uaBuf, sizeof(uaBuf));
		sessionHistRecord(ipStr.c_str(), SE_CONNECT, uaBuf);
	}
	singleClientIP = remote;
	singleClientConnectedAt = now;
	singleClientLastActivity = now;
	activeHtmlSessions = activeHtmlSessions + 1;
	request->onDisconnect([]() {
		if (activeHtmlSessions > 0) activeHtmlSessions = activeHtmlSessions - 1;
		singleClientIP = (uint32_t)0;
		singleClientConnectedAt = 0;
		singleClientLastActivity = 0;
	});
	return true;
}

void webclientCleanup() {
	unsigned long now = millis();

	// Multi-client mode safety net: reset session counter if no activity for 30s.
	// This recovers from leaked counters when onDisconnect is not called (e.g. reused
	// TCP connections, stack-level resets, or responses completed before disconnect fires).
	if (!configManager.getWebSingleClientOnly()) {
		multiTrackPrune(now);
		activeHtmlSessions = multiTrackCount();
		if (activeHtmlSessions > 0 && multiClientLastActivityMs > 0 && (now - multiClientLastActivityMs >= 30000UL)) {
			for (uint8_t i = 0; i < MULTI_TRACK_MAX; i++) multiTrack[i].used = false;
			activeHtmlSessions = 0;
			sendVirtualMessage(F("Web: reset multi-client sessions (stale tracker)"), INFO);
		}
		return; // rest of the function handles single-client mode only
	}

	if (singleClientConnectedAt == 0) return;

	unsigned long idleTime = now - singleClientLastActivity;
	unsigned long totalTime = now - singleClientConnectedAt;

	const char* kickReason = nullptr;
	if (idleTime >= cfgIdleMs()) kickReason = "inatividade (idle timeout)";
	else if (totalTime >= cfgAbsMs()) kickReason = "tempo limite (abs timeout)";

	if (kickReason == nullptr) return;
	if (configManager.getChartsExemptSessions()) return;

	{ String ipStr = singleClientIP.toString(); sessionHistRecord(ipStr.c_str(), SE_EXPIRE); }
	sendVirtualMessage(String(F("Sessao web expirada: ")) + kickReason, INFO);
	singleClientIP = (uint32_t)0;
	singleClientConnectedAt = 0;
	singleClientLastActivity = 0;
	activeHtmlSessions = 0;
}

void webclientForceReset() {
	sessionHistRecord("kick-all", SE_KICK);
	singleClientIP = (uint32_t)0;
	singleClientConnectedAt = 0;
	singleClientLastActivity = 0;
	activeHtmlSessions = 0;
}

void decrementActiveHtmlSessions() {
	if (!configManager.getWebSingleClientOnly()) return;
	if (activeHtmlSessions > 0) activeHtmlSessions = activeHtmlSessions - 1;
}

// Local-processing pages (live MJPEG, dashboards): an active viewer here
// must never be kicked by idle/absolute timeouts while it keeps polling.
// The page itself polls /api/camera/status every 5 s, so any request from
// the tracked client refreshes its slot. Called by page routes (camera)
// before serving; no-op when single-client mode is off.
void webLocalPageHeartbeat(AsyncWebServerRequest* request) {
	if (configManager.getWebSingleClientOnly()) return;
	if (!request || !request->client()) return;
	bool dummy = false;
	multiTrackTouch(request->client()->remoteIP(), millis(), dummy);
}

bool ensureBasicAuth(AsyncWebServerRequest* request) {
#if ENABLE_HTTP_BASIC_AUTH==1
	// HTTP auth master switch (persisted, default OFF). When off, no login.
	if (!configManager.getWebAuthEnabled()) return true;

	String user = configManager.getWebUsername();
	String pass = configManager.getWebPassword();
	// No credentials configured → open (avoids lockout).
	if (user.length() == 0 && pass.length() == 0) return true;
	if (request->authenticate(user.c_str(), pass.c_str())) {
		if (!configManager.getWebSingleClientOnly()) {
			const unsigned long nowMs = millis();
			multiClientLastActivityMs = nowMs;
			bool dummy = false;
			multiTrackTouch(request->client()->remoteIP(), nowMs, dummy);
		}
		return true;
	}

	// Also accept the viewer user if enabled and has a password.
	if (configManager.getWebViewerEnabled()) {
		String vUser = configManager.getWebViewerUsername();
		String vPass = configManager.getWebViewerPassword();
		if (vPass.length() > 0 && request->authenticate(vUser.c_str(), vPass.c_str())) {
			if (!configManager.getWebSingleClientOnly()) {
				const unsigned long nowMs = millis();
				multiClientLastActivityMs = nowMs;
				bool dummy = false;
				multiTrackTouch(request->client()->remoteIP(), nowMs, dummy);
			}
			// Viewer is read-only: block write methods.
			WebRequestMethodComposite m = request->method();
			if (m != HTTP_GET && m != HTTP_HEAD && m != HTTP_OPTIONS) {
				request->send(405, "text/plain", "Viewer is read-only");
				return false;
			}
			return true;
		}
	}

	request->requestAuthentication("ESP32 Web");
	return false;
#else
	(void)request;
#endif
	return true;
}
