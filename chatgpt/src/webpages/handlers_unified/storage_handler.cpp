#include "storage_handler.h"
#include <Arduino.h>
#include "../../feature_flags.h"
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
#include "../../touch7_display.h" // Touch7FlashGuard (SD actions may hit NVS/flash)
#endif
#include "../../web_shared_logic.h"
#include "../../sd_diagnostics.h"
#include "../../sdcard.h"
#include "../../ConfigManager.h"
#include "../../structures.h"

extern volatile StorageMode currentStorageMode;
extern bool retrySDStorage();
extern const char* getStorageModeString();

namespace {

String urlDecode(const String& input) {
    String out;
    out.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < input.length()) {
            char h1 = input.charAt(i + 1);
            char h2 = input.charAt(i + 2);
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

String getFormValue(const String& body, const char* key) {
    String prefix = String(key) + '=';
    int start = 0;
    while (start <= (int)body.length()) {
        int amp = body.indexOf('&', start);
        if (amp < 0) amp = body.length();
        String pair = body.substring(start, amp);
        if (pair.startsWith(prefix)) {
            return urlDecode(pair.substring(prefix.length()));
        }
        start = amp + 1;
    }
    return String();
}

bool formHasTruthyValue(const String& body, const char* key) {
    String value = getFormValue(body, key);
    value.toLowerCase();
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

bool hasFormKey(const String& body, const char* key) {
    String prefix = String(key) + '=';
    int start = 0;
    while (start <= (int)body.length()) {
        int amp = body.indexOf('&', start);
        if (amp < 0) amp = body.length();
        String pair = body.substring(start, amp);
        if (pair.startsWith(prefix)) return true;
        start = amp + 1;
    }
    return false;
}

} // anonymous namespace

void handleStorageRetrySdPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    (void)req;
    bool success = retrySDStorage();
    String json = F("{\"success\":");
    json += success ? "true" : "false";
    json += F(",\"storage_mode\":\"");
    json += getStorageModeString();
    json += F("\",\"message\":\"");
    json += success ? "SD card is now active" : "SD card still unavailable";
    json += "\"}";
    res.send(success ? 200 : 503, "application/json", json);
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleSdBuffersGet(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    (void)req;
    res.send(200, "application/json", getSdBuffersDiagnosticsJson());
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleSdStatusGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.send(200, "application/json", sharedWebBuildSdStatusJson());
}

void handleSdActionPost(HttpRequest& req, HttpResponse& res) {
#if defined(SELECTED_PACK) && (SELECTED_PACK == PACK_S3_TOUCH7)
    // Manual (non-registry) POST route: take the cross-task flash lock too.
    Touch7FlashGuard _t7g;
#endif
#if ENABLE_SDCARD==1
    String action = getFormValue(req.body, "action");
    if (action.length() == 0) {
        int eq = req.query.indexOf('=');
        if (eq >= 0) action = req.query.substring(eq + 1);
        int amp = action.indexOf('&');
        if (amp >= 0) action = action.substring(0, amp);
    }
    bool ok;
    String json = sharedWebApplySdAction(action, ok);
    res.send(ok ? 200 : 400, "application/json", json);
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleSdResetWriteCounterPost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    (void)req;
    bool ok = sdcardResetWriteCounter();
    String json = F("{\"status\":\"");
    json += ok ? "ok" : "error";
    json += F("\",\"message\":\"");
    json += ok ? "Counter reset" : "Failed to reset counter";
    json += "\"}";
    res.send(200, "application/json", json);
#else
    (void)req;
    res.send(501, "application/json", "{\"status\":\"disabled\"}");
#endif
}

void handleSdcardConfigGet(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.send(200, "application/json", sharedWebBuildSdcardConfigJson());
}

void handleSdcardConfigSavePost(HttpRequest& req, HttpResponse& res) {
#if ENABLE_SDCARD==1
    // A partial POST must only touch the fields actually sent, preserving the
    // others from current config instead of resetting them to hardcoded defaults.
    // This matches the client behaviour (auto.js re-sends the full set from GET)
    // so raw API callers cannot wipe omitted settings (structured, email_from_sd,
    // max_size_mb, emailday_extra_emails, auto_cleanup, ...).
    SharedSdcardConfigInput input;
    input.enabled = configManager.getSdcardEnabled();
    input.structured = configManager.getSdcardStructured();
    input.emailSend = configManager.getSdcardEmailSend();
    input.emailFromSd = configManager.getSdcardEmailFromSd();
    input.flushInterval = configManager.getSdcardFlushInterval();
    input.format = configManager.getSdcardFormat();
    input.maxSizeMb = configManager.getSdcardMaxSize();
    input.extraEmails = configManager.getEmaildayExtraEmails();
    input.autoCleanup = true; // Autoclean is always active (no UI toggle).
    input.cleanupThreshold = configManager.getSdCleanupThreshold();
    input.keepDays = configManager.getSdKeepDays();

    // Override only the fields present in the POST body.
    String v;
    if (hasFormKey(req.body, "enabled")) input.enabled = formHasTruthyValue(req.body, "enabled");
    if (hasFormKey(req.body, "structured")) input.structured = formHasTruthyValue(req.body, "structured");
    if (hasFormKey(req.body, "email_send")) input.emailSend = formHasTruthyValue(req.body, "email_send");
    if (hasFormKey(req.body, "email_from_sd")) input.emailFromSd = formHasTruthyValue(req.body, "email_from_sd");
    v = getFormValue(req.body, "flush_interval");
    if (v.length() > 0) input.flushInterval = v;
    v = getFormValue(req.body, "format");
    if (v.length() > 0) input.format = v;
    v = getFormValue(req.body, "max_size_mb");
    if (v.length() > 0) input.maxSizeMb = v.toInt();
    v = getFormValue(req.body, "emailday_extra_emails");
    if (hasFormKey(req.body, "emailday_extra_emails")) input.extraEmails = v;
    // NOTE: "auto_cleanup" from POST is intentionally ignored — autoclean is always active.
    v = getFormValue(req.body, "cleanup_threshold");
    if (v.length() > 0) input.cleanupThreshold = v.toInt();
    v = getFormValue(req.body, "keep_days");
    if (v.length() > 0) input.keepDays = v.toInt();
    if (input.keepDays < 0) input.keepDays = 0; // 0 = infinity (keep until the % threshold)

    String responseBody;
    bool ok = sharedWebApplySdcardConfig(input, responseBody);
    if (ok) ok = configManager.saveSection("sdcard");
    res.send(ok ? 200 : 400, "application/json", responseBody);
#else
    (void)req;
    SharedSdcardConfigInput input;
    input.enabled = configManager.getSdcardEnabled();
    input.flushInterval = configManager.getSdcardFlushInterval();
    input.format = configManager.getSdcardFormat();
    input.emailSend = configManager.getSdcardEmailSend();
    String v;
    if (hasFormKey(req.body, "enabled")) input.enabled = formHasTruthyValue(req.body, "enabled");
    v = getFormValue(req.body, "flush_interval");
    if (v.length() > 0) input.flushInterval = v;
    v = getFormValue(req.body, "format");
    if (v.length() > 0) input.format = v;
    if (hasFormKey(req.body, "email_send")) input.emailSend = formHasTruthyValue(req.body, "email_send");
    String responseBody;
    bool ok = sharedWebApplySdcardConfig(input, responseBody);
    if (ok) ok = configManager.saveSection("sdcard");
    res.send(ok ? 200 : 400, "application/json", responseBody);
#endif
}
