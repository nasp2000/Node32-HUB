#include "grbl_handler.h"
#include "grbl_sd_utils.h"
#include "../../feature_flags.h"

#if ENABLE_GRBL == 1

#include <Arduino.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <vector>
#include <algorithm>

#include "../../grbl_web_api.h"
#include "../../grbl_psram_gcode.h"
#include "../../usb_host_manager.h"
#include "../../feature_flags.h"
#include "../../ConfigManager.h"
#include "../../runtime_storage.h"
#include "../../psram_vfs.h"
#include "../../sd_security.h"
#include "../../sdcard.h"
#if ENABLE_SDCARD==1
#include <SD_MMC.h>
#endif

extern ConfigManager configManager;
extern bool psramVFSInitialized;

namespace {

String getValue(const String& queryOrBody, const char* key) {
    String prefix = String(key) + '=';
    int start = 0;
    while (start <= (int)queryOrBody.length()) {
        int amp = queryOrBody.indexOf('&', start);
        if (amp < 0) amp = queryOrBody.length();
        String pair = queryOrBody.substring(start, amp);
        if (pair.startsWith(prefix)) {
            String out;
            for (size_t i = prefix.length(); i < pair.length(); i++) {
                char c = pair.charAt(i);
                if (c == '+') { out += ' '; }
                else if (c == '%' && i + 2 < pair.length()) {
                    char h1 = pair.charAt(i + 1);
                    char h2 = pair.charAt(i + 2);
                    int v1 = (h1 >= '0' && h1 <= '9') ? (h1 - '0') : (h1 >= 'A' && h1 <= 'F') ? (h1 - 'A' + 10) : (h1 >= 'a' && h1 <= 'f') ? (h1 - 'a' + 10) : 0;
                    int v2 = (h2 >= '0' && h2 <= '9') ? (h2 - '0') : (h2 >= 'A' && h2 <= 'F') ? (h2 - 'A' + 10) : (h2 >= 'a' && h2 <= 'f') ? (h2 - 'a' + 10) : 0;
                    out += (char)(v1 * 16 + v2);
                    i += 2;
                } else {
                    out += c;
                }
            }
            return out;
        }
        start = amp + 1;
    }
    return String();
}

String queryOrBody(const HttpRequest& req, const char* key) {
    String v = getValue(req.query, key);
    if (v.length() > 0) return v;
    return getValue(req.body, key);
}

bool isTruthyValue(const String& raw) {
    String v = raw;
    v.trim();
    v.toLowerCase();
    return (v == "1" || v == "true" || v == "on" || v == "yes");
}

String jsonEscape(const String& raw) {
    String out;
    out.reserve(raw.length() + 8);
    for (size_t i = 0; i < raw.length(); i++) {
        const char c = raw[i];
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

void sendSimpleActionJson(HttpResponse& res,
                          int statusCode,
                          bool success,
                          const String& message,
                          const String& filePath = String(),
                          size_t bytesWritten = 0) {
    String payload = String("{\"success\":") + (success ? "true" : "false") +
                     ",\"message\":\"" + jsonEscape(message) + "\"";
    if (filePath.length() > 0) {
        payload += ",\"file_path\":\"" + jsonEscape(filePath) + "\"";
    }
    if (bytesWritten > 0) {
        payload += ",\"bytes\":" + String((unsigned long)bytesWritten);
    }
    payload += "}";
    res.send(statusCode, "application/json", payload);
}

void sendGrblActionResult(HttpResponse& res, const GrblWebActionResult& result) {
    String json = grblWebBuildActionJson(result);
    res.send(result.success ? 200 : 400, "application/json", json);
}

uint8_t readUsbSlot(const HttpRequest& req) {
    const String raw = queryOrBody(req, "slot");
    return grblWebNormalizeSlot(raw.toInt());
}

String readSessionId(const HttpRequest& req) {
    String sid = queryOrBody(req, "sid");
    sid.trim();
    if (sid.length() > 0) return sid;
    if (req.query.startsWith("ip:")) return req.query;
    if (req.body.startsWith("ip:")) return req.body;
    return "ip:unknown";
}

// ------------- SD helpers -------------

String pathLeafName(const String& path) {
    int slash = path.lastIndexOf('/');
    if (slash < 0) return path;
    if (slash + 1 >= (int)path.length()) return path;
    return path.substring((unsigned)(slash + 1));
}

bool ensureGrblRootExists(const String& rootDir) {
#if ENABLE_SDCARD==0
    (void)rootDir;
    return false;
#else
    if (rootDir.length() == 0 || rootDir[0] != '/') return false;
    int slash = rootDir.lastIndexOf('/');
    String parent = (slash > 0) ? rootDir.substring(0, (unsigned)slash) : String("/");
    if (parent.length() == 0) parent = "/";

    if (parent != "/" && !SD_MMC.exists(parent.c_str())) {
        if (!SD_MMC.mkdir(parent.c_str())) return false;
    }
    if (!SD_MMC.exists(rootDir.c_str())) {
        if (!SD_MMC.mkdir(rootDir.c_str())) return false;
    }
    File root = SD_MMC.open(rootDir.c_str());
    const bool ok = root && root.isDirectory();
    if (root) root.close();
    return ok;
#endif
}

bool removeSdTree(const String& path, const String& rootDir) {
#if ENABLE_SDCARD==0
    (void)path;
    (void)rootDir;
    return false;
#else
    File node = SD_MMC.open(path.c_str());
    if (!node) return false;
    if (!node.isDirectory()) {
        node.close();
        return SD_MMC.remove(path.c_str());
    }
    File entry = node.openNextFile();
    while (entry) {
        String child = String(entry.name());
        if (!child.startsWith("/")) child = path + "/" + child;
        child = SDSecurity::normalizePath(child);
        const bool isDir = entry.isDirectory();
        entry.close();
        bool ok = false;
        if (isDir) ok = removeSdTree(child, rootDir);
        else ok = SD_MMC.remove(child.c_str());
        if (!ok) { node.close(); return false; }
        entry = node.openNextFile();
    }
    node.close();
    if (path == rootDir) return true;
    return SD_MMC.rmdir(path.c_str());
#endif
}

bool parseTrailingDateDDMMYYYY(const String& name, int& day, int& month, int& year) {
    if (name.length() < 10) return false;
    const String datePart = name.substring(name.length() - 10);
    return sscanf(datePart.c_str(), "%d-%d-%d", &day, &month, &year) == 3;
}

bool grblAutoCleanupIfNeeded(const String& rootDir) {
#if ENABLE_SDCARD==0
    (void)rootDir;
    return true;
#else
    // Autoclean is always active (no UI toggle).
    if (!sdcardIsMounted()) return false;
    const uint64_t total = sdcardTotalBytes();
    if (total == 0) return true;
    const uint64_t used = sdcardUsedBytes();
    const int threshold = configManager.getSdCleanupThreshold();
    if ((used * 100ULL / total) < (uint64_t)threshold) return true;

    struct Entry { String path; String name; bool isDir; };
    std::vector<Entry> candidates;

    File root = SD_MMC.open(rootDir.c_str());
    if (!root || !root.isDirectory()) { if (root) root.close(); return true; }
    File entry = root.openNextFile();
    while (entry) {
        Entry item;
        item.path = String(entry.name());
        if (!item.path.startsWith("/")) item.path = rootDir + "/" + item.path;
        item.path = SDSecurity::normalizePath(item.path);
        item.name = pathLeafName(item.path);
        item.isDir = entry.isDirectory();
        candidates.push_back(item);
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
    if (candidates.empty()) return true;

    std::sort(candidates.begin(), candidates.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir < b.isDir;
        return a.name < b.name;
    });

    const int keepDays = configManager.getSdKeepDays();
    const time_t nowT = time(nullptr);
    for (size_t i = 0; i < candidates.size(); i++) {
        const Entry& item = candidates[i];
        if (keepDays > 0 && nowT > 100000) {
            int d = 0, m = 0, y = 0;
            if (parseTrailingDateDDMMYYYY(item.name, d, m, y) && y > 2000) {
                struct tm t = {};
                t.tm_year = y - 1900;
                t.tm_mon = m - 1;
                t.tm_mday = d;
                t.tm_hour = 12;
                const time_t fileT = mktime(&t);
                if (fileT > 0) {
                    const int ageDays = (int)(difftime(nowT, fileT) / 86400.0);
                    if (ageDays < keepDays) continue;
                }
            }
        }
        bool ok = false;
        if (item.isDir) ok = removeSdTree(item.path, rootDir);
        else ok = SD_MMC.remove(item.path.c_str());
        if (ok) return true;
    }
    return true;
#endif
}

// ------------- Frame bounds -------------

struct GrblFrameBounds {
    bool hasAny = false;
    float minX = 0.0f, maxX = 0.0f;
    float minY = 0.0f, maxY = 0.0f;
};

void grblUpdateBoundsPoint(GrblFrameBounds& b, float x, float y) {
    if (!b.hasAny) { b.hasAny = true; b.minX = b.maxX = x; b.minY = b.maxY = y; return; }
    if (x < b.minX) b.minX = x;
    if (x > b.maxX) b.maxX = x;
    if (y < b.minY) b.minY = y;
    if (y > b.maxY) b.maxY = y;
}

bool grblParseLineAndAccumulateBounds(const String& rawLine,
                                      bool& absoluteMode,
                                      float& curX, float& curY,
                                      GrblFrameBounds& bounds) {
    String line = rawLine;
    int semi = line.indexOf(';');
    if (semi >= 0) line = line.substring(0, semi);
    line.trim();
    if (line.length() == 0) return true;

    bool inParen = false, hasX = false, hasY = false;
    float xVal = 0.0f, yVal = 0.0f;
    size_t i = 0;
    while (i < line.length()) {
        char c = line[i];
        if (c == '(') { inParen = true; i++; continue; }
        if (c == ')') { inParen = false; i++; continue; }
        if (inParen || isspace((unsigned char)c)) { i++; continue; }

        char letter = (char)toupper((unsigned char)c);
        if (letter < 'A' || letter > 'Z') { i++; continue; }
        i++;
        while (i < line.length() && isspace((unsigned char)line[i])) i++;
        if (i >= line.length()) break;

        size_t start = i;
        if (line[i] == '+' || line[i] == '-') i++;
        bool hasDigit = false;
        while (i < line.length() && isdigit((unsigned char)line[i])) { hasDigit = true; i++; }
        if (i < line.length() && line[i] == '.') {
            i++;
            while (i < line.length() && isdigit((unsigned char)line[i])) { hasDigit = true; i++; }
        }
        if (!hasDigit) continue;

        String numText = line.substring(start, i);
        float v = numText.toFloat();

        if (letter == 'G') {
            int g = (int)(v + 0.5f);
            if (g == 90) absoluteMode = true;
            else if (g == 91) absoluteMode = false;
        } else if (letter == 'X') { hasX = true; xVal = v; }
        else if (letter == 'Y') { hasY = true; yVal = v; }
    }
    if (!hasX && !hasY) return true;

    float nextX = curX, nextY = curY;
    if (hasX) nextX = absoluteMode ? xVal : (curX + xVal);
    if (hasY) nextY = absoluteMode ? yVal : (curY + yVal);
    grblUpdateBoundsPoint(bounds, nextX, nextY);
    curX = nextX; curY = nextY;
    return true;
}

bool grblComputeFrameBoundsFromFile(File& in, GrblFrameBounds& boundsOut) {
    bool absoluteMode = true;
    float curX = 0.0f, curY = 0.0f;
    String line; line.reserve(160);
    uint8_t buf[512];
    while (in.available()) {
        const size_t n = in.read(buf, sizeof(buf));
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            char ch = (char)buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                grblParseLineAndAccumulateBounds(line, absoluteMode, curX, curY, boundsOut);
                line = ""; continue;
            }
            line += ch;
            if (line.length() > 2048) {
                grblParseLineAndAccumulateBounds(line, absoluteMode, curX, curY, boundsOut);
                line = "";
            }
        }
        esp_task_wdt_reset();
    }
    if (line.length() > 0) grblParseLineAndAccumulateBounds(line, absoluteMode, curX, curY, boundsOut);
    if (!boundsOut.hasAny) return false;
    if ((boundsOut.maxX - boundsOut.minX) <= 0.0f) return false;
    if ((boundsOut.maxY - boundsOut.minY) <= 0.0f) return false;
    return true;
}

} // anonymous namespace

// ====================================================================
// Handler implementations
// ====================================================================

void handleGrblStatusGet(HttpRequest& req, HttpResponse& res) {
    const uint32_t lastEventId = (uint32_t)getValue(req.query, "last_id").toInt();
    const String sid = getValue(req.query, "sid");
    const String sessionId = (sid.length() > 0) ? sid : "ip:unknown";
    String json = grblWebBuildStatusJson(sessionId, lastEventId);
    if (isTruthyValue(getValue(req.query, "pretty"))) {
        SpiRamJsonDocument doc(6144);
        if (deserializeJson(doc, json) == DeserializationError::Ok) {
            json = "";
            serializeJsonPretty(doc, json);
        }
    }
    res.send(200, "application/json", json);
}

void handleGrblDebugGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    const String dump = grblDebugDump();
    res.send(200, "text/plain; charset=utf-8", dump);
}

void handleGrblUsbRestartPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
#if ENABLE_USB_HOST
    usbHostRequestRestart();
    sendSimpleActionJson(res, 200, true, "restart_scheduled");
#else
    sendSimpleActionJson(res, 404, false, "not found");
#endif
}

// Session-only GRBL USB OTG toggle for the S3 CNC pack.
// mode=usb → lazy host start; mode=uart → stop host. Never persisted —
// reboot defaults to host off. Rejected while a job is active.
void handleGrblTransportPost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_CNC) && ENABLE_USB_HOST == 1
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const String mode = queryOrBody(req, "mode");
    GrblTransportMode target;
    if (mode == "usb") {
        target = GrblTransportMode::USB;
    } else if (mode == "uart" || mode.length() == 0) {
        target = GrblTransportMode::UART;
    } else {
        sendSimpleActionJson(res, 400, false, "invalid_mode");
        return;
    }

    if (grblStreamIsActive() || grblQueueIsActive()) {
        sendSimpleActionJson(res, 409, false, "job_active");
        return;
    }
    if (!grblSetTransportMode(target)) {
        sendSimpleActionJson(res, 409, false, "switch_rejected");
        return;
    }
    // The USB begin runs in a background task: report "starting" while the
    // host is still coming up so the UI waits for usb_running polls instead
    // of assuming the switch completed.
    const bool starting = (target == GrblTransportMode::USB) && !usbHostIsRunning();
    String json = F("{\"success\":true,\"message\":\"ok\",\"grbl_transport\":\"");
    json += (target == GrblTransportMode::USB) ? "usb" : "uart";
    json += F("\",\"starting\":");
    json += starting ? F("true") : F("false");
    json += F("}");
    res.send(200, "application/json", json);
#else
    (void)req;
    sendSimpleActionJson(res, 404, false, "not found");
#endif
}

void handleGrblUsbLineCodingPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_USB_HOST
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);
    uint32_t baud = (uint32_t)queryOrBody(req, "baud").toInt();
    uint8_t dataBits = (uint8_t)queryOrBody(req, "data_bits").toInt();
    uint8_t stopBits = (uint8_t)queryOrBody(req, "stop_bits").toInt();
    uint8_t parity = (uint8_t)queryOrBody(req, "parity").toInt();

    if (baud == 0) baud = 115200U;
    if (dataBits == 0) dataBits = 8U;
    if (stopBits == 0) stopBits = 1U;

    if (!usbHostIsRunning()) {
        sendSimpleActionJson(res, 503, false, "usb_not_running");
        return;
    }
    const UsbCdcDevice dev = usbHostGetDevice(slot);
    if (!dev.connected) {
        sendSimpleActionJson(res, 400, false, "usb_slot_not_connected");
        return;
    }
    if (dev.baud > 0 && dev.baud == baud && dataBits == 8U && stopBits == 1U && parity == 0U) {
        sendSimpleActionJson(res, 200, true, "already_applied");
        return;
    }
    const bool ok = usbHostSetLineCoding(slot, baud, dataBits, stopBits, parity);
    if (!ok) {
        const char* err = usbHostLastError();
        sendSimpleActionJson(res, 400, false, (err && err[0]) ? err : "line_coding_failed");
        return;
    }
    sendSimpleActionJson(res, 200, true, "ok");
#else
    (void)req;
    sendSimpleActionJson(res, 404, false, "not found");
#endif
}

void handleGrblAutoDetectBaudPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);

    if (!usbHostIsRunning()) {
        sendSimpleActionJson(res, 503, false, "usb_not_running");
        return;
    }
    const GrblWebActionResult result = grblWebAutoDetectBaud(slot, sid);
    sendGrblActionResult(res, result);
}

void handleGrblControlTokenGet(HttpRequest& req, HttpResponse& res) {
    const String sid = getValue(req.query, "sid");
    const String sessionId = (sid.length() > 0) ? sid : "ip:unknown";
    const String json = grblWebBuildControlTokenJson(sessionId);
    res.send(200, "application/json", json);
}

void handleGrblEventsGet(HttpRequest& req, HttpResponse& res) {
    const String sid = getValue(req.query, "sid");
    const uint32_t lastEventId = (uint32_t)getValue(req.query, "last_id").toInt();
    const String sessionId = (sid.length() > 0) ? sid : "ip:unknown";
    const String sse = grblWebBuildEventsSse(sessionId, lastEventId);
    res.send(200, "text/event-stream; charset=utf-8", sse);
}

void handleGrblSendPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);
    const String cmd = queryOrBody(req, "cmd");
    sendGrblActionResult(res, grblWebSendCommand(slot, cmd, sid));
}

void handleGrblOverridePost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);
    const String feedStr = queryOrBody(req, "feed");
    const String spinStr = queryOrBody(req, "spin");
    const int feed = feedStr.length() ? feedStr.toInt() : -1;
    const int spin = spinStr.length() ? spinStr.toInt() : -1;
    sendGrblActionResult(res, grblWebSetOverride(slot, feed, spin, sid));
}

void handleGrblStreamPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);
    const String path = queryOrBody(req, "path");
    sendGrblActionResult(res, grblWebStartStream(slot, path, sid));
}

void handleGrblQueueStartPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);
    const String queueText = queryOrBody(req, "queue");
    String mode = queryOrBody(req, "queue_error_mode");
    if (mode.length() == 0) mode = "stop";
    uint8_t retryMax = 0;
    const String retryRaw = queryOrBody(req, "queue_retry_n");
    if (retryRaw.length() > 0) {
        long rv = retryRaw.toInt();
        if (rv < 0) rv = 0;
        if (rv > 10) rv = 10;
        retryMax = (uint8_t)rv;
    }
    sendGrblActionResult(res, grblWebStartQueue(slot, queueText, sid, mode, retryMax));
}

void handleGrblQueueStopPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    sendGrblActionResult(res, grblWebStopQueue(sid));
}

void handleGrblPausePost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    sendGrblActionResult(res, grblWebPauseStream(sid));
}

void handleGrblResumePost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    sendGrblActionResult(res, grblWebResumeStream(sid));
}

// Assisted resume after an ALARM ("restart from line N"): unlock ($X),
// home ($H), modal preamble, restart with line offset. Runs in a background
// task; the UI follows resume_busy/resume_error via /api/grbl/status.
// Requires confirm=1 (operator checked the machine is clear).
void handleGrblResumeAlarmPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_GRBL == 1
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const String confirm = queryOrBody(req, "confirm");
    if (confirm != "1") {
        sendSimpleActionJson(res, 400, false, "confirm_required");
        return;
    }
    switch (grblResumeFromAlarm()) {
        case GrblResumeLaunch::OK: {
            String json = F("{\"success\":true,\"message\":\"ok\",\"started\":true}");
            res.send(200, "application/json", json);
            return;
        }
        case GrblResumeLaunch::BUSY:
            sendSimpleActionJson(res, 409, false, "resume_busy");
            return;
        case GrblResumeLaunch::NOT_IDLE:
            sendSimpleActionJson(res, 409, false, "job_active");
            return;
        case GrblResumeLaunch::NO_RESUME:
        default:
            sendSimpleActionJson(res, 409, false, "no_resume");
            return;
    }
#else
    (void)req;
    sendSimpleActionJson(res, 404, false, "not found");
#endif
}

void handleGrblCancelPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    sendGrblActionResult(res, grblWebCancelStream(sid));
}

void handleGrblPollIntervalPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    uint32_t ms = 0;
    const String msRaw = queryOrBody(req, "ms");
    if (msRaw.length() > 0) ms = (uint32_t)msRaw.toInt();
    sendGrblActionResult(res, grblWebSetPollInterval(ms, sid));
}

void handleGrblListGcodeGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    const String rootDir = grblSdRootDir();
    if (!sdcardShouldUseSD()) {
        sendSimpleActionJson(res, 503, false, "sd_unavailable");
        return;
    }

    String dirPath = grblSdNormalizePath(getValue(req.query, "dir"));
    if (dirPath.length() == 0 || !grblSdIsAllowedDir(dirPath, rootDir)) dirPath = rootDir;

    if (!acquireSDAccess("grbl-list", 8000UL)) {
        sendSimpleActionJson(res, 503, false, "sd_busy");
        return;
    }

    if (!ensureGrblRootExists(rootDir)) {
        releaseSDAccess();
        sendSimpleActionJson(res, 500, false, "root_unavailable");
        return;
    }

    File dir = SD_MMC.open(dirPath.c_str());
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        releaseSDAccess();
        sendSimpleActionJson(res, 404, false, "directory_not_found");
        return;
    }

    String payload = "{\"success\":true,\"message\":\"ok\",\"root\":\"" +
                     jsonEscape(rootDir) + "\",\"dir\":\"" +
                     jsonEscape(dirPath) + "\",\"files\":[";
    bool first = true;
    size_t count = 0;
    File entry = dir.openNextFile();
    while (entry) {
        String fullPath = String(entry.name());
        if (!fullPath.startsWith("/")) fullPath = dirPath + "/" + fullPath;
        fullPath = SDSecurity::normalizePath(fullPath);

        if (entry.isDirectory()) {
            if (grblSdIsAllowedDir(fullPath, rootDir)) {
                if (!first) payload += ",";
                payload += "{\"path\":\"" + jsonEscape(fullPath) +
                           "\",\"name\":\"" + jsonEscape(pathLeafName(fullPath)) +
                           "\",\"is_dir\":true}";
                first = false; count++;
            }
        } else if (grblSdIsAllowedPath(fullPath, rootDir)) {
            if (!first) payload += ",";
            payload += "{\"path\":\"" + jsonEscape(fullPath) +
                       "\",\"name\":\"" + jsonEscape(pathLeafName(fullPath)) +
                       "\",\"size\":" + String((unsigned long)entry.size()) +
                       ",\"mod_time\":" + String((unsigned long)entry.getLastWrite()) +
                       ",\"is_dir\":false}";
            first = false; count++;
        }
        if (count >= 300) { entry.close(); break; }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    releaseSDAccess();
    payload += "]}";
    res.send(200, "application/json", payload);
#else
    (void)req;
    sendSimpleActionJson(res, 503, false, "sd_disabled");
#endif
}

void handleGrblReadGcodeTextGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    const String rootDir = grblSdRootDir();
    if (!sdcardShouldUseSD()) {
        res.send(503, "text/plain; charset=utf-8", "sd_unavailable");
        return;
    }
    String path = grblSdNormalizePath(getValue(req.query, "path"));
    if (!grblSdIsAllowedPath(path, rootDir)) {
        res.send(400, "text/plain; charset=utf-8", "invalid_path");
        return;
    }
    if (!acquireSDAccess("grbl-read-text", 8000UL)) {
        res.send(503, "text/plain; charset=utf-8", "sd_busy");
        return;
    }

    File file = SD_MMC.open(path.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
        if (file) file.close();
        releaseSDAccess();
        res.send(404, "text/plain; charset=utf-8", "file_not_found");
        return;
    }

    const size_t maxRead = 50UL * 1024UL * 1024UL;
    const size_t bytes = (size_t)file.size();
    if (bytes > maxRead) {
        file.close();
        releaseSDAccess();
        res.send(413, "text/plain; charset=utf-8", "file_too_large");
        return;
    }

    String content;
    if (!content.reserve(bytes + 1)) {
        file.close();
        releaseSDAccess();
        res.send(413, "text/plain; charset=utf-8", "file_too_large");
        return;
    }

    uint8_t readBuf[512];
    while (file.available()) {
        size_t n = file.read(readBuf, sizeof(readBuf));
        if (n == 0) break;
        content.concat((const char*)readBuf, n);
        esp_task_wdt_reset();
    }
    file.close();
    releaseSDAccess();

    res.code = 200;
    res.contentType = "text/plain; charset=utf-8";
    res.body = content;
#else
    (void)req;
    res.send(503, "text/plain; charset=utf-8", "sd_disabled");
#endif
}

void handleGrblFrameBboxGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    const String rootDir = grblSdRootDir();
    if (!sdcardShouldUseSD()) {
        sendSimpleActionJson(res, 503, false, "sd_unavailable");
        return;
    }
    String path = grblSdNormalizePath(getValue(req.query, "path"));
    if (!grblSdIsAllowedPath(path, rootDir)) {
        sendSimpleActionJson(res, 400, false, "invalid_path");
        return;
    }
    if (!acquireSDAccess("grbl-frame-bbox", 8000UL)) {
        sendSimpleActionJson(res, 503, false, "sd_busy");
        return;
    }
    File in = SD_MMC.open(path.c_str(), FILE_READ);
    if (!in || in.isDirectory()) {
        if (in) in.close();
        releaseSDAccess();
        sendSimpleActionJson(res, 404, false, "file_not_found");
        return;
    }
    GrblFrameBounds bounds;
    const bool ok = grblComputeFrameBoundsFromFile(in, bounds);
    in.close();
    releaseSDAccess();
    if (!ok) {
        sendSimpleActionJson(res, 422, false, "bbox_unavailable");
        return;
    }
    String payload = "{\"success\":true,\"message\":\"ok\",\"min_x\":" +
                     String(bounds.minX, 4) +
                     ",\"max_x\":" + String(bounds.maxX, 4) +
                     ",\"min_y\":" + String(bounds.minY, 4) +
                     ",\"max_y\":" + String(bounds.maxY, 4) +
                     "}";
    res.send(200, "application/json", payload);
#else
    (void)req;
    sendSimpleActionJson(res, 503, false, "sd_disabled");
#endif
}

void handleGrblCreateDirPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

#if ENABLE_SDCARD==1
    const String rootDir = grblSdRootDir();
    if (!sdcardShouldUseSD()) {
        sendSimpleActionJson(res, 503, false, "sd_unavailable");
        return;
    }
    String dirPath = grblSdNormalizePath(queryOrBody(req, "path"));
    if (!grblSdIsAllowedDir(dirPath, rootDir) || dirPath == rootDir) {
        sendSimpleActionJson(res, 400, false, "invalid_directory");
        return;
    }
    if (!acquireSDAccess("grbl-mkdir", 8000UL)) {
        sendSimpleActionJson(res, 503, false, "sd_busy");
        return;
    }
    if (!ensureGrblRootExists(rootDir)) {
        releaseSDAccess();
        sendSimpleActionJson(res, 500, false, "root_unavailable");
        return;
    }
    if (SD_MMC.exists(dirPath.c_str())) {
        File existing = SD_MMC.open(dirPath.c_str());
        const bool isDir = existing && existing.isDirectory();
        if (existing) existing.close();
        releaseSDAccess();
        if (isDir) { sendSimpleActionJson(res, 200, true, "ok"); }
        else { sendSimpleActionJson(res, 409, false, "path_conflict"); }
        return;
    }
    const bool ok = SD_MMC.mkdir(dirPath.c_str());
    releaseSDAccess();
    if (!ok) { sendSimpleActionJson(res, 500, false, "mkdir_failed"); return; }
    sendSimpleActionJson(res, 200, true, "ok");
#else
    sendSimpleActionJson(res, 503, false, "sd_disabled");
#endif
}

void handleGrblDeletePathPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

#if ENABLE_SDCARD==1
    const String rootDir = grblSdRootDir();
    if (!sdcardShouldUseSD()) {
        sendSimpleActionJson(res, 503, false, "sd_unavailable");
        return;
    }
    String path = grblSdNormalizePath(queryOrBody(req, "path"));
    if (path == rootDir || (!grblSdIsAllowedPath(path, rootDir) && !grblSdIsAllowedDir(path, rootDir))) {
        sendSimpleActionJson(res, 400, false, "invalid_path");
        return;
    }
    if (!acquireSDAccess("grbl-delete", 8000UL)) {
        sendSimpleActionJson(res, 503, false, "sd_busy");
        return;
    }
    if (!SD_MMC.exists(path.c_str())) {
        releaseSDAccess();
        sendSimpleActionJson(res, 404, false, "not_found");
        return;
    }
    File node = SD_MMC.open(path.c_str());
    const bool isDir = node && node.isDirectory();
    if (node) node.close();

    bool ok = false;
    if (isDir) ok = removeSdTree(path, rootDir);
    else ok = SD_MMC.remove(path.c_str());
    releaseSDAccess();
    if (!ok) { sendSimpleActionJson(res, 500, false, "delete_failed"); return; }
    sendSimpleActionJson(res, 200, true, "ok");
#else
    sendSimpleActionJson(res, 503, false, "sd_disabled");
#endif
}

void handleGrblPsramFileGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.send(200, "application/json", grblWebBuildPsramFileInfoJson());
}

void handleGrblPsramReadTextGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    const char* data = grblPsramGcodeGetData();
    const size_t size = grblPsramGcodeGetSize();
    if (!data || size == 0) {
        res.send(404, "text/plain; charset=utf-8", "no_file");
        return;
    }
    res.code = 200;
    res.contentType = F("text/plain; charset=utf-8");
    res.isBinary = true;
    res.binaryData = (const uint8_t*)data;
    res.binaryLen = size;
}

void handleGrblPsramUploadPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const size_t total = req.body.length();
    if (total == 0) {
        sendSimpleActionJson(res, 400, false, "empty_content");
        return;
    }
    if (total > (size_t)GRBL_STREAM_PSRAM_MAX_BYTES) {
        sendSimpleActionJson(res, 413, false, "file_too_large");
        return;
    }
    if (psramVFSInitialized && total > psramVFSGetFreeBytes()) {
        sendSimpleActionJson(res, 413, false, "vfs_budget_exceeded");
        return;
    }

    String fileName = queryOrBody(req, "name");
    if (fileName.length() == 0) fileName = "upload.nc";

    uint8_t* buf = (uint8_t*)malloc(total + 1);
    if (!buf) {
        sendSimpleActionJson(res, 500, false, "alloc_failed");
        return;
    }
    memcpy(buf, req.body.c_str(), total);
    buf[total] = '\0';

    const bool stored = grblPsramGcodeStore(fileName.c_str(), buf, total);
    free(buf);

    if (!stored) {
        sendSimpleActionJson(res, 500, false, "psram_store_failed");
        return;
    }
    sendSimpleActionJson(res, 200, true, "saved", String("/psram/") + fileName, total);
}

void handleGrblPsramStreamPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);
    sendGrblActionResult(res, grblWebStartPsramStream(slot, sid));
}

void handleGrblPsramClearPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    (void)sid;
    (void)token;
    grblPsramGcodeClear();
    sendSimpleActionJson(res, 200, true, "ok");
}

void handleGrblUploadGcodeStreamPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

#if ENABLE_SDCARD==1
    const String rootDir = grblSdRootDir();
    if (!sdcardShouldUseSD()) {
        sendSimpleActionJson(res, 503, false, "sd_unavailable");
        return;
    }

    const size_t total = req.body.length();
    if (total == 0) {
        sendSimpleActionJson(res, 400, false, "empty_content");
        return;
    }
    if (total > 50UL * 1024UL * 1024UL) {
        sendSimpleActionJson(res, 413, false, "file_too_large");
        return;
    }

    String path = grblSdNormalizePath(queryOrBody(req, "path"));
    String fileName = queryOrBody(req, "name");
    fileName.trim();
    if (path.length() == 0 && fileName.length() > 0) {
        path = grblSdNormalizePath(rootDir + "/" + fileName);
    }
    if (!grblSdIsAllowedPath(path, rootDir)) {
        sendSimpleActionJson(res, 400, false, "invalid_path");
        return;
    }

    const bool overwrite = isTruthyValue(queryOrBody(req, "overwrite"));
    if (!acquireSDAccess("grbl-upload-stream", 8000UL)) {
        sendSimpleActionJson(res, 503, false, "sd_busy");
        return;
    }

    if (!ensureGrblRootExists(rootDir)) {
        releaseSDAccess();
        sendSimpleActionJson(res, 500, false, "root_unavailable");
        return;
    }
    if (!grblAutoCleanupIfNeeded(rootDir)) {
        releaseSDAccess();
        sendSimpleActionJson(res, 500, false, "cleanup_failed");
        return;
    }
    if (SD_MMC.exists(path.c_str()) && !overwrite) {
        releaseSDAccess();
        sendSimpleActionJson(res, 409, false, "file_exists");
        return;
    }
    if (SD_MMC.exists(path.c_str()) && overwrite) {
        SD_MMC.remove(path.c_str());
    }

    File out = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (!out) {
        releaseSDAccess();
        sendSimpleActionJson(res, 500, false, "open_failed");
        return;
    }

    size_t written = out.write((const uint8_t*)req.body.c_str(), total);
    out.close();
    storNoteSdWrite(written);
    releaseSDAccess();

    if (written != total) {
        SD_MMC.remove(path.c_str());
        sendSimpleActionJson(res, 500, false, "write_failed");
        return;
    }
    sendSimpleActionJson(res, 200, true, "saved", path, written);
#else
    sendSimpleActionJson(res, 503, false, "sd_disabled");
#endif
}

// ── Probe Grid / Height Map handlers ────────────────────────────────

void handleGrblProbeGridGet(HttpRequest& req, HttpResponse& res) {
    const ProbeGridConfig* cfg = grblGetProbeGridConfig();
    if (!cfg) {
        res.send(200, "application/json", "{\"success\":true,\"config\":null,\"phase\":\"idle\"}");
        return;
    }
    String json = "{\"success\":true,\"phase\":\"";
    switch (grblGetProbeGridPhase()) {
        case ProbeGridPhase::IDLE:       json += "idle"; break;
        case ProbeGridPhase::MOVING_XY:  json += "moving_xy"; break;
        case ProbeGridPhase::PROBING:    json += "probing"; break;
        case ProbeGridPhase::RETRACTING: json += "retracting"; break;
        case ProbeGridPhase::COMPLETE:   json += "complete"; break;
        case ProbeGridPhase::CANCELLED:  json += "cancelled"; break;
        case ProbeGridPhase::ERROR:      json += "error"; break;
    }
    json += "\",\"total\":" + String(grblGetProbeGridTotal());
    json += ",\"completed\":" + String(grblGetProbeGridCompleted());
    json += ",\"config\":{";
    json += "\"startX\":" + String(cfg->startX, 1);
    json += ",\"startY\":" + String(cfg->startY, 1);
    json += ",\"endX\":" + String(cfg->endX, 1);
    json += ",\"endY\":" + String(cfg->endY, 1);
    json += ",\"spacing\":" + String(cfg->spacing, 1);
    json += ",\"probeFeed\":" + String(cfg->probeFeed, 1);
    json += ",\"probeMaxDepth\":" + String(cfg->probeMaxDepth, 1);
    json += ",\"safeZ\":" + String(cfg->safeZ, 1);
    json += ",\"clearHeight\":" + String(cfg->clearHeight, 1);
    json += "},\"heightMapEnabled\":" + String(grblGetHeightMapEnabled() ? "true" : "false");
    json += "}";
    res.send(200, "application/json", json);
}

void handleGrblProbeGridPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    const uint8_t slot = readUsbSlot(req);
    ProbeGridConfig cfg;
    cfg.startX = queryOrBody(req, "startX").toFloat();
    cfg.startY = queryOrBody(req, "startY").toFloat();
    cfg.endX   = queryOrBody(req, "endX").toFloat();
    cfg.endY   = queryOrBody(req, "endY").toFloat();
    cfg.spacing = queryOrBody(req, "spacing").toFloat();
    cfg.probeFeed = queryOrBody(req, "probeFeed").toFloat();
    cfg.probeMaxDepth = queryOrBody(req, "probeMaxDepth").toFloat();
    cfg.safeZ   = queryOrBody(req, "safeZ").toFloat();
    cfg.clearHeight = queryOrBody(req, "clearHeight").toFloat();

    if (cfg.spacing <= 0 || cfg.probeFeed <= 0) {
        sendSimpleActionJson(res, 400, false, "Invalid grid parameters");
        return;
    }

    bool started = grblProbeGridStart(slot, cfg);
    sendSimpleActionJson(res, started ? 200 : 409, started,
                         started ? "Probe grid started" : "Grid busy or no points");
}

void handleGrblProbeGridCancelPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    grblProbeGridCancel();
    sendSimpleActionJson(res, 200, true, "Probe grid cancelled");
}

void handleGrblHeightMapGet(HttpRequest& req, HttpResponse& res) {
    uint16_t count = 0;
    const ProbeGridPoint* pts = grblGetProbeGridPoints(&count);
    String json = "{\"success\":true,\"enabled\":" + String(grblGetHeightMapEnabled() ? "true" : "false");
    json += ",\"count\":" + String(count);
    json += ",\"points\":[";
    for (uint16_t i = 0; i < count; ++i) {
        if (i > 0) json += ",";
        json += "{\"x\":" + String(pts[i].x, 3);
        json += ",\"y\":" + String(pts[i].y, 3);
        json += ",\"z\":" + String(pts[i].z, 3);
        json += ",\"valid\":" + String(pts[i].valid ? "true" : "false");
        json += "}";
    }
    json += "]}";
    res.send(200, "application/json", json);
}

void handleGrblHeightMapEnablePost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    bool enable = queryOrBody(req, "enabled") == "1" || queryOrBody(req, "enabled") == "true";
    grblSetHeightMapEnabled(enable);
    sendSimpleActionJson(res, 200, true, enable ? "Height map enabled" : "Height map disabled");
}

void handleGrblHeightMapClearPost(HttpRequest& req, HttpResponse& res) {
    const String sid = queryOrBody(req, "sid");
    const String token = queryOrBody(req, "token");
    GrblWebActionResult auth = grblWebAuthorizeWrite(sid, token);
    if (!auth.success) { sendGrblActionResult(res, auth); return; }

    grblClearHeightMap();
    sendSimpleActionJson(res, 200, true, "Height map cleared");
}

#endif // ENABLE_GRBL
