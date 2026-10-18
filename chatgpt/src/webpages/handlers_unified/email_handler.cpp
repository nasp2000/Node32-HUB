#include "email_handler.h"
#include "../../feature_flags.h"
#include "../../languages/lang_active.h"

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1

#include "../../runtime_email.h"
#include "../../runtime_environment.h"
#include "../../email_message_builder.h"
#include "../../email_transport.h"
#include "../../languages/lang_active.h"
#include "../../web_shared_logic.h"

struct EmailTestApiResponse {
    int statusCode;
    String payload;
};

static EmailTestApiResponse buildEmailTestApiResponse() {
    if (isReceivingData) {
        return {409, String(F("{\"success\":false,\"error\":\"receiving_data\",\"message\":\"")) + L(activeLang->apiEmailTestBusy) + "\"}"};
    }

    static unsigned long lastEmailTestAttempt = 0;
    const unsigned long emailTestThrottleMs = 10000;
    const unsigned long now = millis();
    if (lastEmailTestAttempt > 0 && (now - lastEmailTestAttempt) < emailTestThrottleMs) {
        const unsigned long remainingSec = (emailTestThrottleMs - (now - lastEmailTestAttempt)) / 1000;
        return {429, String(F("{\"success\":false,\"error\":\"throttle\",\"message\":\"⏱️ Aguarde ")) + String(remainingSec) + String(F(" segundos antes de tentar novamente.\"}"))};
    }
    lastEmailTestAttempt = now;

    if (!isEmailConfiguredAndWifiConnected()) {
        const String message = L(activeLang->apiEmailTestNoConfig) + emailTransportUnavailableReason();
        return {400, String(F("{\"success\":false,\"error\":\"config_incomplete\",\"message\":\"")) + message + String(F("\"}"))};
    }

#if ENABLE_SMTP==1 || ENABLE_GOOGLE_EMAIL==1
    const bool started = startEmailTestAsync(
        String(F("✉️ - ")) + buildEmailTitlePrefix() + String(F(" - ")) + L(activeLang->emailTestSubject),
        buildEmailInfoTable(L(activeLang->emailTestBody), "", "", "", L(activeLang->emailTestFreq), "", "", "")
    );
    if (started) {
        return {200, String(F("{\"success\":true,\"message\":\"")) + L(activeLang->apiEmailTestStarted) + "\"}"};
    }
    return {500, String(F("{\"success\":false,\"error\":\"start_failed\",\"message\":\"")) + L(activeLang->apiEmailTestStartFail) + "\"}"};
#else
    return {503, String(F("{\"success\":false,\"error\":\"email_transport_disabled\",\"message\":\"❌ Transporte de email desativado na firmware (ENABLE_SMTP=0 e ENABLE_GOOGLE_EMAIL=0)\"}"))};
#endif
}

void handleEmailTestPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    const EmailTestApiResponse response = buildEmailTestApiResponse();
    res.code = response.statusCode;
    res.contentType = F("application/json");
    res.body = response.payload;
}

#if ENABLE_SMTP==1
// Manual reset for a stuck EMAILDAY send: releases a wedged flag/handle so the
// system sends again immediately. Safe to call anytime (no-op when idle).
// The loop watchdog (emailDayWatchdogTick) does the same automatically past 10 min.
void handleEmailResetPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    bool wasStuck = emailDayInProgress;
    emailDayForceCleanup("manual reset via /api/email-reset");
    emailDayStartMs = 0;
    res.code = 200;
    res.contentType = F("application/json");
    res.body = wasStuck
        ? F("{\"success\":true,\"message\":\"Stuck email released — new sends unblocked\"}")
        : F("{\"success\":true,\"message\":\"No stuck email — nothing to release\"}");
}
#endif

#else
void handleEmailTestPost(HttpRequest& req, HttpResponse& res) {
    (void)req;
    res.code = 503;
    res.contentType = F("application/json");
    res.body = String(F("{\"success\":false,\"error\":\"email_transport_disabled\",\"message\":\"")) + L(activeLang->apiEmailDisabled) + F("\"}");
}
#endif
