#pragma once

#include <ArduinoJson.h>
#include <WiFi.h>

#include "config_manager_runtime.h"
#include "eth_w5500.h"
#include "runtime_environment.h"

inline void fillNetworkWifiStatusJson(JsonDocument& doc) {
    IPAddress ethIp = ethernetLocalIP();
    bool ethernetConnected = (ethIp != IPAddress(0, 0, 0, 0));

#if ENABLE_WIFI==1
    doc["sta"]["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["sta"]["ssid"] = WiFi.SSID();
    doc["sta"]["ip"] = WiFi.localIP().toString();
    doc["sta"]["rssi"] = WiFi.RSSI();
    doc["sta"]["mac"] = WiFi.macAddress();

    doc["ap"]["active"] = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    doc["ap"]["ssid"] = WiFi.softAPSSID();
    bool apOpen = (configManager.getAccessPointPassword().length() == 0);
    doc["ap"]["open"] = apOpen;
    doc["ap"]["password"] = apOpen ? String("<open>") : String("<set>");
    doc["ap"]["ip"] = WiFi.softAPIP().toString();
    doc["ap"]["clients"] = WiFi.softAPgetStationNum();
    doc["ap"]["mac"] = WiFi.softAPmacAddress();
#else
    doc["sta"]["connected"] = false;
    doc["sta"]["ssid"] = String("");
    doc["sta"]["ip"] = String("");
    doc["sta"]["rssi"] = 0;
    doc["sta"]["mac"] = String("");

    doc["ap"]["active"] = false;
    doc["ap"]["ssid"] = String("");
    doc["ap"]["open"] = false;
    doc["ap"]["password"] = String("");
    doc["ap"]["ip"] = String("");
    doc["ap"]["clients"] = 0;
    doc["ap"]["mac"] = String("");
#endif

    doc["ethernet"]["connected"] = ethernetConnected;
    doc["ethernet"]["ip"] = ethernetConnected ? ethIp.toString() : String("");

    doc["network_connected"] = hasNetworkConnectivity();
    doc["network_ip"] = hasNetworkConnectivity() ? getCurrentNetworkIPString() : String("");
#if ENABLE_WIFI==1
    doc["mode"] = WiFi.getMode();
    doc["hostname"] = WiFi.getHostname();
#else
    doc["mode"] = 0;
    doc["hostname"] = String("");
#endif
}