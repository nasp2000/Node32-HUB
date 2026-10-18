#include "network_saved_profiles.h"

#include <WiFi.h>

#include <stdarg.h>

#include "commands.h"
#include "config_manager_runtime.h"

std::vector<WifiNetwork> savedNetworks;

namespace {

int currentNetworkIndex = -1;

void logFormatted(int level, const char* format, ...);

void rebuildSavedNetworksCacheFromConfig(bool logResult) {
    JsonArray networks = configManager.getSavedNetworks();

    savedNetworks.clear();
    savedNetworks.shrink_to_fit();

    for (JsonObject network : networks) {
        if (network.containsKey("ssid") && network.containsKey("password")) {
            WifiNetwork wifiNet;
            wifiNet.ssid = network["ssid"].as<String>();
            wifiNet.password = network["password"].as<String>();
            wifiNet.rssi = network["rssi"] | -100;
            wifiNet.lastConnected = network["lastConnected"] | 0;
            wifiNet.connectionCount = network["connectionCount"] | 0;
            savedNetworks.push_back(wifiNet);
        }
    }

    if (logResult) {
        logFormatted(INFO, "WiFi carregadas: %u", (unsigned)savedNetworks.size());
    }
}

void persistSavedNetworksCacheToConfig(bool logResult) {
    configManager.clearSavedNetworks();
    JsonArray networks = configManager.getSavedNetworks();

    for (const auto& network : savedNetworks) {
        JsonObject net = networks.createNestedObject();
        net["ssid"] = network.ssid;
        net["password"] = network.password;
        net["rssi"] = network.rssi;
        net["lastConnected"] = network.lastConnected;
        net["connectionCount"] = network.connectionCount;
    }

    configManager.save(true);

    if (logResult) {
        logFormatted(INFO, "WiFi salvas: %u", (unsigned)savedNetworks.size());
    }
}

void logFormatted(int level, const char* format, ...) {
    char buffer[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    sendVirtualMessage(String(buffer), level);
}

}  // namespace

extern int performCachedWiFiScan(bool forceNew);
extern bool getCachedScanResult(int index, String& ssid, int32_t& rssi, wifi_auth_mode_t& encryption);

void loadSavedNetworks() {
    rebuildSavedNetworksCacheFromConfig(true);
}

void saveSavedNetworks() {
    persistSavedNetworksCacheToConfig(true);
}

void addOrUpdateNetwork(const String& networkSSID, const String& networkPassword) {
    bool updated = false;
    for (const auto& network : savedNetworks) {
        if (network.ssid == networkSSID) {
            updated = true;
            break;
        }
    }

    configManager.addWifiNetwork(networkSSID, networkPassword);
    rebuildSavedNetworksCacheFromConfig(false);

    logFormatted(INFO, updated ? "WiFi upd: %s" : "Added: %s", networkSSID.c_str());
}

String findBestAvailableNetwork() {
    sendVirtualMessage(F("Scan redes..."), INFO);

    int n = performCachedWiFiScan(false);
    while (n == 0) {
        delay(100);
        n = performCachedWiFiScan(false);
    }
    if (n == 0) {
        sendVirtualMessage(F("Empty scan"), WARN);
        currentNetworkIndex = -1;
        return "";
    }

    String bestSSID = "";
    int bestScore = -200;
    int bestIndex = -1;

    for (int i = 0; i < n; i++) {
        String scannedSSID;
        int32_t scannedRSSI;
        wifi_auth_mode_t encryption;

        if (!getCachedScanResult(i, scannedSSID, scannedRSSI, encryption)) {
            continue;
        }

        for (size_t j = 0; j < savedNetworks.size(); ++j) {
            if (savedNetworks[j].ssid == scannedSSID) {
                int score = scannedRSSI;
                if (savedNetworks[j].connectionCount > 0) score += 2;
                unsigned long age = millis() - savedNetworks[j].lastConnected;
                if (age < 3600000UL) score += 3;
                else if (age < 86400000UL) score += 1;
                if (score > bestScore) {
                    bestScore = score;
                    bestSSID = scannedSSID;
                    bestIndex = (int)j;
                }
                break;
            }
        }
    }

    if (!bestSSID.isEmpty()) {
        currentNetworkIndex = bestIndex;
        logFormatted(INFO, "Melhor: %s (score %d)", bestSSID.c_str(), bestScore);
    } else {
        currentNetworkIndex = -1;
        sendVirtualMessage(F("Sem rede salva"), WARN);
    }

    return bestSSID;
}

WifiNetwork* findBestAvailableNetworkPtr() {
    String bestSSID = findBestAvailableNetwork();
    if (bestSSID.isEmpty() || currentNetworkIndex < 0) {
        return nullptr;
    }
    return &savedNetworks[currentNetworkIndex];
}