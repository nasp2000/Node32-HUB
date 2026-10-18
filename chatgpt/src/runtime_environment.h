#pragma once

#include <Arduino.h>

String storageGetTodayFolder();
bool storageIsAvailable();
bool ethernetHasIp();
bool hasNetworkConnectivity();
String getCurrentNetworkIPString();
bool isEmailConfiguredAndNetworkConnected();
bool isEmailConfiguredAndWifiConnected();