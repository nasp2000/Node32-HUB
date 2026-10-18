#pragma once

#include <Arduino.h>

#include <vector>

struct WifiNetwork {
    String ssid;
    String password;
    int rssi;
    unsigned long lastConnected;
    int connectionCount;
};

inline constexpr int MAX_SAVED_NETWORKS = 5;

extern std::vector<WifiNetwork> savedNetworks;

void loadSavedNetworks();
void saveSavedNetworks();
void addOrUpdateNetwork(const String& networkSSID, const String& networkPassword);
String findBestAvailableNetwork();
WifiNetwork* findBestAvailableNetworkPtr();