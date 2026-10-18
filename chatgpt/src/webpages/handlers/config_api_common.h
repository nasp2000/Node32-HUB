#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../../arduinojson_fix.h"
#include "../../commands.h"         // LogLevel enum (INFO, WARN, etc.) and sendVirtualMessage
#include "../../coordinator_hooks.h"
#include "../../direct_log.h"
#include "../../feature_flags.h"
#include "../../observability.h"
#include "../../psram_helpers.h"
#include "../../telnet_server.h"
#include "../../web_shared_logic.h"

extern volatile bool wifiConnectJobPending;
extern int wifiConnectJobPhase;
extern unsigned long wifiConnectJobStart;
extern String wifiJobSsid;
extern String wifiJobPass;
extern void syncGlobalVariables();
extern void syncGlobalVariables(bool includeRS232);
extern void setupLanguage();

inline void notifyScreenConfigSaved() {
    triggerScreenConfigSaved();
#if ENABLE_BUZZER == 1
    buzzerOnSave();
#endif
}

inline void appendConfigUpdate(String& message, bool& updated, const char* token) {
    message += token;
    updated = true;
}

inline void applyTelnetEnabledValue(bool newValue,
                                    const char* token,
                                    String& message,
                                    bool& updated,
                                    const __FlashStringHelper* onMessage,
                                    const __FlashStringHelper* pendingMessage,
                                    const __FlashStringHelper* offMessage) {
    bool current = configManager.getTelnetEnabled();
    if (newValue == current) return;

    configManager.setTelnetEnabled(newValue);
    appendConfigUpdate(message, updated, token);

    if (newValue) {
        if (startTelnetServer()) sendVirtualMessage(onMessage, INFO);
        else sendVirtualMessage(pendingMessage, WARN);
        return;
    }

    stopTelnetServer();
    sendVirtualMessage(offMessage, INFO);
}