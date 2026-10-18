#include "network_manual_switch.h"

#include <WiFi.h>

#include "coordinator_hooks.h"
#include "commands.h"
#include "network_runtime_state.h"
#include "network_saved_profiles.h"

void switchToNetworkManually(String targetSSID) {
    safeEnviarMensagemVirtual("Manual network switch: %s", INFO, targetSSID.c_str());

    manualNetworkSwitch = true;

    WifiNetwork* targetNetwork = nullptr;
    for (auto& saved : savedNetworks) {
        if (saved.ssid == targetSSID) {
            targetNetwork = &saved;
            break;
        }
    }

    if (targetNetwork == nullptr) {
        safeEnviarMensagemVirtual("Network not found in saved networks: %s", ERROR, targetSSID.c_str());
        manualNetworkSwitch = false;
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        String currentSsid = WiFi.SSID();
        safeEnviarMensagemVirtual("Disconnecting from current network: %s", INFO, currentSsid.c_str());
        WiFi.disconnect();
        delay(1000);
    }

    safeEnviarMensagemVirtual("Manually connecting to: %s", INFO, targetSSID.c_str());
    WiFi.begin(targetNetwork->ssid.c_str(), targetNetwork->password.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        targetNetwork->lastConnected = millis();
        targetNetwork->connectionCount++;
        saveSavedNetworks();

        currentConnectedSSID = targetNetwork->ssid;
        ssid = targetNetwork->ssid;
        password = targetNetwork->password;

        safeEnviarMensagemVirtual("Manual switch OK: %s", INFO, targetSSID.c_str());
        logManualNetworkSwitchReconnect();
    } else {
        safeEnviarMensagemVirtual("Manual switch failed for: %s", ERROR, targetSSID.c_str());
    }

    manualNetworkSwitch = false;
}