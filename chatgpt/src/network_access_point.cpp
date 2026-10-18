#include "network_access_point.h"

#include <Arduino.h>
#include <WiFi.h>
// Direct driver calls (stop/deinit/init/set_ps/set_bandwidth) are not available
// on ESP32-P4 where WiFi is delegated to the C6 companion chip via SDIO.
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
#include <esp_wifi.h>
#endif

#include "config_manager_runtime.h"
#include "coordinator_hooks.h"
#include "commands.h"
#include "feature_flags.h"
#include "network_mdns.h"
#include "network_runtime_state.h"

extern unsigned long AP_ACTIVATION_DELAY;
extern unsigned long AP_RETRY_DELAY;
extern int MAX_AP_FAILURES;
extern unsigned long AP_FAILURE_PAUSE;
extern unsigned long hybridBootTimeout;
extern unsigned long staStableTime;
extern unsigned long staRecoveryTimeout;

static void applyApLatencyTuning() {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4==1
    // On P4 the WiFi runs on the C6 companion. Direct esp_wifi_* APIs are not
    // available, but disabling sleep through the Arduino WiFi facade still works.
    // This removes AP-side HTTP latency spikes caused by modem power-save.
    WiFi.setSleep(false);
#else
    // Disable WiFi modem sleep in AP mode — default modem sleep adds
    // noticeable request latency for browser clients connected to the AP.
    esp_wifi_set_ps(WIFI_PS_NONE);

    // Keep AP in HT20 for better 2.4 GHz stability and lower web latency.
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
#endif
}

static void resetWiFiStack() {
#if ENABLE_WIFI!=1
    return;
#else
    sendVirtualMessage(F("Full WiFi stack reset..."), WARN);

    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    stopMdns();
    WiFi.mode(WIFI_OFF);
    delay(1000);

#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(500);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nano_enable = 0;

    esp_err_t err = esp_wifi_init(&cfg);
    if (err == ESP_OK) {
        esp_wifi_start();
        sendVirtualMessage(F("WiFi stack reinitialized"), INFO);
    } else {
        safeEnviarMensagemVirtual("Error reinitializing WiFi stack: %d", ERROR, (int)err);
    }
#else
    // On P4 (WiFi via C6 SDIO) skip native driver reinit — not applicable.
    sendVirtualMessage(F("WiFi stack reset skipped on P4 (C6 SDIO)"), INFO);
#endif

    delay(1000);
#endif
}

void loadAccessPointConfig() {
    AP_ACTIVATION_DELAY = configManager.getAccessPointActivationDelay();
    AP_RETRY_DELAY = configManager.getAccessPointRetryDelay();
    MAX_AP_FAILURES = configManager.getAccessPointMaxFailures();
    AP_FAILURE_PAUSE = configManager.getAccessPointFailurePause();

    hybridBootTimeout = configManager.getWifiHybridBootTimeout();
    if (hybridBootTimeout == 0) hybridBootTimeout = 60000;
    staStableTime = configManager.getWifiHybridStaStableTime();
    staRecoveryTimeout = configManager.getWifiHybridStaRecoveryTimeout();
}

void startAccessPoint() {
#if ENABLE_WIFI!=1
    apActive = false;
    return;
#else
    WiFi.enableProv(false);

    if (apLockedUntilReboot) {
        sendVirtualMessage(String(F("AP locked until reboot (STA stable)")), INFO);
        return;
    }

    prepareRs232ForAccessPointStart();

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // P4: avoid WIFI_OFF which tears down the SDIO link to the C6 WiFi companion.
    // Going through OFF can leave the C6 in a degraded state (partial re-init).
    // Instead, disconnect STA if active and set pure WIFI_AP.
    WiFi.disconnect(false);
    delay(50);
    WiFi.mode(WIFI_AP);
    delay(500);
#else
    WiFi.disconnect(true);
    delay(200);
    stopMdns();
    WiFi.mode(WIFI_OFF);
    delay(1000);

    WiFi.mode(WIFI_AP);
    delay(500);
#endif

    if (WiFi.getMode() != WIFI_AP) {
        sendVirtualMessage(String(F("Failed to configure WiFi AP mode")), ERROR);
        apFailureCount++;
        return;
    }

    delay(100);

    String apSSID = configManager.getAccessPointSSID();
    String apPassword = configManager.getAccessPointPassword();
    safeEnviarMensagemVirtual("DBG: SSID bruto (ConfigManager): '%s'", DEBUG, apSSID.c_str());
    safeEnviarMensagemVirtual("DBG: Password (len): %u", DEBUG, (unsigned)apPassword.length());

    if (apSSID.isEmpty() || apSSID == "") {
        apSSID = "NODE32-HUB";
        safeEnviarMensagemVirtual("FAILSAFE: SSID empty; using: %s", WARN, apSSID.c_str());
    }

    if (apPassword.isEmpty() || apPassword == "") {
        apPassword = "root";
        safeEnviarMensagemVirtual("FAILSAFE: password empty; using: %s", WARN, apPassword.c_str());
    }

    bool apResult = false;

    sendVirtualMessage(F("Attempt 1: AP channel 6 (basic)..."), INFO);
    apResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str());

    if (!apResult) {
        delay(1000);
        sendVirtualMessage(F("Attempt 2: channel 6 (explicit params)..."), INFO);
        apResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str(), 6, 0, 4);
    }

    if (!apResult) {
        delay(1000);
        sendVirtualMessage(F("Attempt 3: channel 11 (alternative)..."), INFO);
        apResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str(), 11, 0, 2);
    }

    if (!apResult) {
        delay(1000);
        sendVirtualMessage(F("Attempt 4: channel 1 (backup)..."), INFO);
        apResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str(), 1, 0, 1);
    }

    if (!apResult) {
        delay(1000);
        sendVirtualMessage(F("Attempt 5: no password (emergency)..."), WARN);
        apResult = WiFi.softAP(apSSID.c_str(), "", 6, 0, 4);
        if (apResult) {
            sendVirtualMessage(F("AP created WITH NO PASSWORD (emergency)"), WARN);
        }
    }

    if (!apResult && apFailureCount > 1) {
        sendVirtualMessage(F("Final attempt with WiFi stack reset..."), WARN);
        resetWiFiStack();
        delay(2000);
        WiFi.mode(WIFI_AP);
        delay(500);
        apResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str());
    }

    delay(1000);
    String realApSSID = WiFi.softAPSSID();
    safeEnviarMensagemVirtual("DBG: real SSID after creation: '%s'", DEBUG, realApSSID.c_str());
    if (apResult && realApSSID.length() > 0 && realApSSID != apSSID) {
        safeEnviarMensagemVirtual("SSID divergence (expected: '%s', real: '%s') -> attempting correction", WARN, apSSID.c_str(), realApSSID.c_str());
        WiFi.softAPdisconnect(true);
        delay(500);
        WiFi.mode(WIFI_AP);
        delay(300);
        bool retryResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str(), 6, 0, 4);
        String correctedSSID = WiFi.softAPSSID();
        safeEnviarMensagemVirtual("DBG: SSID correction ok=%s, now: '%s'", DEBUG, (retryResult ? "1" : "0"), correctedSSID.c_str());
        realApSSID = correctedSSID;
    }

    IPAddress apIP = WiFi.softAPIP();
    bool apIsRunning = (apIP != IPAddress(0,0,0,0));
    bool modeIsCorrect = (WiFi.getMode() == WIFI_AP);

    if (apResult && apIsRunning && modeIsCorrect) {
        apActive = true;
        apFailureCount = 0;
        WiFi.softAPsetHostname(apSSID.c_str());
        WiFi.setHostname(apSSID.c_str());
        applyApLatencyTuning();

        sendVirtualMessage(F("AP active"), INFO);
        safeEnviarMensagemVirtual("SSID (pretendido): %s", INFO, apSSID.c_str());
        safeEnviarMensagemVirtual("SSID (real): %s", INFO, realApSSID.c_str());
        safeEnviarMensagemVirtual("Password: %s", INFO, apPassword.c_str());
        safeEnviarMensagemVirtual("AP IP: %s", INFO, apIP.toString().c_str());
        String ipString = apIP.toString();
        safeEnviarMensagemVirtual("Access at: http://%s", INFO, ipString.c_str());
    } else {
        apFailureCount++;

        safeEnviarMensagemVirtual("Error starting AP (%u/%u)", ERROR, (unsigned)apFailureCount, (unsigned)configManager.getAccessPointMaxFailures());

        sendVirtualMessage(F("Diag WiFi:"), INFO);
        safeEnviarMensagemVirtual("   - SSID tentado: %s", INFO, apSSID.c_str());
        safeEnviarMensagemVirtual("   - Password defined: %s", INFO, (apPassword.length() > 0 ? "Yes" : "No"));
        safeEnviarMensagemVirtual("   - Modo WiFi: %d", INFO, WiFi.getMode());
        safeEnviarMensagemVirtual("   - Heap livre: %u bytes", INFO, (unsigned)ESP.getFreeHeap());

        if (apFailureCount >= configManager.getAccessPointMaxFailures()) {
            safeEnviarMensagemVirtual("Too many AP failures; pausing %u s", WARN, (unsigned)(configManager.getAccessPointFailurePause() / 1000));
            sendVirtualMessage(F("Suggestion: check WiFi/hardware interference"), INFO);
            sendVirtualMessage(F("Full WiFi stack reset..."), WARN);
            resetWiFiStack();
        } else {
            safeEnviarMensagemVirtual("Automatic retry in %u s...", INFO, (unsigned)(configManager.getAccessPointRetryDelay() / 1000));
        }
    }
#endif
}

void stopAccessPoint() {
#if ENABLE_WIFI!=1
    apActive = false;
    return;
#else
    if (!apActive) {
        return;
    }

    sendVirtualMessage(F("AP OFF to save power"), INFO);
    WiFi.softAPdisconnect(true);

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_STA);
    } else {
        WiFi.mode(WIFI_STA);
    }

    apActive = false;
    sendVirtualMessage(F("AP OFF"), INFO);
#endif
}