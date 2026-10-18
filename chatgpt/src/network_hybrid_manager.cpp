#include "network_hybrid_manager.h"

#include <WiFi.h>
// esp_wifi.h provides direct driver calls (set_ps, set_bandwidth) unavailable
// on ESP32-P4 where WiFi is handled by the C6 companion chip via SDIO/esp_wifi_remote.
#include "feature_flags.h"
#if !defined(TARGET_BOARD_P4) || TARGET_BOARD_P4!=1
#include <esp_wifi.h>
#endif
#include <esp_task_wdt.h>

#include "config_manager_runtime.h"
#include "coordinator_hooks.h"
#include "commands.h"
#include "network_access_point.h"
#include "network_runtime_state.h"
#include "network_saved_profiles.h"

WiFiHybridState currentHybridState = HYBRID_BOOT_AP_STA;
unsigned long hybridBootTime = 0;
unsigned long staConnectedTime = 0;
unsigned long staDisconnectedStartTime = 0;
bool staWasConnected = false;
unsigned long hybridBootTimeout;
unsigned long staStableTime;
unsigned long staRecoveryTimeout;
int staReconnectAttempts = 0;
unsigned long lastStaRetryTime = 0;

extern int performCachedWiFiScan(bool forceNew);
extern bool getCachedScanResult(int index, String& ssid, int32_t& rssi, wifi_auth_mode_t& encryption);
extern unsigned long lastScanTime;
extern const unsigned long WIFI_DISCONNECT_FALLBACK_SCAN_DELAY;
extern const unsigned long WIFI_RETRY_OTHER_NETWORK_COOLDOWN;
extern unsigned long lastFallbackScanAttempt;
extern int lastTriedAltNetworkIndex;

static void applyApLatencyTuning() {
#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    // P4 uses the C6 companion for WiFi; disable modem sleep via Arduino facade.
    WiFi.setSleep(false);
#else
    // Disable modem sleep and force HT20 for stable AP browsing latency.
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
#endif
}

int getCurrentHybridStateValue() { return (int)currentHybridState; }

void startHybridWiFiConnection() {
#if ENABLE_WIFI!=1
    sendVirtualMessage(F("WiFi disabled by build flag"), INFO);
    apActive = false;
    return;
#else
    if (configManager.getWifiDisabled()) {
        sendVirtualMessage(F("WiFi disabled by user setting — skipping init"), WARN);
        apActive = false;
        return;
    }

    if (forceAPMode) {
        sendVirtualMessage(F("WiFi hybrid bypassed: forced AP mode"), INFO);
        startAccessPoint();
        currentHybridState = HYBRID_AP_ONLY;
        hybridBootTime = millis();
        staConnectedTime = 0;
        staDisconnectedStartTime = 0;
        staWasConnected = false;
        return;
    }

    sendVirtualMessage(String(F("WiFi hybrid")), INFO);

#if defined(TARGET_BOARD_S3_TOUCH7) && TARGET_BOARD_S3_TOUCH7==1
    // TOUCH7: Arduino WiFi persistence writes STA config to NVS on every
    // begin() — a flash op outside any guard (async wifi task) racing the
    // always-on RGB VSYNC restart ISR (flash-hosted) into a reset. Our
    // firmware persists credentials itself (NVS cfg_wifi + LittleFS), so the
    // redundant Arduino copy is pure risk: keep it in RAM only.
    WiFi.persistent(false);
#endif

    loadSavedNetworks();

#if defined(TARGET_BOARD_P4) && TARGET_BOARD_P4 == 1
    WiFi.setPins(18, 19, 14, 15, 16, 17, 54);
#endif

    // Determine if we have any networks to try
    bool hasSavedNetworks = (!savedNetworks.empty() || ssid.length() > 0);

    if (!hasSavedNetworks) {
        // No saved networks — pure AP only
        sendVirtualMessage(F("No saved networks; pure AP"), INFO);
        WiFi.disconnect(false);
        yield();
        WiFi.mode(WIFI_AP);
        yield();
        applyApLatencyTuning();
        startAccessPoint();
        currentHybridState = HYBRID_AP_ONLY;
        hybridBootTime = millis();
        staConnectedTime = 0;
        staDisconnectedStartTime = 0;
        staWasConnected = false;
        lastStaRetryTime = millis();
        return;
    }

    // Start AP
    String apSSID = configManager.getAccessPointSSID();
    String apPassword = configManager.getAccessPointPassword();
    if (apSSID.isEmpty()) apSSID = "NODE32-HUB";
    if (apPassword.isEmpty()) apPassword = "";

    WiFi.mode(WIFI_AP_STA);
    delay(300);

    bool apResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str(), 6, 0, 4);
    if (!apResult) {
        delay(1000);
        apResult = WiFi.softAP(apSSID.c_str(), apPassword.c_str(), 11, 0, 2);
    }
    if (apResult) {
        apActive = true;
        applyApLatencyTuning();
        safeEnviarMensagemVirtual("AP: %s", INFO, apSSID.c_str());
    } else {
        sendVirtualMessage(F("AP failed; retry later"), WARN);
    }

    // Start STA connection attempt
    WifiNetwork* bestNetwork = findBestAvailableNetworkPtr();
    if (bestNetwork != nullptr) {
        safeEnviarMensagemVirtual("STA: %s", INFO, bestNetwork->ssid.c_str());
        WiFi.begin(bestNetwork->ssid.c_str(), bestNetwork->password.c_str());
    } else if (ssid.length() > 0) {
        safeEnviarMensagemVirtual("Legacy STA: %s", INFO, ssid.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());
        addOrUpdateNetwork(ssid, password);
    }

    // Go to boot state — 60s timeout, async
    currentHybridState = HYBRID_BOOT_AP_STA;
    hybridBootTime = millis();
    hybridBootTimeout = 60000;
    staConnectedTime = 0;
    staDisconnectedStartTime = 0;
    staWasConnected = false;
    staReconnectAttempts = 0;
    safeEnviarMensagemVirtual("AP+STA: 60s to connect", INFO);
#endif
}

void manageHybridWiFiMode() {
#if ENABLE_WIFI!=1
    return;
#else
    if (WiFi.getMode() == WIFI_OFF) return;

    if (forceAPMode) {
        if (WiFi.getMode() != WIFI_AP) {
            WiFi.disconnect(false);
            yield();
            WiFi.mode(WIFI_AP);
            yield();
            applyApLatencyTuning();
        }
        apActive = true;
        currentHybridState = HYBRID_AP_ONLY;
        staConnectedTime = 0;
        staDisconnectedStartTime = 0;
        staWasConnected = false;
        return;
    }

    unsigned long now = millis();
    bool staHasIP = (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0));
    yield();

    switch (currentHybridState) {

        // ── BOOT: AP+STA, 60s timeout ──────────────────────────────────
        case HYBRID_BOOT_AP_STA:
            if (staHasIP) {
                sendVirtualMessage(String(F("STA ok -> disable AP")), INFO);
                WiFi.softAPdisconnect(true);
                delay(100);
                WiFi.mode(WIFI_STA);
                apActive = false;
                currentHybridState = HYBRID_STA_ONLY;
                staWasConnected = true;
                staConnectedTime = 0;
                staDisconnectedStartTime = 0;
                staReconnectAttempts = 0;
                currentConnectedSSID = WiFi.SSID();
            } else if (now - hybridBootTime >= hybridBootTimeout) {
                sendVirtualMessage(F("STA timeout -> AP only"), INFO);
                WiFi.disconnect(false);
                WiFi.mode(WIFI_AP);
                applyApLatencyTuning();
                apActive = true;
                currentHybridState = HYBRID_AP_ONLY;
                lastStaRetryTime = now;
                staReconnectAttempts = 0;
            }
            break;

        // ── STA CONNECTED: AP off, monitor STA ────────────────────────
        case HYBRID_STA_ONLY:
            if (staHasIP) {
                staDisconnectedStartTime = 0;
            } else {
                if (staDisconnectedStartTime == 0) {
                    staDisconnectedStartTime = now;
                    staReconnectAttempts = 0;
                    sendVirtualMessage(F("STA lost; retry 1/3"), WARN);
                    WiFi.begin();
                    break;
                }

                unsigned long elapsed = now - staDisconnectedStartTime;

                if (elapsed >= 60000 && elapsed < 120000 && staReconnectAttempts < 1) {
                    staReconnectAttempts = 1;
                    sendVirtualMessage(F("STA retry 2/3"), INFO);
                    WiFi.begin();
                } else if (elapsed >= 120000 && elapsed < 180000 && staReconnectAttempts < 2) {
                    staReconnectAttempts = 2;
                    sendVirtualMessage(F("STA retry 3/3"), INFO);
                    WiFi.begin();
                } else if (elapsed >= 180000 && staReconnectAttempts < 3) {
                    staReconnectAttempts = 3;
                    sendVirtualMessage(F("STA failed 3x; enabling AP"), WARN);
                    WiFi.mode(WIFI_AP_STA);
                    String as = configManager.getAccessPointSSID();
                    String ap = configManager.getAccessPointPassword();
                    if (as.isEmpty()) as = "NODE32-HUB";
                    WiFi.softAP(as.c_str(), ap.c_str());
                    apActive = true;
                    currentHybridState = HYBRID_RECOVERY_AP_STA;
                    lastStaRetryTime = now;
                }
            }
            break;

        // ── RECOVERY AP+STA: STA retry every 5min ─────────────────────
        case HYBRID_RECOVERY_AP_STA:
            if (staHasIP) {
                sendVirtualMessage(F("STA recovered -> STA only"), INFO);
                WiFi.softAPdisconnect(true);
                delay(100);
                WiFi.mode(WIFI_STA);
                apActive = false;
                currentHybridState = HYBRID_STA_ONLY;
                staDisconnectedStartTime = 0;
                staReconnectAttempts = 0;
                currentConnectedSSID = WiFi.SSID();
            } else if (now - lastStaRetryTime >= 300000) {
                lastStaRetryTime = now;
                sendVirtualMessage(F("AP+STA: retrying (5min)"), INFO);
                WiFi.begin();
            }
            break;

        // ── AP ONLY: pure AP, no STA overlap ──────────────────────────
        case HYBRID_AP_ONLY: {
            bool hasNets = (!savedNetworks.empty() || ssid.length() > 0);
            if (!hasNets) break;

            if (now - lastStaRetryTime >= 300000) {
                lastStaRetryTime = now;
                sendVirtualMessage(F("AP: brief STA check..."), INFO);
                // AP off, STA only, 10s to connect
                WiFi.softAPdisconnect(true);
                delay(50);
                WiFi.mode(WIFI_STA);
                apActive = false;
                if (savedNetworks.size() > 0) {
                    WiFi.begin(savedNetworks[0].ssid.c_str(), savedNetworks[0].password.c_str());
                } else if (ssid.length() > 0) {
                    WiFi.begin(ssid.c_str(), password.c_str());
                }
                currentHybridState = HYBRID_AP_RETRY_STA;
                hybridBootTime = now;
            }
            break;
        }

        // ── AP RETRY STA: 10s window, AP off ──────────────────────────
        case HYBRID_AP_RETRY_STA:
            if (staHasIP) {
                sendVirtualMessage(F("AP retry: STA OK"), INFO);
                currentHybridState = HYBRID_STA_ONLY;
                staWasConnected = true;
                currentConnectedSSID = WiFi.SSID();
            } else if (now - hybridBootTime >= 10000) {
                sendVirtualMessage(F("AP retry: no network"), INFO);
                WiFi.disconnect(false);
                WiFi.mode(WIFI_AP);
                applyApLatencyTuning();
                String as = configManager.getAccessPointSSID();
                String ap = configManager.getAccessPointPassword();
                if (as.isEmpty()) as = "NODE32-HUB";
                WiFi.softAP(as.c_str(), ap.c_str());
                apActive = true;
                currentHybridState = HYBRID_AP_ONLY;
            }
            break;
    }
#endif
}